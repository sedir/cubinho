#include "wifi_manager.h"
#include "config.h"
#include "logger.h"
#include "ota_manager.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

// ── Estado async ─────────────────────────────────────────────────────────────
enum AsyncState { ASYNC_IDLE, ASYNC_CONNECTING, ASYNC_NTP_SYNCING };

static AsyncState   _state        = ASYNC_IDLE;
static WeatherData* _asyncOut     = nullptr;
static uint32_t     _asyncStartMs = 0;
static bool         _firstFetch         = true;
static bool         _keepAlive          = false;
static uint32_t     _lastFetchMs        = 0;
static uint32_t     _updateIntervalMs   = WEATHER_UPDATE_INTERVAL_MS;

// ── Portal cativo (item #23) ─────────────────────────────────────────────────
static bool       _portalMode     = false;
static DNSServer* _dnsServer      = nullptr;
static WebServer* _webServer      = nullptr;
static uint8_t    _failCount      = 0;

static Preferences _prefs;
static String _nvsSSID;
static String _nvsPass;

static void loadCredentials() {
    _prefs.begin("wifi", true);  // read-only
    _nvsSSID = _prefs.getString("ssid", "");
    _nvsPass = _prefs.getString("pass", "");
    _prefs.end();
}

static void saveCredentials(const String& ssid, const String& pass) {
    _prefs.begin("wifi", false);
    _prefs.putString("ssid", ssid);
    _prefs.putString("pass", pass);
    _prefs.end();
    LOG_I("wifi", "Credenciais salvas em NVS");
}

bool wifiHasStoredCredentials() {
    loadCredentials();
    return _nvsSSID.length() > 0;
}

void wifiClearStoredCredentials() {
    _prefs.begin("wifi", false);
    _prefs.clear();
    _prefs.end();
    _nvsSSID = "";
    _nvsPass = "";
    LOG_I("wifi", "Credenciais NVS apagadas");
}

// Retorna SSID/senha do NVS — credenciais hardcoded foram removidas
static const char* getSSID() { return _nvsSSID.c_str(); }
static const char* getPass() { return _nvsPass.c_str(); }

// ── Portal HTML ──────────────────────────────────────────────────────────────
static const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cidinha Setup</title>
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
<h1>Cidinha Setup</h1>
<form method="POST" action="/save">
<label>SSID</label><input name="ssid" required>
<label>Senha</label><input name="pass" type="password" required>
<button type="submit">Salvar e Reiniciar</button>
</form></div></body></html>
)rawliteral";

static void startPortal() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_PORTAL_AP_NAME);
    delay(100);

    _dnsServer = new DNSServer();
    _dnsServer->start(53, "*", WiFi.softAPIP());

    _webServer = new WebServer(80);
    _webServer->on("/", HTTP_GET, []() {
        _webServer->send(200, "text/html", PORTAL_HTML);
    });
    _webServer->on("/save", HTTP_POST, []() {
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
        }
    });
    _webServer->onNotFound([]() {
        _webServer->sendHeader("Location", "http://192.168.4.1/");
        _webServer->send(302, "text/plain", "");
    });
    _webServer->begin();

    _portalMode = true;
    LOG_I("wifi", "Portal cativo ativo — AP: %s  IP: %s",
          WIFI_PORTAL_AP_NAME, WiFi.softAPIP().toString().c_str());
}

// ── Helpers ──────────────────────────────────────────────────────────────────
static void wifiOff() {
    if (_keepAlive) {
        LOG_I("wifi", "WiFi mantido ativo (Telnet/OTA)");
        return;
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    LOG_I("wifi", "WiFi OFF");
}

static void finishAsync() {
    if (_asyncOut) weatherFetch(*_asyncOut);
    // Inicializa OTA quando WiFi está ativo com keep-alive (item #21)
    if (_keepAlive) otaInit();
    wifiOff();
    _state       = ASYNC_IDLE;
    _firstFetch  = false;
    _lastFetchMs = millis();
    _failCount   = 0;  // reset falhas no sucesso
}

static void pollAsync() {
    switch (_state) {
        case ASYNC_IDLE: return;

        case ASYNC_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                struct tm _tmcheck;
                bool timeValid = getLocalTime(&_tmcheck, 0) && (time(nullptr) > 1577836800L);
                if (timeValid) {
                    LOG_I("wifi", "Conectado (RSSI %d dBm) — RTC valido", WiFi.RSSI());
                    finishAsync();
                } else {
                    LOG_I("wifi", "Conectado (RSSI %d dBm) — NTP sync", WiFi.RSSI());
                    configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
                    _asyncStartMs = millis();
                    _state = ASYNC_NTP_SYNCING;
                }
            } else if (millis() - _asyncStartMs >= 10000) {
                LOG_W("wifi", "Timeout de conexao (%d falha(s))", _failCount + 1);
                _failCount++;
                wifiOff();
                _state       = ASYNC_IDLE;
                _firstFetch  = false;
                _lastFetchMs = millis();
            }
            break;

        case ASYNC_NTP_SYNCING: {
            struct tm timeinfo;
            bool ntpOk  = getLocalTime(&timeinfo, 0);
            bool timeout = millis() - _asyncStartMs >= 10000;
            if (ntpOk)            LOG_I("wifi", "NTP sincronizado");
            if (timeout && !ntpOk) LOG_W("wifi", "NTP timeout");
            if (ntpOk || timeout)  finishAsync();
            break;
        }
    }
}

