#include "screen_home.h"
#include "battery_ui.h"
#include "config.h"
#include <time.h>

static const char* DIAS[]  = { "dom", "seg", "ter", "qua", "qui", "sex", "sab" };
static const char* MESES[] = { "jan", "fev", "mar", "abr", "mai", "jun",
                                "jul", "ago", "set", "out", "nov", "dez" };

// ── Presets do timer ─────────────────────────────────────────────────────────
static const uint32_t PRESETS_MS[]    = { 60000, 3*60000, 5*60000, 10*60000,
                                          15*60000, 20*60000, 30*60000 };
static const char*    PRESET_LABELS[] = { "1", "3", "5", "10", "15", "20", "30" };
static const int      NUM_PRESETS     = 7;

// ── Estado do timer ──────────────────────────────────────────────────────────
enum TimerState { TIMER_SETTING, TIMER_RUNNING, TIMER_PAUSED, TIMER_DONE };

static TimerState timerState    = TIMER_SETTING;
static int        presetIdx     = 2;           // default: 5 min
static uint32_t   timerRemainMs = 5 * 60000;  // tempo restante (snapshot ao pausar)
static uint32_t   timerStartMs  = 0;           // millis() no último início/resume

// Tempo restante considerando o intervalo em curso
static uint32_t getRemaining() {
    if (timerState != TIMER_RUNNING) return timerRemainMs;
    uint32_t elapsed = millis() - timerStartMs;
    return (elapsed >= timerRemainMs) ? 0 : timerRemainMs - elapsed;
}

// ── API de controle ──────────────────────────────────────────────────────────
void screenHomeTimerTap() {
    switch (timerState) {
        case TIMER_SETTING:
            presetIdx     = (presetIdx + 1) % NUM_PRESETS;
            timerRemainMs = PRESETS_MS[presetIdx];
            break;
        case TIMER_RUNNING:
            timerRemainMs = getRemaining();
            timerState    = TIMER_PAUSED;
            break;
        case TIMER_PAUSED:
            timerStartMs = millis();
            timerState   = TIMER_RUNNING;
            break;
        case TIMER_DONE:
            timerState    = TIMER_SETTING;
            timerRemainMs = PRESETS_MS[presetIdx];
            break;
    }
}

void screenHomeTimerLongPress() {
    switch (timerState) {
        case TIMER_SETTING:
            timerStartMs = millis();
            timerState   = TIMER_RUNNING;
            break;
        case TIMER_RUNNING:
        case TIMER_PAUSED:
        case TIMER_DONE:
            timerState    = TIMER_SETTING;
            timerRemainMs = PRESETS_MS[presetIdx];
            break;
    }
}

bool screenHomeIsAlarmActive() {
    return timerState == TIMER_DONE;
}

bool screenHomeInit() {
    timerRemainMs = PRESETS_MS[presetIdx];
    return true;
}

bool screenHomeIsTimerActive() {
    return timerState == TIMER_RUNNING || timerState == TIMER_PAUSED;
}

TimerPersist screenHomeGetTimerPersist() {
    // RUNNING é salvo como PAUSED (não poderia dormir com timer ativo,
    // mas por segurança preserva o tempo restante)
    int s = (timerState == TIMER_PAUSED) ? 2 : 0;
    return { s, presetIdx, getRemaining() };
}

void screenHomeSetTimerPersist(const TimerPersist& p) {
    presetIdx = p.presetIdx;
    if (p.state == 2 && p.remainMs > 0) {  // PAUSED
        timerState    = TIMER_PAUSED;
        timerRemainMs = p.remainMs;
    } else {
        timerState    = TIMER_SETTING;
        timerRemainMs = PRESETS_MS[presetIdx];
    }
}

// ── Primitivas de ícone ──────────────────────────────────────────────────────
static void drawPlayIcon(lgfx::LovyanGFX& display, int cx, int cy, uint16_t color) {
    display.fillTriangle(cx - 6, cy - 9, cx - 6, cy + 9, cx + 9, cy, color);
}

static void drawPauseIcon(lgfx::LovyanGFX& display, int cx, int cy, uint16_t color) {
    display.fillRect(cx - 7, cy - 9, 5, 18, color);
    display.fillRect(cx + 2, cy - 9, 5, 18, color);
}

