#include "alarm_manager.h"
#include "theme.h"
#include "logger.h"
#include <time.h>

// ── Estado interno ─────────────────────────────────────────────────────────────
enum AlarmState { AL_IDLE, AL_RINGING, AL_SNOOZED };
static AlarmState g_alarmState    = AL_IDLE;
static time_t     g_snoozeUntil   = 0;
static int        g_triggeredYday = -1;

// ── Estado do time-picker ──────────────────────────────────────────────────────
static bool g_pickerOpen = false;
static int  g_pickerH    = 7;
static int  g_pickerM    = 0;

// ── Constantes de layout ───────────────────────────────────────────────────────
static const int HX        = 100;  // centro coluna hora
static const int MX        = 220;  // centro coluna minuto
static const int ARR_UP_Y  =  78;  // base da seta para cima
static const int ARR_DN_Y  = 152;  // ponta da seta para baixo
static const int ARR_W     =  22;
static const int ARR_H     =  15;
static const int NUM_Y     = 115;  // centro dos dígitos

// ── Lógica principal ───────────────────────────────────────────────────────────
void alarmCheck(const RuntimeConfig& cfg) {
    if (!cfg.alarmEnabled) {
        g_alarmState = AL_IDLE;
        return;
    }
    if (g_alarmState == AL_RINGING) return;

    if (g_alarmState == AL_SNOOZED) {
        if (time(nullptr) >= g_snoozeUntil) {
            g_alarmState = AL_RINGING;
            LOG_I("alarm", "Snooze expirado — alarme tocando novamente");
        }
        return;
    }

    struct tm t;
    if (!getLocalTime(&t, 0)) return;
    if (t.tm_hour == cfg.alarmHour && t.tm_min == cfg.alarmMinute) {
        if (t.tm_yday != g_triggeredYday) {
            g_triggeredYday = t.tm_yday;
            g_alarmState    = AL_RINGING;
            LOG_I("alarm", "Alarme disparado — %02d:%02d", cfg.alarmHour, cfg.alarmMinute);
        }
    }
}

bool alarmIsRinging() { return g_alarmState == AL_RINGING; }
bool alarmIsSnoozed() { return g_alarmState == AL_SNOOZED; }

void alarmDismiss() {
    g_alarmState = AL_IDLE;
    LOG_I("alarm", "Alarme dispensado");
}

void alarmSnooze() {
    g_snoozeUntil = time(nullptr) + 300;  // +5 min
    g_alarmState  = AL_SNOOZED;
    LOG_I("alarm", "Soneca ativada — retoma em 5 min");
}

// ── Persistência ───────────────────────────────────────────────────────────────
void alarmGetPersist(AlarmPersist& out) {
    out.state         = (int)g_alarmState;
    out.triggeredYday = g_triggeredYday;
    out.snoozeUntil   = (int64_t)g_snoozeUntil;
}

void alarmSetPersist(const AlarmPersist& in) {
    g_alarmState    = (AlarmState)constrain(in.state, 0, 2);
    g_triggeredYday = in.triggeredYday;
    g_snoozeUntil   = (time_t)in.snoozeUntil;
}

int64_t alarmSecondsUntilNext(const RuntimeConfig& cfg) {
    if (!cfg.alarmEnabled) return -1;

    struct tm t;
    if (!getLocalTime(&t, 0)) return -1;

    struct tm alarmTm  = t;
    alarmTm.tm_hour    = cfg.alarmHour;
    alarmTm.tm_min     = cfg.alarmMinute;
    alarmTm.tm_sec     = 0;
    time_t alarmTs     = mktime(&alarmTm);
    time_t nowTs       = time(nullptr);

    // Se já passou hoje ou já disparou hoje, agenda para amanhã
    if (alarmTs <= nowTs || t.tm_yday == g_triggeredYday) {
        alarmTs += 86400;
    }

    int64_t secs = (int64_t)(alarmTs - nowTs);
    return (secs > 0) ? secs : -1;
}

// ── Time-picker ────────────────────────────────────────────────────────────────
void alarmPickerOpen(int h, int m) {
    g_pickerH    = h;
    g_pickerM    = m;
    g_pickerOpen = true;
}

void alarmPickerClose()  { g_pickerOpen = false; }
bool alarmPickerIsOpen() { return g_pickerOpen; }

