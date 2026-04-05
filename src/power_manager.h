#pragma once
#include <Arduino.h>

// Inicializa o gerenciador de energia. Chame no setup() após M5.begin().
void powerInit();

// Registrar toque detectado — restaura brilho e reseta timer de inatividade.
void powerOnTouch();

// Gerencia transição entre estados ACTIVE e DIM. Chame no loop().
void powerUpdate();

// Retorna true se o display está em modo dim (escurecido por inatividade)
bool powerIsDim();

// Retorna nível de bateria em % (0–100), ou -1 se não disponível
int batteryPercent();

// Retorna true se a bateria está carregando
bool batteryIsCharging();

// Retorna true se o tempo sem toque excedeu DEEP_SLEEP_TIMEOUT_MS
bool powerShouldDeepSleep();

// Desliga display/WiFi e entra em deep sleep.
// Wake por toque (EXT0) ou timer (para atualizar clima).
// NUNCA retorna — o dispositivo reinicia ao acordar.
void powerEnterDeepSleep();
