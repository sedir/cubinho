#pragma once
#include <Arduino.h>
#include "config.h"

#define TREND_SAMPLES 48  // 24h a cada 30 min

// Dados do clima retornados pela API OpenMeteo
struct WeatherData {
    float tempCurrent;
    float apparentTemp;
    float tempPrevious;     // NaN = sem histórico
    float tempMax;
    float tempMin;
    float humidity;
    int   weatherCode;
    char  description[32];
    char  lastUpdated[6];   // "HH:MM"
    float hourlyTemp[48];   // até 48h de previsão horária
    int   hourlyCode[48];
    uint8_t hourlyPrecipProb[48];
    int   hourlyStartHour;
    int   hourlyCount;      // quantas horas válidas (até 48)
    bool  valid;

    // Histórico de temperatura para sparkline (#17)
    float trendTemp[TREND_SAMPLES];
    uint8_t trendCount;     // amostras válidas (0–48)
    uint8_t trendIdx;       // próximo índice de escrita (circular)
};

// Busca clima na API OpenMeteo e preenche 'out'. Retorna true em caso de sucesso.
bool weatherFetch(WeatherData& out);

// Converte código WMO para descrição em português
const char* wmoToDescription(int code);
