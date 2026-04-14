#include <M5Unified.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include "config.h"
#include "theme.h"
#include "logger.h"
#include "weather_api.h"
#include "wifi_manager.h"
#include "power_manager.h"
#include "screen_home.h"
#include "screen_weather.h"
#include "screen_system.h"
#include "screen_splash.h"
#include "screen_settings.h"
#include "runtime_config.h"
#include "led_strip.h"
#include "telnet_log.h"
#include "ota_manager.h"
#include "events.h"
#include "chime_wav.h"

// ── LTR553 — sensor de proximidade embutido ──────────────────────────────────
#define LTR553_ADDR         0x23
#define LTR553_PS_CONTR     0x81
#define LTR553_PS_DATA_LOW  0x8D
#define LTR553_PS_DATA_HIGH 0x8E
#define LTR553_I2C_FREQ     400000

static void ltr553Init() {
    M5.In_I2C.writeRegister8(LTR553_ADDR, LTR553_PS_CONTR, 0x03, LTR553_I2C_FREQ);
    delay(10);
    LOG_I("ltr553", "Proximidade iniciado (limiar=%d)", LTR553_PROX_THRESH);
}

static uint16_t ltr553ReadProx() {
    uint8_t lo = M5.In_I2C.readRegister8(LTR553_ADDR, LTR553_PS_DATA_LOW,  LTR553_I2C_FREQ);
    uint8_t hi = M5.In_I2C.readRegister8(LTR553_ADDR, LTR553_PS_DATA_HIGH, LTR553_I2C_FREQ);
    return ((uint16_t)(hi & 0x07) << 8) | lo;
}

// ── Estado RTC ───────────────────────────────────────────────────────────────
RTC_DATA_ATTR static uint8_t     rtcBootCount   = 0;
RTC_DATA_ATTR static int         rtcScreen      = 0;
RTC_DATA_ATTR static WeatherData rtcWeather     = {};
RTC_DATA_ATTR static int         rtcTimerState[MAX_TIMERS]   = {};
RTC_DATA_ATTR static int         rtcTimerMinutes[MAX_TIMERS] = { 5, 10, 15 };
RTC_DATA_ATTR static uint32_t    rtcTimerRemain[MAX_TIMERS]  = { 300000, 600000, 900000 };
RTC_DATA_ATTR static int         rtcTimerFocused = 0;

// ── Estado volátil ───────────────────────────────────────────────────────────
static int         currentScreen  = 0;
static WeatherData weatherData    = {};
static RuntimeConfig g_runtimeCfg = {};
static int         g_settingsScrollTarget = 0;   // destino do scroll (px)
static float       g_settingsScrollAnim   = 0.0f; // posição animada atual
static uint32_t    lastDrawMs    = 0;
static bool        needsRedraw   = true;

static LGFX_Sprite*     canvas     = nullptr;
static LGFX_Sprite*     transSprite = nullptr;  // pré-alocado para transições
static lgfx::LovyanGFX* fb         = nullptr;

static bool     touchStartedInDim = false;
static int      touchStartX       = 0;
static int      touchStartY       = 0;
static uint32_t touchStartMs      = 0;

static bool     alarmWasActive  = false;
static uint32_t lastBeepMs      = 0;
static uint8_t  lastDimMinute   = 255;

// ── Orientação via acelerômetro (apenas rotações 1 e 3 — item #7) ────────────
static int      currentRotation  = 1;
static uint32_t lastImuMs        = 0;

static void applyConfiguredTimezone() {
    long totalMinutes = TIMEZONE_OFFSET_SEC / 60L;
    char sign = (totalMinutes >= 0) ? '-' : '+';
    unsigned long absMinutes = (unsigned long)labs(totalMinutes);
    unsigned long hours = absMinutes / 60UL;
    unsigned long minutes = absMinutes % 60UL;
    char tz[20];

    if (minutes == 0) snprintf(tz, sizeof(tz), "UTC%c%lu", sign, hours);
    else              snprintf(tz, sizeof(tz), "UTC%c%lu:%02lu", sign, hours, minutes);

    setenv("TZ", tz, 1);
    tzset();
}

