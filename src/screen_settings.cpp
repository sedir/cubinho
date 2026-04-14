#include "screen_settings.h"
#include "theme.h"
#include "config.h"
#include "status_ui.h"
#include "wifi_manager.h"
#include "runtime_config.h"
#include "screen_home.h"
#include <M5Unified.h>

// ── Layout interno ───────────────────────────────────────────────────────────
static const int FOOTER_H   = 18;
static const int CAT_H      = 22;  // altura de label de categoria
static const int ROW_H      = 40;  // altura de cada item
static const int CONTENT_H  = 240 - SETTINGS_HEADER_H - 4 - FOOTER_H;  // 194px

// Tipos de entrada na lista virtual
enum EntryKind { EKIND_CATEGORY, EKIND_TOGGLE, EKIND_CYCLE, EKIND_ACTION };
enum ConfirmAction { CONFIRM_NONE, CONFIRM_WIFI_CHANGE, CONFIRM_RESTART, CONFIRM_FACTORY_RESET };

struct VEntry {
    EntryKind   kind;
    const char* label;
    const char* hint;
};

// Lista virtual completa (ordem fixa)
static const VEntry kEntries[] = {
    { EKIND_CATEGORY, "Conexao",         nullptr                         },  // 0
    { EKIND_TOGGLE,   "WiFi permanente", "Mantem OTA e Telnet ativos"   },  // 1
    { EKIND_CYCLE,    "Intervalo clima", "Busca novos dados automaticamente" },  // 2
    { EKIND_ACTION,   "Alterar WiFi",    "Apaga credenciais e abre portal" },  // 3
    { EKIND_CATEGORY, "Display",         nullptr                         },  // 4
    { EKIND_CYCLE,    "Brilho ativo",    "Brilho usado em operacao normal" },  // 5
    { EKIND_CYCLE,    "Tempo p/ dim",    "Inatividade ate reduzir brilho" },  // 6
    { EKIND_TOGGLE,   "Auto-brilho",     "Ajusta pela luz ambiente"      },  // 7
    { EKIND_CATEGORY, "Timers",          nullptr                         },  // 8
    { EKIND_CYCLE,    "Nome T1",         "Nome mostrado na tela inicial" },  // 9
    { EKIND_CYCLE,    "Nome T2",         "Nome mostrado na tela inicial" },  // 10
    { EKIND_CYCLE,    "Nome T3",         "Nome mostrado na tela inicial" },  // 11
    { EKIND_CATEGORY, "Energia",         nullptr                         },  // 12
    { EKIND_CYCLE,    "Deep sleep",      "Tempo ate suspender a tela"    },  // 13
    { EKIND_TOGGLE,   "Acelerometro",    "Acorda do dim ao mover"        },  // 14
    { EKIND_CATEGORY, "Sistema",         nullptr                         },  // 15
    { EKIND_ACTION,   "Reiniciar",       "Reinicia o aparelho agora"     },  // 16
    { EKIND_ACTION,   "Reset de fabrica","Apaga WiFi e configuracoes"    },  // 17
};
static const int kEntryCount = (int)(sizeof(kEntries) / sizeof(kEntries[0]));

// Índices dos itens de configuração (não-categoria)
enum ItemIdx {
    IDX_KEEP_ALIVE    = 1,
    IDX_WEATHER_INT   = 2,
    IDX_WIFI_CHANGE   = 3,
    IDX_BRIGHTNESS    = 5,
    IDX_DIM_TIMEOUT   = 6,
    IDX_AUTO_BRIGHT   = 7,
    IDX_TIMER_LABEL_1 = 9,
    IDX_TIMER_LABEL_2 = 10,
    IDX_TIMER_LABEL_3 = 11,
    IDX_DEEP_SLEEP    = 13,
    IDX_ACCEL_WAKE    = 14,
    IDX_RESTART       = 16,
    IDX_FACTORY_RESET = 17,
};

// Opções de ciclo
static const int kWeatherOpts[]  = { 15, 30, 60, 120 };  // minutos
static const int kBrightOpts[]   = { 80, 100, 150, 200, 255 };
static const int kDimOpts[]      = { 15, 30, 60, 120, 300 };  // segundos
static const int kSleepOpts[]    = { 5, 10, 30, 60, 0 };      // minutos (0=Nunca)
static const int kWeatherCnt  = 4;
static const int kBrightCnt   = 5;
static const int kDimCnt      = 5;
static const int kSleepCnt    = 5;
static ConfirmAction g_confirmAction = CONFIRM_NONE;

