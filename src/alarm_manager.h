#pragma once
#include <M5Unified.h>
#include "runtime_config.h"

// Verifica se é hora do alarme e dispara se necessário. Chame a cada ciclo do loop.
void alarmCheck(const RuntimeConfig& cfg);

// Retorna true se o alarme está tocando ativamente.
bool alarmIsRinging();

// Retorna true se o alarme está em snooze (aguardando).
bool alarmIsSnoozed();

// Descarta o alarme (RINGING → IDLE).
void alarmDismiss();

// Snooze: +5 min (RINGING → SNOOZED).
void alarmSnooze();

// ── Persistência pelo deep sleep ──────────────────────────────────────────────
// Estado do alarme que deve ser salvo em RTC_DATA_ATTR antes do sleep.
struct AlarmPersist {
    int     state;        // AlarmState: 0=IDLE, 1=RINGING, 2=SNOOZED
    int     triggeredYday;
    int64_t snoozeUntil;  // time_t como int64_t
};

// Exporta o estado atual para salvar em RTC_DATA_ATTR.
void alarmGetPersist(AlarmPersist& out);

// Restaura estado a partir de RTC_DATA_ATTR (chame logo após runtimeConfigLoad).
void alarmSetPersist(const AlarmPersist& in);

// Retorna segundos até o próximo disparo (–1 se alarme desativado ou tempo indisponível).
// Considera o estado atual (ex.: se já disparou hoje, retorna o tempo até amanhã).
int64_t alarmSecondsUntilNext(const RuntimeConfig& cfg);

// Time-picker: abre/fecha/verifica estado.
void alarmPickerOpen(int initialHour, int initialMin);
void alarmPickerClose();
bool alarmPickerIsOpen();

// Desenha o time-picker (tela completa).
void alarmPickerDraw(lgfx::LovyanGFX& d);

// Processa toque no picker. Retorna true se o usuário confirmou;
// nesse caso outHour/outMin recebem o horário escolhido.
bool alarmPickerHandleTap(int x, int y, int& outHour, int& outMin);

// Overlay exibido enquanto o alarme toca (tela completa).
void alarmDrawRingingOverlay(lgfx::LovyanGFX& d);

