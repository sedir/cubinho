#include "voice_cmd.h"
#include "theme.h"
#include "logger.h"
#include "config.h"
#include <M5Unified.h>

// ── Parâmetros de captura ─────────────────────────────────────────────────────
#define VOICE_SAMPLE_RATE     8000
#define VOICE_FRAME_SAMPLES   256           // ~32ms de áudio por frame @ 8kHz
#define VOICE_LISTEN_MS       2200          // janela máxima de classificação (ms)

// ── Parâmetros de detecção ────────────────────────────────────────────────────
#ifndef VOICE_ENERGY_THRESH_MIN
#define VOICE_ENERGY_THRESH_MIN  600        // piso absoluto de RMS (ajuste em config.h)
#endif
#define VOICE_NOISE_MULT      4.0f          // threshold = noiseFloor × VOICE_NOISE_MULT
#define VOICE_NOISE_DECAY     0.992f        // EMA lenta do piso de ruído (~5s p/ 63%)
#define VOICE_WAKE_FRAMES     2             // frames consecutivos loud p/ sair do IDLE (~64ms)

#define VOICE_SILENCE_MS      340           // silêncio p/ fechar um pulso (ms)
#define VOICE_MIN_BURST_MS    70            // pulso válido mínimo (ms)
#define VOICE_SHORT_BURST_MS  430           // limiar curto vs. longo (ms)
#define VOICE_LONG_BURST_MS   680           // pulso longo mínimo (ms)

#define VOICE_RESUME_DELAY_MS 1800          // atraso após speaker parar p/ religar mic

// ── Estado interno ────────────────────────────────────────────────────────────
static VoiceState _state       = VSTATE_DISABLED;
static bool       _micRunning  = false;
static uint32_t   _resumeAt    = 0;         // millis() de quando o mic pode ser reiniciado

// Calibração adaptativa do piso de ruído (EMA durante silêncio)
static float    _noiseFloor   = (float)VOICE_ENERGY_THRESH_MIN / VOICE_NOISE_MULT;
static uint32_t _energyThresh = VOICE_ENERGY_THRESH_MIN;

// Detecção de wake em IDLE
static int      _wakeConsec   = 0;          // frames loud consecutivos

// Detecção de bursts em LISTENING
static uint32_t _listenStartMs  = 0;
static int      _burstCount     = 0;
static bool     _inBurst        = false;
static uint32_t _burstStartMs   = 0;
static uint32_t _lastVoicedMs   = 0;
static bool     _hasLongBurst   = false;
static int      _shortBurstCount= 0;

// Métricas visuais (overlay)
static uint32_t _lastRMS        = 0;
static float    _smoothedRMS    = 0.0f;

static int16_t  _frameBuf[VOICE_FRAME_SAMPLES];

// ── Helpers ───────────────────────────────────────────────────────────────────
static uint32_t computeRMS(const int16_t* buf, int n) {
    if (n <= 0) return 0;
    uint64_t sum = 0;
    for (int i = 0; i < n; i++) {
        int32_t s = (int32_t)buf[i];
        sum += (uint64_t)(s * s);
    }
    return (uint32_t)sqrtf((float)(sum / (uint32_t)n));
}

static bool startMic() {
    auto cfg         = M5.Mic.config();
    cfg.sample_rate  = VOICE_SAMPLE_RATE;
    cfg.dma_buf_len  = VOICE_FRAME_SAMPLES;
    cfg.dma_buf_count= 3;
    M5.Mic.config(cfg);
    bool ok = M5.Mic.begin();
    _micRunning = ok;
    if (!ok) LOG_E("voice", "M5.Mic.begin() falhou");
    return ok;
}

static void stopMic() {
    M5.Mic.end();
    _micRunning = false;
}

static void enterListening(uint32_t now) {
    _state          = VSTATE_LISTENING;
    _listenStartMs  = now;
    _burstCount     = 1;       // o som que acordou = burst #1 já iniciado
    _inBurst        = true;
    _burstStartMs   = now;
    _lastVoicedMs   = now;
    _hasLongBurst   = false;
    _shortBurstCount= 0;
    LOG_I("voice", "Wake -> LISTENING (thresh=%u)", _energyThresh);
}