// ── Altura virtual de cada entrada ──────────────────────────────────────────
static int entryHeight(int idx) {
    return (kEntries[idx].kind == EKIND_CATEGORY) ? CAT_H : ROW_H;
}

// Calcula y virtual (acumulado) da entrada idx
static int entryVirtualY(int idx) {
    int y = 0;
    for (int i = 0; i < idx; i++) y += entryHeight(i);
    return y;
}

// Altura virtual total da lista
static int virtualTotalHeight() {
    return entryVirtualY(kEntryCount);
}

int screenSettingsMaxScroll() {
    int total = virtualTotalHeight();
    int avail = CONTENT_H;
    return max(0, total - avail);
}

// ── Helpers de valor ─────────────────────────────────────────────────────────
static int findClosestIdx(const int* opts, int count, int val) {
    int best = 0;
    int bestDiff = abs(opts[0] - val);
    for (int i = 1; i < count; i++) {
        int d = abs(opts[i] - val);
        if (d < bestDiff) { bestDiff = d; best = i; }
    }
    return best;
}

static String valueLabel(int entryIdx, const RuntimeConfig& cfg) {
    switch (entryIdx) {
        case IDX_KEEP_ALIVE:  return cfg.wifiKeepAlive  ? "ON" : "OFF";
        case IDX_AUTO_BRIGHT: return cfg.autoBrightness ? "ON" : "OFF";
        case IDX_ACCEL_WAKE:  return cfg.accelWake      ? "ON" : "OFF";
        case IDX_TIMER_LABEL_1:
        case IDX_TIMER_LABEL_2:
        case IDX_TIMER_LABEL_3:
            return screenHomeGetTimerLabelPresetName(cfg.timerLabelPreset[entryIdx - IDX_TIMER_LABEL_1]);
        case IDX_WEATHER_INT: {
            int v = cfg.weatherIntervalMin;
            if (v < 60) { char buf[8]; snprintf(buf, sizeof(buf), "%dmin", v); return buf; }
            char buf[8]; snprintf(buf, sizeof(buf), "%dh", v / 60); return buf;
        }
        case IDX_BRIGHTNESS: {
            char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg.brightnessActive); return buf;
        }
        case IDX_DIM_TIMEOUT: {
            int v = cfg.dimTimeoutSec;
            if (v < 60) { char buf[8]; snprintf(buf, sizeof(buf), "%ds", v); return buf; }
            char buf[8]; snprintf(buf, sizeof(buf), "%dmin", v / 60); return buf;
        }
        case IDX_DEEP_SLEEP: {
            if (cfg.deepSleepTimeoutMin == 0) return "Nunca";
            char buf[10]; snprintf(buf, sizeof(buf), "%dmin", cfg.deepSleepTimeoutMin); return buf;
        }
        default: return "";
    }
}

static const char* confirmTitle() {
    switch (g_confirmAction) {
        case CONFIRM_WIFI_CHANGE:   return "Alterar WiFi?";
        case CONFIRM_RESTART:       return "Reiniciar?";
        case CONFIRM_FACTORY_RESET: return "Reset de fabrica?";
        default:                    return "";
    }
}

static const char* confirmBody() {
    switch (g_confirmAction) {
        case CONFIRM_WIFI_CHANGE:   return "Limpa as credenciais e abre o portal no proximo boot.";
        case CONFIRM_RESTART:       return "Reinicia o aparelho imediatamente.";
        case CONFIRM_FACTORY_RESET: return "Apaga WiFi e configuracoes salvas.";
        default:                    return "";
    }
}

