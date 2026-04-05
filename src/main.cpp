#include <M5Unified.h>
#include <esp_sleep.h>
#include <sys/time.h>
#include "config.h"
#include "weather_api.h"
#include "wifi_manager.h"
#include "power_manager.h"
#include "screen_home.h"
#include "screen_weather.h"
#include "screen_splash.h"

// ── Estado persistido no RTC (sobrevive ao deep sleep) ───────────────────────
RTC_DATA_ATTR static uint8_t     rtcBootCount   = 0;      // 0 = cold boot
RTC_DATA_ATTR static int         rtcScreen      = 0;
RTC_DATA_ATTR static WeatherData rtcWeather     = {};
RTC_DATA_ATTR static int         rtcTimerState  = 0;      // 0=SETTING, 2=PAUSED
RTC_DATA_ATTR static int         rtcTimerPreset = 2;
RTC_DATA_ATTR static uint32_t    rtcTimerRemain = 5 * 60000;
RTC_DATA_ATTR static time_t      rtcSavedTime   = 0;      // hora salva antes do deep sleep

// ── Estado volátil (loop) ────────────────────────────────────────────────────
static int         currentScreen = 0;
static WeatherData weatherData   = {};
static uint32_t    lastDrawMs    = 0;
static bool        needsRedraw   = true;

static LGFX_Sprite*     canvas = nullptr;
static lgfx::LovyanGFX* fb     = nullptr;

static bool     touchStartedInDim = false;
static int      touchStartY       = 0;
static uint32_t touchStartMs      = 0;

static bool     alarmWasActive  = false;
static uint32_t lastBeepMs      = 0;
static uint8_t  lastDimMinute   = 255;  // controla refresh do relógio no dim

// ── Helpers ──────────────────────────────────────────────────────────────────
static void initSprite() {
    canvas = new LGFX_Sprite(&M5.Display);
    canvas->setColorDepth(16);
    if (canvas->createSprite(M5.Display.width(), M5.Display.height())) {
        fb = canvas;
        Serial.println("[main] Sprite alocado — sem flickering");
    } else {
        delete canvas;
        canvas = nullptr;
        fb = &M5.Display;
        Serial.println("[main] AVISO: sprite nao alocado, usando display direto");
    }
}

static void pushFrame() {
    if (canvas) canvas->pushSprite(0, 0);
}

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    auto cause       = esp_sleep_get_wakeup_cause();
    bool isColdBoot  = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);
    bool isTimerWake = (cause == ESP_SLEEP_WAKEUP_TIMER);
    // ESP32-S3 pode reportar GPIO em vez de EXT0 para wake por toque;
    // qualquer wake que não seja cold boot nem timer é tratado como toque
    bool isTouchWake = !isColdBoot && !isTimerWake;

    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    M5.Speaker.setVolume(200);

    // Restaura timezone. No cold boot: configTime() completo (inicia SNTP).
    if (isColdBoot) {
        configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
    } else {
        // Restaura timezone e exibe hora aproximada enquanto NTP sincroniza async.
        int h = TIMEZONE_OFFSET_SEC / 3600;
        char tz[16];
        snprintf(tz, sizeof(tz), "UTC%+d", -h);
        setenv("TZ", tz, 1);
        tzset();
        if (rtcSavedTime > 1577836800L) {
            struct timeval tv = { .tv_sec = rtcSavedTime, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
            Serial.printf("[main] Hora provisória restaurada: %ld\n", (long)rtcSavedTime);
        }
    }

    initSprite();
    powerInit();
    screenHomeInit();

    // ── Wake silencioso por timer: atualiza clima e volta a dormir ──────────
    if (isTimerWake && rtcBootCount > 0) {
        Serial.println("[main] Wake por timer — atualizando clima");
        M5.Display.setBrightness(0);
        weatherData = rtcWeather;
        wifiConnectAndFetch(weatherData);
        rtcWeather = weatherData;
        rtcBootCount++;
        rtcSavedTime = time(nullptr);
        powerEnterDeepSleep();
        return;  // nunca chega aqui
    }

    // ── Cold boot ou wake por toque ─────────────────────────────────────────
    currentScreen = rtcScreen;
    weatherData   = rtcWeather;

    if (!isColdBoot && isTouchWake && rtcBootCount > 0) {
        // Restaura estado do timer e inicia NTP + clima de forma assíncrona.
        // A hora provisória já foi restaurada acima via settimeofday.
        Serial.println("[main] Wake por toque — iniciando NTP async");
        TimerPersist tp = { rtcTimerState, rtcTimerPreset, rtcTimerRemain };
        screenHomeSetTimerPersist(tp);
        wifiBeginAsync(weatherData);  // não bloqueia; progride via wifiScheduleUpdate()
    } else {
        // Cold boot: splash + init completo
        Serial.println("[main] Boot inicial");
        drawSplash(*fb);
        pushFrame();
        wifiInit(weatherData);
        rtcWeather = weatherData;
    }

    rtcBootCount++;
    needsRedraw = true;
    Serial.println("[main] Setup concluido");
}

// ── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    M5.update();

    // ── Toque ────────────────────────────────────────────────────────────────
    auto touch = M5.Touch.getDetail();

    if (touch.wasPressed()) {
        touchStartedInDim = powerIsDim();
        touchStartY       = touch.y;
        touchStartMs      = millis();
        powerOnTouch();
        if (touchStartedInDim) needsRedraw = true;  // acorda do dim → redesenha
    }

    if (touch.wasReleased() && !touchStartedInDim) {
        uint32_t held      = millis() - touchStartMs;
        bool     longPress = (held >= 800);

        if (currentScreen == 0 && screenHomeIsAlarmActive()) {
            screenHomeTimerTap();
            needsRedraw = true;
        } else if (currentScreen == 0 && touchStartY >= 118) {
            if (longPress) {
                screenHomeTimerLongPress();
                Serial.println("[main] Timer: long press");
            } else {
                screenHomeTimerTap();
                Serial.println("[main] Timer: tap");
            }
            needsRedraw = true;
        } else if (!longPress) {
            currentScreen = (currentScreen + 1) % 2;
            needsRedraw   = true;
            Serial.printf("[main] Tela -> %d\n", currentScreen);
        }
    }

    // ── Dim por inatividade ───────────────────────────────────────────────────
    bool wasDim = powerIsDim();
    powerUpdate();
    if (wasDim && !powerIsDim()) needsRedraw = true;

    // ── WiFi / clima ─────────────────────────────────────────────────────────
    wifiScheduleUpdate(weatherData);

    // ── Deep sleep ───────────────────────────────────────────────────────────
    // Bloqueia se timer ativo (rodando/pausado) ou alarme tocando
    if (!screenHomeIsTimerActive() && !screenHomeIsAlarmActive() &&
        powerShouldDeepSleep()) {

        Serial.println("[main] Deep sleep — salvando estado");
        rtcScreen = currentScreen;
        rtcWeather = weatherData;

        TimerPersist tp = screenHomeGetTimerPersist();
        rtcTimerState  = tp.state;
        rtcTimerPreset = tp.presetIdx;
        rtcTimerRemain = tp.remainMs;

        rtcBootCount++;
        rtcSavedTime = time(nullptr);
        powerEnterDeepSleep();
        return;  // nunca chega aqui
    }

    // ── Alarme sonoro ─────────────────────────────────────────────────────────
    bool alarmActive = screenHomeIsAlarmActive();
    if (alarmActive) {
        uint32_t now = millis();
        if (now - lastBeepMs >= 1800) {
            M5.Speaker.tone(880,  150);
            M5.Speaker.tone(1100, 150);
            M5.Speaker.tone(1320, 400);
            lastBeepMs = now;
        }
        if ((now / 400) % 2 == 0 != (lastDrawMs / 400) % 2 == 0) {
            needsRedraw = true;
        }
    } else if (alarmWasActive) {
        M5.Speaker.stop();
    }
    alarmWasActive = alarmActive;

    // ── Redesenho ─────────────────────────────────────────────────────────────
    // No dim + tela do relógio: atualiza uma vez por minuto (no segundo 0)
    if (powerIsDim() && currentScreen == 0 && !needsRedraw) {
        struct tm t;
        if (getLocalTime(&t, 0) && t.tm_sec == 0 && t.tm_min != lastDimMinute) {
            needsRedraw    = true;
            lastDimMinute  = t.tm_min;
        }
    }
    if (!powerIsDim()) lastDimMinute = 255;  // reseta ao sair do dim

    if (powerIsDim() && !needsRedraw) { delay(100); return; }

    uint32_t now = millis();
    bool timeToRefresh = (now - lastDrawMs >= (alarmActive ? 400 : 1000));
    if (needsRedraw || timeToRefresh) {
        if (currentScreen == 0) {
            screenHomeDraw(*fb, wifiIsFetching());
        } else {
            screenWeatherDraw(*fb, weatherData, wifiIsFetching());
        }
        pushFrame();
        lastDrawMs  = now;
        needsRedraw = false;
    }

    delay(20);
}
