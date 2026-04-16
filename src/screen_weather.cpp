#include "screen_weather.h"
#include "status_ui.h"
#include "theme.h"
#include "config.h"
#include <string.h>
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

// ── Faixa de previsão 7 dias ─────────────────────────────────────────────────
static const char* WDAY_ABR[] = { "Dom","Seg","Ter","Qua","Qui","Sex","Sab" };

static void drawSevenDayForecast(lgfx::LovyanGFX& d, const WeatherData& data, int y)
{
    if (data.dailyCount < 2) return;

    struct tm t = {};
    time_t now  = time(nullptr);
    int todayWday = 0;
    if (now > 0 && localtime_r(&now, &t) != nullptr)
        todayWday = t.tm_wday;

    const int N      = min((int)data.dailyCount, 7);
    const int COL_W  = 44;
    const int startX = (d.width() - N * COL_W) / 2;

    for (int i = 0; i < N; i++) {
        int cx   = startX + i * COL_W + COL_W / 2;
        int wday = (todayWday + i) % 7;
        bool today = (i == 0);

        // Destaque leve no dia de hoje
        if (today)
            d.fillRoundRect(startX + i * COL_W, y, COL_W - 1, 34, 3, 0x1082);

        uint16_t bgColor = today ? 0x1082 : COLOR_BACKGROUND;

        // Nome do dia
        d.setFont(&fonts::Font0);
        d.setTextDatum(TC_DATUM);
        d.setTextColor(today ? COLOR_TEXT_PRIMARY : COLOR_TEXT_DIM, bgColor);
        d.drawString(WDAY_ABR[wday], cx, y + 2);

        // Mini ícone do clima
        drawMiniWeatherIcon(d, data.dailyCode[i], cx, y + 14);

        // Temp máxima (cor de conforto)
        char buf[6];
        snprintf(buf, sizeof(buf), "%.0f", data.dailyTempMax[i]);
        d.setTextDatum(TC_DATUM);
        // tempColor ainda não está declarado aqui — definido depois; usamos inline
        float mx = data.dailyTempMax[i];
        uint16_t mxCol = (mx < COMFORT_TEMP_MIN) ? COLOR_TEMP_COLD
                       : (mx > COMFORT_TEMP_MAX) ? COLOR_TEMP_HOT
                       :                           COLOR_TEMP_COMFORT;
        d.setTextColor(mxCol, bgColor);
        d.drawString(buf, cx, y + 22);

        // Temp mínima (sempre fria/neutra)
        snprintf(buf, sizeof(buf), "%.0f", data.dailyTempMin[i]);
        d.setTextColor(COLOR_TEMP_COLD, bgColor);
        d.drawString(buf, cx, y + 30);
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

static uint16_t uvColor(float uv)
{
    if (uv < 3.0f)  return COLOR_TEMP_COMFORT;  // baixo
    if (uv < 6.0f)  return COLOR_BOLT;           // moderado
    if (uv < 8.0f)  return 0xFD20;              // alto
    if (uv < 11.0f) return TFT_RED;             // muito alto
    return 0xF81F;                               // extremo
}

static bool isRainCode(int code)
{
    return (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95;
}

static bool buildRainSummary(const WeatherData &data, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';

    if (data.hourlyCount <= 0)
    {
        snprintf(out, outSize, "Sem previsao horaria");
        return false;
    }

    for (int i = 0; i < min(data.hourlyCount, 12); i++)
    {
        if (!isRainCode(data.hourlyCode[i]))
            continue;

        int chance = data.hourlyPrecipProb[i];
        if (i == 0)
        {
            if (chance > 0)
                snprintf(out, outSize, "Chuva agora (%d%%)", chance);
            else
                snprintf(out, outSize, "Chuva agora");
        }
        else if (i == 1)
        {
            if (chance > 0)
                snprintf(out, outSize, "Chuva na proxima hora (%d%%)", chance);
            else
                snprintf(out, outSize, "Chuva na proxima hora");
        }
        else
        {
            if (chance > 0)
                snprintf(out, outSize, "Chuva em %dh (%d%%)", i, chance);
            else
                snprintf(out, outSize, "Chuva em %dh", i);
        }
        return true;
    }

    snprintf(out, outSize, "Sem chuva nas proximas horas");
    return false;
}

static void trimTrailingSpaces(char *text)
{
    int len = (int)strlen(text);
    while (len > 0 && text[len - 1] == ' ')
    {
        text[--len] = '\0';
    }
}

static void appendEllipsisToFit(lgfx::LovyanGFX &d, char *text, int maxWidth)
{
    static const char kDots[] = "...";
    trimTrailingSpaces(text);

    while (text[0] && d.textWidth(text) + d.textWidth(kDots) > maxWidth)
    {
        size_t len = strlen(text);
        if (len == 0)
            break;
        text[len - 1] = '\0';
        trimTrailingSpaces(text);
    }

    if (text[0])
    {
        size_t len = strlen(text);
        snprintf(text + len, 48 - len, "%s", kDots);
    }
    else
    {
        strncpy(text, kDots, 4);
    }
}

static void drawWrappedCenteredText(lgfx::LovyanGFX &d, const char *text, int centerX,
                                    int topY, int maxWidth, int lineHeight, int maxLines)
{
    if (!text || !text[0] || maxLines <= 0)
        return;

    const char *p = text;
    char line[48];

    for (int lineIdx = 0; lineIdx < maxLines && *p; ++lineIdx)
    {
        while (*p == ' ')
            ++p;
        if (!*p)
            break;

        line[0] = '\0';
        bool usedWord = false;

        while (*p)
        {
            while (*p == ' ')
                ++p;
            if (!*p)
                break;

            char word[24];
            int wlen = 0;
            while (p[wlen] && p[wlen] != ' ' && wlen < (int)sizeof(word) - 1)
            {
                word[wlen] = p[wlen];
                ++wlen;
            }
            word[wlen] = '\0';

            char candidate[48];
            if (usedWord)
                snprintf(candidate, sizeof(candidate), "%s %s", line, word);
            else
                snprintf(candidate, sizeof(candidate), "%s", word);

            if (usedWord && d.textWidth(candidate) > maxWidth)
            {
                break;
            }

            strncpy(line, candidate, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
            usedWord = true;
            p += wlen;

            if (!usedWord)
                break;

            while (*p == ' ')
                ++p;

            if (!usedWord || d.textWidth(line) > maxWidth)
            {
                break;
            }
        }

        if (!usedWord)
        {
            strncpy(line, text, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
        }

        if (lineIdx == maxLines - 1)
        {
            while (*p == ' ')
                ++p;
            if (*p)
                appendEllipsisToFit(d, line, maxWidth);
        }

        d.setTextDatum(MC_DATUM);
        d.drawString(line, centerX, topY + lineIdx * lineHeight);
    }
}

// ── Gráfico horário: sparkline + banda de conforto + precipitação ────────────
static void drawHourlyChart(lgfx::LovyanGFX &d, const WeatherData &data)
{
    const int N = min(data.hourlyCount, 48);
    if (N < 2)
        return;

    // Geometria do gráfico (deslocado para baixo da faixa de 7 dias)
    const int CX    = 10;                  // margem esquerda
    const int CW    = d.width() - 20;      // 300px
    const int CT    = 150;                 // topo da área de temperatura
    const int CB    = 207;                 // base da área de temperatura
    const int CH    = CB - CT;             // 57px de altura
    const int PR_Y  = 209;                 // topo das barras de precipitação
    const int PR_H  = 6;                   // altura máxima das barras
    const int LBL_Y = 217;                 // y das labels de hora

    // ── Faixa de temperatura (sempre inclui a zona de conforto) ─────────────
    float tmin = data.hourlyTemp[0], tmax = data.hourlyTemp[0];
    for (int i = 1; i < N; i++) {
        if (data.hourlyTemp[i] < tmin) tmin = data.hourlyTemp[i];
        if (data.hourlyTemp[i] > tmax) tmax = data.hourlyTemp[i];
    }
    tmin = min(tmin, (float)COMFORT_TEMP_MIN - 2.0f);
    tmax = max(tmax, (float)COMFORT_TEMP_MAX + 2.0f);
    float trange = tmax - tmin;

    auto tempToY = [&](float t) -> int {
        return CB - (int)((t - tmin) / trange * CH + 0.5f);
    };

    // ── Preenchimento sutil da banda de conforto ─────────────────────────────
    int yBandHi = max(CT, tempToY(COMFORT_TEMP_MAX));
    int yBandLo = min(CB, tempToY(COMFORT_TEMP_MIN));
    if (yBandLo > yBandHi)
        d.fillRect(CX, yBandHi, CW, yBandLo - yBandHi, 0x0841); // verde muito escuro

    // ── Linhas pontilhadas nos limites da zona de conforto ───────────────────
    int yCH = tempToY(COMFORT_TEMP_MAX);
    int yCL = tempToY(COMFORT_TEMP_MIN);
    for (int x = CX; x < CX + CW; x += 4) {
        if (yCH >= CT && yCH <= CB) d.drawPixel(x, yCH, COLOR_TEMP_COMFORT);
        if (yCL >= CT && yCL <= CB) d.drawPixel(x, yCL, COLOR_TEMP_COMFORT);
    }

    // ── Sparkline 2px, colorida por zona de conforto ─────────────────────────
    for (int i = 1; i < N; i++) {
        int x1 = CX + (i - 1) * CW / (N - 1);
        int x2 = CX + i       * CW / (N - 1);
        int y1 = tempToY(data.hourlyTemp[i - 1]);
        int y2 = tempToY(data.hourlyTemp[i]);
        uint16_t col = tempColor(data.hourlyTemp[i]);
        d.drawLine(x1, y1,     x2, y2,     col);
        d.drawLine(x1, y1 + 1, x2, y2 + 1, col);
    }

    // Marcador da hora atual (primeiro ponto)
    d.fillCircle(CX, tempToY(data.hourlyTemp[0]), 2, TFT_WHITE);

    // ── Labels de temperatura em extremos significativos ─────────────────────
    // Estratégia: extremos locais (pico/vale) filtrados por espaçamento mínimo
    // (MIN_GAP px) e variação mínima (LABEL_THRESH °C) desde o último label.
    {
        const float LABEL_THRESH = 2.0f;   // °C mínimo de variação
        const int   MIN_GAP      = 40;     // px mínimo entre labels

        int   kept[48];
        int   nkept   = 0;
        int   lastX   = CX;
        float lastT   = data.hourlyTemp[0];

        kept[nkept++] = 0; // primeiro ponto sempre

        for (int i = 1; i < N - 1; i++) {
            float prev = data.hourlyTemp[i - 1];
            float cur  = data.hourlyTemp[i];
            float next = data.hourlyTemp[i + 1];
            if (!(cur > prev && cur > next) && !(cur < prev && cur < next)) continue;

            int x = CX + i * CW / (N - 1);
            if (x - lastX >= MIN_GAP && fabsf(cur - lastT) >= LABEL_THRESH) {
                kept[nkept++] = i;
                lastX = x;
                lastT = cur;
            }
        }

        d.setFont(&fonts::Font0);
        for (int k = 0; k < nkept; k++) {
            int   i   = kept[k];
            int   px  = CX + i * CW / (N - 1);
            int   py  = tempToY(data.hourlyTemp[i]);
            char  tbuf[5];
            snprintf(tbuf, sizeof(tbuf), "%.0f", data.hourlyTemp[i]);
            d.setTextColor(tempColor(data.hourlyTemp[i]), COLOR_BACKGROUND);
            // Acima do ponto se houver espaço, senão abaixo
            if (py - 4 >= CT + 8) {
                d.setTextDatum(BC_DATUM);
                d.drawString(tbuf, px, py - 4);
            } else {
                d.setTextDatum(TC_DATUM);
                d.drawString(tbuf, px, py + 3);
            }
        }
    }

    // ── Barras de probabilidade de precipitação ──────────────────────────────
    for (int i = 0; i < N; i++) {
        int prob = data.hourlyPrecipProb[i];
        if (prob < 10) continue;
        int x  = CX + i * CW / (N - 1);
        int bh = max(1, prob * PR_H / 100);
        uint16_t col = (prob >= 70) ? COLOR_RAIN
                     : (prob >= 40) ? COLOR_HUMIDITY
                     :                COLOR_TEXT_SUBTLE;
        d.fillRect(x, PR_Y + PR_H - bh, 2, bh, col);
    }

    // ── Labels de hora a cada 6h ─────────────────────────────────────────────
    d.setFont(&fonts::Font0);
    d.setTextDatum(TC_DATUM);
    d.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    for (int i = 0; i < N; i += 6) {
        int x = CX + i * CW / (N - 1);
        int h = (data.hourlyStartHour + i) % 24;
        char buf[5];
        snprintf(buf, sizeof(buf), "%dh", h);
        d.drawString(buf, x, LBL_Y);
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
    drawWrappedCenteredText(display, data.description, LEFT_CX, 82, 132, 18, 2);

    char rainSummary[40];
    bool hasUpcomingRain = buildRainSummary(data, rainSummary, sizeof(rainSummary));

    // Separador vertical entre colunas
    display.drawFastVLine(150, 20, 90, COLOR_DIVIDER);

    // ── Coluna direita: temperatura e umidade ─────────────────────────────────
    const int RIGHT_X = 158;
    char buf[48];

    int afterTemp = drawTempC(display, "Atual: ", data.tempCurrent, 1, RIGHT_X, 24,
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
            display.drawString(buf, afterTemp + 4, 28);
            display.setFont(&fonts::FreeSans9pt7b);
        }
    }

    drawTempC(display, "Max:", data.tempMax, 0, RIGHT_X, 44, COLOR_TEXT_PRIMARY);
    drawTempC(display, "Min:", data.tempMin, 0, RIGHT_X + 83, 44, COLOR_TEXT_PRIMARY);

    display.setFont(&fonts::FreeSans9pt7b);
    snprintf(buf, sizeof(buf), "Umidade: %.0f%%", data.humidity);
    display.setTextColor(COLOR_HUMIDITY, COLOR_BACKGROUND);
    display.setTextDatum(TL_DATUM);
    display.drawString(buf, RIGHT_X, 64);

    if (!isnan(data.apparentTemp))
    {
        drawTempC(display, "Sensacao: ", data.apparentTemp, 0, RIGHT_X, 81,
                  tempColor(data.apparentTemp));
    }

    if (!isnan(data.uvIndexMax) && data.uvIndexMax >= 0.0f)
    {
        char uvbuf[16];
        snprintf(uvbuf, sizeof(uvbuf), "UV max: %.0f", data.uvIndexMax);
        display.setFont(&fonts::Font0);
        display.setTextColor(uvColor(data.uvIndexMax), COLOR_BACKGROUND);
        display.setTextDatum(TL_DATUM);
        display.drawString(uvbuf, RIGHT_X, 99);
        display.setFont(&fonts::FreeSans9pt7b);
    }

    // Divisor horizontal
    display.drawFastHLine(10, 110, display.width() - 20, COLOR_DIVIDER);

    // Faixa de previsão 7 dias (y=113, altura=34px)
    if (data.dailyCount >= 2) {
        drawSevenDayForecast(display, data, 113);
    } else {
        // Fallback: resumo de chuva quando não há dados diários
        display.setFont(&fonts::Font0);
        display.setTextColor((isRainCode(data.weatherCode) || hasUpcomingRain)
                             ? COLOR_TEXT_ACCENT : COLOR_TEXT_DIM, COLOR_BACKGROUND);
        display.setTextDatum(MC_DATUM);
        display.drawString(rainSummary, display.width() / 2, 122);
    }

    // Gráfico horário: sparkline + banda de conforto + precipitação
    drawHourlyChart(display, data);

    // Timestamp no mesmo nível dos dots (sem sobreposição no eixo X)
    display.setFont(&fonts::Font0);
    display.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    display.setTextDatum(ML_DATUM);
    snprintf(buf, sizeof(buf), "Atualizado %s", data.lastUpdated);
    display.drawString(buf, 6, display.height() - 10);

    drawBatteryWarning(display, 220);
    drawScreenIndicator(display, 1, SCREEN_COUNT);
}
