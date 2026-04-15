#include "qr_scanner.h"
#include "wifi_manager.h"
#include "runtime_config.h"
#include "logger.h"
#include "theme.h"
#include <M5CoreS3.h>
#include <esp_camera.h>
#include <new>

#include "quirc/quirc.h"

constexpr int kCameraWidth = 320;
constexpr int kCameraHeight = 240;
constexpr uint32_t kStatusHoldMs = 2500;

static struct quirc* _q = nullptr;
static bool          _active = false;
static bool          _done = false;
static bool          _success = false;
static QRScanMode    _mode;
static uint32_t      _statusMs = 0;
static char          _statusMsg[64] = "";
static int           _quircWidth = 0;
static int           _quircHeight = 0;

// Diagnóstico — reset em qrScannerBegin
static uint32_t _frameCount  = 0;
static uint32_t _detectCount = 0;
static bool     _firstFrame  = true;
static bool     _statsLogged = false;
static uint32_t _lastQrLog   = 0;
static bool     _usingDirectLuma = false;

enum QrImageTransform : uint8_t {
    kQrTransformNone  = 0,
    kQrTransformFlipX = 1 << 0,
    kQrTransformFlipY = 1 << 1,
};

// ── Helpers ─────────────────────────────────────────────────────────────────
static void setStatusMessage(const char* message) {
    snprintf(_statusMsg, sizeof(_statusMsg), "%s", message ? message : "");
    _statusMs = millis();
}

static void trimAsciiWhitespace(char* text) {
    if (!text || !text[0]) return;
    size_t start = 0;
    while (text[start] == ' ' || text[start] == '\n' || text[start] == '\r' || text[start] == '\t') ++start;
    size_t len = strlen(text);
    while (len > start) {
        char c = text[len - 1];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
        --len;
    }
    if (start > 0) memmove(text, text + start, len - start);
    text[len - start] = '\0';
}

static void copyQrPayload(const struct quirc_data& data, char* out, size_t outLen) {
    if (!out || outLen == 0) return;
    size_t copyLen = min((size_t)data.payload_len, outLen - 1);
    memcpy(out, data.payload, copyLen);
    out[copyLen] = '\0';
    trimAsciiWhitespace(out);
}

static bool parseWifiQR(const char* payload,
                        char* ssid, size_t ssidLen,
                        char* pass, size_t passLen) {
    if (strncmp(payload, "WIFI:", 5) != 0) return false;
    char auth[16] = "";
    ssid[0] = '\0';
    pass[0] = '\0';
    const char* p = payload + 5;
    while (*p) {
        while (*p == ';') ++p;
        if (!*p) break;
        char key = *p++;
        if (*p != ':') { while (*p && *p != ';') ++p; continue; }
        ++p;
        char* out = nullptr; size_t outLen = 0;
        switch (key) {
            case 'S': out = ssid; outLen = ssidLen; break;
            case 'P': out = pass; outLen = passLen; break;
            case 'T': out = auth; outLen = sizeof(auth); break;
            default: break;
        }
        size_t written = 0; bool escape = false;
        while (*p) {
            char c = *p++;
            if (escape) { if (out && written + 1 < outLen) out[written++] = c; escape = false; continue; }
            if (c == '\\') { escape = true; continue; }
            if (c == ';') break;
            if (out && written + 1 < outLen) out[written++] = c;
        }
        if (out && outLen) out[written] = '\0';
    }
    if (strcmp(auth, "nopass") == 0) pass[0] = '\0';
    return ssid[0] != '\0';
}

static inline uint8_t rgb565BytesToLuma(uint8_t hb, uint8_t lb) {
    uint8_t r8 = hb & 0xF8;
    uint8_t g8 = ((hb & 0x07) << 5) | ((lb & 0xE0) >> 3);
    uint8_t b8 = (lb & 0x1F) << 3;
    return (uint8_t)((r8 * 77 + g8 * 150 + b8 * 29) >> 8);
}

static inline uint16_t lumaToRgb565(uint8_t y) {
    return lgfx::swap565(y, y, y);
}

static inline bool frameHasDirectLuma(const camera_fb_t* fb) {
    return _usingDirectLuma || fb->format == PIXFORMAT_YUV422 || fb->format == PIXFORMAT_GRAYSCALE;
}

