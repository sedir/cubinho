#pragma once
#include <Arduino.h>

struct RuntimeConfig {
    bool  wifiKeepAlive;        // WiFi permanente (keep-alive)
    int   weatherIntervalMin;   // Intervalo de atualização do clima (minutos)
    int   brightnessActive;     // Brilho em modo ativo (0–255)
    int   dimTimeoutSec;        // Tempo de inatividade até dim (segundos)
    bool  autoBrightness;       // Auto-brilho via sensor ALS
    int   deepSleepTimeoutMin;  // Timeout deep sleep (minutos, 0 = nunca)
    bool  accelWake;            // Acorda do dim ao detectar movimento
};

// Carrega configuração do NVS (usa defaults de config.h se namespace vazio).
void runtimeConfigLoad(RuntimeConfig& cfg);

// Salva configuração no NVS.
void runtimeConfigSave(const RuntimeConfig& cfg);

// Aplica os valores em runtime (sem salvar).
void runtimeConfigApply(const RuntimeConfig& cfg);

// Apaga o namespace NVS "cfg" (factory reset).
void runtimeConfigClear();