static VoiceCommand classify() {
    LOG_I("voice", "Classify: total=%d short=%d long=%s",
          _burstCount, _shortBurstCount, _hasLongBurst ? "sim" : "nao");

    if (_burstCount == 0)                              return VCMD_NONE;
    if (_hasLongBurst && _shortBurstCount == 0)        return VCMD_TIMER_TOGGLE;
    if (_shortBurstCount == 1 && !_hasLongBurst)       return VCMD_NEXT_SCREEN;
    if (_shortBurstCount == 2 && !_hasLongBurst)       return VCMD_PREV_SCREEN;
    if (_shortBurstCount >= 3  && !_hasLongBurst)      return VCMD_TIMER_RESET;
    return VCMD_AMBIGUOUS;
}

// ── API pública ───────────────────────────────────────────────────────────────
void voiceCmdInit(bool enable) {
    // runtimeConfigApply() pode ter chamado voiceCmdSetEnabled() antes desta função.
    // Se o estado já está correto, não reinicializa (evita double M5.Mic.begin()).
    if (enable && _state != VSTATE_DISABLED) {
        LOG_I("voice", "Init: ja ativo via runtimeConfigApply, ignorando");
        return;
    }
    if (!enable && _state == VSTATE_DISABLED) {
        LOG_I("voice", "Init: desabilitado");
        return;
    }

    if (enable) {
        if (startMic()) {
            _state = VSTATE_IDLE;
            LOG_I("voice", "Modulo iniciado — monitorando");
        } else {
            _state = VSTATE_DISABLED;
        }
    } else {
        stopMic();
        _state = VSTATE_DISABLED;
        LOG_I("voice", "Modulo iniciado — desabilitado");
    }
}

void voiceCmdSetEnabled(bool enable) {
    if (enable && _state == VSTATE_DISABLED) {
        if (startMic()) {
            _state      = VSTATE_IDLE;
            _wakeConsec = 0;
            LOG_I("voice", "Habilitado");
        }
    } else if (!enable && _state != VSTATE_DISABLED) {
        stopMic();
        _state = VSTATE_DISABLED;
        LOG_I("voice", "Desabilitado");
    }
}

bool voiceCmdIsEnabled() {
    return _state != VSTATE_DISABLED;
}

VoiceState voiceCmdGetState() {
    return _state;
}

// Chamado quando o speaker precisa usar o I2S (ex: alarm WAV começa)
void voiceCmdSuspend() {
    if (_state == VSTATE_SUSPENDED || _state == VSTATE_DISABLED) return;
    stopMic();
    _state    = VSTATE_SUSPENDED;
    _resumeAt = 0;   // ainda não sabemos quando o speaker vai parar
    LOG_I("voice", "Suspenso (speaker ativo)");
}

// Chamado quando o speaker parou — agenda reinício adiado para evitar ruído de tail
void voiceCmdResume() {
    if (_state != VSTATE_SUSPENDED) return;
    _resumeAt = millis() + VOICE_RESUME_DELAY_MS;
    LOG_I("voice", "Resume agendado em %ums", VOICE_RESUME_DELAY_MS);
}

