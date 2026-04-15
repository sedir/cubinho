#include "wifi_manager.h"
#include "calendar_feed.h"
#include "config.h"
#include "logger.h"
#include "ota_manager.h"
#include "runtime_config.h"
#include "bg_network.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
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
static DNSServer *_dnsServer = nullptr;
static WebServer *_webServer = nullptr;
static uint8_t _failCount = 0;
static bool _calendarStopPending = false;
static uint32_t _calendarStopAtMs = 0;
static char _calendarConfigAddress[40] = "";

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
    return _nvsSSID.length() > 0;
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
    if (_keepAlive || _calendarConfigMode)
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
    if (_portalMode || _calendarConfigMode)
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
    if (_portalMode || _calendarConfigMode)
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
    if (_portalMode || _calendarConfigMode)
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
    if (_state != ASYNC_IDLE || _calendarConfigMode)
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
    if ((_portalMode || _calendarConfigMode) && _webServer)
        _webServer->handleClient();
    if (_calendarConfigMode && _calendarStopPending && millis() >= _calendarStopAtMs)
    {
        stopWebUi();
        if (!_keepAlive)
            wifiOff();
        LOG_I("wifi", "Config iCal encerrada");
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
    if (_portalMode || _calendarConfigMode)
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
