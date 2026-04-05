#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>
#include <time.h>

static bool _fetching = false;
static bool _firstFetch = true;

// Sobrevivem ao deep sleep — NTP não precisa re-sincronizar a cada wake
RTC_DATA_ATTR static bool     _ntpSynced   = false;
RTC_DATA_ATTR static uint32_t _lastFetchMs = 0;  // não usado após wake (millis reseta)

// Sincroniza NTP — chama apenas no boot ou após reconexão
static bool syncNTP() {
    configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
    Serial.print("[wifi] Aguardando NTP");
    struct tm timeinfo;
    for (int i = 0; i < 20; i++) {
        if (getLocalTime(&timeinfo)) {
            Serial.println(" OK");
            return true;
        }
        Serial.print(".");
        delay(500);
    }
    Serial.println(" FALHOU");
    return false;
}

bool wifiConnectAndFetch(WeatherData& out) {
    _fetching = true;

    // Liga o WiFi e conecta
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
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        _fetching = false;
        return false;
    }
    Serial.println(" OK");

    // Sincroniza NTP apenas na primeira vez (ou se ainda não sincronizou)
    if (!_ntpSynced) {
        _ntpSynced = syncNTP();
    }

    // Busca clima
    bool ok = weatherFetch(out);

    // Desliga WiFi para economizar energia (~80–170mA)
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[wifi] Desconectado — WiFi OFF");

    _fetching = false;
    return ok;
}

void wifiInit(WeatherData& weatherData) {
    // Executa primeiro fetch no boot
    wifiConnectAndFetch(weatherData);
    _lastFetchMs = millis();
    _firstFetch  = false;
}

void wifiScheduleUpdate(WeatherData& weatherData) {
    // Não disparar se já está em andamento
    if (_fetching) return;

    // Primeiro fetch já foi feito no init
    if (_firstFetch) return;

    // Verifica se chegou a hora de atualizar
    if (millis() - _lastFetchMs >= WEATHER_UPDATE_INTERVAL_MS) {
        _lastFetchMs = millis();  // atualiza antes para evitar duplo disparo
        Serial.println("[wifi] Iniciando atualizacao programada do clima");
        wifiConnectAndFetch(weatherData);
    }
}

bool wifiIsFetching() {
    return _fetching;
}

void wifiResetFetchTimer() {
    _firstFetch  = false;
    _lastFetchMs = millis();  // millis() recomeça do zero após deep sleep
    // _ntpSynced já é RTC_DATA_ATTR — mantém valor do boot original
}
