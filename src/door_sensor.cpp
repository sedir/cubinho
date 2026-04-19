#include "door_sensor.h"
#include <M5Unified.h>
#include <Preferences.h>
#include <time.h>
#include <math.h>
#include "logger.h"

#define DOOR_SAMPLE_INTERVAL_MS   200    // 5 Hz
#define DOOR_OPEN_THRESHOLD_G     0.35f  // desvio do baseline para considerar aberta
#define DOOR_DEBOUNCE_MS          500    // estabilidade exigida antes de confirmar mudança
#define DOOR_BASELINE_CALIB_MS    3000   // janela de captura do baseline inicial
#define DOOR_LONG_OPEN_MS         60000  // alerta após 60 s aberta

static Preferences _prefs;

static bool     _enabled          = true;
static bool     _baselineReady    = false;
static uint32_t _baselineStartMs  = 0;
static float    _bx = 0.0f, _by = 0.0f, _bz = 0.0f;
static int      _baselineSamples  = 0;

static uint32_t _lastSampleMs = 0;

// Debounce do estado
static bool     _rawOpen        = false;
static uint32_t _rawChangedMs   = 0;
static bool     _doorOpen       = false;
static uint32_t _openSinceMs    = 0;

// Contagem diária
static int _todayCount     = 0;
static int _yesterdayCount = 0;
static int _lastYearDay    = -1;

static int currentYearDay() {
    struct tm now;
    if (!getLocalTime(&now, 0)) return -1;
    return now.tm_yday;
}

static void loadNvs() {
    _prefs.begin("door", true);
    _todayCount     = _prefs.getInt("today", 0);
    _yesterdayCount = _prefs.getInt("yday",  0);
    _lastYearDay    = _prefs.getInt("day",  -1);
    _prefs.end();
}

static void saveNvs() {
    _prefs.begin("door", false);
    _prefs.putInt("today", _todayCount);
    _prefs.putInt("yday",  _yesterdayCount);
    _prefs.putInt("day",   _lastYearDay);
    _prefs.end();
}

static void rolloverIfNeeded() {
    int today = currentYearDay();
    if (today < 0 || today == _lastYearDay) return;
    if (_lastYearDay >= 0) {
        _yesterdayCount = _todayCount;
    }
    _todayCount  = 0;
    _lastYearDay = today;
    saveNvs();
    LOG_I("door", "Rollover: ontem=%d, hoje=0", _yesterdayCount);
}

void doorSensorInit() {
    loadNvs();
    rolloverIfNeeded();
    _baselineReady   = false;
    _baselineStartMs = millis();
    _baselineSamples = 0;
    _bx = _by = _bz = 0.0f;
    _rawOpen      = false;
    _doorOpen     = false;
    _openSinceMs  = 0;
    LOG_I("door", "Sensor de porta iniciado (calibrando baseline %d ms)",
          DOOR_BASELINE_CALIB_MS);
}

void doorSensorRecalibrate() {
    _baselineReady   = false;
    _baselineStartMs = millis();
    _baselineSamples = 0;
    _bx = _by = _bz = 0.0f;
    LOG_I("door", "Baseline recalibrando...");
}

void doorSensorSetEnabled(bool en) {
    if (_enabled == en) return;
    _enabled = en;
    if (en) doorSensorRecalibrate();
    LOG_I("door", "Enabled=%d", (int)en);
}

bool doorSensorIsEnabled()       { return _enabled; }
bool doorSensorIsOpen()          { return _doorOpen; }
int  doorSensorTodayCount()      { return _todayCount; }
int  doorSensorYesterdayCount()  { return _yesterdayCount; }

uint32_t doorSensorOpenDurationMs() {
    if (!_doorOpen) return 0;
    return millis() - _openSinceMs;
}

bool doorSensorIsLongOpen() {
    return _doorOpen && (doorSensorOpenDurationMs() >= DOOR_LONG_OPEN_MS);
}

void doorSensorUpdate() {
    if (!_enabled) return;

    uint32_t now = millis();
    if (now - _lastSampleMs < DOOR_SAMPLE_INTERVAL_MS) return;
    _lastSampleMs = now;

    float ax, ay, az;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return;

    // Calibração inicial — média dos primeiros N ms de amostras
    if (!_baselineReady) {
        _bx += ax; _by += ay; _bz += az;
        _baselineSamples++;
        if ((now - _baselineStartMs) >= DOOR_BASELINE_CALIB_MS && _baselineSamples > 5) {
            _bx /= _baselineSamples;
            _by /= _baselineSamples;
            _bz /= _baselineSamples;
            _baselineReady = true;
            LOG_I("door", "Baseline: (%.2f, %.2f, %.2f) [%d amostras]",
                  _bx, _by, _bz, _baselineSamples);
        }
        return;
    }

    float dx = ax - _bx;
    float dy = ay - _by;
    float dz = az - _bz;
    float delta = sqrtf(dx*dx + dy*dy + dz*dz);

    bool newRaw = (delta > DOOR_OPEN_THRESHOLD_G);
    if (newRaw != _rawOpen) {
        _rawOpen      = newRaw;
        _rawChangedMs = now;
    }

    if (_rawOpen != _doorOpen && (now - _rawChangedMs) >= DOOR_DEBOUNCE_MS) {
        _doorOpen = _rawOpen;
        if (_doorOpen) {
            _openSinceMs = now;
            rolloverIfNeeded();
            _todayCount++;
            saveNvs();
            LOG_I("door", "ABERTA (delta=%.2f g, hoje=%d)", delta, _todayCount);
        } else {
            uint32_t dur = now - _openSinceMs;
            LOG_I("door", "Fechada apos %lu s", (unsigned long)(dur / 1000));
        }
    }

    // Verifica rollover mesmo com porta parada (caso passe meia-noite sem eventos)
    static uint32_t lastRolloverCheck = 0;
    if (now - lastRolloverCheck > 60000) {
        lastRolloverCheck = now;
        rolloverIfNeeded();
    }
}
