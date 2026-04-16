#pragma once
#include <M5Unified.h>

// Desenha splash screen de inicialização (layout completo).
// Chamar após M5.begin() e antes de wifiInit(), com canvas já criado.
inline void drawSplash(lgfx::LovyanGFX &display)
{
    display.fillScreen(TFT_BLACK);

    // Título principal
    display.setFont(&fonts::FreeSansBold18pt7b);
    display.setTextColor(0xFD20, TFT_BLACK); // laranja
    display.setTextDatum(MC_DATUM);
    display.drawString("Cubinho", display.width() / 2, 72);
    display.drawString("Dashboard", display.width() / 2, 108);

    // Linha separadora
    display.drawFastHLine(50, 132, display.width() - 100, 0x4208);

    // Mensagem de status inicial
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(0x8410, TFT_BLACK);
    display.drawString("Inicializando...", display.width() / 2, 155);

    // Rodapé
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(0xDEDB, TFT_BLACK); // off-white
    display.drawString("Criado por Sedir Morais", display.width() / 2, 210);
}

// Atualiza a mensagem de status e barra de progresso na splash.
// step: passo atual (1-indexed), totalSteps: total de passos.
inline void splashStatus(lgfx::LovyanGFX &display, const char* message,
                         int step, int totalSteps)
{
    int w = display.width();

    // Limpa a zona de status (entre linha separadora e rodapé)
    display.fillRect(0, 140, w, 48, TFT_BLACK);

    // Texto de status
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(0xAD55, TFT_BLACK);  // cinza mais claro que o dim
    display.setTextDatum(MC_DATUM);
    display.drawString(message, w / 2, 155);

    // Barra de progresso
    int barX = 50;
    int barY = 174;
    int barW = w - 100;  // 220px
    int barH = 6;
    float progress = (totalSteps > 0)
                     ? (float)step / (float)totalSteps
                     : 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    int filled = (int)(progress * (float)barW);

    // Trilho (fundo)
    display.fillRoundRect(barX, barY, barW, barH, 3, 0x2104);

    // Preenchimento laranja
    if (filled > 0) {
        display.fillRoundRect(barX, barY, filled, barH, 3, 0xFD20);
    }
}
