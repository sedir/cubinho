#include "screen_home.h"
#include "status_ui.h"
#include "theme.h"
#include "config.h"
#include <time.h>

static const char* DIAS[]  = { "dom", "seg", "ter", "qua", "qui", "sex", "sab" };
static const char* MESES[] = { "jan", "fev", "mar", "abr", "mai", "jun",
                                "jul", "ago", "set", "out", "nov", "dez" };
static const char* TIMER_LABEL_PRESETS[] = {
    "Forno", "Massa", "Cafe", "Cha", "Sopa",
    "Arroz", "Bolo", "Ovos", "Frango", "Livre"
};
static const int TIMER_LABEL_PRESET_COUNT = (int)(sizeof(TIMER_LABEL_PRESETS) / sizeof(TIMER_LABEL_PRESETS[0]));

// ── Estado do timer — múltiplos slots (item #16) ─────────────────────────────
enum TimerState { TIMER_SETTING, TIMER_RUNNING, TIMER_PAUSED, TIMER_DONE };

static TimerState timerStates[MAX_TIMERS]   = { TIMER_SETTING, TIMER_SETTING, TIMER_SETTING };
static int        timerMinutes[MAX_TIMERS]  = { 5, 10, 15 };
static uint32_t   timerRemainMs[MAX_TIMERS] = { 5*60000, 10*60000, 15*60000 };
static uint32_t   timerStartMs[MAX_TIMERS]  = { 0, 0, 0 };
static int        timerLabelPreset[MAX_TIMERS] = { 0, 1, 2 };
static int        focusedSlot = 0;

// Evento próximo (item #24)
static char nextEventText[48] = "";

static uint32_t getRemaining(int slot) {
    if (timerStates[slot] != TIMER_RUNNING) return timerRemainMs[slot];
    uint32_t elapsed = millis() - timerStartMs[slot];
    return (elapsed >= timerRemainMs[slot]) ? 0 : timerRemainMs[slot] - elapsed;
}

int screenHomeGetTimerLabelPresetCount() {
    return TIMER_LABEL_PRESET_COUNT;
}

const char* screenHomeGetTimerLabelPresetName(int presetIdx) {
    if (presetIdx < 0 || presetIdx >= TIMER_LABEL_PRESET_COUNT) return TIMER_LABEL_PRESETS[0];
    return TIMER_LABEL_PRESETS[presetIdx];
}

int screenHomeGetTimerLabelPreset(int slot) {
    if (slot < 0 || slot >= MAX_TIMERS) return 0;
    return timerLabelPreset[slot];
}

const char* screenHomeGetTimerLabel(int slot) {
    return screenHomeGetTimerLabelPresetName(screenHomeGetTimerLabelPreset(slot));
}

void screenHomeSetTimerLabelPreset(int slot, int presetIdx) {
    if (slot < 0 || slot >= MAX_TIMERS) return;
    if (presetIdx < 0 || presetIdx >= TIMER_LABEL_PRESET_COUNT) presetIdx = slot % TIMER_LABEL_PRESET_COUNT;
    timerLabelPreset[slot] = presetIdx;
}

// ── API de controle ──────────────────────────────────────────────────────────
void screenHomeTimerTap(int tapX) {
    int s = focusedSlot;

    // Verificar se algum alarme está ativo — prioridade
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timerStates[i] == TIMER_DONE) { s = i; break; }
    }

    switch (timerStates[s]) {
        case TIMER_SETTING:
            if (tapX < 160) timerMinutes[s] = max(1,  timerMinutes[s] - 1);
            else             timerMinutes[s] = min(99, timerMinutes[s] + 1);
            timerRemainMs[s] = (uint32_t)timerMinutes[s] * 60000;
            M5.Speaker.tone(80, 30);   // vibração tátil
            M5.Speaker.tone(800, 20);
            break;
        case TIMER_RUNNING:
            timerRemainMs[s] = getRemaining(s);
            timerStates[s]   = TIMER_PAUSED;
            break;
        case TIMER_PAUSED:
            timerStartMs[s] = millis();
            timerStates[s]  = TIMER_RUNNING;
            break;
        case TIMER_DONE:
            timerStates[s]   = TIMER_SETTING;
            timerRemainMs[s] = (uint32_t)timerMinutes[s] * 60000;
            break;
    }
}

