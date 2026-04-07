#pragma once
#include <M5Unified.h>
#include "power_manager.h"
#include "theme.h"

// Desenha ícone de bateria no canto superior direito.
inline void drawBatteryIndicator(lgfx::LovyanGFX& display) {
    int pct = batteryPercent();
    if (pct < 0) return;

    bool charging = batteryIsCharging();

    uint16_t outlineColor = (pct <= 20) ? COLOR_BATTERY_LOW : COLOR_TEXT_DIM;
    uint16_t fillColor    = (pct <= 20) ? COLOR_BATTERY_LOW
                          : (pct <= 50) ? COLOR_BATTERY_MED
                          :               COLOR_BATTERY_OK;

    const int bw = 24, bh = 12, nw = 3;
    const int bx = display.width() - bw - nw - 4;
    const int by = 4;

    display.drawRect(bx, by, bw, bh, outlineColor);
    int ny = by + (bh - 6) / 2;
    display.fillRect(bx + bw, ny, nw, 6, outlineColor);

    int fw = (int)((bw - 2) * pct / 100);
    if (fw < 1 && pct > 0) fw = 1;
    if (fw > 0)
        display.fillRect(bx + 1, by + 1, fw, bh - 2, fillColor);

    if (charging) {
        display.setFont(&fonts::Font0);
        display.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BACKGROUND);
        display.setTextDatum(MC_DATUM);
        display.drawString("+", bx + bw / 2, by + bh / 2);
    }

    char buf[6];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    display.setFont(&fonts::Font0);
    display.setTextColor(outlineColor, COLOR_BACKGROUND);
    display.setTextDatum(TR_DATUM);
    display.drawString(buf, bx - 3, by + 2);
}

// Aviso de bateria crítica — compartilhado entre telas (item #11).
inline void drawBatteryWarning(lgfx::LovyanGFX& display, int y = 218) {
    int pct = batteryPercent();
    if (pct >= 0 && pct <= 5 && (millis() / 1000) % 2 == 0) {
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(TFT_RED, COLOR_BACKGROUND);
        display.setTextDatum(MC_DATUM);
        display.drawString("BATERIA BAIXA!", display.width() / 2, y);
    }
}

// Indicador de tela (dots) — compartilhado (item #12).
inline void drawScreenIndicator(lgfx::LovyanGFX& display, int current, int total) {
    int dotSpacing = 20;
    int cx = display.width() / 2 - (total - 1) * dotSpacing / 2;
    int cy = display.height() - 10;
    for (int i = 0; i < total; i++) {
        if (i == current)
            display.fillCircle(cx + i * dotSpacing, cy, 4, COLOR_TEXT_PRIMARY);
        else
            display.drawCircle(cx + i * dotSpacing, cy, 4, COLOR_TEXT_DIM);
    }
}

// Estimativa de tempo restante de bateria em minutos (item #22).
// Retorna -1 se não há dados suficientes.
inline int batteryEstimateMinutes() {
    return batteryGetEstimateMinutes();
}
