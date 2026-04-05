#include "screen_weather.h"
#include "battery_ui.h"
#include "config.h"

// ── Paleta dos ícones ────────────────────────────────────────────────────────
static const uint16_t COL_SUN   = 0xFFE0;  // amarelo
static const uint16_t COL_CLOUD = 0xC618;  // cinza claro
static const uint16_t COL_DARK  = 0x7BEF;  // cinza médio (nuvem de chuva)
static const uint16_t COL_RAIN  = 0x5D9F;  // azul claro (gotas)
static const uint16_t COL_SNOW  = 0xEF7D;  // branco acinzentado (neve)
static const uint16_t COL_BOLT  = 0xFFE0;  // amarelo (raio)

// Nuvem centrada em (cx, cy). Área: ±21px horizontal, -17 a +12 vertical.
static void drawCloud(lgfx::LovyanGFX& display, int cx, int cy, uint16_t color) {
    display.fillCircle(cx - 10, cy + 2,  9, color);
    display.fillCircle(cx +  2, cy - 5, 12, color);
    display.fillCircle(cx + 13, cy + 2,  8, color);
    display.fillRect(cx - 19, cy + 3, 42, 9, color);
}

// Sol centrado em (cx, cy) com raio r e raios em 8 direções.
// Usa vetores pré-calculados para evitar dependência de math.h.
static void drawSun(lgfx::LovyanGFX& display, int cx, int cy, int r) {
    // Vetores unitários *10 para N, NE, E, SE, S, SW, W, NW
    static const int8_t UX[] = {  0,  7, 10,  7,  0, -7, -10, -7 };
    static const int8_t UY[] = {-10, -7,  0,  7, 10,  7,   0, -7 };
    int ri = r + 3;
    int ro = r + 9;
    for (int i = 0; i < 8; i++) {
        int x1 = cx + UX[i] * ri / 10;
        int y1 = cy + UY[i] * ri / 10;
        int x2 = cx + UX[i] * ro / 10;
        int y2 = cy + UY[i] * ro / 10;
        // Desenha cada raio com 2px de largura
        display.drawLine(x1,     y1, x2,     y2, COL_SUN);
        display.drawLine(x1 + 1, y1, x2 + 1, y2, COL_SUN);
    }
    display.fillCircle(cx, cy, r, COL_SUN);
}

// N gotas de chuva abaixo de (cx, topY) — linhas diagonais curtas
static void drawRain(lgfx::LovyanGFX& display, int cx, int topY, int n) {
    int step   = 10;
    int startX = cx - (n - 1) * step / 2;
    for (int i = 0; i < n; i++) {
        int rx = startX + i * step;
        display.drawLine(rx,     topY, rx - 3, topY + 8, COL_RAIN);
        display.drawLine(rx + 1, topY, rx - 2, topY + 8, COL_RAIN);
    }
}

// N flocos de neve abaixo de (cx, topY) — cruzes sobrepostas
static void drawSnow(lgfx::LovyanGFX& display, int cx, int topY, int n) {
    int step   = 13;
    int startX = cx - (n - 1) * step / 2;
    for (int i = 0; i < n; i++) {
        int sx = startX + i * step;
        int sy = topY + 5;
        display.drawLine(sx - 5, sy,     sx + 5, sy,     COL_SNOW);
        display.drawLine(sx,     sy - 5, sx,     sy + 5, COL_SNOW);
        display.drawLine(sx - 3, sy - 3, sx + 3, sy + 3, COL_SNOW);
        display.drawLine(sx + 3, sy - 3, sx - 3, sy + 3, COL_SNOW);
    }
}

// Relâmpago abaixo de (cx, topY)
static void drawBolt(lgfx::LovyanGFX& display, int cx, int topY) {
    display.fillTriangle(cx + 4, topY,      cx - 2, topY + 8,  cx + 2, topY + 8,  COL_BOLT);
    display.fillTriangle(cx + 2, topY + 7,  cx - 4, topY + 15, cx + 2, topY + 15, COL_BOLT);
}