void screenHomeTimerLongPress() {
    int s = focusedSlot;
    switch (timerStates[s]) {
        case TIMER_SETTING:
            timerStartMs[s] = millis();
            timerStates[s]  = TIMER_RUNNING;
            M5.Speaker.tone(80,  40);   // vibração tátil
            M5.Speaker.tone(440, 80);
            M5.Speaker.tone(554, 80);
            M5.Speaker.tone(659, 120);
            break;
        case TIMER_RUNNING:
        case TIMER_PAUSED:
        case TIMER_DONE:
            timerStates[s]   = TIMER_SETTING;
            timerRemainMs[s] = (uint32_t)timerMinutes[s] * 60000;
            break;
    }
}

void screenHomeTimerSwitchSlot(int slot) {
    if (slot >= 0 && slot < MAX_TIMERS) {
        focusedSlot = slot;
        M5.Speaker.tone(80,  25);   // vibração tátil
        M5.Speaker.tone(600, 15);
    }
}

int screenHomeGetFocusedSlot() { return focusedSlot; }

bool screenHomeIsAlarmActive() {
    for (int i = 0; i < MAX_TIMERS; i++)
        if (timerStates[i] == TIMER_DONE) return true;
    return false;
}

int screenHomeAlarmSlot() {
    for (int i = 0; i < MAX_TIMERS; i++)
        if (timerStates[i] == TIMER_DONE) return i;
    return -1;
}

bool screenHomeInit() {
    for (int i = 0; i < MAX_TIMERS; i++)
        timerRemainMs[i] = (uint32_t)timerMinutes[i] * 60000;
    return true;
}

bool screenHomeIsTimerActive() {
    for (int i = 0; i < MAX_TIMERS; i++)
        if (timerStates[i] == TIMER_RUNNING || timerStates[i] == TIMER_PAUSED) return true;
    return false;
}

bool screenHomeIsTimerRunning() {
    for (int i = 0; i < MAX_TIMERS; i++)
        if (timerStates[i] == TIMER_RUNNING) return true;
    return false;
}

// Fix #4: RUNNING é salvo como PAUSED (não mais como SETTING)
TimerPersist screenHomeGetTimerPersist() {
    TimerPersist tp;
    tp.focused = focusedSlot;
    for (int i = 0; i < MAX_TIMERS; i++) {
        int s = 0;
        if (timerStates[i] == TIMER_RUNNING || timerStates[i] == TIMER_PAUSED) s = 2;
        tp.state[i]    = s;
        tp.minutes[i]  = timerMinutes[i];
        tp.remainMs[i] = getRemaining(i);
    }
    return tp;
}

void screenHomeSetTimerPersist(const TimerPersist& p) {
    focusedSlot = (p.focused >= 0 && p.focused < MAX_TIMERS) ? p.focused : 0;
    for (int i = 0; i < MAX_TIMERS; i++) {
        timerMinutes[i] = (p.minutes[i] >= 1 && p.minutes[i] <= 99) ? p.minutes[i] : 5;
        if (p.state[i] == 2 && p.remainMs[i] > 0) {
            timerStates[i]   = TIMER_PAUSED;
            timerRemainMs[i] = p.remainMs[i];
        } else {
            timerStates[i]   = TIMER_SETTING;
            timerRemainMs[i] = (uint32_t)timerMinutes[i] * 60000;
        }
    }
}

void screenHomeSetNextEvent(const char* text) {
    strlcpy(nextEventText, text, sizeof(nextEventText));
}

