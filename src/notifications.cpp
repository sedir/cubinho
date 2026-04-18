#include "notifications.h"
#include "theme.h"
#include "logger.h"
#include "wifi_manager.h"
#include "power_manager.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ── Storage ──────────────────────────────────────────────────────────────────
static NotifItem _items[NOTIF_MAX];
static int       _count = 0;   // número de itens armazenados (0..NOTIF_MAX)

static void shiftRight() {
    int n = (_count < NOTIF_MAX) ? _count : (NOTIF_MAX - 1);
    for (int i = n; i > 0; i--) _items[i] = _items[i - 1];
}

// ── Toast ────────────────────────────────────────────────────────────────────
#define TOAST_DURATION_MS 5000
static bool     _toastActive  = false;
static uint32_t _toastUntilMs = 0;
static NotifItem _toastItem   = {};

// ── Drawer ───────────────────────────────────────────────────────────────────
#define DRAWER_ANIM_STEP   0.18f
#define DRAWER_HEADER_H    34
#define DRAWER_FOOTER_H    26
#define DRAWER_ITEM_H      46
#define DRAWER_VISIBLE_MAX 3    // quantos itens cabem na janela visivel

static bool  _drawerOpen     = false;
static float _drawerProgress = 0.0f;    // 0.0 (fechado) → 1.0 (aberto)
static float _drawerTarget   = 0.0f;

// ── HTTP server ──────────────────────────────────────────────────────────────
static WebServer* _server = nullptr;

// ── Utilidades ───────────────────────────────────────────────────────────────
static NotifIcon parseIcon(const char* s) {
    if (!s) return NOTIF_ICON_INFO;
    if (strcasecmp(s, "warn")    == 0 || strcasecmp(s, "warning") == 0) return NOTIF_ICON_WARN;
    if (strcasecmp(s, "error")   == 0 || strcasecmp(s, "err")     == 0) return NOTIF_ICON_ERROR;
    if (strcasecmp(s, "ok")      == 0 || strcasecmp(s, "success") == 0) return NOTIF_ICON_OK;
    return NOTIF_ICON_INFO;
}

static uint16_t iconColor(NotifIcon ic) {
    switch (ic) {
        case NOTIF_ICON_WARN:  return COLOR_TEXT_ACCENT;         // laranja
        case NOTIF_ICON_ERROR: return COLOR_BATTERY_LOW;         // vermelho
        case NOTIF_ICON_OK:    return COLOR_TIMER_RUNNING;       // verde
        default:               return 0x5D9F;                    // azul
    }
}

static const char* iconGlyph(NotifIcon ic) {
    switch (ic) {
        case NOTIF_ICON_WARN:  return "!";
        case NOTIF_ICON_ERROR: return "x";
        case NOTIF_ICON_OK:    return "v";
        default:               return "i";
    }
}

static void formatTimeAgo(const NotifItem& n, char* out, size_t outSize) {
    uint32_t now = millis();
    uint32_t ageMs = (now >= n.recvMs) ? (now - n.recvMs) : 0;
    uint32_t ageSec = ageMs / 1000UL;

    if (ageSec < 45)       snprintf(out, outSize, "agora");
    else if (ageSec < 3600) snprintf(out, outSize, "%lum", (unsigned long)(ageSec / 60UL));
    else if (ageSec < 86400) snprintf(out, outSize, "%luh", (unsigned long)(ageSec / 3600UL));
    else                    snprintf(out, outSize, "%lud", (unsigned long)(ageSec / 86400UL));
}

// ── API: storage ─────────────────────────────────────────────────────────────
void notifInit() {
    _count = 0;
    _drawerOpen = false;
    _drawerProgress = 0.0f;
    _drawerTarget = 0.0f;
    _toastActive = false;
    LOG_I("notif", "Modulo de notificacoes iniciado (MAX=%d)", NOTIF_MAX);
}

