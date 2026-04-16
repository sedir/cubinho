#include "screen_home.h"
#include "status_ui.h"
#include "theme.h"
#include "config.h"
#include "logger.h"
#include <time.h>
#include <math.h>

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
static char       timerCustomName[MAX_TIMERS][16] = { {0}, {0}, {0} };
static bool       timerHasCustomName[MAX_TIMERS]  = { false, false, false };
static int        focusedSlot = 0;

// ── Estado do cronômetro (slot MAX_TIMERS) ───────────────────────────────────
enum SwState { SW_IDLE, SW_RUNNING, SW_PAUSED };
static SwState  g_swState   = SW_IDLE;
static uint32_t g_swStartMs = 0;
static uint32_t g_swAccumMs = 0;

static uint32_t swGetElapsed() {
    if (g_swState != SW_RUNNING) return g_swAccumMs;
    return g_swAccumMs + (millis() - g_swStartMs);
}

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
    if (slot >= 0 && slot < MAX_TIMERS && timerHasCustomName[slot] && timerCustomName[slot][0])
        return timerCustomName[slot];
    return screenHomeGetTimerLabelPresetName(screenHomeGetTimerLabelPreset(slot));
}

void screenHomeSetTimerLabelPreset(int slot, int presetIdx) {
    if (slot < 0 || slot >= MAX_TIMERS) return;
    if (presetIdx < 0 || presetIdx >= TIMER_LABEL_PRESET_COUNT) presetIdx = slot % TIMER_LABEL_PRESET_COUNT;
    timerLabelPreset[slot] = presetIdx;
}

int  screenHomeGetTotalSlots()      { return MAX_TIMERS + 1; }
bool screenHomeIsStopwatchRunning() { return g_swState == SW_RUNNING; }

void screenHomeStopwatchTap() {
    switch (g_swState) {
        case SW_IDLE:
            g_swStartMs = millis();
            g_swState   = SW_RUNNING;
            M5.Speaker.tone(440, 60);
            break;
        case SW_RUNNING:
            g_swAccumMs += millis() - g_swStartMs;
            g_swState    = SW_PAUSED;
            break;
        case SW_PAUSED:
            g_swStartMs = millis();
            g_swState   = SW_RUNNING;
            M5.Speaker.tone(440, 30);
            break;
    }
}

