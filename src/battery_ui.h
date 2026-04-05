#pragma once
#include <M5Unified.h>
#include "power_manager.h"

// Desenha ícone de bateria no canto superior direito.
// Formato visual: [ ===  ] 85%  (retângulo proporcional + percentual)
// Chamar ao final de cada função de desenho de tela.
inline void drawBatteryIndicator(lgfx::LovyanGFX& display) {
    int pct = batteryPercent();
    if (pct < 0) return;  // leitura inválida — não exibir

    bool charging = batteryIsCharging();

    // Cores baseadas no nível
    uint16_t outlineColor = (pct <= 20) ? TFT_RED : (uint16_t)0x8410;
    uint16_t fillColor    = (pct <= 20) ? TFT_RED
                          : (pct <= 50) ? (uint16_t)0xFD20   // laranja
                          :               TFT_GREEN;

    // Dimensões e posição (canto superior direito)
    const int bw = 24;   // largura do corpo da bateria
    const int bh = 12;   // altura do corpo
    const int nw = 3;    // largura do polo positivo
    const int bx = display.width() - bw - nw - 4;
    const int by = 4;

    // Corpo da bateria (contorno)
    display.drawRect(bx, by, bw, bh, outlineColor);

    // Polo positivo (nub)
    int ny = by + (bh - 6) / 2;
    display.fillRect(bx + bw, ny, nw, 6, outlineColor);

    // Preenchimento proporcional ao nível de carga
    int fw = (int)((bw - 2) * pct / 100);
    if (fw < 1 && pct > 0) fw = 1;
    if (fw > 0) {
        display.fillRect(bx + 1, by + 1, fw, bh - 2, fillColor);
    }

    // Símbolo "+" quando carregando, centralizado no corpo
    if (charging) {
        display.setFont(&fonts::Font0);
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.setTextDatum(MC_DATUM);
        display.drawString("+", bx + bw / 2, by + bh / 2);
    }

    // Percentual à esquerda do ícone
    char buf[6];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    display.setFont(&fonts::Font0);
    display.setTextColor(outlineColor, TFT_BLACK);
    display.setTextDatum(TR_DATUM);
    display.drawString(buf, bx - 3, by + 2);
}