// Ícone do clima desenhado em pixels, centrado em (cx, cy).
// Cada ícone ocupa aprox. 42px de largura e 42px de altura.
static void drawWeatherIcon(lgfx::LovyanGFX& display, int code, int cx, int cy) {
    if (code == 0) {
        // Sol pleno
        drawSun(display, cx, cy, 13);

    } else if (code >= 1 && code <= 3) {
        // Parcialmente nublado — sol atrás da nuvem
        drawSun(display, cx - 8, cy - 8, 9);
        drawCloud(display, cx + 4, cy + 4, COL_CLOUD);

    } else if (code == 45 || code == 48) {
        // Neblina — faixas horizontais com offset alternado
        for (int i = 0; i < 4; i++) {
            int lw = (i % 2 == 0) ? 36 : 26;
            int lx = cx - lw / 2 + (i % 2) * 5;
            int ly = cy - 13 + i * 9;
            display.fillRoundRect(lx, ly, lw, 3, 1, 0x8410);
        }

    } else if (code >= 51 && code <= 55) {
        // Chuvisco — nuvem clara + poucas gotas
        drawCloud(display, cx, cy - 8, COL_CLOUD);
        drawRain(display, cx, cy + 8, 3);

    } else if ((code >= 61 && code <= 65) || (code >= 80 && code <= 82)) {
        // Chuva — nuvem escura + mais gotas
        drawCloud(display, cx, cy - 8, COL_DARK);
        drawRain(display, cx, cy + 8, 5);

    } else if (code >= 71 && code <= 75) {
        // Neve — nuvem + flocos
        drawCloud(display, cx, cy - 8, COL_CLOUD);
        drawSnow(display, cx, cy + 8, 3);

    } else if (code == 95) {
        // Trovoada — nuvem escura + raio
        drawCloud(display, cx, cy - 8, COL_DARK);
        drawBolt(display, cx, cy + 8);

    } else {
        // Variável/genérico — nuvem cinza
        drawCloud(display, cx, cy, COL_CLOUD);
    }
}

// ── Desenho da tela de clima ─────────────────────────────────────────────────
void screenWeatherDraw(lgfx::LovyanGFX& display, const WeatherData& data, bool fetching) {
    display.fillScreen(TFT_BLACK);

    // --- Linha 1: Cidade + bateria ---
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(0x8410, TFT_BLACK);
    display.setTextDatum(TL_DATUM);
    display.drawString(CITY_NAME, 6, 4);

    // Ícone de sincronização piscante durante busca
    if (fetching && (millis() / 400) % 2 == 0) {
        display.setTextColor(0xFD20, TFT_BLACK);
        display.setTextDatum(TR_DATUM);
        display.drawString("...", display.width() - 70, 4);
    }

    drawBatteryIndicator(display);

    if (!data.valid) {
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(0x8410, TFT_BLACK);
        display.setTextDatum(MC_DATUM);
        display.drawString("Sem conexao", display.width() / 2, 100);
        display.drawString("Aguardando WiFi...", display.width() / 2, 125);

        int cx = display.width() / 2;
        int cy = display.height() - 12;
        display.drawCircle(cx - 10, cy, 4, 0x8410);
        display.fillCircle(cx + 10, cy, 4, TFT_WHITE);
        return;
    }

    // --- Ícone do clima (desenhado com primitivas) ---
    drawWeatherIcon(display, data.weatherCode, display.width() / 2, 52);

    // --- Descrição textual ---
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextDatum(MC_DATUM);
    display.drawString(data.description, display.width() / 2, 84);

    // --- Divisor ---
    display.drawFastHLine(10, 100, display.width() - 20, 0x4208);

    // --- Dados numéricos ---
    display.setTextDatum(TL_DATUM);
    char buf[40];

    // Temperatura atual com cor baseada no conforto
    uint16_t tempColor;
    if (data.tempCurrent < COMFORT_TEMP_MIN)       tempColor = 0x001F;
    else if (data.tempCurrent > COMFORT_TEMP_MAX)  tempColor = TFT_RED;
    else                                            tempColor = TFT_GREEN;

    snprintf(buf, sizeof(buf), "Atual: %.1f\xC2\xB0""C", data.tempCurrent);
    display.setTextColor(tempColor, TFT_BLACK);
    display.drawString(buf, 16, 112);

    snprintf(buf, sizeof(buf), "Max: %.0f\xC2\xB0""C  Min: %.0f\xC2\xB0""C",
             data.tempMax, data.tempMin);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawString(buf, 16, 138);

    snprintf(buf, sizeof(buf), "Umidade: %.0f%%", data.humidity);
    display.setTextColor(0x5D9F, TFT_BLACK);
    display.drawString(buf, 16, 162);

    // --- Aviso de bateria crítica (piscante) ---
    int pct = batteryPercent();
    if (pct >= 0 && pct <= 5 && (millis() / 1000) % 2 == 0) {
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(TFT_RED, TFT_BLACK);
        display.setTextDatum(MC_DATUM);
        display.drawString("BATERIA BAIXA!", display.width() / 2, 190);
    }

    // --- Timestamp + indicador de tela ---
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(0x8410, TFT_BLACK);
    display.setTextDatum(TL_DATUM);
    snprintf(buf, sizeof(buf), "Atualizado %s", data.lastUpdated);
    display.drawString(buf, 6, display.height() - 24);

    int cx = display.width() / 2;
    int cy = display.height() - 12;
    display.drawCircle(cx - 10, cy, 4, 0x8410);
    display.fillCircle(cx + 10, cy, 4, TFT_WHITE);
}