VoiceCommand voiceCmdUpdate() {
    if (_state == VSTATE_DISABLED) return VCMD_NONE;

    uint32_t now = millis();

    // ── Reinício após suspensão ──────────────────────────────────────────────
    if (_state == VSTATE_SUSPENDED) {
        if (_resumeAt > 0 && now >= _resumeAt) {
            if (startMic()) {
                _state      = VSTATE_IDLE;
                _wakeConsec = 0;
                LOG_I("voice", "Mic reiniciado apos suspensao");
            }
        }
        return VCMD_NONE;
    }

    // ── Tenta drenar um frame de DMA (non-blocking) ──────────────────────────
    if (!M5.Mic.record(_frameBuf, VOICE_FRAME_SAMPLES, 0)) {
        return VCMD_NONE;   // DMA ainda não encheu um frame (~32ms) — ok, volta no próximo loop
    }

    uint32_t rms = computeRMS(_frameBuf, VOICE_FRAME_SAMPLES);
    _lastRMS     = rms;
    _smoothedRMS = _smoothedRMS * 0.72f + (float)rms * 0.28f;

    // ── IDLE: calibra ruído e vigia wake ─────────────────────────────────────
    if (_state == VSTATE_IDLE) {
        // Atualiza EMA do piso de ruído apenas durante silêncio
        // (para não contaminar a estimativa com sons de voz)
        if (rms < _energyThresh) {
            _noiseFloor   = _noiseFloor * VOICE_NOISE_DECAY + (float)rms * (1.0f - VOICE_NOISE_DECAY);
            uint32_t newT = (uint32_t)(_noiseFloor * VOICE_NOISE_MULT);
            _energyThresh = (newT < (uint32_t)VOICE_ENERGY_THRESH_MIN)
                          ? (uint32_t)VOICE_ENERGY_THRESH_MIN
                          : newT;
        }

        // Contagem de frames consecutivos acima do limiar para confirmar wake
        if (rms > _energyThresh) {
            _wakeConsec++;
            if (_wakeConsec >= VOICE_WAKE_FRAMES) {
                _wakeConsec = 0;
                enterListening(now);
            }
        } else {
            _wakeConsec = 0;
        }
        return VCMD_NONE;
    }

    // ── LISTENING: detecta bursts e classifica ────────────────────────────────
    bool voiced = (rms > _energyThresh);

    if (voiced) {
        _lastVoicedMs = now;
        if (!_inBurst) {
            _inBurst      = true;
            _burstStartMs = now;
            _burstCount++;
            LOG_I("voice", "Burst #%d iniciado (rms=%u)", _burstCount, rms);
        }
    } else if (_inBurst && (now - _lastVoicedMs) >= (uint32_t)VOICE_SILENCE_MS) {
        uint32_t dur = _lastVoicedMs - _burstStartMs;
        _inBurst = false;
        if (dur < (uint32_t)VOICE_MIN_BURST_MS) {
            _burstCount--;
            LOG_I("voice", "Burst descartado (%ums < min)", dur);
        } else if (dur >= (uint32_t)VOICE_LONG_BURST_MS) {
            _hasLongBurst = true;
            LOG_I("voice", "Burst longo (#%d, %ums)", _burstCount, dur);
        } else {
            _shortBurstCount++;
            LOG_I("voice", "Burst curto (#%d, %ums)", _shortBurstCount, dur);
        }
    }

    // Critério de encerramento
    bool timeout  = (now - _listenStartMs >= (uint32_t)VOICE_LISTEN_MS);
    bool earlyEnd = (_burstCount > 0 && !_inBurst &&
                     (now - _lastVoicedMs) > (uint32_t)(VOICE_SILENCE_MS + 50));

    if (!timeout && !earlyEnd) return VCMD_NONE;

    // Fecha burst em andamento (timeout chegou com som ainda rolando)
    if (_inBurst) {
        uint32_t dur = now - _burstStartMs;
        _inBurst = false;
        if (dur >= (uint32_t)VOICE_MIN_BURST_MS) {
            if (dur >= (uint32_t)VOICE_LONG_BURST_MS) _hasLongBurst = true;
            else                                        _shortBurstCount++;
        }
    }

    VoiceCommand cmd = classify();

    // Para o mic e agenda reinício: o DMA do I2S leva alguns ms para esvaziar após end().
    // Mantemos SUSPENDED por um breve período antes de chamar begin() novamente.
    // NÃO usamos M5.Speaker.tone() aqui — speaker e mic disputam o mesmo periférico
    // I2S no CoreS3; qualquer acesso ao speaker durante esta janela corrompe o driver
    // e causa stack overflow na spk_task interna do M5Unified.
    // Feedback é visual: o overlay desaparece e a ação ocorre na tela.
    stopMic();
    _state      = VSTATE_SUSPENDED;
    _wakeConsec = 0;
    _resumeAt   = millis() + 60;  // ~2 frames: tempo para o DMA flush antes do begin()

    return cmd;
}

