#include "weather_api.h"
#include "config.h"
#include "logger.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

// Mapeamento de código WMO para descrição em português
const char* wmoToDescription(int code) {
    if (code == 0)                                  return "Ceu limpo";
    if (code >= 1  && code <= 3)                    return "Parcialmente nublado";
    if (code == 45 || code == 48)                   return "Neblina";
    if (code == 51 || code == 53 || code == 55)     return "Chuvisco";
    if (code == 56 || code == 57)                   return "Chuvisco gelado";
    if (code == 61 || code == 63 || code == 65)     return "Chuva";
    if (code == 66 || code == 67)                   return "Chuva gelada";
    if (code == 71 || code == 73 || code == 75)     return "Neve";
    if (code == 77)                                 return "Granizo";
    if (code == 80 || code == 81 || code == 82)     return "Pancadas de chuva";
    if (code == 85 || code == 86)                   return "Pancadas de neve";
    if (code == 95)                                 return "Trovoada";
    if (code == 96 || code == 99)                   return "Trovoada c/ granizo";
    return "Variavel";
}


bool weatherFetch(WeatherData& out) {
    float prevTemp = out.valid ? out.tempCurrent : NAN;

    // forecast_days=2 para suportar wrap de horas além de 23h (fix #5)
    char url[320];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m,weather_code,windspeed_10m"
        "&daily=temperature_2m_max,temperature_2m_min,weather_code"
        "&hourly=temperature_2m,weather_code"
        "&timezone=auto&forecast_days=2",
        GEO_LATITUDE, GEO_LONGITUDE);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, url)) {
        LOG_E("weather", "Falha ao iniciar HTTPClient");
        return false;
    }
    http.setTimeout(8000);

    int httpCode = http.GET();
    if (httpCode != 200) {
        LOG_E("weather", "Erro HTTP: %d (%s)", httpCode, http.errorToString(httpCode).c_str());
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument filter;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["relative_humidity_2m"] = true;
    filter["current"]["weather_code"] = true;
    filter["daily"]["temperature_2m_max"][0] = true;
    filter["daily"]["temperature_2m_min"][0] = true;
    filter["hourly"]["temperature_2m"] = true;
    filter["hourly"]["weather_code"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload,
                                               DeserializationOption::Filter(filter));
    if (err) {
        LOG_E("weather", "JSON erro: %s", err.c_str());   // fix #6
        return false;
    }

    // Validação de campos obrigatórios (fix #14)
    if (doc["current"]["temperature_2m"].isNull() ||
        doc["current"]["weather_code"].isNull() ||
        doc["daily"]["temperature_2m_max"][0].isNull()) {
        LOG_E("weather", "Campos obrigatorios ausentes no JSON");
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

    // Previsão horária — próximas 6h com wrap (fix #5, 48h de dados)
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

    // Contar horas disponíveis no JSON
    JsonArray hourlyTemps = doc["hourly"]["temperature_2m"];
    JsonArray hourlyCodes = doc["hourly"]["weather_code"];
    int available = min((int)hourlyTemps.size(), 48);
    out.hourlyCount = 0;

    for (int i = 0; i < min(available - startHour, 48); i++) {
        int idx = startHour + i;
        if (idx >= available) break;
        out.hourlyTemp[i] = hourlyTemps[idx].as<float>();
        out.hourlyCode[i] = hourlyCodes[idx].as<int>();
        out.hourlyCount++;
    }

    // Sparkline: adiciona temperatura atual ao histórico circular (item #17)
    bool wasEmpty = (out.trendCount == 0);
    out.trendTemp[out.trendIdx] = out.tempCurrent;
    out.trendIdx = (out.trendIdx + 1) % TREND_SAMPLES;
    if (out.trendCount < TREND_SAMPLES) out.trendCount++;
    // Garante >=2 amostras no primeiro boot para o drawSparkline poder desenhar
    if (wasEmpty) {
        out.trendTemp[out.trendIdx] = out.tempCurrent;
        out.trendIdx = (out.trendIdx + 1) % TREND_SAMPLES;
        out.trendCount++;
    }

    LOG_I("weather", "OK — %.1f°C (prev %.1f°C), WMO:%d, %dh dados",
          out.tempCurrent, out.tempPrevious, out.weatherCode, out.hourlyCount);
    return true;
}
