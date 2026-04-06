#pragma once
#include <M5Unified.h>

bool screenHomeInit();
void screenHomeDraw(lgfx::LovyanGFX& display, bool syncing = false);

// Chamados pelo handler de toque em main.cpp
void screenHomeTimerTap(int tapX); // toque curto na zona do timer (tapX decide -/+)
void screenHomeTimerLongPress();  // pressão longa na zona do timer

// Sinaliza para o main.cpp disparar o som do alarme
bool screenHomeIsAlarmActive();

// Deep sleep — retorna true se o timer está rodando ou pausado (bloqueia sleep)
bool screenHomeIsTimerActive();

// Persistência do estado do timer através do deep sleep
struct TimerPersist {
    int      state;      // 0=SETTING, 2=PAUSED (RUNNING→PAUSED, DONE→SETTING)
    int      presetIdx;
    uint32_t remainMs;
};
TimerPersist screenHomeGetTimerPersist();
void         screenHomeSetTimerPersist(const TimerPersist& p);