void screenHomeStopwatchLongPress() {
    g_swState   = SW_IDLE;
    g_swAccumMs = 0;
    M5.Speaker.tone(300, 40);
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
            // Ajuste de minutos agora é feito por swipe vertical — tap sem efeito
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

void screenHomeTimerSwipeAdjust(int deltaY) {
    int s = focusedSlot;
    if (timerStates[s] != TIMER_SETTING) return;
    // deltaY > 0: swipe para cima → mais minutos; < 0: para baixo → menos
    int delta = deltaY / 10;
    if (delta == 0) delta = (deltaY > 0) ? 1 : -1;
    delta = constrain(delta, -20, 20);
    timerMinutes[s] = constrain(timerMinutes[s] + delta, 1, 99);
    timerRemainMs[s] = (uint32_t)timerMinutes[s] * 60000;
    M5.Speaker.tone(deltaY > 0 ? 880 : 660, 25);
}

void screenHomeTimerSwitchSlot(int slot) {
    if (slot >= 0 && slot <= MAX_TIMERS) {  // MAX_TIMERS = índice do SW
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

// Atualiza timers independentemente da tela — retorna true se algum acabou de disparar
bool screenHomeTimerUpdate() {
    bool justFired = false;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timerStates[i] == TIMER_RUNNING && getRemaining(i) == 0) {
            timerStates[i]   = TIMER_DONE;
            timerRemainMs[i] = 0;
            justFired = true;
        }
    }
    return justFired;
}

// RUNNING é salvo como PAUSED para persistência no deep sleep
TimerPersist screenHomeGetTimerPersist() {
    TimerPersist tp;
    tp.focused = (focusedSlot < MAX_TIMERS) ? focusedSlot : 0;  // SW não persiste
    for (int i = 0; i < MAX_TIMERS; i++) {
        int s = 0;
        if (timerStates[i] == TIMER_RUNNING || timerStates[i] == TIMER_PAUSED) s = 2;
        tp.state[i]    = s;
        tp.minutes[i]  = timerMinutes[i];
        tp.remainMs[i] = getRemaining(i);
        strlcpy(tp.customName[i], timerCustomName[i], sizeof(tp.customName[0]));
        tp.hasCustomName[i] = timerHasCustomName[i];
    }
    return tp;
}

void screenHomeSetTimerPersist(const TimerPersist& p) {
    focusedSlot = (p.focused >= 0 && p.focused < MAX_TIMERS) ? p.focused : 0;
    for (int i = 0; i < MAX_TIMERS; i++) {
        timerMinutes[i] = (p.minutes[i] >= 1 && p.minutes[i] <= 99) ? p.minutes[i] : 5;
        strlcpy(timerCustomName[i], p.customName[i], sizeof(timerCustomName[0]));
        timerHasCustomName[i] = p.hasCustomName[i];
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
static void drawUpArrow(lgfx::LovyanGFX& d, int cx, int cy, uint16_t c) {
    d.fillTriangle(cx, cy - 7, cx - 7, cy + 6, cx + 7, cy + 6, c);
}
static void drawDownArrow(lgfx::LovyanGFX& d, int cx, int cy, uint16_t c) {
    d.fillTriangle(cx, cy + 7, cx - 7, cy - 6, cx + 7, cy - 6, c);
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
static const int TIMER_HINT_Y    = 210;
static const int TIMER_SUMMARY_Y = 210;

// ── Teclado on-screen ────────────────────────────────────────────────────────
static bool keyboardActive = false;
static int  keyboardSlot   = 0;
static char keyboardBuf[16] = "";
static bool keyboardShift  = true;  // true = próxima letra maiúscula

static const char* KB_ROW1 = "QWERTYUIOP";  // 10 teclas
static const char* KB_ROW2 = "ASDFGHJKL";   //  9 teclas
static const char* KB_ROW3 = "ZXCVBNM";     //  7 teclas + DEL

// Layout: input (h=42) + 4 linhas de teclas (h=42 cada, gap=3)
static const int KB_INPUT_H  = 42;
static const int KB_KEY_W    = 28;
static const int KB_KEY_H    = 42;
static const int KB_GAP      = 3;
static const int KB_ROW1_Y   = KB_INPUT_H + 2;
static const int KB_ROW2_Y   = KB_ROW1_Y + KB_KEY_H + KB_GAP;
static const int KB_ROW3_Y   = KB_ROW2_Y + KB_KEY_H + KB_GAP;
static const int KB_ROW4_Y   = KB_ROW3_Y + KB_KEY_H + KB_GAP;
// startX para cada linha (centralizado em 320px)
static const int KB_ROW1_X   = (320 - (10 * 28 + 9 * 3)) / 2;   //  6
static const int KB_ROW2_X   = (320 - ( 9 * 28 + 8 * 3)) / 2;   // 22

static void drawKbKey(lgfx::LovyanGFX& d, int x, int y, int w, int h, const char* label, uint16_t bg, uint16_t fg) {
    d.fillRoundRect(x, y, w, h, 4, bg);
    d.drawRoundRect(x, y, w, h, 4, 0x4208);
    d.setFont(&fonts::Font0);
    d.setTextDatum(MC_DATUM);
    d.setTextColor(fg, bg);
    d.drawString(label, x + w / 2, y + h / 2);
}

static void drawKeyboardOverlay(lgfx::LovyanGFX& d) {
    // Fundo completo
    d.fillScreen(COLOR_BACKGROUND);

    // Área de input
    d.fillRect(0, 0, 320, KB_INPUT_H, 0x1082);
    d.drawFastHLine(0, KB_INPUT_H, 320, 0x4208);

    char slotLabel[6];
    snprintf(slotLabel, sizeof(slotLabel), "T%d:", keyboardSlot + 1);
    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_ACCENT, 0x1082);
    d.setTextDatum(ML_DATUM);
    d.drawString(slotLabel, 8, KB_INPUT_H / 2);

    // Texto digitado + cursor em bloco piscante
    // Font0: largura fixa de 6px por caractere, altura ~8px
    {
        int textLen = (int)strlen(keyboardBuf);
        int textX   = 36;
        int textY   = KB_INPUT_H / 2;
        bool cursorOn = (millis() / 500) % 2 == 0;

        d.setTextColor(TFT_WHITE, 0x1082);
        d.setTextDatum(ML_DATUM);
        d.drawString(keyboardBuf, textX, textY);

        // Cursor em bloco laranja no final do texto (visível em fundo escuro)
        if (cursorOn && textLen < 15) {
            int cx = textX + textLen * 6;
            d.fillRect(cx, (KB_INPUT_H - 14) / 2, 6, 14, COLOR_TEXT_ACCENT);
        }
    }

    // Botão cancelar (×)
    d.fillRoundRect(284, 6, 30, 30, 4, 0x4208);
    d.setTextDatum(MC_DATUM);
    d.setTextColor(TFT_WHITE, 0x4208);
    d.drawString("X", 299, 21);

    // Linha 1: QWERTYUIOP
    for (int i = 0; i < 10; i++) {
        int x = KB_ROW1_X + i * (KB_KEY_W + KB_GAP);
        char label[2] = { keyboardShift ? KB_ROW1[i] : (char)tolower(KB_ROW1[i]), 0 };
        drawKbKey(d, x, KB_ROW1_Y, KB_KEY_W, KB_KEY_H, label, 0x2124, TFT_WHITE);
    }
    // Linha 2: ASDFGHJKL
    for (int i = 0; i < 9; i++) {
        int x = KB_ROW2_X + i * (KB_KEY_W + KB_GAP);
        char label[2] = { keyboardShift ? KB_ROW2[i] : (char)tolower(KB_ROW2[i]), 0 };
        drawKbKey(d, x, KB_ROW2_Y, KB_KEY_W, KB_KEY_H, label, 0x2124, TFT_WHITE);
    }
    // Linha 3: ^ (SHIFT) + ZXCVBNM + DEL
    // Layout: 42 + 3 + 7×28 + 6×3 + 3 + 42 = 304px → margem 8px
    static const int KB_SHIFT_W  = KB_KEY_W + 14;  // 42px
    static const int KB_DEL_W    = KB_KEY_W + 14;  // 42px
    static const int KB_ROW3_TW  = KB_SHIFT_W + KB_GAP + 7 * KB_KEY_W + 6 * KB_GAP + KB_GAP + KB_DEL_W;
    static const int KB_ROW3_X   = (320 - KB_ROW3_TW) / 2;
    // Botão Shift: branco quando ativo, cinza escuro quando inativo
    uint16_t shiftBg = keyboardShift ? 0xFFFF : 0x2124;
    uint16_t shiftFg = keyboardShift ? COLOR_BACKGROUND : TFT_WHITE;
    drawKbKey(d, KB_ROW3_X, KB_ROW3_Y, KB_SHIFT_W, KB_KEY_H, "^", shiftBg, shiftFg);
    for (int i = 0; i < 7; i++) {
        int x = KB_ROW3_X + KB_SHIFT_W + KB_GAP + i * (KB_KEY_W + KB_GAP);
        char label[2] = { keyboardShift ? KB_ROW3[i] : (char)tolower(KB_ROW3[i]), 0 };
        drawKbKey(d, x, KB_ROW3_Y, KB_KEY_W, KB_KEY_H, label, 0x2124, TFT_WHITE);
    }
    int delX = KB_ROW3_X + KB_SHIFT_W + KB_GAP + 7 * (KB_KEY_W + KB_GAP);
    drawKbKey(d, delX, KB_ROW3_Y, KB_DEL_W, KB_KEY_H, "<", 0x3186, TFT_WHITE);

    // Linha 4: ESPAÇO + OK
    static const int KB_SPACE_W = 180;
    static const int KB_OK_W    = 80;
    static const int KB_ROW4_TW = KB_SPACE_W + KB_GAP + KB_OK_W;
    static const int KB_ROW4_X  = (320 - KB_ROW4_TW) / 2;
    drawKbKey(d, KB_ROW4_X, KB_ROW4_Y, KB_SPACE_W, KB_KEY_H, "ESPACO", 0x2124, TFT_WHITE);
    drawKbKey(d, KB_ROW4_X + KB_SPACE_W + KB_GAP, KB_ROW4_Y, KB_OK_W, KB_KEY_H, "OK", 0x0460, COLOR_TIMER_RUNNING);
}

bool screenHomeIsKeyboardActive() { return keyboardActive; }

void screenHomeOpenKeyboard(int slot) {
    keyboardSlot  = slot;
    keyboardShift = true;  // auto-shift: primeira letra em maiúscula
    strlcpy(keyboardBuf, screenHomeGetTimerLabel(slot), sizeof(keyboardBuf));
    keyboardActive = true;
    LOG_I("timer", "Teclado aberto para T%d", slot + 1);
}

void screenHomeKeyboardHandleTouch(int x, int y) {
    if (!keyboardActive) return;

    // Botão cancelar
    if (x >= 284 && x <= 314 && y >= 6 && y <= 36) {
        keyboardActive = false;
        M5.Speaker.tone(300, 30);
        return;
    }
    // Linha 1
    if (y >= KB_ROW1_Y && y < KB_ROW1_Y + KB_KEY_H) {
        for (int i = 0; i < 10; i++) {
            int kx = KB_ROW1_X + i * (KB_KEY_W + KB_GAP);
            if (x >= kx && x < kx + KB_KEY_W) {
                char ch[2] = { keyboardShift ? KB_ROW1[i] : (char)tolower(KB_ROW1[i]), 0 };
                if (strlen(keyboardBuf) < 15) strncat(keyboardBuf, ch, 1);
                keyboardShift = false;  // auto-revert: letras seguintes em minúscula
                M5.Speaker.tone(880, 12); return;
            }
        }
    }
    // Linha 2
    if (y >= KB_ROW2_Y && y < KB_ROW2_Y + KB_KEY_H) {
        for (int i = 0; i < 9; i++) {
            int kx = KB_ROW2_X + i * (KB_KEY_W + KB_GAP);
            if (x >= kx && x < kx + KB_KEY_W) {
                char ch[2] = { keyboardShift ? KB_ROW2[i] : (char)tolower(KB_ROW2[i]), 0 };
                if (strlen(keyboardBuf) < 15) strncat(keyboardBuf, ch, 1);
                keyboardShift = false;
                M5.Speaker.tone(880, 12); return;
            }
        }
    }
    // Linha 3: ^ (SHIFT) + ZXCVBNM + DEL
    if (y >= KB_ROW3_Y && y < KB_ROW3_Y + KB_KEY_H) {
        const int shiftW   = KB_KEY_W + 14;  // 42px
        const int delW     = KB_KEY_W + 14;  // 42px
        const int r3tw     = shiftW + KB_GAP + 7 * KB_KEY_W + 6 * KB_GAP + KB_GAP + delW;
        const int r3x      = (320 - r3tw) / 2;
        const int lettersX = r3x + shiftW + KB_GAP;
        const int delX     = lettersX + 7 * (KB_KEY_W + KB_GAP);
        // Botão Shift
        if (x >= r3x && x < r3x + shiftW) {
            keyboardShift = !keyboardShift;
            M5.Speaker.tone(660, 15); return;
        }
        // DEL
        if (x >= delX && x < delX + delW) {
            int len = strlen(keyboardBuf);
            if (len > 0) keyboardBuf[len - 1] = '\0';
            M5.Speaker.tone(400, 20); return;
        }
        // Letras
        for (int i = 0; i < 7; i++) {
            int kx = lettersX + i * (KB_KEY_W + KB_GAP);
            if (x >= kx && x < kx + KB_KEY_W) {
                char ch[2] = { keyboardShift ? KB_ROW3[i] : (char)tolower(KB_ROW3[i]), 0 };
                if (strlen(keyboardBuf) < 15) strncat(keyboardBuf, ch, 1);
                keyboardShift = false;
                M5.Speaker.tone(880, 12); return;
            }
        }
    }
    // Linha 4
    if (y >= KB_ROW4_Y && y < KB_ROW4_Y + KB_KEY_H) {
        const int spaceW = 180, okW = 80;
        const int r4tw   = spaceW + KB_GAP + okW;
        const int r4x    = (320 - r4tw) / 2;
        const int okX    = r4x + spaceW + KB_GAP;
        if (x >= r4x && x < r4x + spaceW) {
            size_t klen = strlen(keyboardBuf);
            if (klen < 15) { keyboardBuf[klen] = ' '; keyboardBuf[klen + 1] = '\0'; }
            M5.Speaker.tone(880, 12); return;
        }
        if (x >= okX && x < okX + okW) {
            strlcpy(timerCustomName[keyboardSlot], keyboardBuf, sizeof(timerCustomName[0]));
            timerHasCustomName[keyboardSlot] = (strlen(keyboardBuf) > 0);
            keyboardActive = false;
            LOG_I("timer", "T%d renomeado para \"%s\"", keyboardSlot + 1, keyboardBuf);
            M5.Speaker.tone(880, 60);
            return;
        }
    }
}

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

static void appendEllipsisToHeader(lgfx::LovyanGFX& d, char* text, size_t size, int maxWidth) {
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

// ── Slot tabs (T1 T2 T3 SW) com barra de progresso ──────────────────────────
static void drawSlotTabs(lgfx::LovyanGFX& d, int y) {
    d.setFont(&fonts::Font0);
    const int TOTAL = MAX_TIMERS + 1;  // 3 timers + cronômetro
    const int tabW  = 42, tabH = 20, gap = 6;
    int startX = d.width() / 2 - (TOTAL * tabW + (TOTAL - 1) * gap) / 2;

    for (int i = 0; i < TOTAL; i++) {
        int tx    = startX + i * (tabW + gap);
        bool focused = (i == focusedSlot);
        bool isSW    = (i == MAX_TIMERS);
        bool active  = !isSW && (timerStates[i] == TIMER_RUNNING || timerStates[i] == TIMER_PAUSED);
        bool done    = !isSW && (timerStates[i] == TIMER_DONE);
        bool swRun   =  isSW && (g_swState == SW_RUNNING || g_swState == SW_PAUSED);

        uint16_t bg = focused ? COLOR_TEXT_DIM : COLOR_BACKGROUND;
        uint16_t fg = done ? TFT_RED : ((active || swRun) ? COLOR_TIMER_RUNNING : COLOR_TEXT_SUBTLE);

        d.fillRoundRect(tx, y, tabW, tabH, 4, bg);
        d.drawRoundRect(tx, y, tabW, tabH, 4, fg);

        char label[4];
        if (isSW) snprintf(label, sizeof(label), "SW");
        else      snprintf(label, sizeof(label), "T%d", i + 1);

        int textY = (active || done || swRun) ? y + tabH / 2 - 2 : y + tabH / 2;
        d.setTextColor(fg, bg);
        d.setTextDatum(MC_DATUM);
        d.drawString(label, tx + tabW / 2, textY);

        // Barra de progresso na base (apenas timers regressivos)
        if (active || done) {
            uint32_t total   = (uint32_t)timerMinutes[i] * 60000;
            uint32_t rem     = getRemaining(i);
            float    prog    = done ? 1.0f : (total > 0 ? (float)(total - rem) / (float)total : 0.0f);
            int      barMaxW = tabW - 4;
            int      barW    = (int)(barMaxW * prog);
            uint16_t barBg   = focused ? 0x4208 : 0x2104;
            uint16_t barFg   = done ? TFT_RED : COLOR_TIMER_RUNNING;
            d.fillRect(tx + 2, y + tabH - 4, barMaxW, 3, barBg);
            if (barW > 0) d.fillRect(tx + 2, y + tabH - 4, barW, 3, barFg);
        }
    }
}

// ── Desenho da tela ──────────────────────────────────────────────────────────
void screenHomeDraw(lgfx::LovyanGFX& display, bool syncing, bool isDim) {
    // Teclado on-screen tem prioridade sobre o resto
    if (keyboardActive) {
        drawKeyboardOverlay(display);
        return;
    }

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
        char headerEvent[48];
        strlcpy(headerEvent, nextEventText, sizeof(headerEvent));
        display.setFont(&fonts::Font0);
        display.setTextColor(COLOR_TEXT_ACCENT, COLOR_BACKGROUND);
        display.setTextDatum(TL_DATUM);
        appendEllipsisToHeader(display, headerEvent, sizeof(headerEvent), display.width() - 12);
        display.drawString(headerEvent, 6, 25);
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

        // Slot tabs — altura 20 px, fáceis de tocar
        drawSlotTabs(display, TIMER_ZONE_Y + 5);

        const int timerY = TIMER_VALUE_Y;
        const int iconX  = display.width() / 2 - 54;  // 106
        const int textX  = display.width() / 2 + 44;  // 204

        int s = focusedSlot;

        if (s == MAX_TIMERS) {
            // ── Cronômetro ──────────────────────────────────────────────────
            uint32_t elapsed = swGetElapsed();
            uint16_t swColor = (g_swState == SW_RUNNING) ? COLOR_TIMER_RUNNING
                             : (g_swState == SW_PAUSED)  ? COLOR_TIMER_PAUSED
                             :                             COLOR_TIMER_SETTING;

            display.setFont(&fonts::Font0);
            display.setTextColor(swColor, COLOR_BACKGROUND);
            display.setTextDatum(MC_DATUM);
            display.drawString("SW · Cronometro", display.width() / 2, TIMER_LABEL_Y);

            char swBuf[16];
            uint32_t h  = elapsed / 3600000;
            uint32_t m  = (elapsed / 60000) % 60;
            uint32_t ss = (elapsed / 1000) % 60;
            uint32_t cs = (elapsed / 10) % 100;  // centésimos (mostrado pausado)

            if (g_swState == SW_RUNNING) {
                // Rodando: mostra HH:MM:SS ou MM:SS limpo
                if (h > 0)
                    snprintf(swBuf, sizeof(swBuf), "%02lu:%02lu:%02lu",
                             (unsigned long)h, (unsigned long)m, (unsigned long)ss);
                else
                    snprintf(swBuf, sizeof(swBuf), "%02lu:%02lu",
                             (unsigned long)m, (unsigned long)ss);
            } else {
                // Parado/pausado: mostra MM:SS.cc para precisão
                if (h > 0)
                    snprintf(swBuf, sizeof(swBuf), "%lu:%02lu:%02lu",
                             (unsigned long)h, (unsigned long)m, (unsigned long)ss);
                else
                    snprintf(swBuf, sizeof(swBuf), "%02lu:%02lu.%02lu",
                             (unsigned long)m, (unsigned long)ss, (unsigned long)cs);
            }

            display.setFont(&fonts::FreeSansBold18pt7b);
            display.setTextColor(swColor, COLOR_BACKGROUND);
            display.setTextDatum(MC_DATUM);
            display.drawString(swBuf, textX, timerY);

            if (g_swState == SW_RUNNING)
                drawPauseIcon(display, iconX, timerY, swColor);
            else
                drawPlayIcon(display, iconX, timerY, swColor);

            display.setFont(&fonts::FreeSans9pt7b);
            display.setTextColor(COLOR_TEXT_SUBTLE, COLOR_BACKGROUND);
            display.setTextDatum(MC_DATUM);
            if (g_swState == SW_IDLE)
                display.drawString("Segurar: iniciar", display.width() / 2, TIMER_HINT_Y);
            else
                display.drawString("Segurar: zerar", display.width() / 2, TIMER_HINT_Y);

        } else {
            // ── Timer regressivo ─────────────────────────────────────────────
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
                snprintf(tBuf, sizeof(tBuf), "%02lu:%02lu",
                         (unsigned long)mins, (unsigned long)secs);
            }

            // Anel de progresso ao redor do ícone (RUNNING ou PAUSED)
            if (timerStates[s] == TIMER_RUNNING || timerStates[s] == TIMER_PAUSED) {
                uint32_t total = (uint32_t)timerMinutes[s] * 60000;
                float    prog  = (total > 0) ? (float)(total - remaining) / (float)total : 0.0f;
                // Fundo do anel (cinza escuro, círculo completo)
                display.fillArc(iconX, timerY, 20, 26, 0.0f, 360.0f, 0x2104);
                // Arco de progresso (de 0° = topo, sentido horário)
                if (prog > 0.005f)
                    display.fillArc(iconX, timerY, 20, 26, 0.0f, prog * 360.0f, timerColor);
            }

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
                drawUpArrow(display,   22,                    timerY - 12, COLOR_TEXT_SUBTLE);
                drawDownArrow(display, 22,                    timerY + 12, COLOR_TEXT_SUBTLE);
                drawUpArrow(display,   display.width() - 22,  timerY - 12, COLOR_TEXT_SUBTLE);
                drawDownArrow(display, display.width() - 22,  timerY + 12, COLOR_TEXT_SUBTLE);
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
    }

    // --- Aviso de bateria + indicador de tela ---
    drawBatteryWarning(display, 218);
    drawScreenIndicator(display, 0, SCREEN_COUNT);
}

// ── Modo ambiente em dim — relógio analógico minimalista ─────────────────────
void screenHomeDrawAmbient(lgfx::LovyanGFX& display) {
    display.fillScreen(COLOR_BACKGROUND);

    struct tm t = {};
    time_t now = time(nullptr);
    bool timeOk = (now > 1577836800L) && (localtime_r(&now, &t) != nullptr);

    const int cx = 160;
    const int cy = 108;
    const int R  = 82;   // raio do mostrador

    // Contorno do mostrador
    display.drawCircle(cx, cy, R,     COLOR_TEXT_SUBTLE);
    display.drawCircle(cx, cy, R - 1, COLOR_DIVIDER);

    // Marcas das horas (4 maiores nos quartos, 8 menores)
    for (int i = 0; i < 12; i++) {
        float a   = (i * 30.0f - 90.0f) * (float)M_PI / 180.0f;
        float cs  = cosf(a), sn = sinf(a);
        int   len = (i % 3 == 0) ? 10 : 5;
        display.drawLine(
            cx + (int)((R - len) * cs), cy + (int)((R - len) * sn),
            cx + (int)(R * cs),         cy + (int)(R * sn),
            (i % 3 == 0) ? COLOR_TEXT_DIM : COLOR_TEXT_SUBTLE
        );
    }

    if (timeOk) {
        int h = t.tm_hour, m = t.tm_min;

        // Ponteiro dos minutos (fino, 2px, comprimento 68)
        float mA = (m * 6.0f - 90.0f) * (float)M_PI / 180.0f;
        int   mx = cx + (int)(68.0f * cosf(mA));
        int   my = cy + (int)(68.0f * sinf(mA));
        display.drawLine(cx,     cy,     mx,     my,     COLOR_TEXT_OFFWHITE);
        display.drawLine(cx + 1, cy,     mx + 1, my,     COLOR_TEXT_OFFWHITE);

        // Ponteiro das horas (espesso, 3px, comprimento 44, laranja)
        float hA = ((h % 12) * 30.0f + m * 0.5f - 90.0f) * (float)M_PI / 180.0f;
        int   hx = cx + (int)(44.0f * cosf(hA));
        int   hy = cy + (int)(44.0f * sinf(hA));
        for (int d = -1; d <= 1; d++) {
            display.drawLine(cx + d, cy,     hx + d, hy,     COLOR_TEXT_ACCENT);
            display.drawLine(cx,     cy + d, hx,     hy + d, COLOR_TEXT_ACCENT);
        }
    }

    // Tampa central
    display.fillCircle(cx, cy, 4, COLOR_TEXT_DIM);

    // Hora digital discreta abaixo do mostrador
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(COLOR_TEXT_SUBTLE, COLOR_BACKGROUND);
    display.setTextDatum(MC_DATUM);
    if (timeOk) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        display.drawString(buf, cx, cy + R + 14);
    } else {
        display.drawString("--:--", cx, cy + R + 14);
    }

    drawScreenIndicator(display, 0, SCREEN_COUNT);
}
