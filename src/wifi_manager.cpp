#include "wifi_manager.h"
#include "config.h"
#include "logger.h"
#include <WiFi.h>
#include <time.h>

// ── Estado async (volátil, reseta a cada boot) ────────────────────────────────
enum AsyncState { ASYNC_IDLE, ASYNC_CONNECTING, ASYNC_NTP_SYNCING };

static AsyncState   _state        = ASYNC_IDLE;
static WeatherData* _asyncOut     = nullptr;
static uint32_t     _asyncStartMs = 0;
static bool         _firstFetch   = true;
static bool         _keepAlive    = false;

static uint32_t _lastFetchMs = 0;

// ── Helpers internos ──────────────────────────────────────────────────────────
static void wifiOff() {
    if (_keepAlive) {
        LOG_I("wifi", "WiFi mantido ativo (cliente Telnet conectado)");
        return;
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    LOG_I("wifi", "WiFi OFF");
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
                struct tm _tmcheck;
                bool timeValid = getLocalTime(&_tmcheck, 0) && (time(nullptr) > 1577836800L);
                if (timeValid) {
                    LOG_I("wifi", "Conectado (RSSI %d dBm) — RTC valido, pulando NTP",
                          WiFi.RSSI());
                    finishAsync();
                } else {
                    LOG_I("wifi", "Conectado (RSSI %d dBm) — sincronizando NTP",
                          WiFi.RSSI());
                    configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
                    _asyncStartMs = millis();
                    _state        = ASYNC_NTP_SYNCING;
                }
            } else if (millis() - _asyncStartMs >= 10000) {
                LOG_W("wifi", "Timeout de conexao");
                wifiOff();
                _state       = ASYNC_IDLE;
                _firstFetch  = false;
                _lastFetchMs = millis();
            }
            break;

        case ASYNC_NTP_SYNCING: {
            struct tm timeinfo;
            bool ntpOk   = getLocalTime(&timeinfo, 0);
            bool timeout = millis() - _asyncStartMs >= 10000;
            if (ntpOk)            LOG_I("wifi", "NTP sincronizado");
            if (timeout && !ntpOk) LOG_W("wifi", "NTP timeout");
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

    // Se keep-alive manteve WiFi ativo, pula direto para o fetch
    if (WiFi.status() == WL_CONNECTED) {
        LOG_I("wifi", "Ja conectado (keep-alive) — buscando clima");
        finishAsync();
        return;
    }

    _state = ASYNC_CONNECTING;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    LOG_I("wifi", "Conectando (async)...");
}

bool wifiConnectAndFetch(WeatherData& out) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    LOG_I("wifi", "Conectando...");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(250);
    }
    if (WiFi.status() != WL_CONNECTED) {
        LOG_W("wifi", "Timeout — sem conexao");
        wifiOff();
        return false;
    }
    LOG_I("wifi", "Conectado — RSSI %d dBm  IP %s",
          WiFi.RSSI(), WiFi.localIP().toString().c_str());

    configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
    struct tm timeinfo;
    bool ntpOk = false;
    for (int i = 0; i < 20 && !ntpOk; i++) {
        ntpOk = getLocalTime(&timeinfo);
        if (!ntpOk) delay(500);
    }
    if (ntpOk) LOG_I("wifi", "NTP sincronizado");
    else       LOG_W("wifi", "NTP timeout");

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
    if (_state != ASYNC_IDLE) {
        _asyncOut = &weatherData;
        pollAsync();
        return;
    }
    if (_firstFetch) return;
    if (millis() - _lastFetchMs >= WEATHER_UPDATE_INTERVAL_MS) {
        LOG_I("wifi", "Atualizacao programada do clima");
        wifiBeginAsync(weatherData);
    }
}

bool wifiIsFetching() {
    return _state != ASYNC_IDLE;
}

void wifiSetKeepAlive(bool keep) {
    _keepAlive = keep;
    if (!keep && _state == ASYNC_IDLE && WiFi.status() == WL_CONNECTED) {
        // Cliente desconectou e não há fetch em curso — desliga WiFi agora
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        LOG_I("wifi", "WiFi OFF (keep-alive liberado)");
    }
}

int wifiGetRSSI() {
    if (WiFi.status() != WL_CONNECTED) return 0;
    return WiFi.RSSI();
}
