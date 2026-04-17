#include "power_manager.h"
#include "config.h"
#include "logger.h"
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_sleep.h>

// ── Estados ──────────────────────────────────────────────────────────────────
enum PowerState { POWER_ACTIVE, POWER_DIM };

// ── CPU boost para interações e animações ────────────────────────────────────
static esp_pm_lock_handle_t _cpuBoostLock   = NULL;
static uint32_t             _cpuBoostUntilMs = 0;
#define CPU_BOOST_DURATION_MS 500   // cobre animação (~190ms) + renders pós-toque

static PowerState _powerState  = POWER_ACTIVE;
static uint32_t   _lastTouchMs = 0;

// ── Parâmetros runtime (ajustáveis via RuntimeConfig) ────────────────────────
static uint32_t _dimTimeoutMs       = DIM_TIMEOUT_MS;
static uint32_t _deepSleepTimeoutMs = DEEP_SLEEP_TIMEOUT_MS;
static uint32_t _weatherIntervalMs  = WEATHER_UPDATE_INTERVAL_MS;
static int      _brightnessActiveRt = BRIGHTNESS_ACTIVE;
static bool     _autoBrightnessEn   = AUTO_BRIGHTNESS_ENABLED;

// ── Auto-brilho ALS (item #19) ──────────────────────────────────────────────
#define LTR553_ADDR          0x23
#define LTR553_ALS_CONTR     0x80
#define LTR553_ALS_DATA_CH1_0 0x88
#define LTR553_ALS_DATA_CH1_1 0x89
#define LTR553_ALS_DATA_CH0_0 0x8A
#define LTR553_ALS_DATA_CH0_1 0x8B
#define LTR553_I2C_FREQ      400000

static bool    _alsInitialized      = false;
static uint8_t _currentBrightness   = BRIGHTNESS_ACTIVE;
static uint8_t _targetBrightness    = BRIGHTNESS_ACTIVE;
static uint32_t _lastFadeMs         = 0;

static uint8_t clampBrightness(int brightness) {
    return (uint8_t)constrain(brightness, 0, 255);
}

static void alsInit() {
    // ALS_CONTR: active mode, gain 1x
    M5.In_I2C.writeRegister8(LTR553_ADDR, LTR553_ALS_CONTR, 0x01, LTR553_I2C_FREQ);
    delay(10);
    _alsInitialized = true;
    LOG_I("als", "Sensor de luz iniciado");
}

uint16_t powerReadAmbientLight() {
    if (!_alsInitialized) return 0;
    uint8_t ch0_lo = M5.In_I2C.readRegister8(LTR553_ADDR, LTR553_ALS_DATA_CH0_0, LTR553_I2C_FREQ);
    uint8_t ch0_hi = M5.In_I2C.readRegister8(LTR553_ADDR, LTR553_ALS_DATA_CH0_1, LTR553_I2C_FREQ);
    return ((uint16_t)ch0_hi << 8) | ch0_lo;
}

// ── Fade suave: caminha _currentBrightness em direção a _targetBrightness ────
static void applyBrightnessFade() {
    if (_currentBrightness == _targetBrightness) return;

    // Roda a cada ~20ms → ~50 fps de fade
    if (millis() - _lastFadeMs < BRIGHTNESS_FADE_INTERVAL_MS) return;
    _lastFadeMs = millis();

    if (_currentBrightness < _targetBrightness) {
        int next = (int)_currentBrightness + BRIGHTNESS_FADE_STEP;
        _currentBrightness = (next > _targetBrightness) ? _targetBrightness : (uint8_t)next;
    } else {
        int next = (int)_currentBrightness - BRIGHTNESS_FADE_STEP;
        _currentBrightness = (next < _targetBrightness) ? _targetBrightness : (uint8_t)next;
    }

    M5.Display.setBrightness(_currentBrightness);
}

// ── Auto-brilho: atualiza _targetBrightness a partir do sensor ALS ──────────
static void updateAutoBrightnessTarget() {
    if (!_autoBrightnessEn) return;
    if (_powerState != POWER_ACTIVE || !_alsInitialized) return;
    if (millis() - _lastTouchMs < 2000) return;  // não ajusta enquanto em uso

    static uint32_t lastAlsMs = 0;
    if (millis() - lastAlsMs < 2000) return;  // lê a cada 2s
    lastAlsMs = millis();

    uint16_t lux = powerReadAmbientLight();
    // Mapeamento: 0–2000 lux → MIN–MAX brilho
    int target;
    if (lux < 10)        target = AUTO_BRIGHTNESS_MIN;
    else if (lux > 2000) target = AUTO_BRIGHTNESS_MAX;
    else {
        float ratio = (float)lux / 2000.0f;
        target = AUTO_BRIGHTNESS_MIN + (int)(ratio * (AUTO_BRIGHTNESS_MAX - AUTO_BRIGHTNESS_MIN));
    }
    // EMA leve no alvo para evitar jitter no sensor
    _targetBrightness = clampBrightness((_targetBrightness * 3 + (uint8_t)target) / 4);
}

