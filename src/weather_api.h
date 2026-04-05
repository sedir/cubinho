#pragma once
#include <Arduino.h>

// Dados do clima retornados pela API OpenMeteo
struct WeatherData {
    float tempCurrent;      // temperatura atual (°C)
    float tempMax;          // máxima do dia (°C)
    float tempMin;          // mínima do dia (°C)
    float humidity;         // umidade relativa (%)
    int   weatherCode;      // código WMO
    char  description[32];  // descrição textual (ex: "Chuva")
    char  lastUpdated[6];   // "HH:MM" da última atualização bem-sucedida
    bool  valid;            // true se os dados são válidos
};

// Busca clima na API OpenMeteo e preenche 'out'. Retorna true em caso de sucesso.
bool weatherFetch(WeatherData& out);

// Converte código WMO para descrição em português
const char* wmoToDescription(int code);

