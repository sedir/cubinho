#include "power_manager.h"
#include "config.h"
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_sleep.h>

// Estados do gerenciador de energia
enum PowerState { POWER_ACTIVE, POWER_DIM };

static PowerState _powerState  = POWER_ACTIVE;
static uint32_t   _lastTouchMs = 0;

void powerInit() {
    _lastTouchMs = millis();
    _powerState  = POWER_ACTIVE;
    M5.Display.setBrightness(BRIGHTNESS_ACTIVE);
}

void powerOnTouch() {
    _lastTouchMs = millis();

    // Se estava em dim, restaura e sinaliza que precisa redesenhar
    if (_powerState == POWER_DIM) {
        _powerState = POWER_ACTIVE;
        M5.Display.setBrightness(BRIGHTNESS_ACTIVE);
        Serial.println("[power] Display restaurado — ACTIVE");
    }
}

void powerUpdate() {
    int pct = batteryPercent();

    // Bateria critica: mesma inatividade de DIM_TIMEOUT_MS
    // (deep sleep já cuida da economia; timeout curto congelava o display)
    if (pct >= 0 && pct <= 10 && _powerState == POWER_ACTIVE) {
        if (millis() - _lastTouchMs > DIM_TIMEOUT_MS) {
            _powerState = POWER_DIM;
            M5.Display.setBrightness(BRIGHTNESS_DIM);
            Serial.println("[power] Bateria <= 10% — dim forcado");
        }
        return;
    }

    // Dim por inatividade
    if (_powerState == POWER_ACTIVE &&
        (millis() - _lastTouchMs > DIM_TIMEOUT_MS)) {
        _powerState = POWER_DIM;
        M5.Display.setBrightness(BRIGHTNESS_DIM);
        Serial.println("[power] Inatividade — display em dim");
    }
}

bool powerIsDim() {
    return _powerState == POWER_DIM;
}

bool powerShouldDeepSleep() {
    return (millis() - _lastTouchMs) > DEEP_SLEEP_TIMEOUT_MS;
}

void powerEnterDeepSleep() {
    M5.Speaker.stop();
    M5.Display.setBrightness(0);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(50);

    // Acorda por toque (INT do FT6336U, ativo em LOW) ou por timer (clima)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)DEEP_SLEEP_WAKEUP_GPIO, 0);
    esp_sleep_enable_timer_wakeup((uint64_t)WEATHER_UPDATE_INTERVAL_MS * 1000ULL);

    Serial.println("[power] Entrando em deep sleep");
    Serial.flush();
    esp_deep_sleep_start();
    // nunca retorna
}

int batteryPercent() {
    return M5.Power.getBatteryLevel();
}

bool batteryIsCharging() {
    return M5.Power.isCharging();
}