// ── Estimativa de bateria (item #22) ─────────────────────────────────────────
#define BATT_HISTORY 10
static uint8_t  _battPct[BATT_HISTORY]  = {};
static uint32_t _battTime[BATT_HISTORY] = {};
static uint8_t  _battIdx   = 0;
static uint8_t  _battCount = 0;
static uint32_t _lastBattTick = 0;

void powerBatteryTick() {
    if (millis() - _lastBattTick < 60000) return;  // uma vez por minuto
    _lastBattTick = millis();

    int pct = M5.Power.getBatteryLevel();
    if (pct < 0) return;

    _battPct[_battIdx]  = (uint8_t)pct;
    _battTime[_battIdx] = millis();
    _battIdx = (_battIdx + 1) % BATT_HISTORY;
    if (_battCount < BATT_HISTORY) _battCount++;
}

int batteryGetEstimateMinutes() {
    if (_battCount < 3) return -1;  // precisa de pelo menos 3 pontos

    // Pega o mais antigo e o mais recente
    int oldest = (_battIdx + BATT_HISTORY - _battCount) % BATT_HISTORY;
    int newest = (_battIdx + BATT_HISTORY - 1) % BATT_HISTORY;

    int pctDiff   = (int)_battPct[oldest] - (int)_battPct[newest];
    uint32_t msElapsed = _battTime[newest] - _battTime[oldest];

    if (pctDiff <= 0 || msElapsed < 60000) return -1;  // carregando ou dado insuficiente

    // Taxa: %/minuto → minutos restantes
    float ratePerMin = (float)pctDiff / ((float)msElapsed / 60000.0f);
    int remaining = (int)((float)_battPct[newest] / ratePerMin);
    return max(0, remaining);
}

// ── API pública ──────────────────────────────────────────────────────────────
void powerInit() {
    _lastTouchMs = millis();
    _powerState  = POWER_ACTIVE;
    _currentBrightness = clampBrightness(_brightnessActiveRt);
    _targetBrightness  = clampBrightness(_brightnessActiveRt);
    M5.Display.setBrightness(_brightnessActiveRt);
    alsInit();

    if (esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "cpu_boost", &_cpuBoostLock) != ESP_OK) {
        _cpuBoostLock = NULL;
        LOG_W("power", "CPU boost lock nao criado");
    }
}

void powerOnTouch() {
    _lastTouchMs = millis();
    if (_powerState == POWER_DIM) {
        _powerState = POWER_ACTIVE;
        // Restaura sempre para o brilho ativo configurado.
        // Usar _brightnessBeforeDim causava brilho baixo porque o auto-brilho
        // pode ter rebaixado _currentBrightness antes do dim ser trigado,
        // e esse valor baixo seria usado como alvo de restauração.
        _targetBrightness = clampBrightness(_brightnessActiveRt);
        LOG_I("power", "Display restaurado — ACTIVE (fade)");
    }
}

void powerBoostCpu() {
    if (!_cpuBoostLock) return;
    if (_cpuBoostUntilMs == 0) {
        esp_pm_lock_acquire(_cpuBoostLock);
    }
    _cpuBoostUntilMs = millis() + CPU_BOOST_DURATION_MS;
}

void powerUpdate(bool keepAwake) {
    // Libera boost de CPU quando o timer expirar
    if (_cpuBoostLock && _cpuBoostUntilMs > 0 && millis() >= _cpuBoostUntilMs) {
        esp_pm_lock_release(_cpuBoostLock);
        _cpuBoostUntilMs = 0;
    }

    int pct = batteryPercent();
    powerBatteryTick();

    // Alarme ativo: reseta o timer de inatividade a cada ciclo para evitar dim
    if (keepAwake) {
        _lastTouchMs = millis();
        if (_powerState == POWER_DIM) {
            _powerState = POWER_ACTIVE;
            _targetBrightness = clampBrightness(_brightnessActiveRt);
            LOG_I("power", "Alarme ativo — restaurando display");
        }
        applyBrightnessFade();
        return;
    }

    uint8_t dimTarget = clampBrightness(BRIGHTNESS_DIM);

    // Bateria critica: dim após timeout normal
    if (pct >= 0 && pct <= 10 && _powerState == POWER_ACTIVE) {
        if (millis() - _lastTouchMs > _dimTimeoutMs) {
            _powerState = POWER_DIM;
            _targetBrightness = dimTarget;  // fade suave até dim
            LOG_I("power", "Bateria <= 10%% — dim (fade)");
        }
        // Fade continua rodando mesmo durante bateria crítica
        applyBrightnessFade();
        return;
    }

    // Dim por inatividade
    if (_powerState == POWER_ACTIVE &&
        (millis() - _lastTouchMs > _dimTimeoutMs)) {
        _powerState = POWER_DIM;
        _targetBrightness = dimTarget;  // fade suave até dim
        LOG_I("power", "Inatividade — display em dim (fade)");
    }

    // Auto-brilho atualiza o alvo quando ativo
    updateAutoBrightnessTarget();

    // Fade suave — roda sempre para caminhar até o alvo
    applyBrightnessFade();
}