void notifPush(const char* title, const char* body, NotifIcon icon) {
    shiftRight();
    NotifItem& n = _items[0];
    strlcpy(n.title, title ? title : "", sizeof(n.title));
    strlcpy(n.body,  body  ? body  : "", sizeof(n.body));
    n.timestamp = time(nullptr);
    n.recvMs    = millis();
    n.icon      = icon;
    n.read      = false;
    if (_count < NOTIF_MAX) _count++;

    // Dispara toast (se gaveta fechada) ou apenas atualiza contagem
    if (!_drawerOpen) {
        _toastItem    = n;
        _toastActive  = true;
        _toastUntilMs = millis() + TOAST_DURATION_MS;
    }

    // Acorda o display para alertar o usuario
    powerOnTouch();

    LOG_I("notif", "+ \"%s\" (%s) total=%d", n.title,
          icon == NOTIF_ICON_WARN  ? "warn"  :
          icon == NOTIF_ICON_ERROR ? "error" :
          icon == NOTIF_ICON_OK    ? "ok"    : "info",
          _count);
}

int notifGetCount()       { return _count; }
int notifGetUnreadCount() {
    int u = 0;
    for (int i = 0; i < _count; i++) if (!_items[i].read) u++;
    return u;
}

const NotifItem* notifGetAt(int i) {
    if (i < 0 || i >= _count) return nullptr;
    return &_items[i];
}

void notifClearAll() {
    _count = 0;
    LOG_I("notif", "Lista limpa");
}

void notifMarkAllRead() {
    for (int i = 0; i < _count; i++) _items[i].read = true;
}

// ── HTTP handlers ────────────────────────────────────────────────────────────
static void handleRoot() {
    String html;
    html.reserve(1400);
    html += F("<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    html += F("<title>Cubinho Push</title><style>");
    html += F("body{font-family:sans-serif;background:#111827;color:#e5e7eb;padding:24px;max-width:480px;margin:0 auto}");
    html += F("h1{color:#f59e0b;margin:0 0 12px}");
    html += F("p{color:#9ca3af;line-height:1.4}");
    html += F("label{display:block;margin:14px 0 6px}");
    html += F("input,select,textarea{width:100%;padding:10px;border:1px solid #374151;border-radius:8px;background:#0f172a;color:#e5e7eb;box-sizing:border-box;font-size:14px}");
    html += F("button{width:100%;padding:12px;margin-top:16px;background:#f59e0b;color:#111827;border:none;border-radius:8px;font-size:16px;font-weight:700;cursor:pointer}");
    html += F("code{background:#0f172a;padding:2px 6px;border-radius:4px}");
    html += F(".tip{font-size:12px;color:#9ca3af;margin-top:20px}");
    html += F("</style></head><body>");
    html += F("<h1>Cubinho Push</h1>");
    html += F("<p>Envie uma notificacao de teste para o aparelho.</p>");
    html += F("<form method=\"POST\" action=\"/notify\" enctype=\"application/x-www-form-urlencoded\">");
    html += F("<label>Titulo</label><input name=\"title\" maxlength=\"39\" required>");
    html += F("<label>Mensagem</label><textarea name=\"body\" rows=\"3\" maxlength=\"127\"></textarea>");
    html += F("<label>Tipo</label><select name=\"icon\"><option value=\"info\">Informativo</option><option value=\"ok\">Sucesso</option><option value=\"warn\">Aviso</option><option value=\"error\">Erro</option></select>");
    html += F("<button type=\"submit\">Enviar</button></form>");
    html += F("<div class=\"tip\"><p><b>API:</b></p><p><code>POST /notify</code> JSON:<br><code>{\"title\":\"Oi\",\"body\":\"mensagem\",\"icon\":\"info\"}</code></p>");
    html += F("<p>ou form urlencoded com os mesmos campos.</p></div>");
    html += F("</body></html>");
    _server->send(200, "text/html", html);
}

static void handleNotify() {
    String title, body, iconStr;

    // Aceita form urlencoded ou JSON
    if (_server->hasArg("plain")) {
        String raw = _server->arg("plain");
        if (raw.length() > 0 && raw[0] == '{') {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, raw);
            if (err) {
                _server->send(400, "application/json",
                    "{\"ok\":false,\"error\":\"json-parse\"}");
                return;
            }
            title   = (const char*)(doc["title"]   | "");
            body    = (const char*)(doc["body"]    | "");
            iconStr = (const char*)(doc["icon"]    | "info");
        }
    }
    if (title.length() == 0) title   = _server->arg("title");
    if (body.length()  == 0) body    = _server->arg("body");
    if (iconStr.length() == 0) iconStr = _server->arg("icon");

    title.trim();
    body.trim();
    if (title.length() == 0) {
        _server->send(400, "application/json",
            "{\"ok\":false,\"error\":\"title-required\"}");
        return;
    }

    notifPush(title.c_str(), body.c_str(), parseIcon(iconStr.c_str()));
    _server->send(200, "application/json", "{\"ok\":true}");
}

