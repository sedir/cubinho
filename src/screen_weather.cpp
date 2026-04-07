#include "screen_weather.h"
#include "battery_ui.h"
#include "theme.h"
#include "config.h"
#include <math.h>

// ── Nuvem ────────────────────────────────────────────────────────────────────
static void drawCloud(lgfx::LovyanGFX &d, int cx, int cy, uint16_t c)
{
    d.fillCircle(cx - 10, cy + 2, 9, c);
    d.fillCircle(cx + 2, cy - 5, 12, c);
    d.fillCircle(cx + 13, cy + 2, 8, c);
    d.fillRect(cx - 19, cy + 3, 42, 9, c);
}

// ── Sol ──────────────────────────────────────────────────────────────────────
static void drawSun(lgfx::LovyanGFX &d, int cx, int cy, int r)
{
    static const int8_t UX[] = {0, 7, 10, 7, 0, -7, -10, -7};
    static const int8_t UY[] = {-10, -7, 0, 7, 10, 7, 0, -7};
    int ri = r + 3, ro = r + 9;
    for (int i = 0; i < 8; i++)
    {
        int x1 = cx + UX[i] * ri / 10, y1 = cy + UY[i] * ri / 10;
        int x2 = cx + UX[i] * ro / 10, y2 = cy + UY[i] * ro / 10;
        d.drawLine(x1, y1, x2, y2, COLOR_SUN);
        d.drawLine(x1 + 1, y1, x2 + 1, y2, COLOR_SUN);
    }
    d.fillCircle(cx, cy, r, COLOR_SUN);
}

static void drawRain(lgfx::LovyanGFX &d, int cx, int topY, int n)
{
    int step = 10, startX = cx - (n - 1) * step / 2;
    for (int i = 0; i < n; i++)
    {
        int rx = startX + i * step;
        d.drawLine(rx, topY, rx - 3, topY + 8, COLOR_RAIN);
        d.drawLine(rx + 1, topY, rx - 2, topY + 8, COLOR_RAIN);
    }
}

static void drawSnow(lgfx::LovyanGFX &d, int cx, int topY, int n)
{
    int step = 13, startX = cx - (n - 1) * step / 2;
    for (int i = 0; i < n; i++)
    {
        int sx = startX + i * step, sy = topY + 5;
        d.drawLine(sx - 5, sy, sx + 5, sy, COLOR_SNOW);
        d.drawLine(sx, sy - 5, sx, sy + 5, COLOR_SNOW);
        d.drawLine(sx - 3, sy - 3, sx + 3, sy + 3, COLOR_SNOW);
        d.drawLine(sx + 3, sy - 3, sx - 3, sy + 3, COLOR_SNOW);
    }
}

static void drawBolt(lgfx::LovyanGFX &d, int cx, int topY)
{
    d.fillTriangle(cx + 4, topY, cx - 2, topY + 8, cx + 2, topY + 8, COLOR_BOLT);
    d.fillTriangle(cx + 2, topY + 7, cx - 4, topY + 15, cx + 2, topY + 15, COLOR_BOLT);
}

// ── Ícone do clima (tamanho normal) ──────────────────────────────────────────
static void drawWeatherIcon(lgfx::LovyanGFX &d, int code, int cx, int cy)
{
    if (code == 0)
    {
        drawSun(d, cx, cy, 13);
    }
    else if (code >= 1 && code <= 3)
    {
        drawSun(d, cx - 8, cy - 8, 9);
        drawCloud(d, cx + 4, cy + 4, COLOR_CLOUD);
    }
    else if (code == 45 || code == 48)
    {
        for (int i = 0; i < 4; i++)
        {
            int lw = (i % 2 == 0) ? 36 : 26;
            d.fillRoundRect(cx - lw / 2 + (i % 2) * 5, cy - 13 + i * 9, lw, 3, 1, COLOR_TEXT_DIM);
        }
    }
    else if (code >= 51 && code <= 57)
    {
        drawCloud(d, cx, cy - 8, COLOR_CLOUD);
        drawRain(d, cx, cy + 8, 3);
    }
    else if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82))
    {
        drawCloud(d, cx, cy - 8, COLOR_CLOUD_DARK);
        drawRain(d, cx, cy + 8, 5);
    }
    else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86))
    {
        drawCloud(d, cx, cy - 8, COLOR_CLOUD);
        drawSnow(d, cx, cy + 8, 3);
    }
    else if (code >= 95)
    {
        drawCloud(d, cx, cy - 8, COLOR_CLOUD_DARK);
        drawBolt(d, cx, cy + 8);
    }
    else
    {
        drawCloud(d, cx, cy, COLOR_CLOUD);
    }
}

