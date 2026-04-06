#include <M5Unified.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include "config.h"
#include "logger.h"
#include "weather_api.h"
#include "wifi_manager.h"
#include "power_manager.h"
#include "screen_home.h"
#include "screen_weather.h"
#include "screen_splash.h"
#include "led_strip.h"
#include "telnet_log.h"

// ── LTR553 — sensor de proximidade embutido (I2C interno 0x23) ───────────────
#define LTR553_ADDR         0x23
#define LTR553_PS_CONTR     0x81   // controle do PS: bits[1:0] 0x03 = active
#define LTR553_PS_DATA_LOW  0x8D   // dado PS, byte baixo (bits 7:0)
#define LTR553_PS_DATA_HIGH 0x8E   // dado PS, byte alto  (bits 2:0)
#define LTR553_I2C_FREQ     400000

static void ltr553Init() {
    M5.In_I2C.writeRegister8(LTR553_ADDR, LTR553_PS_CONTR, 0x03, LTR553_I2C_FREQ);
    delay(10);
    LOG_I("ltr553", "Sensor de proximidade iniciado (limiar=%d)", LTR553_PROX_THRESH);
}

static uint16_t ltr553ReadProx() {
    uint8_t lo = M5.In_I2C.readRegister8(LTR553_ADDR, LTR553_PS_DATA_LOW,  LTR553_I2C_FREQ);
    uint8_t hi = M5.In_I2C.readRegister8(LTR553_ADDR, LTR553_PS_DATA_HIGH, LTR553_I2C_FREQ);
    return ((uint16_t)(hi & 0x07) << 8) | lo;
}

// ── Estado persistido no RTC (sobrevive ao deep sleep) ───────────────────────
RTC_DATA_ATTR static uint8_t     rtcBootCount   = 0;
RTC_DATA_ATTR static int         rtcScreen      = 0;
RTC_DATA_ATTR static WeatherData rtcWeather     = {};
RTC_DATA_ATTR static int         rtcTimerState  = 0;
RTC_DATA_ATTR static int         rtcTimerPreset = 2;
RTC_DATA_ATTR static uint32_t    rtcTimerRemain = 5 * 60000;

// ── Estado volátil (loop) ────────────────────────────────────────────────────
static int         currentScreen = 0;
static WeatherData weatherData   = {};
static uint32_t    lastDrawMs    = 0;
static bool        needsRedraw   = true;

static LGFX_Sprite*     canvas = nullptr;
static lgfx::LovyanGFX* fb     = nullptr;

static bool     touchStartedInDim = false;
static int      touchStartX       = 0;
static int      touchStartY       = 0;
static uint32_t touchStartMs      = 0;

static bool     alarmWasActive  = false;
static uint32_t lastBeepMs      = 0;
static uint8_t  lastDimMinute   = 255;

// ── Orientação via acelerômetro ───────────────────────────────────────────────
static int      currentRotation  = 1;
static uint32_t lastImuMs        = 0;

static void updateOrientation() {
    if (millis() - lastImuMs < 500) return;
    lastImuMs = millis();

    float ax, ay, az;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return;

    int newRot = currentRotation;
    if      (ay >  0.4f) newRot = 1;
    else if (ay < -0.4f) newRot = 3;

    if (newRot != currentRotation) {
        currentRotation = newRot;
        M5.Display.setRotation(newRot);
        needsRedraw = true;
        LOG_I("imu", "Rotacao -> %d (ay=%.2f)", newRot, ay);
    }
}

// ── Helpers ──────────────────────────────────────────────────────────────────
static void initSprite() {
    canvas = new LGFX_Sprite(&M5.Display);
    canvas->setColorDepth(16);
    if (canvas->createSprite(M5.Display.width(), M5.Display.height())) {
        fb = canvas;
        LOG_I("main", "Sprite alocado (%dx%d) — sem flickering",
              M5.Display.width(), M5.Display.height());
    } else {
        delete canvas;
        canvas = nullptr;
        fb = &M5.Display;
        LOG_E("main", "Sprite nao alocado — usando display direto (flickering)");
    }
}

static void pushFrame() {
    if (canvas) canvas->pushSprite(0, 0);
}

// ── Log periódico de sensores ────────────────────────────────────────────────
// Emite status completo de todos os sensores a cada STATUS_LOG_INTERVAL_MS.
#define STATUS_LOG_INTERVAL_MS  60000UL

