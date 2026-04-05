#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>
#include <time.h>

// ── Estado async (volátil, reseta a cada boot) ────────────────────────────────
enum AsyncState { ASYNC_IDLE, ASYNC_CONNECTING, ASYNC_NTP_SYNCING };

static AsyncState   _state        = ASYNC_IDLE;
static WeatherData* _asyncOut     = nullptr;
static uint32_t     _asyncStartMs = 0;
static bool         _firstFetch   = true;

RTC_DATA_ATTR static uint32_t _lastFetchMs = 0;  // não usado após wake (millis reseta)

// ── Helpers internos ──────────────────────────────────────────────────────────
static void wifiOff() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[wifi] WiFi OFF");
}

// Conclui a operação async: busca clima, desliga WiFi, arma próximo ciclo.
static void finishAsync() {
    if (_asyncOut) weatherFetch(*_asyncOut);
    wifiOff();
    _state       = ASYNC_IDLE;
    _firstFetch  = false;
    _lastFetchMs = millis();
}

// Avança a state machine — chamado a cada iteração do loop.
static void pollAsync() {
    switch (_state) {
        case ASYNC_IDLE:
            return;

        case ASYNC_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("\n[wifi] Conectado — sincronizando NTP");
                configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
                _asyncStartMs = millis();
                _state        = ASYNC_NTP_SYNCING;
            } else if (millis() - _asyncStartMs >= 10000) {
                Serial.println("\n[wifi] Timeout WiFi");
                wifiOff();
                _state       = ASYNC_IDLE;
                _firstFetch  = false;
                _lastFetchMs = millis();
            }
            break;

        case ASYNC_NTP_SYNCING: {
            struct tm timeinfo;
            bool ntpOk  = getLocalTime(&timeinfo, 0);
            bool timeout = millis() - _asyncStartMs >= 10000;
            if (ntpOk)   Serial.println("[wifi] NTP OK");
            if (timeout && !ntpOk) Serial.println("[wifi] NTP timeout");
            if (ntpOk || timeout)  finishAsync();
            break;
        }
    }
}

// ── API pública ───────────────────────────────────────────────────────────────

void wifiBeginAsync(WeatherData& out) {
    if (_state != ASYNC_IDLE) return;
    _asyncOut     = &out;
    _asyncStartMs = millis();
    _state        = ASYNC_CONNECTING;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[wifi] Conectando (async)");
}

bool wifiConnectAndFetch(WeatherData& out) {
    // Versão bloqueante — usar apenas quando a tela está apagada (wake por timer).
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[wifi] Conectando");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        Serial.print(".");
        delay(250);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\n[wifi] Timeout — sem conexao");
        wifiOff();
        return false;
    }
    Serial.println(" OK");

    configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
    Serial.print("[wifi] Aguardando NTP");
    struct tm timeinfo;
    bool ntpOk = false;
    for (int i = 0; i < 20 && !ntpOk; i++) {
        ntpOk = getLocalTime(&timeinfo);
        if (!ntpOk) { Serial.print("."); delay(500); }
    }
    Serial.println(ntpOk ? " OK" : " FALHOU");

    bool ok = weatherFetch(out);
    wifiOff();
    _firstFetch  = false;
    _lastFetchMs = millis();
    return ok;
}

void wifiInit(WeatherData& weatherData) {
    wifiConnectAndFetch(weatherData);
}

void wifiScheduleUpdate(WeatherData& weatherData) {
    // Avança operação async em andamento
    if (_state != ASYNC_IDLE) {
        _asyncOut = &weatherData;  // garante ponteiro atualizado
        pollAsync();
        return;
    }
    if (_firstFetch) return;
    if (millis() - _lastFetchMs >= WEATHER_UPDATE_INTERVAL_MS) {
        Serial.println("[wifi] Atualizacao programada do clima");
        wifiBeginAsync(weatherData);
    }
}

bool wifiIsFetching() {
    return _state != ASYNC_IDLE;
}