// ── Primitivas de ícone ──────────────────────────────────────────────────────
static void drawPlayIcon(lgfx::LovyanGFX& d, int cx, int cy, uint16_t c) {
    d.fillTriangle(cx - 6, cy - 9, cx - 6, cy + 9, cx + 9, cy, c);
}
static void drawPauseIcon(lgfx::LovyanGFX& d, int cx, int cy, uint16_t c) {
    d.fillRect(cx - 7, cy - 9, 5, 18, c);
    d.fillRect(cx + 2, cy - 9, 5, 18, c);
}
static void drawLeftArrow(lgfx::LovyanGFX& d, int cx, int cy, uint16_t c) {
    d.fillTriangle(cx + 7, cy - 8, cx + 7, cy + 8, cx - 7, cy, c);
}
static void drawRightArrow(lgfx::LovyanGFX& d, int cx, int cy, uint16_t c) {
    d.fillTriangle(cx - 7, cy - 8, cx - 7, cy + 8, cx + 7, cy, c);
}

static void drawFocusedTimerLabel(lgfx::LovyanGFX& d, int slot, int y, uint16_t color) {
    char label[32];
    snprintf(label, sizeof(label), "T%d · %s", slot + 1, screenHomeGetTimerLabel(slot));
    d.setFont(&fonts::Font0);
    d.setTextColor(color, COLOR_BACKGROUND);
    d.setTextDatum(MC_DATUM);
    d.drawString(label, d.width() / 2, y);
}

static const int TIMER_LABEL_Y   = 148;
static const int TIMER_VALUE_Y   = 176;
static const int TIMER_HINT_Y    = 206;
static const int TIMER_SUMMARY_Y = 198;

static void trimSummary(char* text) {
    size_t len = strlen(text);
    while (len > 0 && text[len - 1] == ' ') {
        text[--len] = '\0';
    }
}

static void appendEllipsisToSummary(lgfx::LovyanGFX& d, char* text, size_t size, int maxWidth) {
    trimSummary(text);
    while (text[0] && d.textWidth(text) + d.textWidth("...") > maxWidth) {
        size_t len = strlen(text);
        if (len == 0) break;
        text[len - 1] = '\0';
        trimSummary(text);
    }

    if (text[0]) {
        size_t len = strlen(text);
        snprintf(text + len, size - len, "...");
    }
}

static bool drawOtherTimersSummary(lgfx::LovyanGFX& d, int focused, int y) {
    char summary[64] = "";
    const int maxWidth = d.width() - 24;

    d.setFont(&fonts::Font0);
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (i == focused) continue;
        if (timerStates[i] != TIMER_RUNNING && timerStates[i] != TIMER_PAUSED) continue;

        uint32_t r = getRemaining(i);
        char part[28];
        snprintf(part, sizeof(part), "%s %02lu:%02lu", screenHomeGetTimerLabel(i),
                 (unsigned long)(r / 60000), (unsigned long)((r / 1000) % 60));

        char candidate[64];
        if (summary[0]) snprintf(candidate, sizeof(candidate), "%s · %s", summary, part);
        else            snprintf(candidate, sizeof(candidate), "%s", part);

        if (d.textWidth(candidate) > maxWidth) {
            if (!summary[0]) {
                strncpy(summary, part, sizeof(summary) - 1);
                summary[sizeof(summary) - 1] = '\0';
            }
            appendEllipsisToSummary(d, summary, sizeof(summary), maxWidth);
            break;
        }

        strncpy(summary, candidate, sizeof(summary) - 1);
        summary[sizeof(summary) - 1] = '\0';
    }

    if (!summary[0]) return false;

    d.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    d.setTextDatum(MC_DATUM);
    d.drawString(summary, d.width() / 2, y);
    return true;
}