// ── Mini ícone do clima para barras horárias (item #20) ──────────────────────
static void drawMiniWeatherIcon(lgfx::LovyanGFX &d, int code, int cx, int cy)
{
    if (code == 0)
    {
        d.fillCircle(cx, cy, 3, COLOR_SUN);
    }
    else if (code >= 1 && code <= 3)
    {
        d.fillCircle(cx - 1, cy - 1, 2, COLOR_SUN);
        d.fillCircle(cx + 1, cy + 1, 2, COLOR_CLOUD);
    }
    else if (code >= 51 && code <= 67)
    {
        d.fillCircle(cx, cy - 1, 2, COLOR_CLOUD_DARK);
        d.drawLine(cx - 1, cy + 2, cx - 2, cy + 4, COLOR_RAIN);
        d.drawLine(cx + 1, cy + 2, cx, cy + 4, COLOR_RAIN);
    }
    else if (code >= 71 && code <= 86)
    {
        d.fillCircle(cx, cy - 1, 2, COLOR_CLOUD);
        d.drawPixel(cx - 1, cy + 3, COLOR_SNOW);
        d.drawPixel(cx + 1, cy + 3, COLOR_SNOW);
    }
    else if (code >= 95)
    {
        d.fillCircle(cx, cy - 1, 2, COLOR_CLOUD_DARK);
        d.drawLine(cx, cy + 1, cx - 1, cy + 4, COLOR_BOLT);
    }
    else if (code == 45 || code == 48)
    {
        d.drawFastHLine(cx - 3, cy - 1, 6, COLOR_TEXT_DIM);
        d.drawFastHLine(cx - 2, cy + 1, 4, COLOR_TEXT_DIM);
    }
    else
    {
        d.fillCircle(cx, cy, 2, COLOR_CLOUD);
    }
}

// ── Cor de temperatura ───────────────────────────────────────────────────────
static uint16_t tempColor(float t)
{
    if (t < COMFORT_TEMP_MIN)
        return COLOR_TEMP_COLD;
    if (t > COMFORT_TEMP_MAX)
        return COLOR_TEMP_HOT;
    return COLOR_TEMP_COMFORT;
}

static int drawTempC(lgfx::LovyanGFX &d, const char *prefix, float val, int decimals,
                     int x, int y, uint16_t color)
{
    char buf[16];
    snprintf(buf, sizeof(buf), decimals ? "%s%.1f" : "%s%.0f", prefix, val);
    d.setTextColor(color, COLOR_BACKGROUND);
    d.setTextDatum(TL_DATUM);
    d.drawString(buf, x, y);
    int x2 = x + d.textWidth(buf);
    d.drawCircle(x2 + 3, y + 3, 2, color);
    d.drawString("C", x2 + 7, y);
    return x2 + 7 + d.textWidth("C");
}

// ── Sparkline — gráfico de tendência 24h (item #17) ──────────────────────────
static void drawSparkline(lgfx::LovyanGFX &d, const WeatherData &data, int x, int y, int w, int h)
{
    if (data.trendCount < 2)
        return;

    // Encontrar min/max para escala
    float tmin = 999, tmax = -999;
    int start = (data.trendIdx + TREND_SAMPLES - data.trendCount) % TREND_SAMPLES;
    for (int i = 0; i < data.trendCount; i++)
    {
        float t = data.trendTemp[(start + i) % TREND_SAMPLES];
        if (t < tmin)
            tmin = t;
        if (t > tmax)
            tmax = t;
    }
    float range = (tmax - tmin < 2.0f) ? 2.0f : (tmax - tmin);

    // Desenhar linha
    int prevPx = -1, prevPy = -1;
    for (int i = 0; i < data.trendCount; i++)
    {
        float t = data.trendTemp[(start + i) % TREND_SAMPLES];
        int px = x + i * w / (data.trendCount - 1);
        int py = y + h - (int)((t - tmin) / range * h);
        if (prevPx >= 0)
        {
            d.drawLine(prevPx, prevPy, px, py, COLOR_TEXT_ACCENT);
        }
        prevPx = px;
        prevPy = py;
    }

    // Labels min/max
    d.setFont(&fonts::Font0);
    char lbl[8];
    snprintf(lbl, sizeof(lbl), "%.0f", tmax);
    d.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    d.setTextDatum(TL_DATUM);
    d.drawString(lbl, x + w + 2, y);
    snprintf(lbl, sizeof(lbl), "%.0f", tmin);
    d.drawString(lbl, x + w + 2, y + h - 8);
}

