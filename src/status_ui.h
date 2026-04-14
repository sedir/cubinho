#pragma once
#include <M5Unified.h>
#include <WiFi.h>
#include "wifi_manager.h"
#include "ota_manager.h"
#include "power_manager.h"
#include "theme.h"

inline void drawStatusChip(lgfx::LovyanGFX& display, int x, int y, uint16_t bg) {
    display.fillRoundRect(x, y, 12, 12, 3, bg);
}

inline void drawWifiGlyph(lgfx::LovyanGFX& display, int x, int y, uint16_t fg) {
    const int cx = x + 6;
    display.drawLine(cx - 4, y + 4, cx, y + 2, fg);
    display.drawLine(cx, y + 2, cx + 4, y + 4, fg);
    display.drawLine(cx - 3, y + 6, cx, y + 4, fg);
    display.drawLine(cx, y + 4, cx + 3, y + 6, fg);
    display.drawLine(cx - 1, y + 8, cx + 1, y + 8, fg);
    display.fillCircle(cx, y + 10, 1, fg);
}

inline void drawPortalGlyph(lgfx::LovyanGFX& display, int x, int y, uint16_t fg) {
    const int cx = x + 6;
    display.drawLine(cx, y + 2, cx, y + 9, fg);
    display.drawLine(cx - 4, y + 5, cx, y + 2, fg);
    display.drawLine(cx + 4, y + 5, cx, y + 2, fg);
    display.drawLine(cx - 3, y + 8, cx, y + 5, fg);
    display.drawLine(cx + 3, y + 8, cx, y + 5, fg);
    display.drawFastHLine(cx - 2, y + 10, 5, fg);
}

inline void drawSyncGlyph(lgfx::LovyanGFX& display, int x, int y, uint16_t fg) {
    const int cx = x + 6;
    const int cy = y + 6;
    display.drawCircle(cx, cy, 3, fg);
    display.drawLine(cx + 1, cy - 3, cx + 4, cy - 3, fg);
    display.drawLine(cx + 4, cy - 3, cx + 3, cy - 5, fg);
    display.drawLine(cx - 1, cy + 3, cx - 4, cy + 3, fg);
    display.drawLine(cx - 4, cy + 3, cx - 3, cy + 5, fg);
}

inline void drawDisconnectedGlyph(lgfx::LovyanGFX& display, int x, int y, uint16_t fg) {
    drawWifiGlyph(display, x, y, fg);
    display.drawLine(x + 2, y + 10, x + 10, y + 2, fg);
}

inline void drawOtaGlyph(lgfx::LovyanGFX& display, int x, int y, uint16_t fg) {
    const int cx = x + 6;
    display.drawLine(cx, y + 2, cx, y + 8, fg);
    display.drawLine(cx, y + 2, cx - 2, y + 4, fg);
    display.drawLine(cx, y + 2, cx + 2, y + 4, fg);
    display.drawFastHLine(x + 3, y + 9, 7, fg);
}

inline void drawHeaderStatusIcons(lgfx::LovyanGFX& display, int rightEdge, int topY) {
    const int chipW = 12;
    const int gap = 4;
    const bool showOta = otaIsActive();
    const int chipCount = 1 + (showOta ? 1 : 0);
    const int totalW = chipCount * chipW + (chipCount - 1) * gap;
    int x = rightEdge - totalW;

    uint16_t chipBg = 0x2104;
    uint16_t wifiFg = COLOR_TEXT_DIM;

    if (wifiIsPortalMode()) {
        wifiFg = COLOR_TEXT_ACCENT;
        drawStatusChip(display, x, topY, chipBg);
        drawPortalGlyph(display, x, topY, wifiFg);
    } else if (wifiIsFetching()) {
        wifiFg = ((millis() / 300) % 2 == 0) ? COLOR_TEXT_ACCENT : COLOR_TEXT_PRIMARY;
        drawStatusChip(display, x, topY, chipBg);
        drawSyncGlyph(display, x, topY, wifiFg);
    } else if (WiFi.status() == WL_CONNECTED) {
        wifiFg = COLOR_TIMER_RUNNING;
        drawStatusChip(display, x, topY, chipBg);
        drawWifiGlyph(display, x, topY, wifiFg);
    } else {
        drawStatusChip(display, x, topY, chipBg);
        drawDisconnectedGlyph(display, x, topY, wifiFg);
    }

    if (showOta) {
        x += chipW + gap;
        drawStatusChip(display, x, topY, chipBg);
        drawOtaGlyph(display, x, topY, COLOR_TEXT_PRIMARY);
    }
}

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

    int pctWidth = display.textWidth(buf);
    int iconRight = bx - 3 - pctWidth - 6;
    drawHeaderStatusIcons(display, iconRight, by);
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
