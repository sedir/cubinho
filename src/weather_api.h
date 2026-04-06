#pragma once
#include <Arduino.h>

// Dados do clima retornados pela API OpenMeteo
struct WeatherData {
    float tempCurrent;      // temperatura atual (°C)
    float tempPrevious;     // temperatura na busca anterior (NaN = sem histórico)
    float tempMax;          // máxima do dia (°C)
    float tempMin;          // mínima do dia (°C)
    float humidity;         // umidade relativa (%)
    int   weatherCode;      // código WMO
    char  description[32];  // descrição textual (ex: "Chuva")
    char  lastUpdated[6];   // "HH:MM" da última atualização bem-sucedida
    float hourlyTemp[6];    // temperatura das próximas 6h a partir da hora atual
    int   hourlyCode[6];    // código WMO de cada hora
    int   hourlyStartHour;  // hora local (0-23) da primeira entrada hourly
    bool  valid;            // true se os dados são válidos
};

// Busca clima na API OpenMeteo e preenche 'out'. Retorna true em caso de sucesso.
bool weatherFetch(WeatherData& out);

// Converte código WMO para descrição em português
const char* wmoToDescription(int code);

