#pragma once
#include "weather_api.h"

// Callback para reportar progresso durante wifiInit (splash screen)
typedef void (*WifiProgressCb)(const char* message);
void wifiSetProgressCallback(WifiProgressCb cb);

void wifiInit(WeatherData& weatherData);
void wifiScheduleUpdate(WeatherData& weatherData);
void wifiBeginAsync(WeatherData& out);
bool wifiConnectAndFetch(WeatherData& out);
bool wifiIsFetching();
bool wifiBgJustCompleted();   // true uma vez após fetch de background concluir
void wifiSetKeepAlive(bool keep);
int  wifiGetRSSI();
void wifiForceRefresh(WeatherData& weatherData);  // dispara atualização imediata
void wifiSetUpdateInterval(uint32_t ms);          // altera intervalo de atualização em runtime

// Portal cativo WiFi (item #23)
bool wifiIsPortalMode();     // true se em modo AP de configuração
void wifiPortalUpdate();     // poll DNS + HTTP do portal — chamar no loop
void wifiCheckPortal();      // verifica se deve entrar em portal mode
bool wifiStartCalendarConfig();
void wifiStopCalendarConfig();
bool wifiIsCalendarConfigMode();
void wifiGetCalendarConfigAddress(char* out, size_t outSize);

// Credenciais armazenadas em NVS (item #23)
bool wifiHasStoredCredentials();
void wifiClearStoredCredentials();
