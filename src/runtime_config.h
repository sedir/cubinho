#pragma once
#include <Arduino.h>
#include "config.h"

struct RuntimeConfig {
    bool  wifiKeepAlive;        // WiFi permanente (keep-alive)
    int   weatherIntervalMin;   // Intervalo de atualização do clima (minutos)
    int   brightnessActive;     // Brilho em modo ativo (0–255)
    int   dimTimeoutSec;        // Tempo de inatividade até dim (segundos)
    bool  autoBrightness;       // Auto-brilho via sensor ALS
    int   deepSleepTimeoutMin;  // Timeout deep sleep (minutos, 0 = nunca)
    bool  accelWake;            // Acorda do dim ao detectar movimento
    int   timerLabelPreset[MAX_TIMERS];  // Nome escolhido para cada slot
    bool  voiceEnabled;         // Comandos por voz via microfone embutido
    bool  nightMode;            // Modo noturno: brilho mínimo + sem auto-brilho
    bool  alarmEnabled;         // Alarme de despertar ativo
    int   alarmHour;            // Hora do alarme (0–23)
    int   alarmMinute;          // Minuto do alarme (0–59)
};

// Carrega configuração do NVS (usa defaults de config.h se namespace vazio).
void runtimeConfigLoad(RuntimeConfig& cfg);

// Salva configuração no NVS.
void runtimeConfigSave(const RuntimeConfig& cfg);

// Aplica os valores em runtime (sem salvar).
void runtimeConfigApply(const RuntimeConfig& cfg);

// Apaga o namespace NVS "cfg" (factory reset).
void runtimeConfigClear();

// URL do feed iCal/ICS armazenada em NVS.
bool runtimeConfigHasCalendarUrl();
void runtimeConfigGetCalendarUrl(char* out, size_t outSize);
void runtimeConfigSaveCalendarUrl(const char* url);