// ── Slot tabs (T1 T2 T3) ────────────────────────────────────────────────────
static void drawSlotTabs(lgfx::LovyanGFX& d, int y) {
    d.setFont(&fonts::Font0);
    const int tabW = 48, tabH = 20, gap = 6;
    int startX = d.width() / 2 - (MAX_TIMERS * tabW + (MAX_TIMERS - 1) * gap) / 2;

    for (int i = 0; i < MAX_TIMERS; i++) {
        int tx = startX + i * (tabW + gap);
        bool focused = (i == focusedSlot);
        bool active  = (timerStates[i] == TIMER_RUNNING || timerStates[i] == TIMER_PAUSED);
        bool done    = (timerStates[i] == TIMER_DONE);

        uint16_t bg = focused ? COLOR_TEXT_DIM : COLOR_BACKGROUND;
        uint16_t fg = done ? TFT_RED : (active ? COLOR_TIMER_RUNNING : COLOR_TEXT_SUBTLE);

        d.fillRoundRect(tx, y, tabW, tabH, 4, bg);
        d.drawRoundRect(tx, y, tabW, tabH, 4, fg);

        char label[4];
        snprintf(label, sizeof(label), "T%d", i + 1);
        d.setTextColor(fg, bg);
        d.setTextDatum(MC_DATUM);
        d.drawString(label, tx + tabW / 2, y + tabH / 2);
    }
}

