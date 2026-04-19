#include "wifi_manager.h"
#include "calendar_feed.h"
#include "config.h"
#include "logger.h"
#include "ota_manager.h"
#include "runtime_config.h"
#include "screen_home.h"
#include "bg_network.h"
#include "notifications.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <time.h>

// ── Estado async ─────────────────────────────────────────────────────────────
enum AsyncState
{
    ASYNC_IDLE,
    ASYNC_CONNECTING,
    ASYNC_NTP_SYNCING,
    ASYNC_BG_FETCHING
};

static AsyncState _state = ASYNC_IDLE;
static WeatherData *_asyncOut = nullptr;
static uint32_t _asyncStartMs = 0;
static bool _firstFetch = true;
static bool _keepAlive = false;
static uint32_t _lastFetchMs = 0;
static uint32_t _updateIntervalMs = WEATHER_UPDATE_INTERVAL_MS;
static bool _bgJustCompleted = false;
static WifiProgressCb _progressCb = nullptr;

// ── Portal cativo (item #23) ─────────────────────────────────────────────────
static bool _portalMode = false;
static bool _calendarConfigMode = false;
static bool _webConfigMode = false;
static DNSServer *_dnsServer = nullptr;
static WebServer *_webServer = nullptr;
static uint8_t _failCount = 0;
static bool _calendarStopPending = false;
static uint32_t _calendarStopAtMs = 0;
static char _calendarConfigAddress[40] = "";
static char _webConfigAddress[40] = "";
static bool _webConfigRestartPending = false;
static uint32_t _webConfigRestartAtMs = 0;

static Preferences _prefs;
static String _nvsSSID;
static String _nvsPass;

static void wifiOff();

static void loadCredentials()
{
    _prefs.begin("wifi", true); // read-only
    _nvsSSID = _prefs.getString("ssid", "");
    _nvsPass = _prefs.getString("pass", "");
    _prefs.end();
}

static void saveCredentials(const String &ssid, const String &pass)
{
    _prefs.begin("wifi", false);
    _prefs.putString("ssid", ssid);
    _prefs.putString("pass", pass);
    _prefs.end();
    LOG_I("wifi", "Credenciais salvas em NVS");
}

bool wifiHasStoredCredentials()
{
    loadCredentials();
    // Valida limites do protocolo 802.11: SSID 1–32 bytes, senha 0–63 bytes.
    // NVS corrompido pode retornar strings fora desses limites.
    int ssidLen = (int)_nvsSSID.length();
    int passLen = (int)_nvsPass.length();
    if (ssidLen < 1 || ssidLen > 32 || passLen > 63) {
        if (ssidLen != 0) {
            LOG_W("wifi", "Credenciais invalidas no NVS (ssid=%d pass=%d bytes) — ignoradas",
                  ssidLen, passLen);
        }
        return false;
    }
    return true;
}

void wifiClearStoredCredentials()
{
    _prefs.begin("wifi", false);
    _prefs.clear();
    _prefs.end();
    _nvsSSID = "";
    _nvsPass = "";
    LOG_I("wifi", "Credenciais NVS apagadas");
}

void wifiSaveCredentials(const char *ssid, const char *pass)
{
    saveCredentials(String(ssid), String(pass));
    _nvsSSID = String(ssid);
    _nvsPass = String(pass);
    LOG_I("wifi", "Credenciais salvas via QR: %s", ssid);
}

// Retorna SSID/senha do NVS — credenciais hardcoded foram removidas
static const char *getSSID() { return _nvsSSID.c_str(); }
static const char *getPass() { return _nvsPass.c_str(); }