// ── Desenho de um item ───────────────────────────────────────────────────────
static void drawEntry(lgfx::LovyanGFX& d, int entryIdx, int screenY,
                      const RuntimeConfig& cfg, bool highlighted) {
    const VEntry& e = kEntries[entryIdx];
    int h = entryHeight(entryIdx);

    if (e.kind == EKIND_CATEGORY) {
        // Label de categoria em laranja, sem fundo especial
        d.setFont(&fonts::FreeSans9pt7b);
        d.setTextColor(COLOR_TEXT_ACCENT, COLOR_BACKGROUND);
        d.setTextDatum(ML_DATUM);
        d.drawString(e.label, 8, screenY + h / 2);
        // Linha divisória abaixo do label
        d.drawFastHLine(8, screenY + h - 1, 304, COLOR_DIVIDER);
        return;
    }

    // Fundo de highlight (long press feedback ou seleção visual)
    if (highlighted) {
        d.fillRect(0, screenY, 320, h, 0x2104);  // cinza muito escuro
    }

    // Linha superior sutil
    d.drawFastHLine(8, screenY, 304, 0x18C3);

    // Label
    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BACKGROUND);
    d.setTextDatum(ML_DATUM);
    d.drawString(e.label, 12, screenY + 13);

    if (e.hint) {
        d.setFont(&fonts::Font0);
        d.setTextColor(COLOR_TEXT_SUBTLE, COLOR_BACKGROUND);
        d.drawString(e.hint, 12, screenY + 29);
    }

    // Valor / badge à direita
    if (e.kind == EKIND_ACTION) {
        // Indicador "pressione" — seta para a direita
        d.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
        d.setTextDatum(MR_DATUM);
        d.drawString("segurar >", 312, screenY + 20);
    } else {
        // Toggle ou ciclo: badge com valor
        String val = valueLabel(entryIdx, cfg);
        bool   isOn = (val == "ON");
        bool   isToggle = (e.kind == EKIND_TOGGLE);

        uint16_t badgeBg  = isToggle
                          ? (isOn ? 0x0580 : 0x4208)   // verde escuro / cinza
                          : 0x2945;                      // azul escuro para ciclo
        uint16_t badgeFg  = isToggle
                          ? (isOn ? COLOR_TIMER_RUNNING : COLOR_TEXT_DIM)
                          : COLOR_TEXT_PRIMARY;

        int bw = val.length() * 7 + 14;
        int bx = 316 - bw;
        int by = screenY + 11;

        d.fillRoundRect(bx, by, bw, 18, 4, badgeBg);
        d.setFont(&fonts::Font0);
        d.setTextColor(badgeFg, badgeBg);
        d.setTextDatum(MC_DATUM);
        d.drawString(val.c_str(), bx + bw / 2, by + 9);
    }
}

static void drawConfirmModal(lgfx::LovyanGFX& d) {
    if (g_confirmAction == CONFIRM_NONE) return;

    const int w = 268, h = 120;
    const int x = (d.width() - w) / 2;
    const int y = (d.height() - h) / 2 + 4;

    d.fillRect(0, 0, d.width(), d.height(), 0x0841);
    d.fillRoundRect(x, y, w, h, 8, 0x18C3);
    d.drawRoundRect(x, y, w, h, 8, COLOR_TEXT_DIM);

    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(COLOR_TEXT_PRIMARY, 0x18C3);
    d.setTextDatum(MC_DATUM);
    d.drawString(confirmTitle(), x + w / 2, y + 20);

    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_DIM, 0x18C3);
    d.drawString(confirmBody(), x + w / 2, y + 48);

    const int btnY = y + 78;
    const int btnW = 96;
    const int btnH = 24;
    const int cancelX = x + 22;
    const int confirmX = x + w - btnW - 22;

    d.fillRoundRect(cancelX, btnY, btnW, btnH, 5, 0x2945);
    d.fillRoundRect(confirmX, btnY, btnW, btnH, 5, 0x8000);

    d.setTextColor(COLOR_TEXT_PRIMARY, 0x2945);
    d.drawString("Cancelar", cancelX + btnW / 2, btnY + btnH / 2);
    d.setTextColor(COLOR_TEXT_PRIMARY, 0x8000);
    d.drawString("Confirmar", confirmX + btnW / 2, btnY + btnH / 2);
}

