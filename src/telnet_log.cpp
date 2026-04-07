#include "telnet_log.h"
#include "wifi_manager.h"
#include "events.h"
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <Arduino.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>

// ── SD card ──────────────────────────────────────────────────────────────────
#define SD_PIN_CS    4
#define SD_PIN_SCK  36
#define SD_PIN_MISO 35
#define SD_PIN_MOSI 37
#define SD_SPI_FREQ 20000000UL

static SPIClass _sdSPI(3);
static bool     _sdAvailable = false;
static uint8_t  _bootNum     = 0;
static bool     _sessionHeaderWritten = false;

bool sdIsAvailable() { return _sdAvailable; }

static const char* sdFilePath() {
    static char path[28];
    struct tm ti;
    if (getLocalTime(&ti, 0) && time(nullptr) > 1577836800L) {
        snprintf(path, sizeof(path), "/logs/%04d-%02d-%02d.txt",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    } else {
        snprintf(path, sizeof(path), "/logs/boot.txt");
    }
    return path;
}

static void sdAppend(const char* line) {
    if (!_sdAvailable) return;
    const char* path = sdFilePath();
    if (!_sessionHeaderWritten) {
        File f = SD.open(path, FILE_APPEND);
        if (!f) { _sdAvailable = false; return; }
        f.printf("\n=== BOOT #%u ===\n", _bootNum);
        f.close();
        _sessionHeaderWritten = true;
    }
    File f = SD.open(path, FILE_APPEND);
    if (!f) { _sdAvailable = false; return; }
    f.println(line);
    f.close();
}

static void sdInit() {
    _sdSPI.begin(SD_PIN_SCK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);
    if (!SD.begin(SD_PIN_CS, _sdSPI, SD_SPI_FREQ)) {
        Serial.println("[sd] Sem cartao SD");
        return;
    }
    if (!SD.exists("/logs")) SD.mkdir("/logs");
    _sdAvailable = true;
    uint64_t totalMB = SD.totalBytes() / (1024ULL * 1024ULL);
    uint64_t usedMB  = SD.usedBytes()  / (1024ULL * 1024ULL);
    Serial.printf("[sd] OK — %lluMB / %lluMB\n", usedMB, totalMB);
}

// ── Ring buffer ──────────────────────────────────────────────────────────────
#define RING_LINES  50
#define RING_LEN   148

static char    _ring[RING_LINES][RING_LEN];
static uint8_t _wIdx  = 0;
static uint8_t _count = 0;

static void ringPush(const char* line) {
    strncpy(_ring[_wIdx], line, RING_LEN - 1);
    _ring[_wIdx][RING_LEN - 1] = '\0';
    _wIdx = (_wIdx + 1) % RING_LINES;
    if (_count < RING_LINES) _count++;
}

// ── Servidor Telnet ──────────────────────────────────────────────────────────
static WiFiServer* _srv     = nullptr;
static WiFiClient  _client;
static bool        _flushed = false;

static void ensureServer() {
    if (_srv) return;
    if (WiFi.status() != WL_CONNECTED) return;
    _srv = new WiFiServer(TELNET_LOG_PORT);
    _srv->begin();
    _srv->setNoDelay(true);
    Serial.printf("[telnet] Porta %d — %s\n", TELNET_LOG_PORT, WiFi.localIP().toString().c_str());
}

void telnetLogInit() {
    sdInit();
}

void telnetLogSetBoot(uint8_t bootNum) {
    _bootNum = bootNum;
    _sessionHeaderWritten = false;
}

// ── Comandos Serial + Telnet (item #24: eventos) ─────────────────────────────
static void processCommand(const char* cmd, bool toTelnet) {
    auto out = [&](const char* fmt, ...) {
        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        Serial.println(buf);
        if (toTelnet && _client.connected()) { _client.print(buf); _client.print("\r\n"); }
    };

    if (strcmp(cmd, "ls") == 0) {
        if (!_sdAvailable) { out("[sd] Sem cartao SD"); return; }
        File dir = SD.open("/logs");
        if (!dir) { out("[sd] Falha ao abrir /logs"); return; }
        out("--- /logs/ ---");
        File f = dir.openNextFile();
        while (f) {
            out("  %-28s  %u bytes", f.name(), (unsigned)f.size());
            f.close();
            f = dir.openNextFile();
        }
        dir.close();
        out("--- fim ---");

    } else if (strncmp(cmd, "cat ", 4) == 0) {
        if (!_sdAvailable) { out("[sd] Sem cartao SD"); return; }
        char path[40];
        snprintf(path, sizeof(path), "/logs/%s", cmd + 4);
        File f = SD.open(path, FILE_READ);
        if (!f) { out("[sd] Nao encontrado: %s", path); return; }
        out("--- %s (%u bytes) ---", path, (unsigned)f.size());
        while (f.available()) {
            int ch = f.read();
            if (ch >= 0) Serial.write((uint8_t)ch);
            if (ch >= 0 && toTelnet && _client.connected()) _client.write((uint8_t)ch);
        }
        f.close();
        out("\n--- fim ---");

    } else if (strncmp(cmd, "event add ", 10) == 0) {
        // Formato: event add "Nome" MM-DD HH:MM
        char name[24] = {};
        int m, d, h, mi;
        const char* p = cmd + 10;
        // Parse nome entre aspas
        if (*p == '"') {
            p++;
            const char* end = strchr(p, '"');
            if (!end) { out("Erro: feche aspas no nome"); return; }
            int len = min((int)(end - p), 23);
            strncpy(name, p, len);
            p = end + 1;
            while (*p == ' ') p++;
        } else {
            out("Uso: event add \"Nome\" MM-DD HH:MM");
            return;
        }
        if (sscanf(p, "%d-%d %d:%d", &m, &d, &h, &mi) == 4) {
            if (eventsAdd(name, m, d, h, mi))
                out("Evento adicionado: %s %02d/%02d %02d:%02d", name, d, m, h, mi);
            else
                out("Erro: maximo de %d eventos", MAX_EVENTS);
        } else {
            out("Uso: event add \"Nome\" MM-DD HH:MM");
        }

    } else if (strcmp(cmd, "event list") == 0 || strcmp(cmd, "events") == 0) {
        int n = eventsCount();
        if (n == 0) { out("Nenhum evento"); return; }
        out("--- Eventos (%d) ---", n);
        for (int i = 0; i < n; i++) {
            const Event* e = eventsGet(i);
            if (e) out("  %d. %s  %02d/%02d %02d:%02d", i, e->name, e->day, e->month, e->hour, e->minute);
        }
        out("--- fim ---");

    } else if (strncmp(cmd, "event del ", 10) == 0) {
        int idx = atoi(cmd + 10);
        if (eventsRemove(idx)) out("Evento %d removido", idx);
        else                   out("Indice invalido: %d", idx);

    } else if (strcmp(cmd, "wificlear") == 0) {
        wifiClearStoredCredentials();
        out("Credenciais WiFi apagadas. Reinicie para entrar em portal mode.");

    } else if (strcmp(cmd, "help") == 0 || cmd[0] == '\0') {
        out("Comandos: ls | cat <arq> | events | event add \"Nome\" MM-DD HH:MM | event del <n> | wificlear | help");

    } else {
        out("Comando desconhecido: %s", cmd);
    }
}

static void handleSerialCommands() {
    static char cmdbuf[96];
    static uint8_t cmdlen = 0;

    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n' || cmdlen >= sizeof(cmdbuf) - 1) {
            cmdbuf[cmdlen] = '\0';
            cmdlen = 0;
            processCommand(cmdbuf, false);
        } else {
            cmdbuf[cmdlen++] = c;
        }
    }
}