static void handleList() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < _count; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["title"] = _items[i].title;
        o["body"]  = _items[i].body;
        o["ts"]    = (long)_items[i].timestamp;
        o["read"]  = _items[i].read;
    }
    String out;
    serializeJson(doc, out);
    _server->send(200, "application/json", out);
}

static void handleClear() {
    notifClearAll();
    _server->send(200, "application/json", "{\"ok\":true}");
}

static void handleNotFound() {
    _server->send(404, "application/json", "{\"ok\":false,\"error\":\"not-found\"}");
}

// ── Server lifecycle ─────────────────────────────────────────────────────────
static void startServer() {
    if (_server) return;
    _server = new WebServer(NOTIF_HTTP_PORT);
    _server->on("/",       HTTP_GET,  handleRoot);
    _server->on("/notify", HTTP_POST, handleNotify);
    _server->on("/list",   HTTP_GET,  handleList);
    _server->on("/clear",  HTTP_POST, handleClear);
    _server->onNotFound(handleNotFound);
    _server->begin();
    LOG_I("notif", "Servidor push ativo — http://%s:%d/",
          WiFi.localIP().toString().c_str(), NOTIF_HTTP_PORT);
}

static void stopServer() {
    if (!_server) return;
    _server->stop();
    delete _server;
    _server = nullptr;
    LOG_I("notif", "Servidor desativado");
}

bool notifServerIsRunning() { return _server != nullptr; }

void notifServerPoll() {
    // Precisamos de WiFi STA ativo e keep-alive habilitado. Porta 8080 nao colide
    // com a 80 do portal/calendario, entao podem coexistir.
    bool canRun = (WiFi.status() == WL_CONNECTED)
                  && wifiIsKeepAlive()
                  && !wifiIsPortalMode();

    if (canRun && !_server)  startServer();
    if (!canRun && _server)  stopServer();
    if (_server) _server->handleClient();
}

// ── Toast ────────────────────────────────────────────────────────────────────
bool notifToastActive() {
    if (_toastActive && millis() >= _toastUntilMs) _toastActive = false;
    return _toastActive;
}

void notifToastDismiss() { _toastActive = false; }