static inline uint8_t frameLumaAt(const camera_fb_t* fb, int x, int y) {
    const int width = (int)fb->width;
    if (frameHasDirectLuma(fb)) {
        if (fb->format == PIXFORMAT_GRAYSCALE) {
            return fb->buf[y * width + x];
        }
        const uint8_t* row = fb->buf + (y * width * 2);
        return row[x * 2];
    }

    const uint8_t* px = fb->buf + ((y * width + x) * 2);
    return rgb565BytesToLuma(px[0], px[1]);
}

static void fillQuircImageFromFrame(const camera_fb_t* fb, uint8_t* qimage, int width, int height,
                                    QrImageTransform transform) {
    for (int y = 0; y < height; ++y) {
        int sy = (transform & kQrTransformFlipY) ? (height - 1 - y) : y;
        uint8_t* dstRow = qimage + y * width;
        for (int x = 0; x < width; ++x) {
            int sx = (transform & kQrTransformFlipX) ? (width - 1 - x) : x;
            dstRow[x] = frameLumaAt(fb, sx, sy);
        }
    }
}

static bool tryDecodeDetectedQRCodes(int count) {
    if (count <= 0) return false;

    struct quirc_code* code = new (std::nothrow) quirc_code;
    struct quirc_data* data = new (std::nothrow) quirc_data;
    if (!code || !data) {
        if (code) delete code;
        if (data) delete data;
        LOG_E("qr", "Falha ao alocar buffers de decode");
        return false;
    }

    bool decoded = false;
    for (int i = 0; i < count; i++) {
        quirc_extract(_q, i, code);
        quirc_decode_error_t decErr = quirc_decode(code, data);
        if (decErr != QUIRC_SUCCESS) {
            LOG_W("qr", "quirc_decode falhou: %s", quirc_strerror(decErr));
            continue;
        }

        char payload[512];
        copyQrPayload(*data, payload, sizeof(payload));
        LOG_I("qr", "QR: %.80s", payload);
        if (_mode == QR_SCAN_WIFI) {
            char ssid[64], pass[64];
            if (parseWifiQR(payload, ssid, sizeof(ssid), pass, sizeof(pass))) {
                wifiSaveCredentials(ssid, pass);
                _success = true;
                setStatusMessage("WiFi salvo! Reiniciando...");
                decoded = true;
            }
        } else {
            bool isUrl = strncmp(payload, "http://",  7) == 0 ||
                         strncmp(payload, "https://", 8) == 0 ||
                         strncmp(payload, "webcal://",9) == 0;
            if (isUrl) {
                runtimeConfigSaveCalendarUrl(payload);
                _success = true;
                setStatusMessage("URL iCal salva!");
                decoded = true;
            }
        }

        if (decoded) break;
    }

    delete code;
    delete data;
    return decoded;
}

static bool ensureQuircSize(int width, int height) {
    if (!_q) return false;
    if (_quircWidth == width && _quircHeight == height) return true;
    if (quirc_resize(_q, width, height) != 0) return false;
    _quircWidth = width;
    _quircHeight = height;
    return true;
}

// Desenha a UI sobrepondo o preview da câmera
static void drawOverlayUI(lgfx::LovyanGFX& d) {
    const char* hint = (_mode == QR_SCAN_WIFI) ? "QR de WiFi" : "QR de iCal";

    d.fillRect(0, 0, kCameraWidth, 30, d.color565(0, 0, 0));
    d.fillRect(0, 204, kCameraWidth, 36, d.color565(0, 0, 0));

    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(COLOR_TEXT_PRIMARY);
    d.setTextDatum(MC_DATUM);
    d.drawString(hint, kCameraWidth / 2, 15);

    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_ACCENT);
    char liveBuf[32];
    snprintf(liveBuf, sizeof(liveBuf), "f=%lu det=%lu", _frameCount, _detectCount);
    d.drawString(liveBuf, kCameraWidth / 2, 26);

    const int cx = kCameraWidth / 2, cy = kCameraHeight / 2, sz = 80, cs = 16;
    uint16_t cc = COLOR_TIMER_RUNNING;
    d.drawFastHLine(cx-sz,    cy-sz,    cs, cc); d.drawFastVLine(cx-sz,    cy-sz,    cs, cc);
    d.drawFastHLine(cx+sz-cs, cy-sz,    cs, cc); d.drawFastVLine(cx+sz-1,  cy-sz,    cs, cc);
    d.drawFastHLine(cx-sz,    cy+sz-1,  cs, cc); d.drawFastVLine(cx-sz,    cy+sz-cs, cs, cc);
    d.drawFastHLine(cx+sz-cs, cy+sz-1,  cs, cc); d.drawFastVLine(cx+sz-1,  cy+sz-cs, cs, cc);

    d.setFont(&fonts::Font0);
    d.setTextColor(COLOR_TEXT_SUBTLE);
    d.drawString("Centralize o QR dentro da moldura", kCameraWidth / 2, 217);
    d.drawString("Toque para cancelar", kCameraWidth / 2, 228);
}