static void logSensorStatus() {
    static uint32_t lastStatusMs = 0;
    if (millis() - lastStatusMs < STATUS_LOG_INTERVAL_MS) return;
    lastStatusMs = millis();

    // Bateria e estado do display
    int  batt     = batteryPercent();
    bool charging = batteryIsCharging();
    LOG_I("status", "bat=%d%% chg=%s dim=%s tela=%d boot#%u",
          batt,
          charging  ? "sim" : "nao",
          powerIsDim() ? "sim" : "nao",
          currentScreen,
          (unsigned)rtcBootCount);

    // LTR553 — sensor de proximidade
    uint16_t prox = ltr553ReadProx();
    LOG_I("ltr553", "prox=%u  limiar=%d  %s",
          prox, LTR553_PROX_THRESH,
          prox > LTR553_PROX_THRESH ? "ATIVADO" : "ok");

    // IMU — acelerômetro
    float ax, ay, az;
    if (M5.Imu.getAccel(&ax, &ay, &az)) {
        LOG_I("imu", "ax=%.2f ay=%.2f az=%.2f  rot=%d", ax, ay, az, currentRotation);
    } else {
        LOG_W("imu", "Leitura indisponivel");
    }

    // WiFi — conectividade e sinal
    if (WiFi.status() == WL_CONNECTED) {
        LOG_I("wifi", "Conectado  RSSI=%d dBm  IP=%s",
              WiFi.RSSI(), WiFi.localIP().toString().c_str());
    } else {
        LOG_I("wifi", "Desconectado%s", wifiIsFetching() ? " (buscando...)" : "");
    }

    // Timer — estado atual
    const char* timerState = "SETTING";
    if      (screenHomeIsAlarmActive())  timerState = "ALARME";
    else if (screenHomeIsTimerRunning()) timerState = "RUNNING";
    else if (screenHomeIsTimerActive())  timerState = "PAUSED";
    TimerPersist tp = screenHomeGetTimerPersist();
    LOG_I("timer", "%s  preset=%d  remain=%lus",
          timerState, tp.presetIdx, (unsigned long)(tp.remainMs / 1000));

    // Clima — últimos dados recebidos
    if (weatherData.valid) {
        LOG_I("clima", "temp=%.1fC max=%.1f min=%.1f umid=%.0f%% cod=%d (%s) upd=%s",
              weatherData.tempCurrent,
              weatherData.tempMax,
              weatherData.tempMin,
              weatherData.humidity,
              weatherData.weatherCode,
              weatherData.description,
              weatherData.lastUpdated);
    } else {
        LOG_W("clima", "Sem dados validos");
    }
}

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    auto cause       = esp_sleep_get_wakeup_cause();
    bool isColdBoot  = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);
    bool isTimerWake = (cause == ESP_SLEEP_WAKEUP_TIMER);
    bool isTouchWake = !isColdBoot && !isTimerWake;

    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    M5.Speaker.setVolume(140);

    if (isColdBoot) {
        configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
    } else {
        int h = TIMEZONE_OFFSET_SEC / 3600;
        char tz[16];
        snprintf(tz, sizeof(tz), "UTC%+d", -h);
        setenv("TZ", tz, 1);
        tzset();
    }

    initSprite();
    ltr553Init();
    ledInit();
    powerInit();
    screenHomeInit();
    telnetLogInit();

    const char* wakeReason = isColdBoot  ? "cold-boot"  :
                             isTimerWake ? "timer-wake" : "touch-wake";
    LOG_I("main", "=== BOOT #%u (%s) ===", (unsigned)rtcBootCount, wakeReason);

    // ── Wake silencioso por timer ──────────────────────────────────────────
    if (isTimerWake && rtcBootCount > 0) {
        LOG_I("main", "Wake por timer — atualizando clima e voltando a dormir");
        M5.Display.setBrightness(0);
        weatherData = rtcWeather;
        wifiConnectAndFetch(weatherData);
        rtcWeather = weatherData;
        rtcBootCount++;
        powerEnterDeepSleep();
        return;
    }

    // ── Cold boot ou wake por toque ────────────────────────────────────────
    currentScreen = rtcScreen;
    weatherData   = rtcWeather;

    if (!isColdBoot && isTouchWake && rtcBootCount > 0) {
        LOG_I("main", "Wake por toque — restaurando estado");
        TimerPersist tp = { rtcTimerState, rtcTimerPreset, rtcTimerRemain };
        screenHomeSetTimerPersist(tp);
        wifiBeginAsync(weatherData);
    } else {
        LOG_I("main", "Boot inicial — splash + init completo");
        drawSplash(*fb);
        pushFrame();
        wifiInit(weatherData);
        rtcWeather = weatherData;
    }

    rtcBootCount++;
    telnetLogSetBoot(rtcBootCount);
    needsRedraw = true;
    LOG_I("main", "Setup concluido — Telnet porta %d (apos WiFi) | SD: %s",
          TELNET_LOG_PORT, "ver [sd] acima");
}

// ── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    M5.update();
    telnetLogUpdate();

    // ── Toque ────────────────────────────────────────────────────────────────
    auto touch = M5.Touch.getDetail();

    if (touch.wasPressed()) {
        touchStartedInDim = powerIsDim();
        touchStartX       = touch.x;
        touchStartY       = touch.y;
        touchStartMs      = millis();
        powerOnTouch();
        if (touchStartedInDim) needsRedraw = true;
    }

    if (touch.wasReleased() && !touchStartedInDim) {
        uint32_t held      = millis() - touchStartMs;
        bool     longPress = (held >= 800);

        if (currentScreen == 0 && screenHomeIsAlarmActive()) {
            screenHomeTimerTap(touchStartX);
            needsRedraw = true;
        } else if (currentScreen == 0 && touchStartY >= 118) {
            if (longPress) {
                screenHomeTimerLongPress();
                LOG_I("main", "Timer: long press");
            } else {
                screenHomeTimerTap(touchStartX);
                LOG_I("main", "Timer: tap (x=%d)", touchStartX);
            }
            needsRedraw = true;
        } else if (!longPress) {
            currentScreen = (currentScreen + 1) % 2;
            needsRedraw   = true;
            LOG_I("main", "Tela -> %d", currentScreen);
        }
    }

    // ── Proximidade — acorda do dim sem precisar tocar ───────────────────────
    static uint32_t lastProxMs = 0;
    if (powerIsDim() && (millis() - lastProxMs >= 100)) {
        lastProxMs = millis();
        uint16_t prox = ltr553ReadProx();
        if (prox > LTR553_PROX_THRESH) {
            LOG_I("ltr553", "Proximidade detectada (%u > %d) — acordando display",
                  prox, LTR553_PROX_THRESH);
            powerOnTouch();
            needsRedraw = true;
        }
    }

    // ── Orientação (acelerômetro) ────────────────────────────────────────────
    if (!powerIsDim()) updateOrientation();

    // ── Dim por inatividade ───────────────────────────────────────────────────
    bool wasDim = powerIsDim();
    powerUpdate();
    if (wasDim != powerIsDim()) needsRedraw = true;

    // ── WiFi / clima ─────────────────────────────────────────────────────────
    wifiScheduleUpdate(weatherData);

    // ── Deep sleep ───────────────────────────────────────────────────────────
    if (!screenHomeIsTimerActive() && !screenHomeIsAlarmActive() &&
        powerShouldDeepSleep()) {

        LOG_I("main", "Deep sleep — salvando estado (bat=%d%% tela=%d)",
              batteryPercent(), currentScreen);
        rtcScreen  = currentScreen;
        rtcWeather = weatherData;

        TimerPersist tp = screenHomeGetTimerPersist();
        rtcTimerState  = tp.state;
        rtcTimerPreset = tp.presetIdx;
        rtcTimerRemain = tp.remainMs;

        ledOff();
        powerEnterDeepSleep();
        return;
    }

    // ── Alarme sonoro ─────────────────────────────────────────────────────────
    bool alarmActive = screenHomeIsAlarmActive();
    if (alarmActive) {
        uint32_t now = millis();
        if (now - lastBeepMs >= 1800) {
            M5.Speaker.tone(523, 200);
            M5.Speaker.tone(659, 200);
            M5.Speaker.tone(784, 500);
            lastBeepMs = now;
        }
        if (((now / 400) % 2 == 0) != ((lastDrawMs / 400) % 2 == 0)) {
            needsRedraw = true;
        }
    } else if (alarmWasActive) {
        M5.Speaker.stop();
        LOG_I("main", "Alarme encerrado");
    }
    alarmWasActive = alarmActive;

    // ── Log periódico de sensores ─────────────────────────────────────────────
    logSensorStatus();

    // ── Redesenho ─────────────────────────────────────────────────────────────
    if (powerIsDim() && currentScreen == 0 && !needsRedraw) {
        struct tm t;
        if (getLocalTime(&t, 0) && t.tm_sec == 0 && t.tm_min != lastDimMinute) {
            needsRedraw   = true;
            lastDimMinute = t.tm_min;
        }
    }
    if (!powerIsDim()) lastDimMinute = 255;

    // ── LEDs ─────────────────────────────────────────────────────────────────
    ledUpdate(powerIsDim(), alarmActive, screenHomeIsTimerRunning());

    if (powerIsDim() && !needsRedraw) { delay(100); return; }

    uint32_t now = millis();
    bool timeToRefresh = (now - lastDrawMs >= (alarmActive ? 400 : 1000));
    if (needsRedraw || timeToRefresh) {
        if (currentScreen == 0) {
            screenHomeDraw(*fb, wifiIsFetching(), powerIsDim());
        } else {
            screenWeatherDraw(*fb, weatherData, wifiIsFetching());
        }
        pushFrame();
        lastDrawMs  = now;
        needsRedraw = false;
    }

    delay(20);
}