void notifToastDraw(lgfx::LovyanGFX& d) {
    if (!notifToastActive()) return;

    const int w = 300;
    const int h = 40;
    const int x = (d.width() - w) / 2;
    const int y = 4;

    // Fundo com leve transparência visual (cor sólida)
    d.fillRoundRect(x, y, w, h, 6, 0x18C3);
    d.drawRoundRect(x, y, w, h, 6, iconColor(_toastItem.icon));

    // Bolinha do icone
    d.fillCircle(x + 16, y + h / 2, 8, iconColor(_toastItem.icon));
    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_BACKGROUND, iconColor(_toastItem.icon));
    d.setTextDatum(MC_DATUM);
    d.drawString(iconGlyph(_toastItem.icon), x + 16, y + h / 2);

    // Título
    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(COLOR_TEXT_PRIMARY, 0x18C3);
    d.setTextDatum(TL_DATUM);

    // Trunca título para caber em ~265px
    char titleBuf[40];
    strlcpy(titleBuf, _toastItem.title, sizeof(titleBuf));
    const int maxTitleW = w - 40;
    while (d.textWidth(titleBuf) > maxTitleW && strlen(titleBuf) > 3) {
        titleBuf[strlen(titleBuf) - 1] = '\0';
    }
    d.drawString(titleBuf, x + 32, y + 4);

    // Corpo (1 linha truncada)
    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_DIM, 0x18C3);
    char bodyBuf[48];
    strlcpy(bodyBuf, _toastItem.body, sizeof(bodyBuf));
    const int maxBodyW = w - 40;
    while (d.textWidth(bodyBuf) > maxBodyW && strlen(bodyBuf) > 3) {
        bodyBuf[strlen(bodyBuf) - 1] = '\0';
    }
    d.drawString(bodyBuf, x + 32, y + 24);
}

// ── Drawer ───────────────────────────────────────────────────────────────────
bool notifDrawerIsOpen()      { return _drawerOpen; }
bool notifDrawerIsAnimating() { return fabsf(_drawerTarget - _drawerProgress) > 0.01f; }
bool notifDrawerIsVisible()   { return _drawerOpen || _drawerProgress > 0.01f; }

void notifDrawerOpen() {
    _drawerOpen   = true;
    _drawerTarget = 1.0f;
    notifToastDismiss();
    notifMarkAllRead();
    LOG_I("notif", "Gaveta abrir");
}

void notifDrawerClose() {
    _drawerOpen   = false;
    _drawerTarget = 0.0f;
    LOG_I("notif", "Gaveta fechar");
}

void notifDrawerUpdate() {
    float diff = _drawerTarget - _drawerProgress;
    if (fabsf(diff) <= 0.01f) {
        _drawerProgress = _drawerTarget;
        return;
    }
    _drawerProgress += diff * DRAWER_ANIM_STEP;
    if (_drawerProgress < 0.0f) _drawerProgress = 0.0f;
    if (_drawerProgress > 1.0f) _drawerProgress = 1.0f;
}

// Posição y base do drawer (-240 fechado, 0 aberto). Ease-out cúbico.
static int drawerBaseY(int screenH) {
    float t = _drawerProgress;
    float inv = 1.0f - t;
    float ease = 1.0f - (inv * inv * inv);
    return -(int)((1.0f - ease) * screenH);
}

static void drawItemRow(lgfx::LovyanGFX& d, const NotifItem& n, int x, int y, int w, int h) {
    // Divisor sutil acima
    d.drawFastHLine(x + 8, y, w - 16, 0x2104);

    // Bolinha de ícone
    uint16_t ic = iconColor(n.icon);
    d.fillCircle(x + 18, y + 14, 8, ic);
    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_BACKGROUND, ic);
    d.setTextDatum(MC_DATUM);
    d.drawString(iconGlyph(n.icon), x + 18, y + 14);

    // Hora
    char ago[10];
    formatTimeAgo(n, ago, sizeof(ago));
    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    d.setTextDatum(TR_DATUM);
    d.drawString(ago, x + w - 10, y + 6);

    // Título
    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BACKGROUND);
    d.setTextDatum(TL_DATUM);
    char titleBuf[40];
    strlcpy(titleBuf, n.title, sizeof(titleBuf));
    int maxTitleW = w - 80;
    while (d.textWidth(titleBuf) > maxTitleW && strlen(titleBuf) > 3) {
        titleBuf[strlen(titleBuf) - 1] = '\0';
    }
    d.drawString(titleBuf, x + 32, y + 4);

    // Corpo (1 linha truncada)
    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
    char bodyBuf[64];
    strlcpy(bodyBuf, n.body, sizeof(bodyBuf));
    int maxBodyW = w - 40;
    while (d.textWidth(bodyBuf) > maxBodyW && strlen(bodyBuf) > 3) {
        bodyBuf[strlen(bodyBuf) - 1] = '\0';
    }
    d.drawString(bodyBuf, x + 32, y + 26);

    (void)h;
}