// ── API pública ──────────────────────────────────────────────────────────────
void wifiCheckPortal() {
    if (_portalMode) return;
    if (_failCount >= WIFI_PORTAL_FAIL_THRESHOLD) {
        LOG_W("wifi", "%d falhas consecutivas — iniciando portal cativo", _failCount);
        startPortal();
    }
}

void wifiBeginAsync(WeatherData& out) {
    if (_state != ASYNC_IDLE || _portalMode) return;
    _asyncOut     = &out;
    _asyncStartMs = millis();

    loadCredentials();  // atualiza possíveis credenciais NVS

    if (WiFi.status() == WL_CONNECTED) {
        LOG_I("wifi", "Ja conectado — buscando clima");
        finishAsync();
        return;
    }

    _state = ASYNC_CONNECTING;
    WiFi.mode(WIFI_STA);
    WiFi.begin(getSSID(), getPass());
    LOG_I("wifi", "Conectando (async) a %s...", getSSID());
}

bool wifiConnectAndFetch(WeatherData& out) {
    if (_portalMode) return false;
    loadCredentials();

    WiFi.mode(WIFI_STA);
    WiFi.begin(getSSID(), getPass());
    LOG_I("wifi", "Conectando a %s...", getSSID());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) delay(250);

    if (WiFi.status() != WL_CONNECTED) {
        LOG_W("wifi", "Timeout — sem conexao");
        _failCount++;
        wifiOff();
        return false;
    }
    LOG_I("wifi", "Conectado — RSSI %d dBm  IP %s",
          WiFi.RSSI(), WiFi.localIP().toString().c_str());
    _failCount = 0;

    configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
    struct tm timeinfo;
    bool ntpOk = false;
    for (int i = 0; i < 20 && !ntpOk; i++) {
        ntpOk = getLocalTime(&timeinfo);
        if (!ntpOk) delay(500);
    }
    if (ntpOk) LOG_I("wifi", "NTP sincronizado");
    else       LOG_W("wifi", "NTP timeout");

    bool ok = weatherFetch(out);
    wifiOff();
    _firstFetch  = false;
    _lastFetchMs = millis();
    return ok;
}

void wifiInit(WeatherData& weatherData) {
    loadCredentials();
    if (!wifiHasStoredCredentials()) {
        LOG_W("wifi", "Sem credenciais — iniciando portal cativo");
        startPortal();
        return;
    }
    bool ok = wifiConnectAndFetch(weatherData);
    if (!ok) {
        LOG_W("wifi", "Falha na conexao no cold boot — iniciando portal cativo");
        startPortal();
    }
}

void wifiScheduleUpdate(WeatherData& weatherData) {
    if (_portalMode) return;
    if (_state != ASYNC_IDLE) {
        _asyncOut = &weatherData;
        pollAsync();
        return;
    }
    if (_firstFetch) return;
    if (millis() - _lastFetchMs >= _updateIntervalMs) {
        LOG_I("wifi", "Atualizacao programada do clima");
        wifiBeginAsync(weatherData);
    }
}

void wifiForceRefresh(WeatherData& weatherData) {
    if (_state != ASYNC_IDLE) return;  // já buscando
    LOG_I("wifi", "Atualizacao forcada pelo usuario");
    wifiBeginAsync(weatherData);
}

bool wifiIsFetching() { return _state != ASYNC_IDLE; }
bool wifiIsPortalMode() { return _portalMode; }

void wifiPortalUpdate() {
    if (!_portalMode) return;
    if (_dnsServer) _dnsServer->processNextRequest();
    if (_webServer) _webServer->handleClient();
}

void wifiSetKeepAlive(bool keep) {
    _keepAlive = keep;
    if (keep && WiFi.status() == WL_CONNECTED) otaInit();  // ativa OTA
    if (!keep && _state == ASYNC_IDLE && WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        LOG_I("wifi", "WiFi OFF (keep-alive liberado)");
    }
}

int wifiGetRSSI() {
    if (WiFi.status() != WL_CONNECTED) return 0;
    return WiFi.RSSI();
}

void wifiSetUpdateInterval(uint32_t ms) {
    _updateIntervalMs = (ms > 0) ? ms : WEATHER_UPDATE_INTERVAL_MS;
    LOG_I("wifi", "Intervalo de atualizacao -> %lu ms", (unsigned long)_updateIntervalMs);
}
