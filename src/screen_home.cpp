#include "screen_home.h"
#include "battery_ui.h"
#include "config.h"
#include <time.h>

static const char* DIAS[]  = { "dom", "seg", "ter", "qua", "qui", "sex", "sab" };
static const char* MESES[] = { "jan", "fev", "mar", "abr", "mai", "jun",
                                "jul", "ago", "set", "out", "nov", "dez" };

// ── Estado do timer ──────────────────────────────────────────────────────────
enum TimerState { TIMER_SETTING, TIMER_RUNNING, TIMER_PAUSED, TIMER_DONE };

static TimerState timerState    = TIMER_SETTING;
static int        timerMinutes  = 5;           // 1–99 min, editável por toque
static uint32_t   timerRemainMs = 5 * 60000;  // tempo restante (snapshot ao pausar)
static uint32_t   timerStartMs  = 0;           // millis() no último início/resume

// Tempo restante considerando o intervalo em curso
static uint32_t getRemaining() {
    if (timerState != TIMER_RUNNING) return timerRemainMs;
    uint32_t elapsed = millis() - timerStartMs;
    return (elapsed >= timerRemainMs) ? 0 : timerRemainMs - elapsed;
}

// ── API de controle ──────────────────────────────────────────────────────────
void screenHomeTimerTap(int tapX) {
    switch (timerState) {
        case TIMER_SETTING:
            if (tapX < 160) timerMinutes = max(1,  timerMinutes - 1);
            else             timerMinutes = min(99, timerMinutes + 1);
            timerRemainMs = (uint32_t)timerMinutes * 60000;
            M5.Speaker.tone(800, 20);
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
            timerRemainMs = (uint32_t)timerMinutes * 60000;
            break;
    }
}

void screenHomeTimerLongPress() {
    switch (timerState) {
        case TIMER_SETTING:
            timerStartMs = millis();
            timerState   = TIMER_RUNNING;
            M5.Speaker.tone(440, 60);
            M5.Speaker.tone(660, 100);
            break;
        case TIMER_RUNNING:
        case TIMER_PAUSED:
        case TIMER_DONE:
            timerState    = TIMER_SETTING;
            timerRemainMs = (uint32_t)timerMinutes * 60000;
            break;
    }
}

bool screenHomeIsAlarmActive() {
    return timerState == TIMER_DONE;
}

bool screenHomeInit() {
    timerRemainMs = (uint32_t)timerMinutes * 60000;
    return true;
}

bool screenHomeIsTimerActive() {
    return timerState == TIMER_RUNNING || timerState == TIMER_PAUSED;
}

bool screenHomeIsTimerRunning() {
    return timerState == TIMER_RUNNING;
}

TimerPersist screenHomeGetTimerPersist() {
    // RUNNING é salvo como PAUSED; presetIdx reutilizado para armazenar timerMinutes
    int s = (timerState == TIMER_PAUSED) ? 2 : 0;
    return { s, timerMinutes, getRemaining() };
}

void screenHomeSetTimerPersist(const TimerPersist& p) {
    timerMinutes = (p.presetIdx >= 1 && p.presetIdx <= 99) ? p.presetIdx : 5;
    if (p.state == 2 && p.remainMs > 0) {  // PAUSED
        timerState    = TIMER_PAUSED;
        timerRemainMs = p.remainMs;
    } else {
        timerState    = TIMER_SETTING;
        timerRemainMs = (uint32_t)timerMinutes * 60000;
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

static void drawLeftArrow(lgfx::LovyanGFX& display, int cx, int cy, uint16_t color) {
    display.fillTriangle(cx + 7, cy - 8, cx + 7, cy + 8, cx - 7, cy, color);
}

static void drawRightArrow(lgfx::LovyanGFX& display, int cx, int cy, uint16_t color) {
    display.fillTriangle(cx - 7, cy - 8, cx - 7, cy + 8, cx + 7, cy, color);
}

// ── Desenho da tela ──────────────────────────────────────────────────────────
void screenHomeDraw(lgfx::LovyanGFX& display, bool syncing, bool isDim) {
    display.fillScreen(TFT_BLACK);

    // --- Data + bateria ---
    struct tm timeinfo = {};
    time_t now = time(nullptr);
    bool timeOk = (now > 1577836800L) && (localtime_r(&now, &timeinfo) != nullptr);  // válido se > 2020-01-01

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
    display.setFont(&fonts::FreeSansBold24pt7b);
    display.setTextColor(0xFD20, TFT_BLACK);
    display.setTextDatum(MC_DATUM);
    if (timeOk) {
        char timeBuf[12];
        if (isDim) snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:--",
                            timeinfo.tm_hour, timeinfo.tm_min);
        else       snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
                            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        display.drawString(timeBuf, display.width() / 2, 76);
    } else {
        display.drawString("--:--:--", display.width() / 2, 76);
    }

    // --- Indicador de sincronização NTP ---
    if (syncing) {
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(0x4208, TFT_BLACK);
        display.setTextDatum(MC_DATUM);
        display.drawString("atualizando...", display.width() / 2, 108);
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

        // Cor indica estado
        uint16_t timerColor;
        if (timerState == TIMER_RUNNING)     timerColor = TFT_GREEN;
        else if (timerState == TIMER_PAUSED) timerColor = TFT_WHITE;
        else                                  timerColor = 0x8410;  // setting: cinza

        // Dígitos do timer: "X min" em SETTING, "MM:SS" durante contagem
        char tBuf[12];
        if (timerState == TIMER_SETTING) {
            snprintf(tBuf, sizeof(tBuf), "%d min", timerMinutes);
        } else {
            uint32_t mins = remaining / 60000;
            uint32_t secs = (remaining / 1000) % 60;
            snprintf(tBuf, sizeof(tBuf), "%02lu:%02lu", mins, secs);
        }
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

        if (timerState == TIMER_SETTING) {
            // Setas laterais indicam toque esquerdo/direito para -/+ 1 min
            drawLeftArrow(display,  28, 152, 0x4208);
            drawRightArrow(display, display.width() - 28, 152, 0x4208);
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