void notifDrawerDraw(lgfx::LovyanGFX& d) {
    if (!notifDrawerIsVisible()) return;

    const int screenW = d.width();
    const int screenH = d.height();
    const int baseY   = drawerBaseY(screenH);
    const int top     = baseY;

    // Backdrop escurece o fundo proporcionalmente ao progresso
    if (_drawerProgress > 0.0f && _drawerProgress < 1.0f) {
        // Backdrop leve usando apenas linhas (economiza preenchimento)
        d.fillRect(0, 0, screenW, screenH, 0x0841);
    }

    // Corpo do drawer
    d.fillRect(0, top, screenW, screenH, COLOR_BACKGROUND);

    // Header
    d.fillRect(0, top, screenW, DRAWER_HEADER_H, 0x2104);
    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(COLOR_TEXT_ACCENT, 0x2104);
    d.setTextDatum(ML_DATUM);
    d.drawString("Notificacoes", 12, top + DRAWER_HEADER_H / 2);

    // Contador
    if (_count > 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", _count);
        int tw = d.textWidth("Notificacoes");
        d.fillRoundRect(12 + tw + 6, top + 7, 22, DRAWER_HEADER_H - 14, 4, COLOR_TEXT_ACCENT);
        d.setFont(&fonts::Font0);
        d.setTextColor(COLOR_BACKGROUND, COLOR_TEXT_ACCENT);
        d.setTextDatum(MC_DATUM);
        d.drawString(buf, 12 + tw + 6 + 11, top + DRAWER_HEADER_H / 2);
    }

    // Botão X (fechar) à direita
    const int closeX = screenW - 28;
    const int closeY = top + 8;
    d.fillRoundRect(closeX, closeY, 20, 20, 4, 0x4208);
    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_PRIMARY, 0x4208);
    d.setTextDatum(MC_DATUM);
    d.drawString("X", closeX + 10, closeY + 10);

    d.drawFastHLine(0, top + DRAWER_HEADER_H, screenW, COLOR_DIVIDER);

    // Lista
    const int listY0 = top + DRAWER_HEADER_H + 2;
    const int listY1 = top + screenH - DRAWER_FOOTER_H;
    const int listH  = listY1 - listY0;

    if (_count == 0) {
        d.setFont(&fonts::FreeSans9pt7b);
        d.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
        d.setTextDatum(MC_DATUM);
        d.drawString("Sem notificacoes", screenW / 2, listY0 + listH / 2 - 8);
        d.setFont(&fonts::Font0);
        d.setTextColor(COLOR_TEXT_SUBTLE, COLOR_BACKGROUND);
        if (wifiIsKeepAlive() && WiFi.status() == WL_CONNECTED) {
            char ipBuf[56];
            snprintf(ipBuf, sizeof(ipBuf), "POST http://%s:%d/notify",
                     WiFi.localIP().toString().c_str(), NOTIF_HTTP_PORT);
            d.drawString(ipBuf, screenW / 2, listY0 + listH / 2 + 12);
        } else {
            d.drawString("Ative o WiFi permanente", screenW / 2, listY0 + listH / 2 + 12);
        }
    } else {
        int visible = (_count < DRAWER_VISIBLE_MAX) ? _count : DRAWER_VISIBLE_MAX;
        for (int i = 0; i < visible; i++) {
            int rowY = listY0 + i * DRAWER_ITEM_H;
            if (rowY + DRAWER_ITEM_H > listY1) break;
            drawItemRow(d, _items[i], 0, rowY, screenW, DRAWER_ITEM_H);
        }
        if (_count > DRAWER_VISIBLE_MAX) {
            char buf[24];
            snprintf(buf, sizeof(buf), "+ %d mais...", _count - DRAWER_VISIBLE_MAX);
            d.setFont(&fonts::Font0);
            d.setTextColor(COLOR_TEXT_DIM, COLOR_BACKGROUND);
            d.setTextDatum(TL_DATUM);
            d.drawString(buf, 12, listY0 + visible * DRAWER_ITEM_H + 2);
        }
    }

    // Footer com dica e botão "Limpar"
    const int footerY = top + screenH - DRAWER_FOOTER_H;
    d.fillRect(0, footerY, screenW, DRAWER_FOOTER_H, 0x18C3);
    d.drawFastHLine(0, footerY, screenW, COLOR_DIVIDER);

    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_SUBTLE, 0x18C3);
    d.setTextDatum(ML_DATUM);
    d.drawString("^ deslize para cima", 10, footerY + DRAWER_FOOTER_H / 2);

    if (_count > 0) {
        const int btnW = 74;
        const int btnH = 20;
        const int btnX = screenW - btnW - 8;
        const int btnY = footerY + (DRAWER_FOOTER_H - btnH) / 2;
        d.fillRoundRect(btnX, btnY, btnW, btnH, 4, 0x5000);
        d.setTextColor(COLOR_TEXT_PRIMARY, 0x5000);
        d.setTextDatum(MC_DATUM);
        d.drawString("Limpar", btnX + btnW / 2, btnY + btnH / 2);
    }
}

