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
    // Guarda temperatura anterior para calcular tendência
    float prevTemp = out.valid ? out.tempCurrent : NAN;

    // Monta URL — snprintf evita fragmentação de heap
    char url[300];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m,weather_code,windspeed_10m"
        "&daily=temperature_2m_max,temperature_2m_min,weather_code"
        "&hourly=temperature_2m,weather_code"
        "&timezone=Asia%%2FTokyo&forecast_days=1",
        GEO_LATITUDE, GEO_LONGITUDE);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(8000);

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[weather] Erro HTTP: %d\n", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    // Filtro para economizar RAM no JsonDocument
    JsonDocument filter;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["relative_humidity_2m"] = true;
    filter["current"]["weather_code"] = true;
    filter["daily"]["temperature_2m_max"][0] = true;
    filter["daily"]["temperature_2m_min"][0] = true;
    filter["hourly"]["temperature_2m"][0] = true;
    filter["hourly"]["weather_code"][0] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload,
                                               DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[weather] JSON erro: %s\n", err.c_str());
        return false;
    }

    // Dados atuais e diários
    out.tempPrevious = prevTemp;
    out.tempCurrent  = doc["current"]["temperature_2m"].as<float>();
    out.humidity     = doc["current"]["relative_humidity_2m"].as<float>();
    out.weatherCode  = doc["current"]["weather_code"].as<int>();
    out.tempMax      = doc["daily"]["temperature_2m_max"][0].as<float>();
    out.tempMin      = doc["daily"]["temperature_2m_min"][0].as<float>();
    out.valid        = true;

    strncpy(out.description, wmoToDescription(out.weatherCode), sizeof(out.description) - 1);
    out.description[sizeof(out.description) - 1] = '\0';

    // Previsão horária — próximas 6h a partir da hora atual
    struct tm timeinfo;
    int startHour = 0;
    if (getLocalTime(&timeinfo)) {
        startHour = timeinfo.tm_hour;
        snprintf(out.lastUpdated, sizeof(out.lastUpdated), "%02d:%02d",
                 timeinfo.tm_hour, timeinfo.tm_min);
    } else {
        strncpy(out.lastUpdated, "--:--", sizeof(out.lastUpdated));
    }
    out.hourlyStartHour = startHour;
    for (int i = 0; i < 6; i++) {
        int idx = min(startHour + i, 23);  // clipa em 23h no fim do dia
        out.hourlyTemp[i] = doc["hourly"]["temperature_2m"][idx].as<float>();
        out.hourlyCode[i] = doc["hourly"]["weather_code"][idx].as<int>();
    }

    Serial.printf("[weather] OK — %.1f°C (prev %.1f°C), WMO:%d\n",
                  out.tempCurrent, out.tempPrevious, out.weatherCode);
    return true;
}
