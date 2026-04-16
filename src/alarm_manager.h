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