bool powerIsDim() {
    return _powerState == POWER_DIM;
}

bool powerShouldDeepSleep() {
    if (_deepSleepTimeoutMs == 0) return false;  // 0 = nunca dormir
    if (powerIsOnExternalPower()) return false;  // cabo conectado → nunca dormir
    return (millis() - _lastTouchMs) > _deepSleepTimeoutMs;
}

void powerEnterDeepSleep() {
    M5.Speaker.stop();
    M5.Display.setBrightness(0);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(50);

    esp_sleep_enable_ext0_wakeup((gpio_num_t)DEEP_SLEEP_WAKEUP_GPIO, 0);

    // Compensa o tempo de boot + fetch (~5s) para evitar drift acumulado ao longo do dia.
    // Sem compensação: cada ciclo de 30 min perde ~5s → ~4h de skew em 30 dias.
    constexpr uint64_t kBootCompensationUs = 5000000ULL;  // 5 segundos em µs
    uint64_t sleepUs = (uint64_t)_weatherIntervalMs * 1000ULL;
    if (sleepUs > kBootCompensationUs) sleepUs -= kBootCompensationUs;
    esp_sleep_enable_timer_wakeup(sleepUs);

    LOG_I("power", "Entrando em deep sleep");
    Serial.flush();
    esp_deep_sleep_start();
}

int batteryPercent() {
    static int      _cached = -1;
    static uint32_t _lastMs = 0;
    if (_cached < 0 || millis() - _lastMs >= 30000) {
        _cached = M5.Power.getBatteryLevel();
        _lastMs = millis();
    }
    return _cached;
}

bool batteryIsCharging() {
    return M5.Power.isCharging();
}

bool powerIsOnExternalPower() {
    // AXP2101: isCharging() vira false quando a bateria atinge a regulacao (~100%).
    // Nesse estado o cabo ainda esta conectado e fornece energia — cubrimos com
    // uma heuristica de nivel >= 100%. Na pratica o nivel so se mantem em 100
    // enquanto o cabo estiver ligado; apos desconectar cai rapidamente para 99.
    if (M5.Power.isCharging()) return true;
    int pct = M5.Power.getBatteryLevel();
    return (pct >= 100);
}

// ── Setters de configuração runtime ─────────────────────────────────────────
void powerSetBrightnessActive(int brightness) {
    _brightnessActiveRt = clampBrightness(brightness);
    if (_powerState == POWER_ACTIVE) {
        _targetBrightness = clampBrightness(_brightnessActiveRt);
    }
    LOG_I("power", "Brilho ativo -> %d", _brightnessActiveRt);
}

void powerSetDimTimeout(uint32_t ms) {
    _dimTimeoutMs = ms;
    LOG_I("power", "Dim timeout -> %lu ms", (unsigned long)ms);
}

void powerSetDeepSleepTimeout(uint32_t ms) {
    _deepSleepTimeoutMs = ms;
    LOG_I("power", "Deep sleep timeout -> %lu ms", (unsigned long)ms);
}

void powerSetAutoBrightness(bool enabled) {
    _autoBrightnessEn = enabled;
    if (!enabled && _powerState == POWER_ACTIVE) {
        // Restaura para o brilho fixo configurado
        _targetBrightness = _brightnessActiveRt;
    }
    LOG_I("power", "Auto-brilho -> %s", enabled ? "ON" : "OFF");
}

void powerSetWeatherInterval(uint32_t ms) {
    _weatherIntervalMs = (ms > 0) ? ms : WEATHER_UPDATE_INTERVAL_MS;
    LOG_I("power", "Weather interval (deep sleep timer) -> %lu ms", (unsigned long)_weatherIntervalMs);
}

static bool _accelWakeEn = ACCEL_WAKE_ENABLED;

void powerSetAccelWake(bool enabled) {
    _accelWakeEn = enabled;
    LOG_I("power", "Accel wake -> %s", enabled ? "ON" : "OFF");
}

bool powerIsAccelWakeEnabled() {
    return _accelWakeEn;
}