// ── Desenho da tela ──────────────────────────────────────────────────────────
void screenHomeDraw(lgfx::LovyanGFX& display, bool syncing, bool isDim) {
    display.fillScreen(COLOR_BACKGROUND);

    // --- Data + evento + bateria ---
    struct tm timeinfo = {};
    time_t now = time(nullptr);
    bool timeOk = (now > 1577836800L) && (localtime_r(&now, &timeinfo) != nullptr);

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    display.setTextDatum(TL_DATUM);
    if (timeOk) {
        char dateBuf[32];
        snprintf(dateBuf, sizeof(dateBuf), "%s, %02d %s %04d",
                 DIAS[timeinfo.tm_wday], timeinfo.tm_mday,
                 MESES[timeinfo.tm_mon], timeinfo.tm_year + 1900);
        display.drawString(dateBuf, 6, 5);
    } else {
        display.drawString("---, -- --- ----", 6, 5);
    }
    drawBatteryIndicator(display);

    // Evento próximo (item #24)
    if (nextEventText[0]) {
        display.setFont(&fonts::Font0);
        display.setTextColor(COLOR_TEXT_ACCENT, COLOR_BACKGROUND);
        display.setTextDatum(TL_DATUM);
        display.drawString(nextEventText, 6, 25);
    }

    // --- Hora grande (Y fixo — não desloca por nextEventText) ---
    const int clockY = 70;
    display.setFont(&fonts::FreeSansBold24pt7b);
    display.setTextColor(COLOR_TEXT_ACCENT, COLOR_BACKGROUND);
    display.setTextDatum(MC_DATUM);
    if (timeOk) {
        char timeBuf[12];
        if (isDim) snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:--",
                            timeinfo.tm_hour, timeinfo.tm_min);
        else       snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
                            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        display.drawString(timeBuf, display.width() / 2, clockY);
    } else {
        display.drawString("--:--:--", display.width() / 2, clockY);
    }

    // --- Indicador de sincronização ---
    if (syncing) {
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(COLOR_TEXT_SUBTLE, COLOR_BACKGROUND);
        display.setTextDatum(MC_DATUM);
        display.drawString("atualizando...", display.width() / 2, 104);
    }

    // --- Divisor ---
    display.drawFastHLine(10, TIMER_ZONE_Y, display.width() - 20, COLOR_DIVIDER);

    // ── Timer ────────────────────────────────────────────────────────────────
    // Verificar alarme em qualquer slot
    int alarmSlot = screenHomeAlarmSlot();
    if (alarmSlot >= 0) {
        drawFocusedTimerLabel(display, alarmSlot, TIMER_LABEL_Y, COLOR_TEXT_ACCENT);

        uint16_t flash = ((millis() / 400) % 2 == 0) ? TFT_RED : COLOR_TEXT_PRIMARY;
        display.setFont(&fonts::FreeSansBold18pt7b);
        display.setTextColor(flash, COLOR_BACKGROUND);
        display.setTextDatum(MC_DATUM);
        display.drawString("PRONTO!", display.width() / 2, TIMER_VALUE_Y);

        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
        display.drawString("toque para fechar", display.width() / 2, TIMER_HINT_Y);
    } else {
        // Verificar se algum running chegou a zero
        for (int i = 0; i < MAX_TIMERS; i++) {
            if (timerStates[i] == TIMER_RUNNING && getRemaining(i) == 0) {
                timerStates[i]   = TIMER_DONE;
                timerRemainMs[i] = 0;
            }
        }

        // Slot tabs — altura 20 px, fáceis de tocar
        drawSlotTabs(display, TIMER_ZONE_Y + 5);

        int s = focusedSlot;
        uint32_t remaining = getRemaining(s);

        uint16_t timerColor = (timerStates[s] == TIMER_RUNNING) ? COLOR_TIMER_RUNNING
                            : (timerStates[s] == TIMER_PAUSED)  ? COLOR_TIMER_PAUSED
                            :                                      COLOR_TIMER_SETTING;
        drawFocusedTimerLabel(display, s, TIMER_LABEL_Y, timerColor);
        bool hasOtherSummary = drawOtherTimersSummary(display, s, TIMER_SUMMARY_Y);

        char tBuf[12];
        if (timerStates[s] == TIMER_SETTING) {
            snprintf(tBuf, sizeof(tBuf), "%d min", timerMinutes[s]);
        } else {
            uint32_t mins = remaining / 60000;
            uint32_t secs = (remaining / 1000) % 60;
            snprintf(tBuf, sizeof(tBuf), "%02lu:%02lu", (unsigned long)mins, (unsigned long)secs);
        }

        // Ícone + texto centralizados como par (ícone em x=106, texto em x=204)
        const int timerY = TIMER_VALUE_Y;
        const int iconX  = display.width() / 2 - 54;  // 106
        const int textX  = display.width() / 2 + 44;  // 204

        display.setFont(&fonts::FreeSansBold18pt7b);
        display.setTextColor(timerColor, COLOR_BACKGROUND);
        display.setTextDatum(MC_DATUM);
        display.drawString(tBuf, textX, timerY);

        // Ícone play/pause
        if (timerStates[s] == TIMER_RUNNING)
            drawPauseIcon(display, iconX, timerY, timerColor);
        else
            drawPlayIcon(display, iconX, timerY, timerColor);

        if (timerStates[s] == TIMER_SETTING) {
            // Setas nas bordas — área de toque generosa
            drawLeftArrow(display, 22, timerY, COLOR_TEXT_SUBTLE);
            drawRightArrow(display, display.width() - 22, timerY, COLOR_TEXT_SUBTLE);
            if (!hasOtherSummary) {
                display.setFont(&fonts::FreeSans9pt7b);
                display.setTextColor(COLOR_TEXT_SUBTLE, COLOR_BACKGROUND);
                display.setTextDatum(MC_DATUM);
                display.drawString("Segurar: iniciar", display.width() / 2, TIMER_HINT_Y);
            }
        } else if (timerStates[s] == TIMER_PAUSED) {
            if (!hasOtherSummary) {
                display.setFont(&fonts::FreeSans9pt7b);
                display.setTextColor(COLOR_TEXT_SUBTLE, COLOR_BACKGROUND);
                display.setTextDatum(MC_DATUM);
                display.drawString("Segurar: zerar", display.width() / 2, TIMER_HINT_Y);
            }
        }
    }

    // --- Aviso de bateria + indicador de tela ---
    drawBatteryWarning(display, 218);
    drawScreenIndicator(display, 0, SCREEN_COUNT);
}