void alarmPickerDraw(lgfx::LovyanGFX& d) {
    d.fillScreen(COLOR_BACKGROUND);

    d.setFont(&fonts::FreeSansBold18pt7b);
    d.setTextColor(COLOR_TEXT_ACCENT, COLOR_BACKGROUND);
    d.setTextDatum(MC_DATUM);
    d.drawString("Definir Alarme", 160, 28);

    d.drawFastHLine(20, 52, 280, COLOR_DIVIDER);

    // Setas para cima
    d.fillTriangle(HX,            ARR_UP_Y - ARR_H,
                   HX - ARR_W/2, ARR_UP_Y,
                   HX + ARR_W/2, ARR_UP_Y, COLOR_TEXT_DIM);
    d.fillTriangle(MX,            ARR_UP_Y - ARR_H,
                   MX - ARR_W/2, ARR_UP_Y,
                   MX + ARR_W/2, ARR_UP_Y, COLOR_TEXT_DIM);

    // Dígitos grandes
    char hBuf[4], mBuf[4];
    snprintf(hBuf, sizeof(hBuf), "%02d", g_pickerH);
    snprintf(mBuf, sizeof(mBuf), "%02d", g_pickerM);

    d.setFont(&fonts::FreeSansBold18pt7b);
    d.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BACKGROUND);
    d.setTextDatum(MC_DATUM);
    d.drawString(hBuf, HX, NUM_Y);
    d.drawString(mBuf, MX, NUM_Y);

    d.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    d.drawString(":", 160, NUM_Y);

    // Setas para baixo
    d.fillTriangle(HX - ARR_W/2, ARR_DN_Y - ARR_H,
                   HX + ARR_W/2, ARR_DN_Y - ARR_H,
                   HX,           ARR_DN_Y, COLOR_TEXT_DIM);
    d.fillTriangle(MX - ARR_W/2, ARR_DN_Y - ARR_H,
                   MX + ARR_W/2, ARR_DN_Y - ARR_H,
                   MX,           ARR_DN_Y, COLOR_TEXT_DIM);

    // Botão CANCELAR
    d.fillRoundRect(20, 190, 120, 38, 8, 0x2945);
    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(COLOR_TEXT_DIM, 0x2945);
    d.setTextDatum(MC_DATUM);
    d.drawString("CANCELAR", 80, 209);

    // Botão OK
    d.fillRoundRect(180, 190, 120, 38, 8, COLOR_TEXT_ACCENT);
    d.setTextColor(COLOR_BACKGROUND, COLOR_TEXT_ACCENT);
    d.drawString("OK", 240, 209);

    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_SUBTLE, COLOR_BACKGROUND);
    d.drawString("Toque nos numeros ou setas para ajustar", 160, 235);
}

bool alarmPickerHandleTap(int x, int y, int& outHour, int& outMin) {
    // Zona hora (x=75–125)
    bool onHour = (x >= 75 && x <= 125);
    // Zona minuto (x=195–245)
    bool onMin  = (x >= 195 && x <= 245);

    if (onHour || onMin) {
        int& target = onHour ? g_pickerH : g_pickerM;
        int  mod    = onHour ? 24 : 60;

        if (y >= 54 && y <= 92) {         // seta para cima + arredores
            target = (target + 1) % mod;
            return false;
        }
        if (y >= 92 && y <= 134) {        // dígito (também incrementa)
            target = (target + 1) % mod;
            return false;
        }
        if (y >= 134 && y <= 168) {       // seta para baixo
            target = (target + mod - 1) % mod;
            return false;
        }
    }

    // Botão CANCELAR
    if (x >= 20 && x <= 140 && y >= 190 && y <= 228) {
        g_pickerOpen = false;
        return false;
    }
    // Botão OK
    if (x >= 180 && x <= 300 && y >= 190 && y <= 228) {
        outHour      = g_pickerH;
        outMin       = g_pickerM;
        g_pickerOpen = false;
        return true;
    }
    return false;
}

// ── Overlay de alarme tocando ──────────────────────────────────────────────────
void alarmDrawRingingOverlay(lgfx::LovyanGFX& d) {
    d.fillScreen(COLOR_BACKGROUND);

    bool blink = ((millis() / 400) % 2 == 0);
    uint16_t titleColor = blink ? COLOR_TEXT_ACCENT : COLOR_TEXT_PRIMARY;

    d.setFont(&fonts::FreeSansBold18pt7b);
    d.setTextDatum(MC_DATUM);
    d.setTextColor(titleColor, COLOR_BACKGROUND);
    d.drawString("ALARME", 160, 58);

    struct tm t;
    char timeBuf[8] = "--:--";
    if (getLocalTime(&t, 0))
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", t.tm_hour, t.tm_min);

    d.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BACKGROUND);
    d.drawString(timeBuf, 160, 108);

    // Botão SONECA (+5 min) — lado esquerdo
    d.fillRoundRect(10, 158, 140, 48, 10, 0x2945);
    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(COLOR_TEXT_PRIMARY, 0x2945);
    d.setTextDatum(MC_DATUM);
    d.drawString("SONECA", 80, 174);
    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_DIM, 0x2945);
    d.drawString("+5 min", 80, 192);

    // Botão DISPENSAR — lado direito
    d.fillRoundRect(170, 158, 140, 48, 10, 0xA000);
    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(COLOR_TEXT_PRIMARY, 0xA000);
    d.setTextDatum(MC_DATUM);
    d.drawString("DISPENSAR", 240, 182);
}
