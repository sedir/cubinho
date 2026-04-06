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

// ── Cor de temperatura baseada no conforto ────────────────────────────────────
static uint16_t tempColor(float t) {
    if (t < COMFORT_TEMP_MIN) return 0x001F;   // azul = frio
    if (t > COMFORT_TEMP_MAX) return TFT_RED;  // vermelho = quente
    return TFT_GREEN;
}

// Desenha "prefixo + valor + °C" com ° como círculo primitivo (FreeSans não tem o glifo).
// Retorna o x após o texto desenhado.
static int drawTempC(lgfx::LovyanGFX& d, const char* prefix, float val, int decimals,
                     int x, int y, uint16_t color) {
    char buf[16];
    snprintf(buf, sizeof(buf), decimals ? "%s%.1f" : "%s%.0f", prefix, val);
    d.setTextColor(color, TFT_BLACK);
    d.setTextDatum(TL_DATUM);
    d.drawString(buf, x, y);
    int x2 = x + d.textWidth(buf);
    d.drawCircle(x2 + 3, y + 3, 2, color);   // símbolo °
    d.drawString("C", x2 + 7, y);
    return x2 + 7 + d.textWidth("C");
}

// ── Gráfico de barras das próximas 6h ────────────────────────────────────────
static void drawHourlyForecast(lgfx::LovyanGFX& display, const WeatherData& data) {
    // 6 barras de 36px com gap de 6px, centralizadas em 320px
    const int BAR_W   = 36;
    const int GAP     = 6;
    const int BAR_MAX = 18;    // altura máxima em pixels
    const int BAR_MIN = 4;     // altura mínima em pixels
    const int BASE_Y  = 188;   // base das barras
    const int START_X = (display.width() - (6 * BAR_W + 5 * GAP)) / 2;

    // Escala: min/max das 6 temperaturas (range mínimo de 4°C)
    float tmin = data.hourlyTemp[0], tmax = data.hourlyTemp[0];
    for (int i = 1; i < 6; i++) {
        if (data.hourlyTemp[i] < tmin) tmin = data.hourlyTemp[i];
        if (data.hourlyTemp[i] > tmax) tmax = data.hourlyTemp[i];
    }
    float range = (tmax - tmin < 4.0f) ? 4.0f : (tmax - tmin);

    for (int i = 0; i < 6; i++) {
        int x   = START_X + i * (BAR_W + GAP);
        int cx  = x + BAR_W / 2;
        int barH = BAR_MIN + (int)((data.hourlyTemp[i] - tmin) / range * (BAR_MAX - BAR_MIN));
        int barY = BASE_Y - barH;

        // Barra colorida por temperatura
        display.fillRect(x, barY, BAR_W, barH, tempColor(data.hourlyTemp[i]));

        // Temperatura acima da barra (Font0 = 6×8px)
        char tbuf[6];
        snprintf(tbuf, sizeof(tbuf), "%.0f", data.hourlyTemp[i]);
        display.setFont(&fonts::Font0);
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.setTextDatum(BC_DATUM);
        display.drawString(tbuf, cx, barY - 1);

        // Hora abaixo da barra
        char hbuf[5];
        snprintf(hbuf, sizeof(hbuf), "%dh", (data.hourlyStartHour + i) % 24);
        display.setTextDatum(TC_DATUM);
        display.setTextColor(0x8410, TFT_BLACK);
        display.drawString(hbuf, cx, BASE_Y + 2);
    }
}

// ── Desenho da tela de clima ─────────────────────────────────────────────────
void screenWeatherDraw(lgfx::LovyanGFX& display, const WeatherData& data, bool fetching) {
    display.fillScreen(TFT_BLACK);

    // --- Cidade + bateria ---
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(0x8410, TFT_BLACK);
    display.setTextDatum(TL_DATUM);
    display.drawString(CITY_NAME, 6, 4);

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

    // --- Ícone + descrição ---
    drawWeatherIcon(display, data.weatherCode, display.width() / 2, 46);

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextDatum(MC_DATUM);
    display.drawString(data.description, display.width() / 2, 76);

    display.drawFastHLine(10, 90, display.width() - 20, 0x4208);

    // --- Temperatura atual + tendência ---
    char buf[48];
    int afterTemp = drawTempC(display, "Atual: ", data.tempCurrent, 1, 16, 102,
                              tempColor(data.tempCurrent));

    // Tendência: delta em relação à busca anterior
    if (!isnan(data.tempPrevious)) {
        float delta = data.tempCurrent - data.tempPrevious;
        if (delta > 0.2f || delta < -0.2f) {
            snprintf(buf, sizeof(buf), "%+.1f", delta);
            uint16_t col = (delta > 0) ? TFT_RED : 0x001F;
            display.setFont(&fonts::Font0);
            display.setTextColor(col, TFT_BLACK);
            display.setTextDatum(TL_DATUM);
            display.drawString(buf, afterTemp + 4, 106);
            display.setFont(&fonts::FreeSans9pt7b);
        }
    }

    int x2 = drawTempC(display, "Max: ", data.tempMax, 0, 16, 120, TFT_WHITE);
    drawTempC(display, "  Min: ", data.tempMin, 0, x2, 120, TFT_WHITE);

    snprintf(buf, sizeof(buf), "Umidade: %.0f%%", data.humidity);
    display.setTextColor(0x5D9F, TFT_BLACK);
    display.drawString(buf, 16, 138);

    display.drawFastHLine(10, 152, display.width() - 20, 0x4208);

    // --- Previsão horária (próximas 6h) ---
    drawHourlyForecast(display, data);

    // --- Aviso de bateria crítica ---
    int pct = batteryPercent();
    if (pct >= 0 && pct <= 5 && (millis() / 1000) % 2 == 0) {
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(TFT_RED, TFT_BLACK);
        display.setTextDatum(MC_DATUM);
        display.drawString("BATERIA BAIXA!", display.width() / 2, 210);
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
