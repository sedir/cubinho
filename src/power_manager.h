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

// Solicita CPU a 240 MHz por ~500 ms (cobre animações e resposta ao toque).
// Libera automaticamente via powerUpdate(). Seguro chamar repetidamente.
void powerBoostCpu();

// Retorna nível de bateria em % (0–100), ou -1 se não disponível.
int batteryPercent();

// Retorna true se a bateria está carregando.
bool batteryIsCharging();

// Retorna true se o dispositivo está recebendo energia de fonte externa
// (carregando OU conectado ao carregador com bateria a 100%). Quando true,
// powerShouldDeepSleep() retorna false para manter o aparelho sempre aceso.
bool powerIsOnExternalPower();

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

// Habilita ou desabilita wake do dim via acelerômetro.
void powerSetAccelWake(bool enabled);

// Retorna true se o wake via acelerômetro está habilitado.
bool powerIsAccelWakeEnabled();

// ── Modo cozinha ativa ──────────────────────────────────────────────────────
// Mantem o display aceso por durationMs (default 1h), bloqueando dim e deep
// sleep. Uso tipico: usuario vai cozinhar e quer relogio + timer sempre
// visiveis sem precisar tocar na tela. Chame novamente para renovar o prazo.
#define COOKING_MODE_DEFAULT_MS  (60UL * 60UL * 1000UL)   // 1 hora

void     powerEnableCookingMode(uint32_t durationMs = COOKING_MODE_DEFAULT_MS);
void     powerDisableCookingMode();
bool     powerIsCookingMode();
// Retorna tempo restante em ms (0 se inativo).
uint32_t powerCookingModeRemainingMs();