// ── Barras horárias + mini ícones ────────────────────────────────────────────
static void drawHourlyForecast(lgfx::LovyanGFX &d, const WeatherData &data)
{
    const int N = min(data.hourlyCount, 6);
    if (N < 1)
        return;

    const int BAR_W = 36, GAP = 6, BAR_MAX = 50, BAR_MIN = 8;
    const int BASE_Y = 213;
    const int START_X = (d.width() - (N * BAR_W + (N - 1) * GAP)) / 2;

    float tmin = data.hourlyTemp[0], tmax = data.hourlyTemp[0];
    for (int i = 1; i < N; i++)
    {
        if (data.hourlyTemp[i] < tmin)
            tmin = data.hourlyTemp[i];
        if (data.hourlyTemp[i] > tmax)
            tmax = data.hourlyTemp[i];
    }
    float range = (tmax - tmin < 4.0f) ? 4.0f : (tmax - tmin);

    for (int i = 0; i < N; i++)
    {
        int x = START_X + i * (BAR_W + GAP);
        int cx = x + BAR_W / 2;
        int barH = BAR_MIN + (int)((data.hourlyTemp[i] - tmin) / range * (BAR_MAX - BAR_MIN));
        int barY = BASE_Y - barH;

        d.fillRect(x, barY, BAR_W, barH, tempColor(data.hourlyTemp[i]));

        // Mini ícone climático acima da barra (item #20)
        drawMiniWeatherIcon(d, data.hourlyCode[i], cx, barY - 10);

        // Temperatura acima do ícone
        char tbuf[6];
        snprintf(tbuf, sizeof(tbuf), "%.0f", data.hourlyTemp[i]);
        d.setFont(&fonts::Font0);
        d.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BACKGROUND);
        d.setTextDatum(BC_DATUM);
        d.drawString(tbuf, cx, barY - 18);

        // Hora abaixo da barra
        char hbuf[5];
        snprintf(hbuf, sizeof(hbuf), "%dh", (data.hourlyStartHour + i) % 24);
        d.setTextDatum(TC_DATUM);
        d.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
        d.drawString(hbuf, cx, BASE_Y + 2);
    }
}

// ── Desenho da tela de clima ─────────────────────────────────────────────────
void screenWeatherDraw(lgfx::LovyanGFX &display, const WeatherData &data, bool fetching)
{
    display.fillScreen(COLOR_BACKGROUND);

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    display.setTextDatum(TL_DATUM);
    display.drawString(CITY_NAME, 6, 4);

    if (fetching && (millis() / 400) % 2 == 0)
    {
        display.setTextColor(COLOR_TEXT_ACCENT, COLOR_BACKGROUND);
        display.setTextDatum(TR_DATUM);
        display.drawString("...", display.width() - 70, 4);
    }

    drawBatteryIndicator(display);

    if (!data.valid)
    {
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
        display.setTextDatum(MC_DATUM);
        display.drawString("Sem conexao", display.width() / 2, 100);
        display.drawString("Aguardando WiFi...", display.width() / 2, 125);
        drawScreenIndicator(display, 1, SCREEN_COUNT);
        return;
    }

    // ── Coluna esquerda: ícone + descrição ───────────────────────────────────
    const int LEFT_CX = 76;
    drawWeatherIcon(display, data.weatherCode, LEFT_CX, 52);

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BACKGROUND);
    display.setTextDatum(MC_DATUM);
    display.drawString(data.description, LEFT_CX, 88);

    // Separador vertical entre colunas
    display.drawFastVLine(150, 20, 90, COLOR_DIVIDER);

    // ── Coluna direita: temperatura e umidade ─────────────────────────────────
    const int RIGHT_X = 158;
    char buf[48];

    int afterTemp = drawTempC(display, "Atual: ", data.tempCurrent, 1, RIGHT_X, 26,
                              tempColor(data.tempCurrent));

    if (!isnan(data.tempPrevious))
    {
        float delta = data.tempCurrent - data.tempPrevious;
        if (delta > 0.2f || delta < -0.2f)
        {
            snprintf(buf, sizeof(buf), "%+.1f", delta);
            uint16_t col = (delta > 0) ? COLOR_TEMP_HOT : COLOR_TEMP_COLD;
            display.setFont(&fonts::Font0);
            display.setTextColor(col, COLOR_BACKGROUND);
            display.setTextDatum(TL_DATUM);
            display.drawString(buf, afterTemp + 4, 30);
            display.setFont(&fonts::FreeSans9pt7b);
        }
    }

    int x2 = drawTempC(display, "Max: ", data.tempMax, 0, RIGHT_X, 50, COLOR_TEXT_PRIMARY);
    drawTempC(display, "  Min: ", data.tempMin, 0, x2, 50, COLOR_TEXT_PRIMARY);

    display.setFont(&fonts::FreeSans9pt7b);
    snprintf(buf, sizeof(buf), "Umidade: %.0f%%", data.humidity);
    display.setTextColor(COLOR_HUMIDITY, COLOR_BACKGROUND);
    display.setTextDatum(TL_DATUM);
    display.drawString(buf, RIGHT_X, 72);

    // Divisor horizontal
    display.drawFastHLine(10, 110, display.width() - 20, COLOR_DIVIDER);

    // Sparkline de tendência 24h (item #17)
    if (data.trendCount >= 2)
    {
        drawSparkline(display, data, 14, 114, 278, 12);
    }

    // Previsão horária com mini ícones (items #5, #20)
    drawHourlyForecast(display, data);

    // Timestamp no mesmo nível dos dots (sem sobreposição no eixo X)
    display.setFont(&fonts::Font0);
    display.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    display.setTextDatum(ML_DATUM);
    snprintf(buf, sizeof(buf), "Atualizado %s", data.lastUpdated);
    display.drawString(buf, 6, display.height() - 10);

    drawBatteryWarning(display, 220);
    drawScreenIndicator(display, 1, SCREEN_COUNT);
}
