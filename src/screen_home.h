#pragma once
#include <M5Unified.h>
#include "config.h"

bool screenHomeInit();
void screenHomeDraw(lgfx::LovyanGFX& display, bool syncing = false, bool isDim = false);

// Presets de nome dos timers
int         screenHomeGetTimerLabelPresetCount();
const char* screenHomeGetTimerLabelPresetName(int presetIdx);
int         screenHomeGetTimerLabelPreset(int slot);
const char* screenHomeGetTimerLabel(int slot);
void        screenHomeSetTimerLabelPreset(int slot, int presetIdx);

// Chamados pelo handler de toque em main.cpp
void screenHomeTimerTap(int tapX);
void screenHomeTimerLongPress();
void screenHomeTimerSwipeAdjust(int deltaY);   // swipe vertical ajusta minutos (SETTING)

// Teclado on-screen para renomear slots
bool screenHomeIsKeyboardActive();
void screenHomeOpenKeyboard(int slot);
void screenHomeKeyboardHandleTouch(int x, int y);

// Troca o slot focado do timer (item #16)
void screenHomeTimerSwitchSlot(int slot);
int  screenHomeGetFocusedSlot();

// Sinaliza para o main.cpp disparar o som do alarme
bool screenHomeIsAlarmActive();
// Retorna o slot que está em DONE (alarme), ou -1
int  screenHomeAlarmSlot();
// Silencia os alarmes em DONE, voltando os slots para SETTING.
void screenHomeDismissAlarm();

// Deep sleep — retorna true se qualquer timer está rodando ou pausado
bool screenHomeIsTimerActive();

// Retorna true se qualquer timer está no estado RUNNING
bool screenHomeIsTimerRunning();

// Atualiza timers (RUNNING→DONE) — retorna true se algum acabou de disparar
bool screenHomeTimerUpdate();

// Persistência do estado dos timers através do deep sleep (item #16)
struct TimerPersist {
    int      state[MAX_TIMERS];
    int      minutes[MAX_TIMERS];
    uint32_t remainMs[MAX_TIMERS];
    int      focused;
    char     customName[MAX_TIMERS][16];
    bool     hasCustomName[MAX_TIMERS];
};
TimerPersist screenHomeGetTimerPersist();
void         screenHomeSetTimerPersist(const TimerPersist& p);

// Define o próximo evento a exibir na tela (item #24)
void screenHomeSetNextEvent(const char* text);

// Cronômetro (slot extra SW, índice MAX_TIMERS)
void screenHomeStopwatchTap();
void screenHomeStopwatchLongPress();
bool screenHomeIsStopwatchRunning();

// Total de slots UI: MAX_TIMERS timers + 1 cronômetro
int screenHomeGetTotalSlots();

// Modo ambiente em dim — relógio analógico minimalista
void screenHomeDrawAmbient(lgfx::LovyanGFX& display);
