#pragma once
#include "weather_api.h"

// Inicializa o gerenciador de WiFi. Chame no setup().
void wifiInit(WeatherData& weatherData);

// Disparado no loop — conecta WiFi, busca clima e desliga WiFi quando for a hora.
// Não bloqueia o loop (retorna imediatamente se já está em andamento ou não é hora).
void wifiScheduleUpdate(WeatherData& weatherData);

// Retorna true enquanto o ciclo de busca está em andamento
bool wifiIsFetching();

// Marca NTP como sincronizado e reseta o timer de fetch para agora.
// Usar ao restaurar estado após deep sleep (evita busca imediata desnecessária).
void wifiResetFetchTimer();

// Executa ciclo completo: liga WiFi → NTP (só no boot) → busca clima → desliga WiFi.
// Retorna true se o clima foi atualizado com sucesso.
bool wifiConnectAndFetch(WeatherData& out);
