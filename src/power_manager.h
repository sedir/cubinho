#pragma once
#include <Arduino.h>

// Inicializa o gerenciador de energia. Chame no setup() após M5.begin().
void powerInit();

// Registrar toque/proximidade — restaura brilho e reseta timer de inatividade.
void powerOnTouch();

// Gerencia transição ACTIVE ↔ DIM e auto-brilho ALS. Chame no loop().
// keepAwake=true impede entrar em dim (ex: alarme ativo).
void powerUpdate(bool keepAwake = false);

// Retorna true se o display está em modo dim.
bool powerIsDim();

// Retorna nível de bateria em % (0–100), ou -1 se não disponível.
int batteryPercent();

// Retorna true se a bateria está carregando.
bool batteryIsCharging();

// Retorna true se o tempo sem toque excedeu DEEP_SLEEP_TIMEOUT_MS.
bool powerShouldDeepSleep();

// Entra em deep sleep. NUNCA retorna.
void powerEnterDeepSleep();

// Lê a luminosidade ambiente via LTR553 ALS (0–65535). Item #19.
uint16_t powerReadAmbientLight();

// Registra leitura de bateria para estimativa de tempo restante (item #22).
void powerBatteryTick();

// Retorna estimativa de minutos restantes. -1 se insuficiente.
int batteryGetEstimateMinutes();

// ── Setters de configuração runtime ─────────────────────────────────────────
// Define o brilho alvo para modo ativo (0–255).
void powerSetBrightnessActive(int brightness);

// Define o timeout de inatividade até dim (ms, 0 = nunca).
void powerSetDimTimeout(uint32_t ms);

// Define o timeout até deep sleep (ms, 0 = nunca).
void powerSetDeepSleepTimeout(uint32_t ms);

// Habilita ou desabilita o auto-brilho via sensor ALS.
void powerSetAutoBrightness(bool enabled);

// Define o intervalo de atualização do clima (ms) — usado no timer do deep sleep.
void powerSetWeatherInterval(uint32_t ms);