// ── Portal HTML ──────────────────────────────────────────────────────────────
static const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cubinho Setup</title>
<style>
body{font-family:sans-serif;background:#1a1a2e;color:#eee;display:flex;justify-content:center;padding:40px}
.c{background:#16213e;padding:30px;border-radius:16px;max-width:360px;width:100%}
h1{color:#FD20;text-align:center;margin:0 0 20px}
input{width:100%;padding:12px;margin:8px 0;border:1px solid #444;border-radius:8px;
background:#0f3460;color:#eee;box-sizing:border-box;font-size:16px}
button{width:100%;padding:14px;margin-top:16px;background:#e94560;color:white;border:none;
border-radius:8px;font-size:18px;cursor:pointer}
button:hover{background:#c81e45}
</style></head><body><div class="c">
<h1>Cubinho Setup</h1>
<form method="POST" action="/save">
<label>SSID</label><input name="ssid" required>
<label>Senha</label><input name="pass" type="password" required>
<button type="submit">Salvar e Reiniciar</button>
</form></div></body></html>
)rawliteral";

static void appendHtmlEscaped(String &out, const char *input)
{
    for (const char *p = input; *p; p++)
    {
        if (*p == '&')
            out += F("&amp;");
        else if (*p == '"')
            out += F("&quot;");
        else if (*p == '<')
            out += F("&lt;");
        else if (*p == '>')
            out += F("&gt;");
        else
            out += *p;
    }
}

static String buildCalendarConfigHtml()
{
    char currentUrl[192];
    runtimeConfigGetCalendarUrl(currentUrl, sizeof(currentUrl));
    const char *statusText = calendarGetStatusText();
    const char *statusLabel = calendarGetStatusLabel();

    String html;
    html.reserve(1500);
    html += F("<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    html += F("<title>Cubinho Calendar</title><style>");
    html += F("body{font-family:sans-serif;background:#111827;color:#e5e7eb;display:flex;justify-content:center;padding:28px}");
    html += F(".c{background:#1f2937;padding:28px;border-radius:16px;max-width:560px;width:100%;box-sizing:border-box}");
    html += F("h1{color:#f59e0b;text-align:center;margin:0 0 10px}");
    html += F("p{color:#9ca3af;line-height:1.4}label{display:block;margin:16px 0 6px}");
    html += F("input{width:100%;padding:12px;border:1px solid #374151;border-radius:8px;background:#0f172a;color:#e5e7eb;box-sizing:border-box;font-size:14px}");
    html += F("button{width:100%;padding:14px;margin-top:18px;background:#f59e0b;color:#111827;border:none;border-radius:8px;font-size:16px;font-weight:700;cursor:pointer}");
    html += F("button:hover{background:#d97706}button:active{background:#b45309}");
    html += F(".tip{font-size:12px;color:#9ca3af;margin-top:14px}.status{margin:14px 0;padding:12px;border-radius:10px;background:#0f172a;border:1px solid #374151}.tag{display:inline-block;padding:2px 8px;border-radius:999px;background:#374151;font-size:12px;font-weight:700;margin-right:8px}</style></head><body><div class=\"c\">");
    html += F("<h1>Calendario iCal</h1><p>Cole aqui a URL privada do feed iCal/ICS. O campo abaixo mostra a URL atualmente salva no aparelho. Salvar vazio desativa a integracao.</p>");
    html += F("<div class=\"status\"><span class=\"tag\">");
    html += statusLabel;
    html += F("</span>");
    html += statusText;
    html += F("</div>");
    html += F("<form method=\"POST\" action=\"/save\"><label>URL do calendario</label><input name=\"url\" value=\"");
    appendHtmlEscaped(html, currentUrl);
    html += F("\" placeholder=\"https://.../basic.ics\" autocomplete=\"off\">");
    html += F("<button type=\"submit\">Salvar</button></form>");
    html += F("<div class=\"tip\">A pagina fica ativa apenas enquanto este modo estiver aberto no dispositivo.</div>");
    html += F("</div></body></html>");
    return html;
}

static void stopWebUi()
{
    if (_dnsServer)
    {
        _dnsServer->stop();
        delete _dnsServer;
        _dnsServer = nullptr;
    }
    if (_webServer)
    {
        _webServer->stop();
        delete _webServer;
        _webServer = nullptr;
    }
    _portalMode = false;
    _calendarConfigMode = false;
    _calendarStopPending = false;
    _calendarStopAtMs = 0;
    _calendarConfigAddress[0] = '\0';
    _webConfigMode = false;
    _webConfigRestartPending = false;
    _webConfigRestartAtMs = 0;
    _webConfigAddress[0] = '\0';
}

static void startPortal()
{
    stopWebUi();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_PORTAL_AP_NAME);
    delay(100);

    _dnsServer = new DNSServer();
    _dnsServer->start(53, "*", WiFi.softAPIP());

    _webServer = new WebServer(80);
    _webServer->on("/", HTTP_GET, []()
                   { _webServer->send(200, "text/html", PORTAL_HTML); });
    _webServer->on("/save", HTTP_POST, []()
                   {
        String ssid = _webServer->arg("ssid");
        String pass = _webServer->arg("pass");
        if (ssid.length() > 0) {
            saveCredentials(ssid, pass);
            _webServer->send(200, "text/html",
                "<h2 style='color:#0f0;text-align:center;font-family:sans-serif'>"
                "Salvo! Reiniciando...</h2>");
            delay(1500);
            ESP.restart();
        } else {
            _webServer->send(400, "text/plain", "SSID vazio");
        } });
    _webServer->onNotFound([]()
                           {
        _webServer->sendHeader("Location", "http://192.168.4.1/");
        _webServer->send(302, "text/plain", ""); });
    _webServer->begin();

    _portalMode = true;
    LOG_I("wifi", "Portal cativo ativo — AP: %s  IP: %s",
          WIFI_PORTAL_AP_NAME, WiFi.softAPIP().toString().c_str());
}

static bool ensureStaConnectedForUi()
{
    if (WiFi.status() == WL_CONNECTED)
        return true;

    loadCredentials();
    if (_nvsSSID.length() == 0)
        return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(getSSID(), getPass());
    LOG_I("wifi", "Conectando para configuracao local em %s...", getSSID());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
    {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        LOG_W("wifi", "Falha ao conectar para configuracao local");
        if (!_keepAlive)
            wifiOff();
        return false;
    }

    LOG_I("wifi", "Config local em IP %s", WiFi.localIP().toString().c_str());
    return true;
}

static void startCalendarConfigServer()
{
    stopWebUi();

    _webServer = new WebServer(80);
    _webServer->on("/", HTTP_GET, []()
                   { _webServer->send(200, "text/html", buildCalendarConfigHtml()); });
    _webServer->on("/save", HTTP_POST, []()
                   {
        String url = _webServer->arg("url");
        url.trim();
        runtimeConfigSaveCalendarUrl(url.c_str());

        String html;
        html.reserve(480);
        html += F("<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"></head><body style='font-family:sans-serif;background:#111827;color:#e5e7eb;padding:28px'>");
        html += F("<div style='max-width:560px;margin:0 auto;background:#1f2937;padding:28px;border-radius:16px'>");
        html += F("<h2 style='margin-top:0;color:#f59e0b'>Configuracao do calendario</h2>");
        if (url.length() == 0) {
            html += F("<p>Calendario desativado. Nenhuma URL ficou salva.</p>");
        } else {
            html += F("<p>URL salva com sucesso.</p>");
            html += F("<p style='color:#9ca3af'>O dispositivo ira validar o feed na proxima conexao.</p>");
        }
        html += F("<p style='color:#9ca3af'>A tela do dispositivo sera fechada em instantes.</p></div></body></html>");
        _webServer->send(200, "text/html", html);
        _calendarStopPending = true;
        _calendarStopAtMs = millis() + 1800;
        LOG_I("wifi", "URL iCal atualizada"); });
    _webServer->onNotFound([]()
                           {
        _webServer->sendHeader("Location", "/");
        _webServer->send(302, "text/plain", ""); });
    _webServer->begin();

    snprintf(_calendarConfigAddress, sizeof(_calendarConfigAddress), "http://%s",
             WiFi.localIP().toString().c_str());
    _calendarConfigMode = true;
    LOG_I("wifi", "Config iCal ativa em %s", _calendarConfigAddress);
}

// ── Helpers ──────────────────────────────────────────────────────────────────
static void wifiOff()
{
    if (_keepAlive || _calendarConfigMode || _webConfigMode)
    {
        LOG_I("wifi", "WiFi mantido ativo");
        return;
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    LOG_I("wifi", "WiFi OFF");
}

static void startBgFetch()
{
    bgNetworkStartFetch(_asyncOut);
    _state = ASYNC_BG_FETCHING;
    LOG_I("wifi", "Fetch delegado ao background (core 0)");
}

static void pollAsync()
{
    switch (_state)
    {
    case ASYNC_IDLE:
        return;

    case ASYNC_CONNECTING:
        if (WiFi.status() == WL_CONNECTED)
        {
            struct tm _tmcheck;
            bool timeValid = getLocalTime(&_tmcheck, 0) && (time(nullptr) > 1577836800L);
            if (timeValid)
            {
                LOG_I("wifi", "Conectado (RSSI %d dBm) — RTC valido", WiFi.RSSI());
                startBgFetch();
            }
            else
            {
                LOG_I("wifi", "Conectado (RSSI %d dBm) — NTP sync", WiFi.RSSI());
                configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
                _asyncStartMs = millis();
                _state = ASYNC_NTP_SYNCING;
            }
        }
        else if (millis() - _asyncStartMs >= 10000)
        {
            LOG_W("wifi", "Timeout de conexao (%d falha(s))", _failCount + 1);
            _failCount++;
            wifiOff();
            _state = ASYNC_IDLE;
            _firstFetch = false;
            _lastFetchMs = millis();
        }
        break;

    case ASYNC_NTP_SYNCING:
    {
        struct tm timeinfo;
        bool ntpOk = getLocalTime(&timeinfo, 0);
        bool timeout = millis() - _asyncStartMs >= 10000;
        if (ntpOk)
            LOG_I("wifi", "NTP sincronizado");
        if (timeout && !ntpOk)
            LOG_W("wifi", "NTP timeout");
        if (ntpOk || timeout)
            startBgFetch();
        break;
    }

    case ASYNC_BG_FETCHING:
        if (bgNetworkIsDone())
        {
            if (_asyncOut)
                bgNetworkConsume(*_asyncOut);
            // Inicializa OTA quando WiFi está ativo com keep-alive (item #21)
            if (_keepAlive)
                otaInit();
            wifiOff();
            _state = ASYNC_IDLE;
            _firstFetch = false;
            _lastFetchMs = millis();
            _failCount = 0;
            _bgJustCompleted = true;
            LOG_I("wifi", "Background fetch concluido — dados atualizados");
        }
        break;
    }
}

// ── API pública ──────────────────────────────────────────────────────────────
void wifiCheckPortal()
{
    if (_portalMode || _calendarConfigMode || _webConfigMode)
        return;
    if (_failCount >= WIFI_PORTAL_FAIL_THRESHOLD)
    {
        LOG_W("wifi", "%d falhas consecutivas — iniciando portal cativo", _failCount);
        startPortal();
    }
}

void wifiBeginAsync(WeatherData &out)
{
    if (_state != ASYNC_IDLE || _portalMode || _calendarConfigMode)
        return;
    _asyncOut = &out;
    _asyncStartMs = millis();

    loadCredentials(); // atualiza possíveis credenciais NVS

    if (WiFi.status() == WL_CONNECTED)
    {
        LOG_I("wifi", "Ja conectado — buscando clima");
        startBgFetch();
        return;
    }

    _state = ASYNC_CONNECTING;
    WiFi.mode(WIFI_STA);
    WiFi.begin(getSSID(), getPass());
    LOG_I("wifi", "Conectando (async) a %s...", getSSID());
}

bool wifiConnectAndFetch(WeatherData &out)
{
    if (_portalMode || _calendarConfigMode || _webConfigMode)
        return false;
    loadCredentials();

    if (_progressCb)
        _progressCb("Conectando WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(getSSID(), getPass());
    LOG_I("wifi", "Conectando a %s...", getSSID());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
        delay(250);

    if (WiFi.status() != WL_CONNECTED)
    {
        LOG_W("wifi", "Timeout — sem conexao");
        _failCount++;
        if (_progressCb)
            _progressCb("WiFi falhou");
        wifiOff();
        return false;
    }
    LOG_I("wifi", "Conectado — RSSI %d dBm  IP %s",
          WiFi.RSSI(), WiFi.localIP().toString().c_str());
    _failCount = 0;

    if (_progressCb)
        _progressCb("Sincronizando relogio...");
    configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
    struct tm timeinfo;
    bool ntpOk = false;
    for (int i = 0; i < 20 && !ntpOk; i++)
    {
        ntpOk = getLocalTime(&timeinfo);
        if (!ntpOk)
            delay(500);
    }
    if (ntpOk)
        LOG_I("wifi", "NTP sincronizado");
    else
        LOG_W("wifi", "NTP timeout");

    if (_progressCb)
        _progressCb("Buscando clima...");
    bool weatherOk = weatherFetch(out);
    if (!weatherOk)
    {
        LOG_W("wifi", "Clima indisponivel — mantendo ultimo dado valido");
    } else {
        notifMarkWeatherFetch();  // telemetria /health
    }
    if (calendarHasFeedConfigured())
    {
        if (_progressCb)
            _progressCb("Buscando calendario...");
        if (!calendarFetchToday())
        {
            LOG_W("wifi", "Calendario indisponivel — mantendo ultimo dado valido");
        }
    }
    wifiOff();
    _firstFetch = false;
    _lastFetchMs = millis();
    return true;
}

void wifiSetProgressCallback(WifiProgressCb cb)
{
    _progressCb = cb;
}

void wifiInit(WeatherData &weatherData)
{
    loadCredentials();
    if (!wifiHasStoredCredentials())
    {
        LOG_W("wifi", "Sem credenciais — iniciando portal cativo");
        if (_progressCb)
            _progressCb("Sem credenciais WiFi");
        startPortal();
        return;
    }
    bool ok = wifiConnectAndFetch(weatherData);
    if (!ok)
    {
        LOG_W("wifi", "Falha na conexao no cold boot — iniciando portal cativo");
        startPortal();
    }
    _progressCb = nullptr; // limpa callback após init
}

void wifiScheduleUpdate(WeatherData &weatherData)
{
    if (_portalMode || _calendarConfigMode || _webConfigMode)
        return;
    if (_state != ASYNC_IDLE)
    {
        _asyncOut = &weatherData;
        pollAsync();
        return;
    }
    if (_firstFetch)
        return;
    if (millis() - _lastFetchMs >= _updateIntervalMs)
    {
        LOG_I("wifi", "Atualizacao programada do clima");
        wifiBeginAsync(weatherData);
    }
}

void wifiForceRefresh(WeatherData &weatherData)
{
    if (_state != ASYNC_IDLE || _calendarConfigMode || _webConfigMode)
        return; // já buscando
    LOG_I("wifi", "Atualizacao forcada pelo usuario");
    wifiBeginAsync(weatherData);
}

bool wifiIsFetching() { return _state != ASYNC_IDLE; }

bool wifiBgJustCompleted()
{
    if (_bgJustCompleted)
    {
        _bgJustCompleted = false;
        return true;
    }
    return false;
}
bool wifiIsPortalMode() { return _portalMode; }
bool wifiIsCalendarConfigMode() { return _calendarConfigMode; }

void wifiPortalUpdate()
{
    if (_portalMode && _dnsServer)
        _dnsServer->processNextRequest();
    if ((_portalMode || _calendarConfigMode || _webConfigMode) && _webServer)
        _webServer->handleClient();
    if (_calendarConfigMode && _calendarStopPending && millis() >= _calendarStopAtMs)
    {
        stopWebUi();
        if (!_keepAlive)
            wifiOff();
        LOG_I("wifi", "Config iCal encerrada");
    }
    if (_webConfigMode && _webConfigRestartPending && millis() >= _webConfigRestartAtMs)
    {
        LOG_I("wifi", "Reiniciando dispositivo apos webConfig");
        delay(100);
        ESP.restart();
    }
}

void wifiSetKeepAlive(bool keep)
{
    _keepAlive = keep;
    if (keep && WiFi.status() == WL_CONNECTED)
        otaInit(); // ativa OTA
    if (!keep && _state == ASYNC_IDLE && WiFi.status() == WL_CONNECTED)
    {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        LOG_I("wifi", "WiFi OFF (keep-alive liberado)");
    }
}

bool wifiIsKeepAlive() { return _keepAlive; }

int wifiGetRSSI()
{
    if (WiFi.status() != WL_CONNECTED)
        return 0;
    return WiFi.RSSI();
}

void wifiSetUpdateInterval(uint32_t ms)
{
    _updateIntervalMs = (ms > 0) ? ms : WEATHER_UPDATE_INTERVAL_MS;
    LOG_I("wifi", "Intervalo de atualizacao -> %lu ms", (unsigned long)_updateIntervalMs);
}

bool wifiStartCalendarConfig()
{
    if (_portalMode || _calendarConfigMode || _webConfigMode)
        return false;
    if (_state != ASYNC_IDLE)
    {
        LOG_W("wifi", "Config iCal bloqueada durante atualizacao");
        return false;
    }
    if (!ensureStaConnectedForUi())
        return false;
    startCalendarConfigServer();
    return true;
}

void wifiStopCalendarConfig()
{
    if (!_calendarConfigMode)
        return;
    stopWebUi();
    if (!_keepAlive)
        wifiOff();
    LOG_I("wifi", "Config iCal encerrada manualmente");
}

void wifiGetCalendarConfigAddress(char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return;
    strlcpy(out, _calendarConfigAddress, outSize);
}

// ── Portal unificado de configuracao via celular ─────────────────────────────
static String webCfgEscape(const char *input)
{
    String out;
    for (const char *p = input; p && *p; p++)
    {
        switch (*p)
        {
            case '&': out += F("&amp;");  break;
            case '"': out += F("&quot;"); break;
            case '<': out += F("&lt;");   break;
            case '>': out += F("&gt;");   break;
            case '\'': out += F("&#39;"); break;
            default: out += *p;
        }
    }
    return out;
}

static String webCfgJsonEscape(const char *input)
{
    String out;
    for (const char *p = input; p && *p; p++)
    {
        unsigned char c = (unsigned char)*p;
        switch (c)
        {
            case '\\': out += F("\\\\"); break;
            case '"':  out += F("\\\""); break;
            case '\n': out += F("\\n");  break;
            case '\r': out += F("\\r");  break;
            case '\t': out += F("\\t");  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

static String webCfgBuildStateJson()
{
    RuntimeConfig *cfg = runtimeConfigLive();
    char calUrl[192];
    runtimeConfigGetCalendarUrl(calUrl, sizeof(calUrl));

    String json;
    json.reserve(900);
    json += F("{");
    if (cfg) {
        json += F("\"wifiKeepAlive\":");        json += (cfg->wifiKeepAlive       ? F("true") : F("false"));
        json += F(",\"weatherIntervalMin\":");  json += cfg->weatherIntervalMin;
        json += F(",\"brightnessActive\":");    json += cfg->brightnessActive;
        json += F(",\"dimTimeoutSec\":");       json += cfg->dimTimeoutSec;
        json += F(",\"autoBrightness\":");      json += (cfg->autoBrightness      ? F("true") : F("false"));
        json += F(",\"deepSleepTimeoutMin\":"); json += cfg->deepSleepTimeoutMin;
        json += F(",\"accelWake\":");           json += (cfg->accelWake           ? F("true") : F("false"));
        json += F(",\"voiceEnabled\":");        json += (cfg->voiceEnabled        ? F("true") : F("false"));
        json += F(",\"nightMode\":");           json += (cfg->nightMode           ? F("true") : F("false"));
        json += F(",\"mqttEnabled\":");         json += (cfg->mqttEnabled         ? F("true") : F("false"));
        json += F(",\"mqttHost\":\"");          json += webCfgJsonEscape(cfg->mqttHost);  json += F("\"");
        json += F(",\"mqttPort\":");            json += cfg->mqttPort;
        json += F(",\"mqttUser\":\"");          json += webCfgJsonEscape(cfg->mqttUser);  json += F("\"");
        json += F(",\"mqttPass\":\"");          json += webCfgJsonEscape(cfg->mqttPass);  json += F("\"");
        json += F(",\"mqttTopic\":\"");         json += webCfgJsonEscape(cfg->mqttTopic); json += F("\"");
        json += F(",\"timerLabels\":[");
        for (int i = 0; i < MAX_TIMERS; i++) {
            if (i) json += ',';
            json += cfg->timerLabelPreset[i];
        }
        json += F("]");
    } else {
        json += F("\"error\":\"no-config\"");
    }
    json += F(",\"calendarUrl\":\"");
    json += webCfgJsonEscape(calUrl);
    json += F("\",\"timerPresets\":[");
    int presetCount = screenHomeGetTimerLabelPresetCount();
    for (int i = 0; i < presetCount; i++) {
        if (i) json += ',';
        json += F("\"");
        json += webCfgJsonEscape(screenHomeGetTimerLabelPresetName(i));
        json += F("\"");
    }
    json += F("],\"ip\":\"");
    json += WiFi.localIP().toString();
    json += F("\",\"ssid\":\"");
    json += webCfgJsonEscape(WiFi.SSID().c_str());
    json += F("\"}");
    return json;
}

// HTML principal — layout moderno, escuro, mobile-first. Todos os campos sao
// controlados via JS que sincroniza com /api/state (GET) e /api/save (POST JSON).
static const char WEBCFG_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pt-BR"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0f172a">
<title>Cubinho</title>
<style>
:root{--bg:#0b1020;--card:#151b2e;--card2:#1f2740;--line:#283150;--text:#e6ebff;--muted:#8892b5;--accent:#fd8a20;--accent2:#f59e0b;--ok:#22c55e;--bad:#ef4444;}
*{box-sizing:border-box}
html,body{margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;background:linear-gradient(180deg,#0b1020 0%,#0a0f1c 100%);color:var(--text);min-height:100dvh;padding:24px 16px 40px;padding-top:max(24px,env(safe-area-inset-top));padding-bottom:max(40px,env(safe-area-inset-bottom))}
.wrap{max-width:560px;margin:0 auto}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:20px}
h1{font-size:22px;margin:0;font-weight:700;letter-spacing:-.01em}
h1 span{color:var(--accent)}
.pill{font-size:12px;background:var(--card2);color:var(--muted);padding:6px 10px;border-radius:999px;border:1px solid var(--line);white-space:nowrap}
.card{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:18px;margin-bottom:16px;box-shadow:0 12px 40px -20px rgba(0,0,0,.6)}
.card h2{margin:0 0 4px;font-size:14px;color:var(--accent);text-transform:uppercase;letter-spacing:.08em;font-weight:700}
.card p.desc{margin:0 0 14px;color:var(--muted);font-size:13px;line-height:1.5}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:12px 0;border-top:1px solid var(--line)}
.row:first-of-type{border-top:none}
.row label{flex:1;min-width:0}
.row label .lbl{display:block;font-size:15px;font-weight:600}
.row label .sub{display:block;font-size:12px;color:var(--muted);margin-top:2px;line-height:1.4}
input[type=text],input[type=url],input[type=number],select{width:100%;padding:14px 14px;background:#0a1023;color:var(--text);border:1px solid var(--line);border-radius:12px;font-size:16px;outline:none;transition:border-color .15s}
input:focus,select:focus{border-color:var(--accent)}
select{appearance:none;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='8' viewBox='0 0 12 8'%3E%3Cpath fill='%23fd8a20' d='M6 8 0 0h12z'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 14px center;padding-right:36px}
.toggle{position:relative;width:52px;height:30px;flex-shrink:0;cursor:pointer}
.toggle input{position:absolute;inset:0;opacity:0;cursor:pointer;margin:0}
.toggle .track{position:absolute;inset:0;background:#2a3254;border-radius:999px;transition:background .2s}
.toggle .thumb{position:absolute;top:3px;left:3px;width:24px;height:24px;background:#fff;border-radius:50%;transition:transform .2s;box-shadow:0 2px 6px rgba(0,0,0,.4)}
.toggle input:checked ~ .track{background:var(--accent)}
.toggle input:checked ~ .thumb{transform:translateX(22px)}
.range{display:flex;align-items:center;gap:10px;flex-shrink:0}
.range input[type=range]{-webkit-appearance:none;appearance:none;width:140px;height:6px;background:#2a3254;border-radius:3px;outline:none}
.range input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;background:var(--accent);border-radius:50%;cursor:pointer;border:0}
.range input[type=range]::-moz-range-thumb{width:22px;height:22px;background:var(--accent);border-radius:50%;cursor:pointer;border:0}
.range .v{min-width:44px;text-align:right;font-variant-numeric:tabular-nums;font-weight:600;color:var(--accent)}
.field{width:100%}
.field .fieldrow{margin-top:10px}
button{font-family:inherit}
.btn{display:block;width:100%;padding:16px;background:var(--accent);color:#0b1020;border:0;border-radius:14px;font-size:16px;font-weight:700;cursor:pointer;text-align:center;text-decoration:none;transition:transform .1s,background .15s}
.btn:hover{background:var(--accent2)}
.btn:active{transform:scale(.98)}
.btn.ghost{background:transparent;color:var(--text);border:1px solid var(--line)}
.btn.danger{background:transparent;color:var(--bad);border:1px solid rgba(239,68,68,.4)}
.btn.danger:hover{background:rgba(239,68,68,.1)}
.save-bar{position:sticky;bottom:calc(env(safe-area-inset-bottom) + 12px);margin-top:24px;z-index:10}
.toast{position:fixed;left:50%;bottom:calc(env(safe-area-inset-bottom) + 24px);transform:translate(-50%,40px);background:var(--ok);color:#0b1020;padding:12px 20px;border-radius:999px;font-weight:700;box-shadow:0 8px 24px rgba(0,0,0,.4);opacity:0;transition:transform .3s,opacity .3s;pointer-events:none;z-index:20}
.toast.show{opacity:1;transform:translate(-50%,0)}
.toast.err{background:var(--bad);color:#fff}
.ip{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;color:var(--accent);font-weight:700}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px}
footer{margin-top:28px;text-align:center;color:var(--muted);font-size:12px;line-height:1.6}
footer a{color:var(--accent)}
.hidden{display:none!important}
</style></head>
<body><div class="wrap">
<header>
  <h1>Cubinho<span>.</span></h1>
  <span class="pill" id="ip">--</span>
</header>

<div class="card">
  <h2>Display</h2>
  <p class="desc">Brilho, timeout e modo noturno.</p>
  <div class="row">
    <label><span class="lbl">Brilho ativo</span><span class="sub">0 = mais escuro, 255 = maximo</span></label>
    <div class="range"><input type="range" id="brightnessActive" min="10" max="255" step="5"><span class="v" id="brightnessActive_v">0</span></div>
  </div>
  <div class="row">
    <label for="autoBrightness"><span class="lbl">Auto-brilho</span><span class="sub">Ajusta pela luz ambiente</span></label>
    <span class="toggle"><input type="checkbox" id="autoBrightness"><span class="track"></span><span class="thumb"></span></span>
  </div>
  <div class="row">
    <label for="nightMode"><span class="lbl">Modo noturno</span><span class="sub">Brilho minimo, sem auto-brilho</span></label>
    <span class="toggle"><input type="checkbox" id="nightMode"><span class="track"></span><span class="thumb"></span></span>
  </div>
  <div class="row">
    <label for="dimTimeoutSec"><span class="lbl">Tempo ate dim</span><span class="sub">Inatividade antes de reduzir brilho</span></label>
    <select id="dimTimeoutSec">
      <option value="15">15 s</option><option value="30">30 s</option>
      <option value="60">1 min</option><option value="120">2 min</option>
      <option value="300">5 min</option>
    </select>
  </div>
</div>

<div class="card">
  <h2>Energia</h2>
  <p class="desc">Deep sleep e wake por movimento.</p>
  <div class="row">
    <label for="deepSleepTimeoutMin"><span class="lbl">Deep sleep</span><span class="sub">Suspende apos periodo em dim</span></label>
    <select id="deepSleepTimeoutMin">
      <option value="0">Nunca</option><option value="2">2 min</option>
      <option value="5">5 min</option><option value="10">10 min</option>
      <option value="30">30 min</option><option value="60">1 hora</option>
    </select>
  </div>
  <div class="row">
    <label for="accelWake"><span class="lbl">Acordar por movimento</span><span class="sub">Acelerometro acorda do dim</span></label>
    <span class="toggle"><input type="checkbox" id="accelWake"><span class="track"></span><span class="thumb"></span></span>
  </div>
</div>

<div class="card">
  <h2>Rede</h2>
  <p class="desc">WiFi permanente mantem OTA e Telnet disponiveis.</p>
  <div class="row">
    <label for="wifiKeepAlive"><span class="lbl">WiFi permanente</span><span class="sub">Conectado em <span class="ip" id="ssid">--</span></span></label>
    <span class="toggle"><input type="checkbox" id="wifiKeepAlive"><span class="track"></span><span class="thumb"></span></span>
  </div>
  <div class="row">
    <label for="weatherIntervalMin"><span class="lbl">Atualizacao do clima</span><span class="sub">Com que frequencia buscar dados</span></label>
    <select id="weatherIntervalMin">
      <option value="15">15 min</option><option value="30">30 min</option>
      <option value="60">1 hora</option><option value="120">2 horas</option>
    </select>
  </div>
</div>

<div class="card">
  <h2>Notificacoes MQTT</h2>
  <p class="desc">Recebe notificacoes publicadas em um broker MQTT. Requer WiFi permanente.</p>
  <div class="row">
    <label for="mqttEnabled"><span class="lbl">Ativar MQTT</span><span class="sub">Conecta ao broker e inscreve no topico</span></label>
    <span class="toggle"><input type="checkbox" id="mqttEnabled"><span class="track"></span><span class="thumb"></span></span>
  </div>
  <div class="row field">
    <label><span class="lbl">Host</span><span class="sub">Ex: broker.hivemq.com ou 192.168.1.10</span></label>
  </div>
  <div class="fieldrow"><input type="text" id="mqttHost" placeholder="broker.exemplo.com" autocomplete="off" spellcheck="false"></div>
  <div class="row field">
    <label><span class="lbl">Porta</span><span class="sub">Padrao 1883 (sem TLS)</span></label>
  </div>
  <div class="fieldrow"><input type="number" id="mqttPort" min="1" max="65535" placeholder="1883"></div>
  <div class="row field">
    <label><span class="lbl">Topico</span><span class="sub">Ex: cubinho/notif</span></label>
  </div>
  <div class="fieldrow"><input type="text" id="mqttTopic" placeholder="cubinho/notif" autocomplete="off" spellcheck="false"></div>
  <div class="row field">
    <label><span class="lbl">Usuario</span><span class="sub">Opcional</span></label>
  </div>
  <div class="fieldrow"><input type="text" id="mqttUser" autocomplete="off" spellcheck="false"></div>
  <div class="row field">
    <label><span class="lbl">Senha</span><span class="sub">Opcional</span></label>
  </div>
  <div class="fieldrow"><input type="text" id="mqttPass" autocomplete="off" spellcheck="false"></div>
</div>

<div class="card">
  <h2>Calendario</h2>
  <p class="desc">URL privada de um feed iCal/ICS. Deixe em branco para desativar.</p>
  <div class="row field">
    <label><span class="lbl">URL do feed</span><span class="sub">Google Agenda: &ldquo;secret&rdquo; da agenda privada</span></label>
  </div>
  <div class="fieldrow"><input type="url" id="calendarUrl" placeholder="https://.../basic.ics" autocomplete="off" spellcheck="false"></div>
</div>

<div class="card">
  <h2>Timers</h2>
  <p class="desc">Nome mostrado no tab de cada slot.</p>
  <div class="row">
    <label for="t0"><span class="lbl">T1</span></label>
    <select id="t0" class="timerSel"></select>
  </div>
  <div class="row">
    <label for="t1"><span class="lbl">T2</span></label>
    <select id="t1" class="timerSel"></select>
  </div>
  <div class="row">
    <label for="t2"><span class="lbl">T3</span></label>
    <select id="t2" class="timerSel"></select>
  </div>
</div>

<div class="card">
  <h2>Extras</h2>
  <div class="row">
    <label for="voiceEnabled"><span class="lbl">Comando por voz</span><span class="sub">Segurar cabecalho para ouvir</span></label>
    <span class="toggle"><input type="checkbox" id="voiceEnabled"><span class="track"></span><span class="thumb"></span></span>
  </div>
</div>

<div class="save-bar"><button type="button" class="btn" id="saveBtn">Salvar</button></div>

<div class="card">
  <h2>Atualizacao de firmware</h2>
  <p class="desc">Envie o binario compilado (<code>.pio/build/m5stack-cores3/firmware.bin</code>). O aparelho reinicia automaticamente ao final.</p>
  <form id="otaForm" enctype="multipart/form-data">
    <input type="file" id="otaFile" name="firmware" accept=".bin" style="color:var(--muted);padding:10px 0;width:100%">
    <div id="otaProgress" class="hidden" style="margin-top:12px">
      <div style="background:#2a3254;border-radius:6px;height:10px;overflow:hidden"><div id="otaBar" style="background:var(--accent);height:100%;width:0%;transition:width .2s"></div></div>
      <p id="otaStatus" class="desc" style="margin-top:8px">Enviando...</p>
    </div>
    <button type="submit" class="btn" id="otaBtn" style="margin-top:14px">Enviar firmware</button>
  </form>
</div>

<div class="card">
  <h2>Manutencao</h2>
  <div class="grid2">
    <button type="button" class="btn ghost" id="restartBtn">Reiniciar</button>
    <button type="button" class="btn danger" id="factoryBtn">Reset de fabrica</button>
  </div>
  <p class="desc" style="margin-top:14px;margin-bottom:0">Reset apaga credenciais WiFi e todas as configuracoes. O aparelho abrira o portal de setup.</p>
</div>

<footer>Pagina ativa apenas enquanto o modo estiver aberto no aparelho.<br>Toque na tela do Cubinho para fechar.</footer>

<div class="toast" id="toast">Salvo!</div>
</div>

<script>
const $ = s => document.querySelector(s);
const state = {};

function toast(msg, err){
  const t = $('#toast');
  t.textContent = msg;
  t.classList.toggle('err', !!err);
  t.classList.add('show');
  clearTimeout(toast._t);
  toast._t = setTimeout(()=>t.classList.remove('show'), 2200);
}

function bindToggle(id){
  const el = $('#'+id);
  el.checked = !!state[id];
}
function bindNumber(id){
  const el = $('#'+id);
  el.value = state[id];
  const v = $('#'+id+'_v');
  if (v) v.textContent = state[id];
  el.addEventListener('input', ()=>{ if (v) v.textContent = el.value; });
}
function bindSelect(id){
  const el = $('#'+id);
  el.value = String(state[id]);
}

function populateTimerSelect(sel, presets, currentIdx){
  sel.innerHTML = '';
  presets.forEach((name, i)=>{
    const opt = document.createElement('option');
    opt.value = i; opt.textContent = name;
    if (i === currentIdx) opt.selected = true;
    sel.appendChild(opt);
  });
}

async function load(){
  try {
    const r = await fetch('/api/state');
    const d = await r.json();
    Object.assign(state, d);
    $('#ip').textContent = d.ip || '--';
    $('#ssid').textContent = d.ssid || '--';
    bindNumber('brightnessActive');
    bindToggle('autoBrightness');
    bindToggle('nightMode');
    bindSelect('dimTimeoutSec');
    bindSelect('deepSleepTimeoutMin');
    bindToggle('accelWake');
    bindToggle('wifiKeepAlive');
    bindSelect('weatherIntervalMin');
    bindToggle('voiceEnabled');
    bindToggle('mqttEnabled');
    $('#mqttHost').value  = d.mqttHost  || '';
    $('#mqttPort').value  = d.mqttPort  || 1883;
    $('#mqttUser').value  = d.mqttUser  || '';
    $('#mqttPass').value  = d.mqttPass  || '';
    $('#mqttTopic').value = d.mqttTopic || '';
    $('#calendarUrl').value = d.calendarUrl || '';
    const presets = d.timerPresets || [];
    for (let i=0;i<3;i++){
      populateTimerSelect($('#t'+i), presets, d.timerLabels ? d.timerLabels[i] : 0);
    }
  } catch(e){
    toast('Erro ao carregar', true);
  }
}

function collect(){
  const v = id => $('#'+id);
  const n = id => parseInt(v(id).value, 10);
  const b = id => v(id).checked;
  return {
    brightnessActive: n('brightnessActive'),
    autoBrightness: b('autoBrightness'),
    nightMode: b('nightMode'),
    dimTimeoutSec: n('dimTimeoutSec'),
    deepSleepTimeoutMin: n('deepSleepTimeoutMin'),
    accelWake: b('accelWake'),
    wifiKeepAlive: b('wifiKeepAlive'),
    weatherIntervalMin: n('weatherIntervalMin'),
    voiceEnabled: b('voiceEnabled'),
    mqttEnabled: b('mqttEnabled'),
    mqttHost: $('#mqttHost').value.trim(),
    mqttPort: n('mqttPort') || 1883,
    mqttUser: $('#mqttUser').value,
    mqttPass: $('#mqttPass').value,
    mqttTopic: $('#mqttTopic').value.trim(),
    calendarUrl: $('#calendarUrl').value.trim(),
    timerLabels: [n('t0'), n('t1'), n('t2')]
  };
}

async function save(){
  const btn = $('#saveBtn');
  btn.disabled = true; btn.textContent = 'Salvando...';
  try {
    const r = await fetch('/api/save', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(collect())});
    if (!r.ok) throw new Error('http '+r.status);
    toast('Salvo!');
  } catch(e){
    toast('Falha ao salvar', true);
  } finally {
    btn.disabled = false; btn.textContent = 'Salvar';
  }
}

async function doAction(path, confirmMsg){
  if (confirmMsg && !confirm(confirmMsg)) return;
  try {
    await fetch(path, {method:'POST'});
    toast('Enviado');
  } catch(e){
    toast('Falha', true);
  }
}

$('#saveBtn').addEventListener('click', save);
$('#restartBtn').addEventListener('click', ()=>doAction('/api/restart', 'Reiniciar o Cubinho?'));
$('#factoryBtn').addEventListener('click', ()=>doAction('/api/factory', 'Apagar todas as configuracoes e credenciais WiFi?'));

$('#otaForm').addEventListener('submit', function(e){
  e.preventDefault();
  const f = $('#otaFile').files[0];
  if (!f) { toast('Escolha o arquivo .bin', true); return; }
  if (!confirm('Enviar firmware e reiniciar?')) return;
  const prog = $('#otaProgress'); const bar = $('#otaBar'); const st = $('#otaStatus');
  const btn = $('#otaBtn');
  prog.classList.remove('hidden'); btn.disabled = true; btn.textContent = 'Enviando...';
  const fd = new FormData(); fd.append('firmware', f, f.name);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/ota', true);
  xhr.upload.onprogress = function(ev){
    if (!ev.lengthComputable) return;
    const p = Math.round(ev.loaded * 100 / ev.total);
    bar.style.width = p + '%'; st.textContent = 'Enviando... ' + p + '%';
  };
  xhr.onload = function(){
    if (xhr.status === 200) {
      st.textContent = 'Sucesso! Reiniciando...'; bar.style.width = '100%'; toast('Firmware atualizado');
    } else {
      st.textContent = 'Falha: ' + xhr.responseText; toast('Falha no upload', true);
      btn.disabled = false; btn.textContent = 'Enviar firmware';
    }
  };
  xhr.onerror = function(){
    st.textContent = 'Erro de rede'; toast('Falha no upload', true);
    btn.disabled = false; btn.textContent = 'Enviar firmware';
  };
  xhr.send(fd);
});

load();
</script>
</body></html>)rawliteral";

// Parser de JSON minimalista para o payload esperado (chaves conhecidas,
// valores booleanos/inteiros/string simples). Retorna true se pelo menos uma
// chave foi reconhecida.
static bool webCfgParseBool(const String &body, const char *key, bool &out)
{
    String needle = String("\"") + key + "\"";
    int k = body.indexOf(needle);
    if (k < 0) return false;
    int colon = body.indexOf(':', k);
    if (colon < 0) return false;
    int i = colon + 1;
    while (i < (int)body.length() && (body[i] == ' ' || body[i] == '\t')) i++;
    if (body.startsWith("true", i))  { out = true;  return true; }
    if (body.startsWith("false", i)) { out = false; return true; }
    return false;
}

static bool webCfgParseInt(const String &body, const char *key, int &out)
{
    String needle = String("\"") + key + "\"";
    int k = body.indexOf(needle);
    if (k < 0) return false;
    int colon = body.indexOf(':', k);
    if (colon < 0) return false;
    int i = colon + 1;
    while (i < (int)body.length() && (body[i] == ' ' || body[i] == '\t')) i++;
    int start = i;
    if (i < (int)body.length() && (body[i] == '-' || body[i] == '+')) i++;
    while (i < (int)body.length() && isDigit(body[i])) i++;
    if (i == start) return false;
    out = body.substring(start, i).toInt();
    return true;
}

static bool webCfgParseString(const String &body, const char *key, String &out)
{
    String needle = String("\"") + key + "\"";
    int k = body.indexOf(needle);
    if (k < 0) return false;
    int colon = body.indexOf(':', k);
    if (colon < 0) return false;
    int q = body.indexOf('"', colon + 1);
    if (q < 0) return false;
    out = "";
    int i = q + 1;
    while (i < (int)body.length()) {
        char c = body[i];
        if (c == '\\' && i + 1 < (int)body.length()) {
            char n = body[i+1];
            switch (n) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case '"': out += '"';  break;
                case '\\': out += '\\'; break;
                case '/': out += '/';  break;
                default:  out += n;    break;
            }
            i += 2;
        } else if (c == '"') {
            return true;
        } else {
            out += c;
            i++;
        }
    }
    return false;
}

static void webCfgParseIntArray(const String &body, const char *key, int *outArr, int maxLen)
{
    String needle = String("\"") + key + "\"";
    int k = body.indexOf(needle);
    if (k < 0) return;
    int open = body.indexOf('[', k);
    if (open < 0) return;
    int close = body.indexOf(']', open);
    if (close < 0) return;
    int i = open + 1, n = 0;
    while (i < close && n < maxLen) {
        while (i < close && (body[i] == ' ' || body[i] == ',' || body[i] == '\t')) i++;
        int start = i;
        if (i < close && (body[i] == '-' || body[i] == '+')) i++;
        while (i < close && isDigit(body[i])) i++;
        if (i > start) {
            outArr[n++] = body.substring(start, i).toInt();
        } else {
            break;
        }
    }
}

static void startWebConfigServer()
{
    stopWebUi();

    _webServer = new WebServer(80);
    _webServer->on("/", HTTP_GET, []() {
        _webServer->send_P(200, "text/html; charset=utf-8", WEBCFG_HTML);
    });
    _webServer->on("/api/state", HTTP_GET, []() {
        _webServer->sendHeader("Cache-Control", "no-store");
        _webServer->send(200, "application/json", webCfgBuildStateJson());
    });
    _webServer->on("/api/save", HTTP_POST, []() {
        RuntimeConfig *cfg = runtimeConfigLive();
        if (!cfg) {
            _webServer->send(500, "application/json", "{\"ok\":false,\"error\":\"no-config\"}");
            return;
        }
        String body = _webServer->arg("plain");
        bool  b;
        int   i;
        String s;
        if (webCfgParseBool(body, "wifiKeepAlive", b))        cfg->wifiKeepAlive = b;
        if (webCfgParseBool(body, "autoBrightness", b))       cfg->autoBrightness = b;
        if (webCfgParseBool(body, "nightMode", b))            cfg->nightMode = b;
        if (webCfgParseBool(body, "accelWake", b))            cfg->accelWake = b;
        if (webCfgParseBool(body, "voiceEnabled", b))         cfg->voiceEnabled = b;
        if (webCfgParseInt (body, "weatherIntervalMin", i))   cfg->weatherIntervalMin  = constrain(i, 1, 1440);
        if (webCfgParseInt (body, "brightnessActive", i))     cfg->brightnessActive    = constrain(i, 0, 255);
        if (webCfgParseInt (body, "dimTimeoutSec", i))        cfg->dimTimeoutSec       = constrain(i, 5, 3600);
        if (webCfgParseInt (body, "deepSleepTimeoutMin", i))  cfg->deepSleepTimeoutMin = constrain(i, 0, 1440);

        if (webCfgParseBool  (body, "mqttEnabled", b))        cfg->mqttEnabled = b;
        if (webCfgParseInt   (body, "mqttPort", i))           cfg->mqttPort    = constrain(i, 1, 65535);
        if (webCfgParseString(body, "mqttHost", s))  { s.trim(); strlcpy(cfg->mqttHost,  s.c_str(), sizeof(cfg->mqttHost));  }
        if (webCfgParseString(body, "mqttUser", s))  {          strlcpy(cfg->mqttUser,  s.c_str(), sizeof(cfg->mqttUser));  }
        if (webCfgParseString(body, "mqttPass", s))  {          strlcpy(cfg->mqttPass,  s.c_str(), sizeof(cfg->mqttPass));  }
        if (webCfgParseString(body, "mqttTopic", s)) { s.trim(); strlcpy(cfg->mqttTopic, s.c_str(), sizeof(cfg->mqttTopic)); }

        int labels[MAX_TIMERS];
        for (int j = 0; j < MAX_TIMERS; j++) labels[j] = cfg->timerLabelPreset[j];
        webCfgParseIntArray(body, "timerLabels", labels, MAX_TIMERS);
        int presetCount = screenHomeGetTimerLabelPresetCount();
        for (int j = 0; j < MAX_TIMERS; j++) {
            if (labels[j] >= 0 && labels[j] < presetCount)
                cfg->timerLabelPreset[j] = labels[j];
        }

        if (webCfgParseString(body, "calendarUrl", s)) {
            s.trim();
            runtimeConfigSaveCalendarUrl(s.c_str());
        }

        runtimeConfigSave(*cfg);
        runtimeConfigApply(*cfg);
        LOG_I("wifi", "WebConfig: configuracoes salvas via celular");

        _webServer->sendHeader("Cache-Control", "no-store");
        _webServer->send(200, "application/json", "{\"ok\":true}");
    });
    _webServer->on("/api/restart", HTTP_POST, []() {
        _webServer->send(200, "application/json", "{\"ok\":true}");
        _webConfigRestartPending = true;
        _webConfigRestartAtMs = millis() + 800;
    });
    _webServer->on("/api/factory", HTTP_POST, []() {
        _webServer->send(200, "application/json", "{\"ok\":true}");
        wifiClearStoredCredentials();
        runtimeConfigClear();
        runtimeConfigSaveCalendarUrl("");
        _webConfigRestartPending = true;
        _webConfigRestartAtMs = millis() + 1200;
    });
    // OTA via navegador — upload multipart de firmware.bin.
    // Dois callbacks: o "response" roda apos o upload completo; o "upload"
    // recebe os chunks e alimenta o Update.
    _webServer->on("/api/ota", HTTP_POST,
        []() {
            // Response callback
            bool ok = !Update.hasError();
            _webServer->sendHeader("Connection", "close");
            _webServer->send(ok ? 200 : 500, "application/json",
                             ok ? "{\"ok\":true}" : "{\"ok\":false}");
            if (ok) {
                LOG_I("ota-web", "Upload concluido — reiniciando em 800ms");
                _webConfigRestartPending = true;
                _webConfigRestartAtMs = millis() + 800;
            }
        },
        []() {
            // Upload callback (chamado por chunk)
            HTTPUpload& up = _webServer->upload();
            if (up.status == UPLOAD_FILE_START) {
                LOG_I("ota-web", "Upload iniciado: %s", up.filename.c_str());
                // UPDATE_SIZE_UNKNOWN = aceita qualquer tamanho
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    LOG_E("ota-web", "Update.begin falhou");
                }
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (Update.write(up.buf, up.currentSize) != up.currentSize) {
                    LOG_E("ota-web", "Update.write falhou");
                }
            } else if (up.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    LOG_I("ota-web", "Upload OK: %u bytes", up.totalSize);
                } else {
                    LOG_E("ota-web", "Update.end falhou (err=%d)", Update.getError());
                }
            } else if (up.status == UPLOAD_FILE_ABORTED) {
                Update.end();
                LOG_W("ota-web", "Upload abortado");
            }
        }
    );
    _webServer->onNotFound([]() {
        _webServer->sendHeader("Location", "/");
        _webServer->send(302, "text/plain", "");
    });
    _webServer->begin();

    snprintf(_webConfigAddress, sizeof(_webConfigAddress), "http://%s",
             WiFi.localIP().toString().c_str());
    _webConfigMode = true;
    LOG_I("wifi", "WebConfig ativo em %s", _webConfigAddress);
}

bool wifiStartWebConfig()
{
    if (_portalMode || _calendarConfigMode || _webConfigMode)
        return false;
    if (_state != ASYNC_IDLE) {
        LOG_W("wifi", "WebConfig bloqueado durante atualizacao");
        return false;
    }
    if (!ensureStaConnectedForUi()) return false;
    startWebConfigServer();
    return true;
}

void wifiStopWebConfig()
{
    if (!_webConfigMode) return;
    stopWebUi();
    if (!_keepAlive) wifiOff();
    LOG_I("wifi", "WebConfig encerrado manualmente");
}

bool wifiIsWebConfigMode() { return _webConfigMode; }

void wifiGetWebConfigAddress(char *out, size_t outSize)
{
    if (!out || outSize == 0) return;
    strlcpy(out, _webConfigAddress, outSize);
}
