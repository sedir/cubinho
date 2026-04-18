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
    // ── MQTT (notificacoes push via broker) ────────────────────────────────
    bool  mqttEnabled;          // Habilita cliente MQTT para receber notificacoes
    char  mqttHost[64];         // Host do broker (ex: "broker.hivemq.com")
    int   mqttPort;             // Porta (default 1883)
    char  mqttUser[32];         // Usuario (opcional)
    char  mqttPass[64];         // Senha (opcional)
    char  mqttTopic[64];        // Topico de subscricao (ex: "cubinho/notif")
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

// Ponteiro para a configuracao ativa em memoria. Usado pelo portal web.
// Permanece valido por toda a vida do processo.
void runtimeConfigRegisterLive(RuntimeConfig* cfg);
RuntimeConfig* runtimeConfigLive();
