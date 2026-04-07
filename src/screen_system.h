#pragma once
#include <M5Unified.h>

// Desenha a tela 2: informações do sistema.
// bootCount: número de boots desde o power-on.
void screenSystemDraw(lgfx::LovyanGFX& display, uint8_t bootCount);
