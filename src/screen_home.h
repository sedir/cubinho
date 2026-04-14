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

// Troca o slot focado do timer (item #16)
void screenHomeTimerSwitchSlot(int slot);
int  screenHomeGetFocusedSlot();

// Sinaliza para o main.cpp disparar o som do alarme
bool screenHomeIsAlarmActive();
// Retorna o slot que está em DONE (alarme), ou -1
int  screenHomeAlarmSlot();

// Deep sleep — retorna true se qualquer timer está rodando ou pausado
bool screenHomeIsTimerActive();

// Retorna true se qualquer timer está no estado RUNNING
bool screenHomeIsTimerRunning();

// Persistência do estado dos timers através do deep sleep (item #16)
struct TimerPersist {
    int      state[MAX_TIMERS];
    int      minutes[MAX_TIMERS];
    uint32_t remainMs[MAX_TIMERS];
    int      focused;
};
TimerPersist screenHomeGetTimerPersist();
void         screenHomeSetTimerPersist(const TimerPersist& p);

// Define o próximo evento a exibir na tela (item #24)
void screenHomeSetNextEvent(const char* text);
