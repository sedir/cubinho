#pragma once
#include <stdint.h>

// Detecção de abertura da porta via acelerômetro.
// Assume o CoreS3 fixado na porta: quando a porta gira, o vetor de gravidade
// muda de direção em relação ao device. Comparamos o vetor atual com um
// baseline calibrado (porta fechada) e declaramos "aberta" quando o desvio
// supera DOOR_OPEN_THRESHOLD_G.

void doorSensorInit();
void doorSensorUpdate();       // chame no loop() — throttle interno ~5Hz

bool     doorSensorIsOpen();                  // estado atual (debounced)
uint32_t doorSensorOpenDurationMs();          // tempo aberta em ms (0 se fechada)
int      doorSensorTodayCount();              // aberturas hoje (reseta à meia-noite)
int      doorSensorYesterdayCount();          // aberturas ontem

// true enquanto a porta está aberta há mais de DOOR_LONG_OPEN_MS
// (alerta visual com LEDs vermelhos + overlay na tela)
bool     doorSensorIsLongOpen();

void     doorSensorSetEnabled(bool en);
bool     doorSensorIsEnabled();

// Reset manual do baseline (útil após remontagem do device na porta)
void     doorSensorRecalibrate();