static void updateOrientation() {
    if (millis() - lastImuMs < 500) return;
    lastImuMs = millis();

    float ax, ay, az;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return;

    // Apenas rotações landscape (1 e 3) — sprite não precisa ser recriado
    int newRot = currentRotation;
    if      (ay >  0.4f) newRot = 1;
    else if (ay < -0.4f) newRot = 3;

    if (newRot != currentRotation) {
        currentRotation = newRot;
        M5.Display.setRotation(newRot);
        // Sprite recreation para rotações não-landscape (futuro, item #13)
        // Rotações 1 e 3 mantêm mesmas dimensões, não precisa recriar.
        needsRedraw = true;
        LOG_I("imu", "Rotacao -> %d (ay=%.2f)", newRot, ay);
    }
}

// ── Helpers ──────────────────────────────────────────────────────────────────
static void initSprite() {
    // Item #13: libera sprites anteriores se existirem
    if (canvas)     { delete canvas;     canvas     = nullptr; }
    if (transSprite) { delete transSprite; transSprite = nullptr; }

    int w = M5.Display.width(), h = M5.Display.height();
    size_t spriteBytes = (size_t)w * h * 2;

    LOG_I("main", "PSRAM livre: %u bytes, maior bloco: %u bytes, heap livre: %u bytes",
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
        heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    LOG_I("main", "Sprite necessita %u bytes cada (%dx%dx2)", spriteBytes, w, h);

    canvas = new LGFX_Sprite(&M5.Display);
    canvas->setColorDepth(16);
    if (canvas->createSprite(w, h)) {
        fb = canvas;
        LOG_I("main", "canvas alocado — PSRAM livre agora: %u bytes, maior bloco: %u bytes",
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
            heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    } else {
        delete canvas;
        canvas = nullptr;
        fb = &M5.Display;
        LOG_E("main", "canvas nao alocado — fallback display direto");
        return;
    }

    // Pré-aloca sprite de transição forçando PSRAM
    transSprite = new LGFX_Sprite(&M5.Display);
    transSprite->setColorDepth(16);
    transSprite->setPsram(true);
    if (!transSprite->createSprite(w, h)) {
        delete transSprite;
        transSprite = nullptr;
        LOG_W("main", "transSprite nao alocado — PSRAM livre: %u bytes, maior bloco: %u bytes",
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
            heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    } else {
        LOG_I("main", "transSprite alocado — PSRAM livre agora: %u bytes",
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}

static void pushFrame() {
    if (canvas) canvas->pushSprite(0, 0);
}

// ── Transição animada entre telas (item #25) ─────────────────────────────────
static void drawCurrentScreen(lgfx::LovyanGFX& target) {
    switch (currentScreen) {
        case 0: screenHomeDraw(target, wifiIsFetching(), powerIsDim()); break;
        case 1: screenWeatherDraw(target, weatherData, wifiIsFetching()); break;
        case 2: screenSystemDraw(target, rtcBootCount); break;
        case 3: screenSettingsDraw(target, g_runtimeCfg, (int)g_settingsScrollAnim); break;
    }
}

static void animateTransition(int fromScreen, int toScreen, int direction) {
    if (!canvas || !transSprite) {
        LOG_W("main", "animateTransition: sprite indisponivel — troca instantânea");
        currentScreen = toScreen;
        return;
    }
    LOG_I("main", "animateTransition: %d -> %d (dir=%d)", fromScreen, toScreen, direction);
    LGFX_Sprite* dest = transSprite;

    // canvas já contém a tela antiga; desenha a nova no sprite temporário
    int oldScreen = currentScreen;
    currentScreen = toScreen;
    drawCurrentScreen(*dest);
    currentScreen = oldScreen;

    // ── Animação suave com ease-out cúbico ──
    // Composição dentro do canvas (sem coordenadas negativas):
    // - canvas tem a tela antiga
    // - a cada frame, desenha a porção esquerda de `dest` sobre a parte direita do canvas
    // - o lado esquerdo do canvas (tela antiga) vai encolhendo naturalmente
    const int FRAMES   = 12;
    const int FRAME_MS = 16;   // ~60fps, ~190ms total
    const int w        = M5.Display.width();
    const int h        = M5.Display.height();

    for (int i = 1; i <= FRAMES; i++) {
        float t    = (float)i / (float)FRAMES;
        float inv  = 1.0f - t;
        float ease = 1.0f - (inv * inv * inv);
        int   offset = (int)(ease * w);

        if (direction >= 0) {
            // Nova tela entra pela direita (swipe left → próxima)
            dest->pushSprite(canvas, w - offset, 0);
        } else {
            // Nova tela entra pela esquerda (swipe right → anterior)
            dest->pushSprite(canvas, -(w - offset), 0);
        }
        canvas->pushSprite(0, 0);
        delay(FRAME_MS);
    }

    // canvas já contém o estado final (dest totalmente copiado).
    // Redesenha para garantir estado limpo e atualizado.
    currentScreen = toScreen;
    drawCurrentScreen(*canvas);
}

// ── Eventos → próximo evento na home (item #24) ──────────────────────────────
static void updateNextEvent() {
    static uint32_t lastEventCheck = 0;
    if (millis() - lastEventCheck < 30000) return;
    lastEventCheck = millis();

    Event next;
    if (eventsGetNext(next)) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d %s",
                 next.day, next.month, next.hour, next.minute, next.name);
        screenHomeSetNextEvent(buf);
    } else {
        screenHomeSetNextEvent("");
    }
}

// ── Log periódico de sensores ────────────────────────────────────────────────
#define STATUS_LOG_INTERVAL_MS 60000UL

static void logSensorStatus() {
    static uint32_t lastStatusMs = 0;
    if (millis() - lastStatusMs < STATUS_LOG_INTERVAL_MS) return;
    lastStatusMs = millis();

    int  batt = batteryPercent();
    bool charging = batteryIsCharging();
    int  estimate = batteryGetEstimateMinutes();
    LOG_I("status", "bat=%d%% chg=%s est=%dmin dim=%s tela=%d boot#%u",
          batt, charging ? "sim" : "nao", estimate,
          powerIsDim() ? "sim" : "nao", currentScreen, (unsigned)rtcBootCount);

    uint16_t prox = ltr553ReadProx();
    uint16_t lux  = powerReadAmbientLight();
    LOG_I("sensor", "prox=%u lux=%u rot=%d", prox, lux, currentRotation);

    if (WiFi.status() == WL_CONNECTED) {
        LOG_I("wifi", "Conectado RSSI=%d IP=%s OTA=%s",
              WiFi.RSSI(), WiFi.localIP().toString().c_str(),
              otaIsActive() ? "sim" : "nao");
    }

    if (weatherData.valid) {
        LOG_I("clima", "%.1fC max=%.1f min=%.1f umid=%.0f%% trend=%d amostras",
              weatherData.tempCurrent, weatherData.tempMax, weatherData.tempMin,
              weatherData.humidity, weatherData.trendCount);
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
    M5.Speaker.setVolume(85);

    if (isColdBoot) {
        configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
    } else {
        applyConfiguredTimezone();
    }

    initSprite();
    ltr553Init();
    ledInit();
    powerInit();
    screenHomeInit();
    telnetLogInit();
    eventsInit();
    runtimeConfigLoad(g_runtimeCfg);
    runtimeConfigApply(g_runtimeCfg);

    const char* wakeReason = isColdBoot  ? "cold-boot"  :
                             isTimerWake ? "timer-wake" : "touch-wake";
    LOG_I("main", "=== BOOT #%u (%s) ===", (unsigned)rtcBootCount, wakeReason);

    // ── Wake silencioso por timer ──
    if (isTimerWake && rtcBootCount > 0) {
        LOG_I("main", "Wake por timer — atualizando clima silenciosamente");
        M5.Display.setBrightness(0);
        weatherData = rtcWeather;
        wifiConnectAndFetch(weatherData);
        rtcWeather = weatherData;
        rtcBootCount++;
        powerEnterDeepSleep();
        return;
    }

    // ── Cold boot ou wake por toque ──
    currentScreen = rtcScreen;
    weatherData   = rtcWeather;

    if (!isColdBoot && isTouchWake && rtcBootCount > 0) {
        LOG_I("main", "Wake por toque — restaurando estado");
        TimerPersist tp;
        tp.focused = rtcTimerFocused;
        for (int i = 0; i < MAX_TIMERS; i++) {
            tp.state[i]   = rtcTimerState[i];
            tp.minutes[i] = rtcTimerMinutes[i];
            tp.remainMs[i] = rtcTimerRemain[i];
        }
        screenHomeSetTimerPersist(tp);
        wifiBeginAsync(weatherData);
    } else {
        LOG_I("main", "Boot inicial — splash + init completo");
        drawSplash(*fb);
        pushFrame();
        wifiInit(weatherData);
        // wifiKeepAlive já aplicado por runtimeConfigApply() acima
        rtcWeather = weatherData;
    }

    rtcBootCount++;
    telnetLogSetBoot(rtcBootCount);
    needsRedraw = true;
    updateNextEvent();  // item #24
    LOG_I("main", "Setup concluido");
}

// ── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    M5.update();
    telnetLogUpdate();
    wifiPortalUpdate();   // item #23
    otaUpdate();          // item #21

    // ── Portal mode: tela de configuração WiFi ───────────────────────────────
    if (wifiIsPortalMode()) {
        static uint32_t lastPortalDraw = 0;
        if (millis() - lastPortalDraw > 2000) {
            lastPortalDraw = millis();
            drawWifiPortalScreen(*fb);
            pushFrame();
        }
        delay(50);
        return;
    }

    // ── Toque ────
    auto touch = M5.Touch.getDetail();

    if (touch.wasPressed()) {
        touchStartedInDim = powerIsDim();
        touchStartX       = touch.x;
        touchStartY       = touch.y;
        touchStartMs      = millis();
        powerOnTouch();
        if (touchStartedInDim) needsRedraw = true;
    }

    bool timerOrAlarm = screenHomeIsTimerActive() || screenHomeIsAlarmActive();
    if (touch.wasReleased() && (!touchStartedInDim || timerOrAlarm)) {
        uint32_t held        = millis() - touchStartMs;
        bool     longPress   = (held >= 800);
        int      swipeDeltaX = touch.x - touchStartX;
        int      swipeDeltaY = touch.y - touchStartY;
        bool     isSwipe     = (abs(swipeDeltaX) >= 30 && abs(swipeDeltaX) > abs(swipeDeltaY));

        if (currentScreen == 0 && screenHomeIsAlarmActive()) {
            screenHomeTimerTap(touchStartX);
            needsRedraw = true;
        } else if (isSwipe) {
            // Swipe horizontal → navega entre telas
            int dir = (swipeDeltaX < 0) ? 1 : -1;
            if (currentScreen == 3) {
                // Tela de configurações: só swipe direita sai (volta para System)
                if (dir == -1) {
                    animateTransition(3, 2, -1);
                    currentScreen = 2;
                    needsRedraw   = true;
                    LOG_I("main", "Settings: swipe dir -> tela 2");
                }
                // Swipe esquerda na settings → ignorado
            } else {
                int newScreen = (currentScreen + dir + SCREEN_COUNT) % SCREEN_COUNT;
                animateTransition(currentScreen, newScreen, dir);
                currentScreen = newScreen;
                needsRedraw   = true;
                LOG_I("main", "Swipe %s -> tela %d", dir > 0 ? "esq" : "dir", currentScreen);
            }
        } else if (currentScreen == 3) {
            // Tela de configurações: scroll vertical, tap e long press
            bool isVertSwipe = (abs(swipeDeltaY) >= 20 && abs(swipeDeltaY) > abs(swipeDeltaX));
            if (isVertSwipe) {
                g_settingsScrollTarget = constrain(g_settingsScrollTarget - swipeDeltaY,
                                                   0, screenSettingsMaxScroll());
                needsRedraw = true;
            } else if (longPress) {
                screenSettingsHandleLongPress(touchStartX, touchStartY, (int)g_settingsScrollAnim);
                // Se chegou aqui, nenhum restart ocorreu (ação não executada)
            } else {
                if (screenSettingsHandleTap(touchStartX, touchStartY,
                                             g_runtimeCfg, (int)g_settingsScrollAnim)) {
                    runtimeConfigSave(g_runtimeCfg);
                    runtimeConfigApply(g_runtimeCfg);
                    needsRedraw = true;
                }
            }
        } else if (currentScreen == 0 && touchStartY >= TIMER_ZONE_Y) {
            // Timer slot tabs (y=TIMER_ZONE_Y+5, 48×20px, gap=6)
            if (touchStartY < TIMER_ZONE_Y + 26) {
                // Toque na área dos tabs → switch slot
                int tabW = 48, gap = 6;
                int startX = 160 - (MAX_TIMERS * tabW + (MAX_TIMERS - 1) * gap) / 2;
                for (int i = 0; i < MAX_TIMERS; i++) {
                    int tx = startX + i * (tabW + gap);
                    if (touchStartX >= tx && touchStartX <= tx + tabW) {
                        screenHomeTimerSwitchSlot(i);
                        needsRedraw = true;
                        break;
                    }
                }
            } else if (longPress) {
                screenHomeTimerLongPress();
                LOG_I("main", "Timer T%d: long press", screenHomeGetFocusedSlot() + 1);
                needsRedraw = true;
            } else {
                screenHomeTimerTap(touchStartX);
                LOG_I("main", "Timer T%d: tap (x=%d)", screenHomeGetFocusedSlot() + 1, touchStartX);
                needsRedraw = true;
            }
        } else if (currentScreen == 1 && longPress) {
            // Long press na tela do clima → força atualização imediata
            wifiForceRefresh(weatherData);
            needsRedraw = true;
            LOG_I("main", "Clima: atualizacao forcada pelo usuario");
        } else if (!longPress && currentScreen != 3) {
            // Tap fora da zona do timer → avança para próxima tela (exceto settings)
            int newScreen = (currentScreen + 1) % SCREEN_COUNT;
            animateTransition(currentScreen, newScreen, 1);
            currentScreen = newScreen;
            needsRedraw   = true;
            LOG_I("main", "Tap -> tela %d", currentScreen);
        }
    }

    // ── Proximidade — acorda do dim ──
    static uint32_t lastProxMs = 0;
    if (powerIsDim() && (millis() - lastProxMs >= 100)) {
        lastProxMs = millis();
        uint16_t prox = ltr553ReadProx();
        if (prox > LTR553_PROX_THRESH) {
            LOG_I("ltr553", "Proximidade %u > %d — acordando", prox, LTR553_PROX_THRESH);
            powerOnTouch();
            needsRedraw = true;
        }
    }

    // ── Acelerômetro — acorda do dim ao detectar movimento ──
    static uint32_t lastAccelMs   = 0;
    static float    lastAccelMag  = -1.0f;  // -1 = não inicializado
    if (powerIsDim() && powerIsAccelWakeEnabled() && (millis() - lastAccelMs >= 200)) {
        lastAccelMs = millis();
        float ax, ay, az;
        if (M5.Imu.getAccel(&ax, &ay, &az)) {
            float mag = sqrtf(ax * ax + ay * ay + az * az);
            if (lastAccelMag >= 0.0f) {
                float delta = fabsf(mag - lastAccelMag);
                if (delta > ACCEL_WAKE_THRESHOLD) {
                    LOG_I("imu", "Movimento detectado (delta=%.2f) — acordando", delta);
                    powerOnTouch();
                    needsRedraw = true;
                    lastAccelMag = -1.0f;  // reinicia para evitar wake em cadeia
                } else {
                    lastAccelMag = mag;
                }
            } else {
                lastAccelMag = mag;
            }
        }
    }
    if (!powerIsDim()) lastAccelMag = -1.0f;  // reinicia ao sair do dim

    // ── Orientação ──
    if (!powerIsDim()) updateOrientation();

    // ── Dim ──
    bool wasDim = powerIsDim();
    powerUpdate(screenHomeIsAlarmActive() || screenHomeIsTimerActive());
    if (wasDim != powerIsDim()) needsRedraw = true;

    // ── WiFi / clima ──
    wifiScheduleUpdate(weatherData);
    wifiCheckPortal();  // item #23

    // ── Eventos ──
    updateNextEvent();

    // ── Deep sleep ──
    if (!screenHomeIsTimerActive() && !screenHomeIsAlarmActive() &&
        powerShouldDeepSleep()) {

        LOG_I("main", "Deep sleep — salvando estado");
        rtcScreen  = currentScreen;
        rtcWeather = weatherData;

        TimerPersist tp = screenHomeGetTimerPersist();
        rtcTimerFocused = tp.focused;
        for (int i = 0; i < MAX_TIMERS; i++) {
            rtcTimerState[i]   = tp.state[i];
            rtcTimerMinutes[i] = tp.minutes[i];
            rtcTimerRemain[i]  = tp.remainMs[i];
        }

        ledOff();
        powerEnterDeepSleep();
        return;
    }

    // ── Alarme sonoro ──
    bool alarmActive = screenHomeIsAlarmActive();
    if (alarmActive) {
        if (!alarmWasActive) {
            M5.Speaker.setVolume(160);  // mais alto que a UI
            lastBeepMs = 0;             // dispara o primeiro chime imediatamente
        }
        uint32_t now = millis();
        if (now - lastBeepMs >= 3200) {
            M5.Speaker.playWav(CHIME_WAV, CHIME_WAV_LEN);
            lastBeepMs = now;
        }
        if (((now / 400) % 2 == 0) != ((lastDrawMs / 400) % 2 == 0))
            needsRedraw = true;
    } else if (alarmWasActive) {
        M5.Speaker.stop();
        M5.Speaker.setVolume(85);  // restaura volume de UI
        LOG_I("main", "Alarme encerrado");
    }
    alarmWasActive = alarmActive;

    // ── Log periódico ──
    logSensorStatus();

    // ── Redesenho em dim ──
    if (powerIsDim() && currentScreen == 0 && !needsRedraw) {
        struct tm t;
        if (getLocalTime(&t, 0) && t.tm_sec == 0 && t.tm_min != lastDimMinute) {
            needsRedraw   = true;
            lastDimMinute = t.tm_min;
        }
    }
    if (!powerIsDim()) lastDimMinute = 255;

    // ── LEDs ──
    ledUpdate(powerIsDim(), alarmActive, screenHomeIsTimerRunning());

    // ── Scroll suave das configurações (lerp ease-out) ──
    if (currentScreen == 3) {
        float diff = (float)g_settingsScrollTarget - g_settingsScrollAnim;
        if (fabsf(diff) > 0.5f) {
            g_settingsScrollAnim += diff * 0.22f;
            needsRedraw = true;
        } else {
            g_settingsScrollAnim = (float)g_settingsScrollTarget;
        }
    }

    if (powerIsDim() && !needsRedraw) { delay(100); return; }

    uint32_t now = millis();
    bool timeToRefresh = (now - lastDrawMs >= (alarmActive ? 400u : 1000u));
    if (needsRedraw || timeToRefresh) {
        drawCurrentScreen(*fb);
        pushFrame();
        lastDrawMs  = now;
        needsRedraw = false;
    }

    delay(20);
}