// ── Desenho da tela ──────────────────────────────────────────────────────────
void screenHomeDraw(lgfx::LovyanGFX& display) {
    display.fillScreen(TFT_BLACK);

    // --- Data + bateria ---
    struct tm timeinfo = {};
    bool timeOk = getLocalTime(&timeinfo, 0);

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(0x8410, TFT_BLACK);
    display.setTextDatum(TL_DATUM);
    if (timeOk) {
        char dateBuf[32];
        snprintf(dateBuf, sizeof(dateBuf), "%s, %02d %s %04d",
                 DIAS[timeinfo.tm_wday], timeinfo.tm_mday,
                 MESES[timeinfo.tm_mon], timeinfo.tm_year + 1900);
        display.drawString(dateBuf, 6, 4);
    } else {
        display.drawString("---, -- --- ----", 6, 4);
    }
    drawBatteryIndicator(display);

    // --- Hora grande ---
    display.setFont(&fonts::FreeSansBold18pt7b);
    display.setTextColor(0xFD20, TFT_BLACK);
    display.setTextDatum(MC_DATUM);
    if (timeOk) {
        char timeBuf[12];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        display.drawString(timeBuf, display.width() / 2, 85);
    } else {
        display.drawString("--:--:--", display.width() / 2, 85);
    }

    // --- Divisor ---
    display.drawFastHLine(10, 118, display.width() - 20, 0x4208);

    // ── Timer ────────────────────────────────────────────────────────────────
    if (timerState == TIMER_DONE) {
        // Alarme: pisca "PRONTO!" em vermelho e branco
        uint16_t flash = ((millis() / 400) % 2 == 0) ? TFT_RED : TFT_WHITE;
        display.setFont(&fonts::FreeSansBold18pt7b);
        display.setTextColor(flash, TFT_BLACK);
        display.setTextDatum(MC_DATUM);
        display.drawString("PRONTO!", display.width() / 2, 165);

        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(0x8410, TFT_BLACK);
        display.drawString("toque para fechar", display.width() / 2, 200);

    } else {
        // Verifica se o timer chegou a zero enquanto rodava
        uint32_t remaining = getRemaining();
        if (timerState == TIMER_RUNNING && remaining == 0) {
            timerState    = TIMER_DONE;
            timerRemainMs = 0;
            // Som é disparado em main.cpp via screenHomeIsAlarmActive()
            return;  // redesenha na próxima iteração como DONE
        }

        uint32_t mins = remaining / 60000;
        uint32_t secs = (remaining / 1000) % 60;

        // Cor indica estado
        uint16_t timerColor;
        if (timerState == TIMER_RUNNING)     timerColor = TFT_GREEN;
        else if (timerState == TIMER_PAUSED) timerColor = TFT_WHITE;
        else                                  timerColor = 0x8410;  // setting: cinza

        // Dígitos do timer
        char tBuf[8];
        snprintf(tBuf, sizeof(tBuf), "%02lu:%02lu", mins, secs);
        display.setFont(&fonts::FreeSansBold18pt7b);
        display.setTextColor(timerColor, TFT_BLACK);
        display.setTextDatum(MC_DATUM);
        display.drawString(tBuf, display.width() / 2 + 14, 152);

        // Ícone play/pause à esquerda dos dígitos
        int iconX = display.width() / 2 - 50;
        int iconY = 152;
        if (timerState == TIMER_RUNNING) {
            drawPauseIcon(display, iconX, iconY, timerColor);
        } else {
            drawPlayIcon(display, iconX, iconY, timerColor);
        }

        // Linha de presets (só no estado SETTING)
        if (timerState == TIMER_SETTING) {
            display.setFont(&fonts::FreeSans9pt7b);

            // Calcula largura total para centralizar
            int totalW = 0;
            for (int i = 0; i < NUM_PRESETS; i++) {
                totalW += display.textWidth(PRESET_LABELS[i]) + 14;
            }
            int px = (display.width() - totalW + 14) / 2;
            int py = 182;

            for (int i = 0; i < NUM_PRESETS; i++) {
                uint16_t col = (i == presetIdx) ? TFT_WHITE : (uint16_t)0x4208;
                display.setTextColor(col, TFT_BLACK);
                display.setTextDatum(TL_DATUM);
                display.drawString(PRESET_LABELS[i], px, py);
                px += display.textWidth(PRESET_LABELS[i]) + 14;
            }

            display.setFont(&fonts::FreeSans9pt7b);
            display.setTextColor(0x4208, TFT_BLACK);
            display.setTextDatum(MC_DATUM);
            display.drawString("Segurar: iniciar", display.width() / 2, 210);

        } else if (timerState == TIMER_PAUSED) {
            display.setFont(&fonts::FreeSans9pt7b);
            display.setTextColor(0x4208, TFT_BLACK);
            display.setTextDatum(MC_DATUM);
            display.drawString("Segurar: zerar", display.width() / 2, 195);
        }
    }

    // --- Aviso de bateria crítica ---
    int pct = batteryPercent();
    if (pct >= 0 && pct <= 5 && (millis() / 1000) % 2 == 0) {
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(TFT_RED, TFT_BLACK);
        display.setTextDatum(MC_DATUM);
        display.drawString("BATERIA BAIXA!", display.width() / 2, 218);
    }

    // --- Indicador de tela ---
    int cx = display.width() / 2;
    int cy = display.height() - 10;
    display.fillCircle(cx - 10, cy, 4, TFT_WHITE);
    display.drawCircle(cx + 10, cy, 4, 0x8410);
}
