#include "runtime_config.h"
#include "config.h"
#include "wifi_manager.h"
#include "power_manager.h"
#include <Preferences.h>

static Preferences _prefs;

void runtimeConfigLoad(RuntimeConfig& cfg) {
    _prefs.begin("cfg", true);  // read-only
    cfg.wifiKeepAlive       = _prefs.getBool("keepalive", WIFI_KEEP_ALIVE);
    cfg.weatherIntervalMin  = _prefs.getInt ("wInterval", (int)(WEATHER_UPDATE_INTERVAL_MS / 60000UL));
    cfg.brightnessActive    = _prefs.getInt ("brightness", BRIGHTNESS_ACTIVE);
    cfg.dimTimeoutSec       = _prefs.getInt ("dimTimeout", (int)(DIM_TIMEOUT_MS / 1000UL));
    cfg.autoBrightness      = _prefs.getBool("autoBright", AUTO_BRIGHTNESS_ENABLED);
    cfg.deepSleepTimeoutMin = _prefs.getInt ("sleepMin",   (int)(DEEP_SLEEP_TIMEOUT_MS / 60000UL));
    cfg.accelWake           = _prefs.getBool("accelWake",  ACCEL_WAKE_ENABLED);
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
}

void runtimeConfigClear() {
    _prefs.begin("cfg", false);
    _prefs.clear();
    _prefs.end();
}
