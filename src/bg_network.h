#pragma once
#include "weather_api.h"

// Task de rede em background (Core 0) — executa weatherFetch + calendarFetchToday
// sem bloquear a UI no Core 1.
//
// Fluxo:
//   1. wifi_manager chama bgNetworkStartFetch() quando WiFi conecta
//   2. Task executa HTTP fetches em background
//   3. wifi_manager polls bgNetworkIsDone() a cada iteração do loop
//   4. Quando pronto, bgNetworkConsume() copia resultado para WeatherData principal

void bgNetworkInit();
void bgNetworkStartFetch(WeatherData* current);
bool bgNetworkIsDone();
void bgNetworkConsume(WeatherData& out);