// ── Overlay visual (chamado apenas quando VSTATE_LISTENING) ───────────────────
void voiceCmdDrawStatusIcon(lgfx::LovyanGFX& d) {
    if (_state == VSTATE_DISABLED || _state == VSTATE_SUSPENDED) return;

    // Microfone miniatura: 10×12 px no canto superior esquerdo (x=4, y=2)
    const int MX = 9, MY = 12;
    uint16_t col = (_state == VSTATE_LISTENING) ? COLOR_TEXT_ACCENT : COLOR_TEXT_DIM;

    // Corpo do microfone
    d.fillRoundRect(MX - 3, MY - 9, 6, 9, 2, col);
    // Base em arco (dois pixels de linha)
    for (int dx = -4; dx <= 4; dx++) {
        int dy = (int)sqrtf(max(0.0f, 16.0f - (float)(dx * dx)));
        d.drawPixel(MX + dx, MY + dy - 4, col);
    }
    // Haste
    d.drawFastVLine(MX, MY + 4, 3, col);
    // Base horizontal
    d.drawFastHLine(MX - 3, MY + 7, 7, col);
}

void voiceCmdDrawOverlay(lgfx::LovyanGFX& d) {
    if (_state != VSTATE_LISTENING) return;

    uint32_t elapsed  = millis() - _listenStartMs;
    float    progress = (float)elapsed / (float)VOICE_LISTEN_MS;
    if (progress > 1.0f) progress = 1.0f;

    const int OX = 0, OY = 140, OW = 320, OH = 100;

    d.fillRect(OX, OY, OW, OH, 0x0841);
    d.drawFastHLine(OX, OY, OW, COLOR_DIVIDER);

    // ── Ícone de microfone (maior, à esquerda) ────────────────────────────────
    const int MX = 38, MY = OY + 48;
    d.fillRoundRect(MX - 7, MY - 21, 14, 22, 5, COLOR_TEXT_ACCENT);
    for (int dx = -12; dx <= 12; dx++) {
        float r = 12.0f;
        int   dy = (int)sqrtf(max(0.0f, r * r - (float)(dx * dx)));
        d.drawPixel(MX + dx, MY + dy - 4, COLOR_TEXT_ACCENT);
    }
    d.drawFastVLine(MX, MY + 8, 6, COLOR_TEXT_ACCENT);
    d.drawFastHLine(MX - 7, MY + 14, 14, COLOR_TEXT_ACCENT);

    // ── Barra de energia do microfone ─────────────────────────────────────────
    float norm = (_energyThresh > 0)
               ? (_smoothedRMS / (float)(_energyThresh * 2.2f))
               : 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    const int BAR_X = 68, BAR_Y = OY + 35, BAR_W = 238, BAR_H = 9;
    d.fillRect(BAR_X, BAR_Y, BAR_W, BAR_H, 0x2104);
    int fillW = (int)(norm * BAR_W);
    if (fillW > 0) {
        uint16_t barCol = _inBurst ? COLOR_TIMER_RUNNING : COLOR_TEXT_DIM;
        d.fillRect(BAR_X, BAR_Y, fillW, BAR_H, barCol);
    }
    d.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, COLOR_DIVIDER);

    // Contagem de pulsos
    d.setFont(&fonts::Font0);
    d.setTextDatum(ML_DATUM);
    if (_burstCount > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "pulsos: %d", _burstCount);
        d.setTextColor(COLOR_TEXT_ACCENT, 0x0841);
        d.drawString(buf, BAR_X, BAR_Y - 10);
    } else {
        d.setTextColor(COLOR_TEXT_DIM, 0x0841);
        d.drawString("aguardando...", BAR_X, BAR_Y - 10);
    }

    // Legenda de padrões
    d.setTextColor(COLOR_TEXT_SUBTLE, 0x0841);
    d.drawString(".=prox  ..=volta  ─=timer  ...=zera", BAR_X, OY + 57);

    // Countdown (barra de tempo restante)
    int remain = (int)((1.0f - progress) * BAR_W);
    d.fillRect(BAR_X, OY + OH - 9, BAR_W, 5, 0x2104);
    if (remain > 0) d.fillRect(BAR_X, OY + OH - 9, remain, 5, COLOR_TEXT_ACCENT);

    // Texto piscante
    if ((millis() / 550) % 2 == 0) {
        d.setTextColor(COLOR_TEXT_PRIMARY, 0x0841);
        d.drawString("Ouvindo...", BAR_X, OY + 75);
    }
}
