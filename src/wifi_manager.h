#pragma once
#include "weather_api.h"

// Inicializa o gerenciador de WiFi no cold boot (bloqueante: connect + NTP + clima).
void wifiInit(WeatherData& weatherData);

// Chamado a cada iteração do loop.
// Avança a state machine async e dispara nova busca a cada WEATHER_UPDATE_INTERVAL_MS.
void wifiScheduleUpdate(WeatherData& weatherData);

// Inicia ciclo assíncrono: liga WiFi → NTP → clima → desliga.
// Não bloqueia; progredir via wifiScheduleUpdate().
void wifiBeginAsync(WeatherData& out);

// Executa ciclo completo bloqueante: liga WiFi → NTP → clima → desliga.
// Usar apenas quando a tela está apagada (wake por timer).
bool wifiConnectAndFetch(WeatherData& out);

// Retorna true enquanto a operação async está em andamento (para indicador na tela).
bool wifiIsFetching();

// Mantém WiFi ativo mesmo após fetch (ex: cliente Telnet conectado).
// Quando liberado (keep=false), WiFi é desligado na primeira oportunidade.
void wifiSetKeepAlive(bool keep);

// Retorna RSSI atual em dBm, ou 0 se desconectado.
int wifiGetRSSI();
