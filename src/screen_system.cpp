#include "screen_system.h"
#include "battery_ui.h"
#include "theme.h"
#include "config.h"
#include "ota_manager.h"
#include "power_manager.h"
#include <WiFi.h>
#include <SD.h>

void screenSystemDraw(lgfx::LovyanGFX& display, uint8_t bootCount) {
    display.fillScreen(COLOR_BACKGROUND);

    // Título
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    display.setTextDatum(TL_DATUM);
    display.drawString("Sistema", 6, 4);
    drawBatteryIndicator(display);

    display.drawFastHLine(10, 24, display.width() - 20, COLOR_DIVIDER);

    // Informações
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextDatum(TL_DATUM);
    int y = 32;
    int lineH = 18;
    char buf[64];

    // WiFi
    if (WiFi.status() == WL_CONNECTED) {
        display.setTextColor(COLOR_TIMER_RUNNING, COLOR_BACKGROUND);
        snprintf(buf, sizeof(buf), "WiFi: %s", WiFi.localIP().toString().c_str());
        display.drawString(buf, 8, y); y += lineH;
        snprintf(buf, sizeof(buf), "RSSI: %d dBm", WiFi.RSSI());
        display.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
        display.drawString(buf, 8, y); y += lineH;
    } else {
        display.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
        display.drawString("WiFi: desconectado", 8, y); y += lineH;
        y += lineH;
    }

    // Bateria detalhada
    int pct = batteryPercent();
    bool chg = batteryIsCharging();
    snprintf(buf, sizeof(buf), "Bateria: %d%% %s", pct, chg ? "(carregando)" : "");
    display.setTextColor(pct <= 20 ? COLOR_BATTERY_LOW : COLOR_TEXT_PRIMARY, COLOR_BACKGROUND);
    display.drawString(buf, 8, y); y += lineH;

    int estimate = batteryGetEstimateMinutes();
    if (estimate > 0 && !chg) {
        int eh = estimate / 60, em = estimate % 60;
        snprintf(buf, sizeof(buf), "Restante: ~%dh%02dmin", eh, em);
        display.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
        display.drawString(buf, 8, y);
    }
    y += lineH;

    // Luminosidade
    uint16_t lux = powerReadAmbientLight();
    snprintf(buf, sizeof(buf), "Luz ambiente: %u", lux);
    display.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    display.drawString(buf, 8, y); y += lineH;

    // Uptime
    uint32_t sec = millis() / 1000;
    uint32_t h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
    snprintf(buf, sizeof(buf), "Uptime: %lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    display.drawString(buf, 8, y); y += lineH;

    // Boot count
    snprintf(buf, sizeof(buf), "Boots: %u", bootCount);
    display.drawString(buf, 8, y); y += lineH;

    // SD card
    if (SD.totalBytes() > 0) {
        uint64_t usedMB  = SD.usedBytes() / (1024ULL * 1024ULL);
        uint64_t totalMB = SD.totalBytes() / (1024ULL * 1024ULL);
        snprintf(buf, sizeof(buf), "SD: %lluMB / %lluMB", usedMB, totalMB);
    } else {
        snprintf(buf, sizeof(buf), "SD: sem cartao");
    }
    display.drawString(buf, 8, y); y += lineH;

    // OTA
    snprintf(buf, sizeof(buf), "OTA: %s", otaIsActive() ? "ativo" : "inativo");
    display.setTextColor(otaIsActive() ? COLOR_TIMER_RUNNING : COLOR_TEXT_DIM, COLOR_BACKGROUND);
    display.drawString(buf, 8, y); y += lineH;

    // Firmware
    display.setTextColor(COLOR_TEXT_SUBTLE, COLOR_BACKGROUND);
    display.drawString("Cidinha Kitchen Dashboard v2.0", 8, y);

    drawBatteryWarning(display);
    drawScreenIndicator(display, 2, SCREEN_COUNT);
}