static void drawStatus(lgfx::LovyanGFX& d) {
    d.fillScreen(COLOR_BACKGROUND);
    d.setFont(&fonts::FreeSans9pt7b);
    d.setTextColor(_success ? COLOR_TIMER_RUNNING : TFT_RED, COLOR_BACKGROUND);
    d.setTextDatum(MC_DATUM);
    d.drawString(_statusMsg, kCameraWidth / 2, kCameraHeight / 2);
}

static bool initCamera() {
    // Usa a API oficial do M5CoreS3 — PIXFORMAT_RGB565, fb_count=2 (idêntico ao UserDemo)
    if (!CoreS3.Camera.begin()) {
        LOG_E("qr", "CoreS3.Camera.begin falhou");
        return false;
    }
    sensor_t* s = CoreS3.Camera.sensor;
    _usingDirectLuma = false;
    if (s) {
        if (s->set_pixformat(s, PIXFORMAT_YUV422) == 0) {
            _usingDirectLuma = true;
        } else {
            LOG_W("qr", "GC0308 nao aceitou YUV422; mantendo RGB565");
        }
        s->set_vflip(s, 1);
        s->set_hmirror(s, 1);
        // Nota: no GC0308, set_brightness e set_saturation são no-ops (set_dummy).
        // set_contrast(s, N) escreve N diretamente no registrador 0xb3;
        // passar qualquer N != 0 sobrescreve o default (≈0x40) causando imagem lavada.
        // Não alterar contrast — manter defaults do sensor.
    }
    LOG_I("qr", "Camera GC0308 inicializada (%s QVGA)",
          _usingDirectLuma ? "YUV422/luma" : "RGB565");
    return true;
}

// ── API pública ─────────────────────────────────────────────────────────────
void qrScannerBegin(QRScanMode mode) {
    if (_active) return;

    _mode         = mode;
    _done         = false;
    _success      = false;
    _statusMs     = 0;
    _statusMsg[0] = '\0';
    _quircWidth   = 0;
    _quircHeight  = 0;
    _frameCount   = 0;
    _detectCount  = 0;
    _firstFrame   = true;
    _statsLogged  = false;
    _lastQrLog    = 0;
    _usingDirectLuma = false;

    _active = true;

    _q = quirc_new();
    if (!_q) { LOG_E("qr", "Falha ao alocar quirc"); setStatusMessage("Erro: memoria QR"); return; }

    if (!ensureQuircSize(kCameraWidth, kCameraHeight)) {
        LOG_E("qr", "Falha ao redimensionar quirc"); setStatusMessage("Erro: buffer QR"); return;
    }

    if (!initCamera()) { setStatusMessage("Erro: camera indisponivel"); return; }

    LOG_I("qr", "Scanner iniciado");
}

bool qrScannerIsActive()    { return _active; }
bool qrScannerWasSuccessful() { return _success; }

