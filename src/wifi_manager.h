#pragma once
#include "weather_api.h"

void wifiInit(WeatherData& weatherData);
void wifiScheduleUpdate(WeatherData& weatherData);
void wifiBeginAsync(WeatherData& out);
bool wifiConnectAndFetch(WeatherData& out);
bool wifiIsFetching();
void wifiSetKeepAlive(bool keep);
int  wifiGetRSSI();
void wifiForceRefresh(WeatherData& weatherData);  // dispara atualização imediata
void wifiSetUpdateInterval(uint32_t ms);          // altera intervalo de atualização em runtime

// Portal cativo WiFi (item #23)
bool wifiIsPortalMode();     // true se em modo AP de configuração
void wifiPortalUpdate();     // poll DNS + HTTP do portal — chamar no loop
void wifiCheckPortal();      // verifica se deve entrar em portal mode

// Credenciais armazenadas em NVS (item #23)
bool wifiHasStoredCredentials();
void wifiClearStoredCredentials();
