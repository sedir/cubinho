#include "weather_api.h"
#include "config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Mapeamento de código WMO para descrição em português
const char* wmoToDescription(int code) {
    if (code == 0)                          return "Ceu limpo";
    if (code >= 1  && code <= 3)            return "Parcialmente nublado";
    if (code == 45 || code == 48)           return "Neblina";
    if (code == 51 || code == 53 || code == 55) return "Chuvisco";
    if (code == 61 || code == 63 || code == 65) return "Chuva";
    if (code == 71 || code == 73 || code == 75) return "Neve";
    if (code == 80 || code == 81 || code == 82) return "Pancadas de chuva";
    if (code == 95)                         return "Trovoada";
    return "Variavel";
}


bool weatherFetch(WeatherData& out) {
    // Monta URL da API OpenMeteo — HTTP sem autenticação
    // snprintf evita fragmentação de heap causada por String + concatenação
    char url[256];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m,weather_code,windspeed_10m"
        "&daily=temperature_2m_max,temperature_2m_min,weather_code"
        "&timezone=Asia%%2FTokyo&forecast_days=1",
        GEO_LATITUDE, GEO_LONGITUDE);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(8000);  // 8 segundos de timeout

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[weather] Erro HTTP: %d\n", httpCode);
        http.end();
        return false;
    }

    // Lê resposta completa como String — mais confiável que stream no ESP32
    String payload = http.getString();
    http.end();

    // Parse do JSON — usa filtro para economizar RAM
    // Nota: OpenMeteo usa "weather_code" (com underscore) na resposta JSON
    JsonDocument filter;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["relative_humidity_2m"] = true;
    filter["current"]["weather_code"] = true;
    filter["daily"]["temperature_2m_max"][0] = true;
    filter["daily"]["temperature_2m_min"][0] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload,
                                               DeserializationOption::Filter(filter));

    if (err) {
        Serial.printf("[weather] JSON erro: %s\n", err.c_str());
        return false;
    }

    // Preenche struct com os dados recebidos
    out.tempCurrent = doc["current"]["temperature_2m"].as<float>();
    out.humidity    = doc["current"]["relative_humidity_2m"].as<float>();
    out.weatherCode = doc["current"]["weather_code"].as<int>();
    out.tempMax     = doc["daily"]["temperature_2m_max"][0].as<float>();
    out.tempMin     = doc["daily"]["temperature_2m_min"][0].as<float>();
    out.valid       = true;

    strncpy(out.description, wmoToDescription(out.weatherCode), sizeof(out.description) - 1);
    out.description[sizeof(out.description) - 1] = '\0';

    // Registra horário da última atualização
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        snprintf(out.lastUpdated, sizeof(out.lastUpdated), "%02d:%02d",
                 timeinfo.tm_hour, timeinfo.tm_min);
    } else {
        strncpy(out.lastUpdated, "--:--", sizeof(out.lastUpdated));
    }

    Serial.printf("[weather] OK — %.1f°C, WMO:%d, %s\n",
                  out.tempCurrent, out.weatherCode, out.description);
    return true;
}
