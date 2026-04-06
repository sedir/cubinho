#pragma once
#include <M5Unified.h>

// Desenha splash screen de inicialização.
// Chamar após M5.begin() e antes de wifiInit(), com canvas já criado.
inline void drawSplash(lgfx::LovyanGFX& display) {
    display.fillScreen(TFT_BLACK);

    // Título principal
    display.setFont(&fonts::FreeSansBold18pt7b);
    display.setTextColor(0xFD20, TFT_BLACK);  // laranja
    display.setTextDatum(MC_DATUM);
    display.drawString("Kitchen", display.width() / 2, 72);
    display.drawString("Dashboard", display.width() / 2, 108);

    // Linha separadora
    display.drawFastHLine(50, 132, display.width() - 100, 0x4208);

    // Mensagem de status
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(0x8410, TFT_BLACK);
    display.drawString("Inicializando...", display.width() / 2, 158);

    // Rodapé
    display.setFont(&fonts::Font0);
    display.setTextColor(0x2104, TFT_BLACK);
    display.drawString("by Sedir Morais", display.width() / 2, 210);
}