// ── Draw principal ───────────────────────────────────────────────────────────
void screenSettingsDraw(lgfx::LovyanGFX& d, const RuntimeConfig& cfg, int scrollOffset) {
    d.fillScreen(COLOR_BACKGROUND);

    // ── Header ──
    d.fillRect(0, 0, 320, SETTINGS_HEADER_H, 0x2104);
    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(COLOR_TEXT_PRIMARY, 0x2104);
    d.setTextDatum(ML_DATUM);
    d.drawString("Configuracoes", 12, SETTINGS_HEADER_H / 2);
    d.drawFastHLine(0, SETTINGS_HEADER_H, 320, COLOR_DIVIDER);

    // ── Lista scrollável ──
    int contentY0 = SETTINGS_CONTENT_Y;
    int contentY1 = 240 - FOOTER_H;

    for (int i = 0; i < kEntryCount; i++) {
        int vy = entryVirtualY(i);
        int sh = entryHeight(i);
        int screenY = contentY0 + vy - scrollOffset;

        // Pula entradas fora da janela visível
        if (screenY + sh <= contentY0) continue;
        if (screenY >= contentY1) break;

        // Clipping vertical: cobre conteúdo que ultrapassa o header/footer
        // LovyanGFX não tem clip por viewport, então só desenhamos se dentro
        drawEntry(d, i, screenY, cfg, false);
    }

    // ── Cobertura do header (evita que itens renderizados acima do header apareçam) ──
    d.fillRect(0, 0, 320, SETTINGS_CONTENT_Y, COLOR_BACKGROUND);
    d.fillRect(0, 0, 320, SETTINGS_HEADER_H, 0x2104);
    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(COLOR_TEXT_PRIMARY, 0x2104);
    d.setTextDatum(ML_DATUM);
    d.drawString("Configuracoes", 12, SETTINGS_HEADER_H / 2);
    d.drawFastHLine(0, SETTINGS_HEADER_H, 320, COLOR_DIVIDER);

    // ── Cobertura do footer ──
    d.fillRect(0, 240 - FOOTER_H, 320, FOOTER_H, COLOR_BACKGROUND);
    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_SUBTLE, COLOR_BACKGROUND);
    d.setTextDatum(ML_DATUM);
    d.drawString("^ deslize ^", 8, 240 - FOOTER_H / 2);

    // Indicador de telas
    drawScreenIndicator(d, 3, SCREEN_COUNT);

    // Bateria
    drawBatteryIndicator(d);

    drawConfirmModal(d);
}

// ── Hit test: converte coordenada de tela em índice de entrada ────────────────
static int hitTestEntry(int tapY, int scrollOffset) {
    int virtualTapY = tapY - SETTINGS_CONTENT_Y + scrollOffset;
    int y = 0;
    for (int i = 0; i < kEntryCount; i++) {
        int h = entryHeight(i);
        if (virtualTapY >= y && virtualTapY < y + h) return i;
        y += h;
    }
    return -1;
}