// ── Swipe detection ──────────────────────────────────────────────────────────
bool notifShouldOpenFromSwipe(int startY, int deltaX, int deltaY) {
    if (_drawerOpen) return false;
    if (startY > 40)  return false;
    if (deltaY < 30)  return false;
    if (abs(deltaY) <= abs(deltaX)) return false;
    return true;
}

// ── Touch handling while drawer is open ──────────────────────────────────────
bool notifDrawerHandleRelease(int x, int y, int deltaX, int deltaY, bool longPress) {
    if (!_drawerOpen) return false;

    // Swipe para cima (em qualquer lugar) fecha a gaveta
    bool vertSwipe = (abs(deltaY) >= 30 && abs(deltaY) > abs(deltaX));
    if (vertSwipe && deltaY <= -30) {
        notifDrawerClose();
        return true;
    }

    // Botao X (canto superior direito)
    const int closeX0 = 320 - 28;
    const int closeY0 = 8;
    if (x >= closeX0 && x <= closeX0 + 20 && y >= closeY0 && y <= closeY0 + 20) {
        notifDrawerClose();
        return true;
    }

    // Botao Limpar
    if (_count > 0) {
        const int btnW = 74, btnH = 20;
        const int btnX = 320 - btnW - 8;
        const int btnY = 240 - DRAWER_FOOTER_H + (DRAWER_FOOTER_H - btnH) / 2;
        if (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH) {
            notifClearAll();
            return true;
        }
    }

    // Long press em qualquer lugar da lista: limpa tudo
    if (longPress && _count > 0) {
        int listY0 = DRAWER_HEADER_H + 2;
        int listY1 = 240 - DRAWER_FOOTER_H;
        if (y >= listY0 && y < listY1) {
            notifClearAll();
            return true;
        }
    }

    (void)deltaX;
    // Qualquer outro toque dentro do drawer é consumido (nao vaza pra tela de baixo)
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT client
// ─────────────────────────────────────────────────────────────────────────────
static WiFiClient   _mqttNet;
static PubSubClient _mqttClient(_mqttNet);

static bool     _mqttEnabled = false;
static char     _mqttHost[64]  = "";
static int      _mqttPort      = 1883;
static char     _mqttUser[32]  = "";
static char     _mqttPass[64]  = "";
static char     _mqttTopic[64] = "";

static uint32_t _mqttLastAttemptMs = 0;
static uint32_t _mqttBackoffMs     = 2000;

static void mqttOnMessage(char* topic, byte* payload, unsigned int len) {
    (void)topic;
    // Aceita JSON {"title":..,"body":..,"icon":..} ou texto simples.
    char buf[256];
    size_t n = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = '\0';

    const char* p = buf;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    if (*p == '{') {
        JsonDocument doc;
        if (deserializeJson(doc, p) == DeserializationError::Ok) {
            const char* t = doc["title"] | "";
            const char* b = doc["body"]  | "";
            const char* ic = doc["icon"] | "info";
            if (t && *t) {
                notifPush(t, b, parseIcon(ic));
                return;
            }
        }
    }
    // Fallback: usa payload inteiro como corpo, titulo generico
    notifPush("MQTT", buf, NOTIF_ICON_INFO);
}

void notifMqttApplyConfig(bool enabled, const char* host, int port,
                          const char* user, const char* pass,
                          const char* topic) {
    bool hostChanged  = strncmp(_mqttHost,  host  ? host  : "", sizeof(_mqttHost))  != 0;
    bool portChanged  = (_mqttPort != port);
    bool topicChanged = strncmp(_mqttTopic, topic ? topic : "", sizeof(_mqttTopic)) != 0;
    bool userChanged  = strncmp(_mqttUser,  user  ? user  : "", sizeof(_mqttUser))  != 0;
    bool passChanged  = strncmp(_mqttPass,  pass  ? pass  : "", sizeof(_mqttPass))  != 0;
    bool enabledChanged = (_mqttEnabled != enabled);

    _mqttEnabled = enabled;
    strlcpy(_mqttHost,  host  ? host  : "", sizeof(_mqttHost));
    _mqttPort = (port > 0 && port < 65536) ? port : 1883;
    strlcpy(_mqttUser,  user  ? user  : "", sizeof(_mqttUser));
    strlcpy(_mqttPass,  pass  ? pass  : "", sizeof(_mqttPass));
    strlcpy(_mqttTopic, topic ? topic : "", sizeof(_mqttTopic));

    if (hostChanged || portChanged || userChanged || passChanged ||
        topicChanged || enabledChanged) {
        if (_mqttClient.connected()) _mqttClient.disconnect();
        _mqttClient.setServer(_mqttHost, _mqttPort);
        _mqttClient.setCallback(mqttOnMessage);
        _mqttBackoffMs     = 2000;
        _mqttLastAttemptMs = 0;
        LOG_I("notif", "MQTT config: %s@%s:%d topic=%s enabled=%d",
              _mqttUser, _mqttHost, _mqttPort, _mqttTopic, (int)_mqttEnabled);
    }
}

bool notifMqttIsConnected() { return _mqttClient.connected(); }

void notifMqttPoll() {
    if (!_mqttEnabled || _mqttHost[0] == '\0' || _mqttTopic[0] == '\0') {
        if (_mqttClient.connected()) _mqttClient.disconnect();
        return;
    }
    if (WiFi.status() != WL_CONNECTED || !wifiIsKeepAlive() || wifiIsPortalMode()) {
        if (_mqttClient.connected()) _mqttClient.disconnect();
        return;
    }

    if (_mqttClient.connected()) {
        _mqttClient.loop();
        return;
    }

    uint32_t now = millis();
    if (_mqttLastAttemptMs && (now - _mqttLastAttemptMs) < _mqttBackoffMs) return;
    _mqttLastAttemptMs = now;

    char clientId[32];
    snprintf(clientId, sizeof(clientId), "cubinho-%06X",
             (unsigned)(ESP.getEfuseMac() & 0xFFFFFF));

    bool ok = (_mqttUser[0] != '\0')
            ? _mqttClient.connect(clientId, _mqttUser, _mqttPass)
            : _mqttClient.connect(clientId);

    if (ok) {
        _mqttClient.subscribe(_mqttTopic);
        _mqttBackoffMs = 2000;
        LOG_I("notif", "MQTT conectado — sub %s", _mqttTopic);
    } else {
        // backoff exponencial com teto em 60s
        _mqttBackoffMs = (_mqttBackoffMs < 60000) ? (_mqttBackoffMs * 2) : 60000;
        LOG_W("notif", "MQTT falha (rc=%d) backoff=%lums", _mqttClient.state(),
              (unsigned long)_mqttBackoffMs);
    }
}
