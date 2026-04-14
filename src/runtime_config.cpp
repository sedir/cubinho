#include "runtime_config.h"
#include "config.h"
#include "wifi_manager.h"
#include "power_manager.h"
#include "screen_home.h"
#include <Preferences.h>

#ifndef CALENDAR_ICS_URL
#define CALENDAR_ICS_URL ""
#endif

static Preferences _prefs;
static char _calendarUrl[192] = CALENDAR_ICS_URL;

void runtimeConfigLoad(RuntimeConfig& cfg) {
    _prefs.begin("cfg", true);  // read-only
    cfg.wifiKeepAlive       = _prefs.getBool("keepalive", WIFI_KEEP_ALIVE);
    cfg.weatherIntervalMin  = _prefs.getInt ("wInterval", (int)(WEATHER_UPDATE_INTERVAL_MS / 60000UL));
    cfg.brightnessActive    = _prefs.getInt ("brightness", BRIGHTNESS_ACTIVE);
    cfg.dimTimeoutSec       = _prefs.getInt ("dimTimeout", (int)(DIM_TIMEOUT_MS / 1000UL));
    cfg.autoBrightness      = _prefs.getBool("autoBright", AUTO_BRIGHTNESS_ENABLED);
    cfg.deepSleepTimeoutMin = _prefs.getInt ("sleepMin",   (int)(DEEP_SLEEP_TIMEOUT_MS / 60000UL));
    cfg.accelWake           = _prefs.getBool("accelWake",  ACCEL_WAKE_ENABLED);
    for (int i = 0; i < MAX_TIMERS; i++) {
        char key[12];
        snprintf(key, sizeof(key), "tLabel%d", i);
        cfg.timerLabelPreset[i] = _prefs.getInt(key, i);
    }
    String calUrl = _prefs.getString("calUrl", CALENDAR_ICS_URL);
    strlcpy(_calendarUrl, calUrl.c_str(), sizeof(_calendarUrl));
    _prefs.end();
}

void runtimeConfigSave(const RuntimeConfig& cfg) {
    _prefs.begin("cfg", false);
    _prefs.putBool("keepalive", cfg.wifiKeepAlive);
    _prefs.putInt ("wInterval", cfg.weatherIntervalMin);
    _prefs.putInt ("brightness", cfg.brightnessActive);
    _prefs.putInt ("dimTimeout", cfg.dimTimeoutSec);
    _prefs.putBool("autoBright", cfg.autoBrightness);
    _prefs.putInt ("sleepMin",   cfg.deepSleepTimeoutMin);
    _prefs.putBool("accelWake",  cfg.accelWake);
    for (int i = 0; i < MAX_TIMERS; i++) {
        char key[12];
        snprintf(key, sizeof(key), "tLabel%d", i);
        _prefs.putInt(key, cfg.timerLabelPreset[i]);
    }
    _prefs.end();
}

void runtimeConfigApply(const RuntimeConfig& cfg) {
    wifiSetKeepAlive(cfg.wifiKeepAlive);
    wifiSetUpdateInterval((uint32_t)cfg.weatherIntervalMin * 60000UL);
    powerSetBrightnessActive(cfg.brightnessActive);
    powerSetDimTimeout((uint32_t)cfg.dimTimeoutSec * 1000UL);
    powerSetAutoBrightness(cfg.autoBrightness);
    uint32_t sleepMs = (cfg.deepSleepTimeoutMin > 0)
                     ? (uint32_t)cfg.deepSleepTimeoutMin * 60000UL
                     : 0;
    powerSetDeepSleepTimeout(sleepMs);
    powerSetWeatherInterval((uint32_t)cfg.weatherIntervalMin * 60000UL);
    powerSetAccelWake(cfg.accelWake);
    for (int i = 0; i < MAX_TIMERS; i++) {
        screenHomeSetTimerLabelPreset(i, cfg.timerLabelPreset[i]);
    }
}

void runtimeConfigClear() {
    _prefs.begin("cfg", false);
    _prefs.clear();
    _prefs.end();
    strlcpy(_calendarUrl, CALENDAR_ICS_URL, sizeof(_calendarUrl));
}

bool runtimeConfigHasCalendarUrl() {
    return _calendarUrl[0] != '\0';
}

void runtimeConfigGetCalendarUrl(char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    strlcpy(out, _calendarUrl, outSize);
}

void runtimeConfigSaveCalendarUrl(const char* url) {
    const char* safeUrl = url ? url : "";
    strlcpy(_calendarUrl, safeUrl, sizeof(_calendarUrl));
    _prefs.begin("cfg", false);
    _prefs.putString("calUrl", _calendarUrl);
    _prefs.end();
}