// ── Tap: cicla ou toggle ──────────────────────────────────────────────────────
bool screenSettingsHandleTap(int tapX, int tapY, RuntimeConfig& cfg, int scrollOffset) {
    (void)tapX;
    if (g_confirmAction != CONFIRM_NONE) {
        const int w = 268, h = 120;
        const int x = (320 - w) / 2;
        const int y = (240 - h) / 2 + 4;
        const int btnY = y + 78;
        const int btnW = 96;
        const int btnH = 24;
        const int cancelX = x + 22;
        const int confirmX = x + w - btnW - 22;

        bool onCancel = (tapX >= cancelX && tapX <= cancelX + btnW &&
                         tapY >= btnY && tapY <= btnY + btnH);
        bool onConfirm = (tapX >= confirmX && tapX <= confirmX + btnW &&
                          tapY >= btnY && tapY <= btnY + btnH);

        if (onCancel) {
            g_confirmAction = CONFIRM_NONE;
            return false;
        }
        if (onConfirm) {
            ConfirmAction action = g_confirmAction;
            g_confirmAction = CONFIRM_NONE;
            switch (action) {
                case CONFIRM_WIFI_CHANGE:
                    wifiClearStoredCredentials();
                    delay(200);
                    ESP.restart();
                    break;
                case CONFIRM_RESTART:
                    delay(200);
                    ESP.restart();
                    break;
                case CONFIRM_FACTORY_RESET:
                    wifiClearStoredCredentials();
                    runtimeConfigClear();
                    delay(200);
                    ESP.restart();
                    break;
                default:
                    break;
            }
        }
        return false;
    }

    int idx = hitTestEntry(tapY, scrollOffset);
    if (idx < 0) return false;
    const VEntry& e = kEntries[idx];
    if (e.kind == EKIND_CATEGORY || e.kind == EKIND_ACTION) return false;

    switch (idx) {
        case IDX_KEEP_ALIVE:
            cfg.wifiKeepAlive = !cfg.wifiKeepAlive;
            break;
        case IDX_AUTO_BRIGHT:
            cfg.autoBrightness = !cfg.autoBrightness;
            break;
        case IDX_ACCEL_WAKE:
            cfg.accelWake = !cfg.accelWake;
            break;
        case IDX_TIMER_LABEL_1:
        case IDX_TIMER_LABEL_2:
        case IDX_TIMER_LABEL_3: {
            int slot = idx - IDX_TIMER_LABEL_1;
            int count = screenHomeGetTimerLabelPresetCount();
            cfg.timerLabelPreset[slot] = (cfg.timerLabelPreset[slot] + 1) % count;
            break;
        }
        case IDX_WEATHER_INT: {
            int i = findClosestIdx(kWeatherOpts, kWeatherCnt, cfg.weatherIntervalMin);
            cfg.weatherIntervalMin = kWeatherOpts[(i + 1) % kWeatherCnt];
            break;
        }
        case IDX_BRIGHTNESS: {
            int i = findClosestIdx(kBrightOpts, kBrightCnt, cfg.brightnessActive);
            cfg.brightnessActive = kBrightOpts[(i + 1) % kBrightCnt];
            break;
        }
        case IDX_DIM_TIMEOUT: {
            int i = findClosestIdx(kDimOpts, kDimCnt, cfg.dimTimeoutSec);
            cfg.dimTimeoutSec = kDimOpts[(i + 1) % kDimCnt];
            break;
        }
        case IDX_DEEP_SLEEP: {
            int i = findClosestIdx(kSleepOpts, kSleepCnt, cfg.deepSleepTimeoutMin);
            cfg.deepSleepTimeoutMin = kSleepOpts[(i + 1) % kSleepCnt];
            break;
        }
        default: return false;
    }
    return true;
}

// ── Long press: ações ─────────────────────────────────────────────────────────
bool screenSettingsHandleLongPress(int tapX, int tapY, int scrollOffset) {
    (void)tapX;
    if (g_confirmAction != CONFIRM_NONE) return false;
    int idx = hitTestEntry(tapY, scrollOffset);
    if (idx < 0) return false;
    if (kEntries[idx].kind != EKIND_ACTION) return false;

    switch (idx) {
        case IDX_WIFI_CHANGE:
            g_confirmAction = CONFIRM_WIFI_CHANGE;
            break;
        case IDX_RESTART:
            g_confirmAction = CONFIRM_RESTART;
            break;
        case IDX_FACTORY_RESET:
            g_confirmAction = CONFIRM_FACTORY_RESET;
            break;
        default: return false;
    }
    return false;
}

bool screenSettingsIsConfirmOpen() {
    return g_confirmAction != CONFIRM_NONE;
}

// ── Tela do portal WiFi ───────────────────────────────────────────────────────
void drawWifiPortalScreen(lgfx::LovyanGFX& d) {
    d.fillScreen(COLOR_BACKGROUND);

    d.setFont(&fonts::FreeSansBold18pt7b);
    d.setTextColor(COLOR_TEXT_ACCENT, COLOR_BACKGROUND);
    d.setTextDatum(MC_DATUM);
    d.drawString("Configure o WiFi", d.width() / 2, 50);

    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    d.drawString("Conecte ao celular:", d.width() / 2, 95);

    d.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BACKGROUND);
    d.drawString(WIFI_PORTAL_AP_NAME, d.width() / 2, 118);

    d.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    d.drawString("e acesse no navegador:", d.width() / 2, 145);

    d.setTextColor(COLOR_TEXT_ACCENT, COLOR_BACKGROUND);
    d.drawString("192.168.4.1", d.width() / 2, 168);

    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_SUBTLE, COLOR_BACKGROUND);
    d.drawString("Apos salvar, reinicia automaticamente.", d.width() / 2, 210);
}