// Lê comandos do cliente Telnet
static void handleTelnetCommands() {
    if (!_client || !_client.connected()) return;
    static char tcmd[96];
    static uint8_t tlen = 0;

    while (_client.available()) {
        char c = _client.read();
        if (c == '\r') continue;
        if (c == '\n' || tlen >= sizeof(tcmd) - 1) {
            tcmd[tlen] = '\0';
            tlen = 0;
            if (tcmd[0]) processCommand(tcmd, true);
        } else {
            tcmd[tlen++] = c;
        }
    }
}

void telnetLogUpdate() {
    handleSerialCommands();
    ensureServer();
    if (!_srv) return;

    if (_client && !_client.connected()) {
        Serial.println("[telnet] Cliente desconectado");
        wifiSetKeepAlive(false);
        _client.stop();
        _flushed = false;
    }

    if (!_client || !_client.connected()) {
        WiFiClient c = _srv->available();
        if (c) {
            _client = c;
            _flushed = false;
            _client.setNoDelay(true);
            wifiSetKeepAlive(true);
            Serial.printf("[telnet] Cliente: %s\n", _client.remoteIP().toString().c_str());
        }
    }

    if (!_client || !_client.connected()) return;

    if (!_flushed) {
        _client.print("\r\n=== Portela v2.0 ===\r\n");
        _client.printf("=== SD: %s ===\r\n", _sdAvailable ? "gravando" : "sem cartao");
        _client.printf("=== %u linha(s) em buffer ===\r\n", _count);
        _client.print("Digite 'help' para comandos\r\n\r\n");
        if (_count > 0) {
            uint8_t start = (_wIdx + RING_LINES - _count) % RING_LINES;
            for (uint8_t i = 0; i < _count; i++) {
                _client.print(_ring[(start + i) % RING_LINES]);
                _client.print("\r\n");
            }
            _client.print("\r\n--- fim do buffer ---\r\n\r\n");
        }
        _flushed = true;
    }

    handleTelnetCommands();
}

bool telnetIsConnected() {
    return _client && _client.connected();
}

// ── logPrint ─────────────────────────────────────────────────────────────────
void logPrint(char level, const char* tag, const char* fmt, ...) {
    char timebuf[14];
    struct tm ti;
    if (getLocalTime(&ti, 0) && time(nullptr) > 1577836800L) {
        snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        uint32_t s = millis() / 1000;
        snprintf(timebuf, sizeof(timebuf), "+%6lus", (unsigned long)s);
    }

    char msgbuf[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgbuf, sizeof(msgbuf), fmt, args);
    va_end(args);

    char line[RING_LEN];
    snprintf(line, sizeof(line), "[%s] %c [%-8s] %s", timebuf, level, tag, msgbuf);

    Serial.println(line);
    ringPush(line);
    sdAppend(line);

    if (_client && _client.connected() && _flushed) {
        _client.print(line);
        _client.print("\r\n");
    }
}