bool qrScannerUpdate(lgfx::LovyanGFX& d, bool touchReleased) {
    if (_done) return true;

    if (_statusMs) {
        drawStatus(CoreS3.Display);
        if (millis() - _statusMs > kStatusHoldMs) { _done = true; return true; }
        return false;
    }

    if (touchReleased) { LOG_I("qr", "Scanner cancelado"); _done = true; return true; }

    if (!CoreS3.Camera.get()) {
        LOG_W("qr", "Falha ao capturar frame");
        setStatusMessage("Erro: sem frame da camera");
        return false;
    }
    camera_fb_t* fb = CoreS3.Camera.fb;

    if (_firstFrame) {
        LOG_I("qr", "Primeiro frame: fmt=%d %dx%d len=%d",
              (int)fb->format, fb->width, fb->height, fb->len);
        _firstFrame = false;
    }

    // Display: empurra apenas as linhas entre as barras de overlay (30..203).
    // As regiões das barras (0-29 e 204-239) nunca são sobrescritas pela câmera,
    // eliminando o flickering causado por redesenhar a UI sobre o frame completo.
    // Em YUV422/GRAYSCALE, desenha um preview monocromático a partir da luma.
    {
        constexpr int kBarTop = 30;
        constexpr int kBarBot = 36;
        constexpr int kMidH   = kCameraHeight - kBarTop - kBarBot;

        if (!frameHasDirectLuma(fb) && fb->format == PIXFORMAT_RGB565) {
            const uint16_t* midStart = (const uint16_t*)fb->buf + kBarTop * (int)fb->width;
            CoreS3.Display.startWrite();
            CoreS3.Display.setAddrWindow(0, kBarTop, (int32_t)fb->width, kMidH);
            CoreS3.Display.writePixels(midStart, (int32_t)fb->width * kMidH, false);
            CoreS3.Display.endWrite();
        } else if (frameHasDirectLuma(fb)) {
            uint16_t grayLine[kCameraWidth];
            CoreS3.Display.startWrite();
            for (int y = 0; y < kMidH; ++y) {
                for (int x = 0; x < (int)fb->width; ++x) {
                    grayLine[x] = lumaToRgb565(frameLumaAt(fb, x, kBarTop + y));
                }
                CoreS3.Display.setAddrWindow(0, kBarTop + y, (int32_t)fb->width, 1);
                CoreS3.Display.writePixels(grayLine, (int32_t)fb->width, false);
            }
            CoreS3.Display.endWrite();
        }
    }
    // Barras e moldura: desenhadas sobre o display; persistem entre frames pois
    // a câmera não sobrescreve mais essas regiões.
    drawOverlayUI(CoreS3.Display);

    // Quirc: usa luma direta em YUV422/GRAYSCALE e cai para RGB565 quando preciso.
    if (_q && ensureQuircSize((int)fb->width, (int)fb->height)) {
        int qw, qh;
        uint8_t* qimage = quirc_begin(_q, &qw, &qh);
        if (qimage && qw == (int)fb->width && qh == (int)fb->height) {
            const int npx = qw * qh;
            fillQuircImageFromFrame(fb, qimage, qw, qh, kQrTransformNone);
            quirc_end(_q);

            // Stats do primeiro frame — confirma qualidade de imagem para quirc
            if (!_statsLogged) {
                uint8_t pmin = 255, pmax = 0; uint32_t psum = 0;
                for (int i = 0; i < npx; ++i) {
                    if (qimage[i] < pmin) pmin = qimage[i];
                    if (qimage[i] > pmax) pmax = qimage[i];
                    psum += qimage[i];
                }
                LOG_I("qr", "luma stats: min=%u max=%u mean=%u range=%u",
                      pmin, pmax, psum / (uint32_t)npx, (uint32_t)(pmax - pmin));
                _statsLogged = true;
            }

            ++_frameCount;
            int count = quirc_count(_q);
            if (count > 0) ++_detectCount;

            if (millis() - _lastQrLog > 3000) {
                LOG_I("qr", "quirc_count=%d frames=%lu det=%lu", count, _frameCount, _detectCount);
                _lastQrLog = millis();
            }

            if (count > 0) {
                LOG_I("qr", "quirc detectou %d codigo(s)", count);
                bool decoded = tryDecodeDetectedQRCodes(count);

                if (!decoded) {
                    struct RetryTransform { QrImageTransform transform; const char* name; };
                    constexpr RetryTransform kRetries[] = {
                        { kQrTransformFlipX, "flip-x"   },
                        { kQrTransformFlipY, "flip-y"   },
                        { (QrImageTransform)(kQrTransformFlipX | kQrTransformFlipY), "rot-180" },
                    };

                    for (const auto& retry : kRetries) {
                        qimage = quirc_begin(_q, &qw, &qh);
                        if (!qimage || qw != (int)fb->width || qh != (int)fb->height) break;

                        fillQuircImageFromFrame(fb, qimage, qw, qh, retry.transform);
                        quirc_end(_q);

                        int retryCount = quirc_count(_q);
                        if (retryCount <= 0) continue;

                        LOG_I("qr", "retry %s detectou %d codigo(s)", retry.name, retryCount);
                        if (tryDecodeDetectedQRCodes(retryCount)) {
                            decoded = true;
                            break;
                        }
                    }
                }
            }
        } else if (qimage) {
            quirc_end(_q);
        }
    }

    CoreS3.Camera.free();
    return false;
}

void qrScannerEnd() {
    if (!_active) return;
    if (_q) { quirc_destroy(_q); _q = nullptr; }
    esp_camera_deinit();
    M5.In_I2C.begin();
    _active = false;
    _quircWidth = 0;
    _quircHeight = 0;
    LOG_I("qr", "Scanner encerrado (sucesso=%s)", _success ? "sim" : "nao");
    if (_success && _mode == QR_SCAN_WIFI) { delay(300); ESP.restart(); }
}
