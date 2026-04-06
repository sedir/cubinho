#include "telnet_log.h"
#include "wifi_manager.h"
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <Arduino.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>

// ── SD card (CoreS3: SPI3 / VSPI) ────────────────────────────────────────────
#define SD_PIN_CS    4
#define SD_PIN_SCK  36
#define SD_PIN_MISO 35
#define SD_PIN_MOSI 37
#define SD_SPI_FREQ 20000000UL

static SPIClass _sdSPI(3);  // 3 = VSPI (removido no ESP32 Arduino core v3)
static bool     _sdAvailable = false;
static uint8_t  _bootNum     = 0;
static bool     _sessionHeaderWritten = false;

// Retorna o caminho do arquivo de log atual.
// Antes do NTP: /logs/boot.txt (todos os boots pré-NTP compartilham o arquivo).
// Após NTP:    /logs/YYYY-MM-DD.txt (arquivo diário).
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

// Grava uma linha no arquivo de log do SD (append).
// Abre e fecha a cada escrita para garantir integridade em caso de queda de energia.
static void sdAppend(const char* line) {
    if (!_sdAvailable) return;

    const char* path = sdFilePath();

    // Cabeçalho de sessão na primeira escrita deste boot
    if (!_sessionHeaderWritten) {
        File f = SD.open(path, FILE_APPEND);
        if (!f) {
            _sdAvailable = false;
            Serial.println("[sd] Falha ao abrir arquivo — SD removido?");
            return;
        }
        f.printf("\n=== BOOT #%u ===\n", _bootNum);
        f.close();
        _sessionHeaderWritten = true;
    }

    File f = SD.open(path, FILE_APPEND);
    if (!f) {
        _sdAvailable = false;
        Serial.println("[sd] Falha ao abrir arquivo — SD removido?");
        return;
    }
    f.println(line);
    f.close();
}

static void sdInit() {
    _sdSPI.begin(SD_PIN_SCK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);
    if (!SD.begin(SD_PIN_CS, _sdSPI, SD_SPI_FREQ)) {
        Serial.println("[sd] Sem cartao SD ou falha na inicializacao — logs so no Serial/Telnet");
        return;
    }
    if (!SD.exists("/logs")) {
        SD.mkdir("/logs");
    }
    uint64_t totalMB = SD.totalBytes() / (1024ULL * 1024ULL);
    uint64_t usedMB  = SD.usedBytes()  / (1024ULL * 1024ULL);
    _sdAvailable = true;
    Serial.printf("[sd] Cartao ok — %lluMB usados de %lluMB\n", usedMB, totalMB);
}

// ── Ring buffer de linhas ────────────────────────────────────────────────────
#define RING_LINES  50
#define RING_LEN   148

static char    _ring[RING_LINES][RING_LEN];
static uint8_t _wIdx  = 0;
static uint8_t _count = 0;

static void ringPush(const char* line) {
    strncpy(_ring[_wIdx], line, RING_LEN - 1);
    _ring[_wIdx][RING_LEN - 1] = '\0';
    _wIdx  = (_wIdx + 1) % RING_LINES;
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
    Serial.printf("[telnet] Servidor na porta %d — %s\n",
                  TELNET_LOG_PORT, WiFi.localIP().toString().c_str());
}

void telnetLogInit() {
    sdInit();
    // Servidor Telnet sobe de forma lazy quando WiFi conectar
}

void telnetLogSetBoot(uint8_t bootNum) {
    _bootNum = bootNum;
    _sessionHeaderWritten = false;  // força novo cabeçalho de sessão no SD
}

// ── Comandos Serial ───────────────────────────────────────────────────────────
// ls              — lista arquivos em /logs/
// cat <arquivo>   — despeja conteúdo de /logs/<arquivo> no Serial
// Entrada lida linha a linha (terminada em '\n').
static void handleSerialCommands() {
    static char cmdbuf[64];
    static uint8_t cmdlen = 0;

    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n' || cmdlen >= sizeof(cmdbuf) - 1) {
            cmdbuf[cmdlen] = '\0';
            cmdlen = 0;

            if (strcmp(cmdbuf, "ls") == 0) {
                if (!_sdAvailable) { Serial.println("[sd] Sem cartao SD"); continue; }
                File dir = SD.open("/logs");
                if (!dir) { Serial.println("[sd] Falha ao abrir /logs"); continue; }
                Serial.println("--- /logs/ ---");
                File f = dir.openNextFile();
                while (f) {
                    Serial.printf("  %-28s  %u bytes\n", f.name(), (unsigned)f.size());
                    f.close();
                    f = dir.openNextFile();
                }
                dir.close();
                Serial.println("--- fim ---");

            } else if (strncmp(cmdbuf, "cat ", 4) == 0) {
                if (!_sdAvailable) { Serial.println("[sd] Sem cartao SD"); continue; }
                char path[40];
                snprintf(path, sizeof(path), "/logs/%s", cmdbuf + 4);
                File f = SD.open(path, FILE_READ);
                if (!f) {
                    Serial.printf("[sd] Arquivo nao encontrado: %s\n", path);
                    continue;
                }
                Serial.printf("--- %s (%u bytes) ---\n", path, (unsigned)f.size());
                while (f.available()) {
                    int ch = f.read();
                    if (ch >= 0) Serial.write((uint8_t)ch);
                }
                f.close();
                Serial.println("\n--- fim ---");

            } else if (cmdlen == 0 || strcmp(cmdbuf, "help") == 0) {
                Serial.println("Comandos: ls | cat <arquivo>");
            } else {
                Serial.printf("[serial] Comando desconhecido: %s\n", cmdbuf);
            }
        } else {
            cmdbuf[cmdlen++] = c;
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
            _client  = c;
            _flushed = false;
            _client.setNoDelay(true);
            wifiSetKeepAlive(true);
            Serial.printf("[telnet] Cliente conectado: %s\n",
                          _client.remoteIP().toString().c_str());
        }
    }

    if (!_client || !_client.connected()) return;

    if (!_flushed) {
        _client.print("\r\n=== Cidinha Kitchen Dashboard ===\r\n");
        _client.printf("=== SD: %s ===\r\n",
                       _sdAvailable ? "gravando" : "sem cartao");
        _client.printf("=== %u linha(s) em buffer ===\r\n\r\n", _count);
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

    while (_client.available()) _client.read();
}

bool telnetIsConnected() {
    return _client && _client.connected();
}

// ── logPrint ──────────────────────────────────────────────────────────────────
void logPrint(char level, const char* tag, const char* fmt, ...) {
    char timebuf[14];
    struct tm ti;
    if (getLocalTime(&ti, 0) && time(nullptr) > 1577836800L) {
        snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d",
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
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
