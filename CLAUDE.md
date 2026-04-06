# M5 CoreS3 — Kitchen Dashboard

## Visão geral do projeto

Firmware para M5 CoreS3 fixado na porta da geladeira via módulo magnético.
Exibe em alternância via toque na tela (320×240, landscape):

1. **Relógio + Timer** — hora atual via NTP, data, timer regressivo de cozinha com alarme sonoro
2. **Clima externo** — temperatura + previsão do dia via OpenMeteo API (gratuita, sem chave)

Atualização automática do clima a cada 30 minutos (WiFi intermitente).

---

## Stack técnico

- **Plataforma**: PlatformIO (não Arduino IDE)
- **Framework**: Arduino
- **Board**: M5Stack CoreS3
- **Bibliotecas**:
  - `M5Unified` (display, touch, speaker, WiFi helper)
  - `M5GFX` / `lgfx` (rendering — inclusa no M5Unified)
  - `ArduinoJson ^7` (parse JSON da OpenMeteo)
  - `WiFi.h`, `HTTPClient.h`, `time.h` (built-in ESP32)

---

## platformio.ini

```ini
[env:m5stack-cores3]
platform = espressif32
board = m5stack-cores3
framework = arduino
monitor_speed = 115200
upload_speed = 921600

lib_deps =
  m5stack/M5Unified @ ^0.2.2
  bblanchon/ArduinoJson @ ^7.0.0

build_flags =
  -DCORE_DEBUG_LEVEL=0
  -DBOARD_HAS_PSRAM
```

---

## Estrutura de arquivos

```
src/
├── main.cpp                ← loop principal, inicialização, LTR553
├── config.h                ← credenciais + coordenadas + parâmetros (não commitado)
├── screen_home.h/.cpp      ← tela 0: relógio + timer
├── screen_weather.h/.cpp   ← tela 1: clima externo
├── screen_splash.h         ← tela de boot (inline)
├── battery_ui.h            ← inline, sem .cpp
├── power_manager.h/.cpp
├── wifi_manager.h/.cpp
└── weather_api.h/.cpp
```

---

## config.h

```cpp
#pragma once

#define WIFI_SSID     "ssid"
#define WIFI_PASSWORD "senha"

#define GEO_LATITUDE         36.3938
#define GEO_LONGITUDE       136.4452
#define TIMEZONE_OFFSET_SEC  32400   // UTC+9 (JST)

#define CITY_NAME "Komatsu, Ishikawa"

#define NTP_SERVER_1 "ntp.nict.jp"
#define NTP_SERVER_2 "pool.ntp.org"

// Conforto térmico — usado apenas na tela de clima para colorir temperatura
#define COMFORT_TEMP_MIN  18.0f
#define COMFORT_TEMP_MAX  26.0f
#define COMFORT_HUM_MIN   40.0f
#define COMFORT_HUM_MAX   70.0f

#define WEATHER_UPDATE_INTERVAL_MS  (30UL * 60UL * 1000UL)

// Brilho do display
#define BRIGHTNESS_ACTIVE  150
#define BRIGHTNESS_DIM      50
#define DIM_TIMEOUT_MS   30000

// Deep sleep
#define DEEP_SLEEP_TIMEOUT_MS   (10UL * 60UL * 1000UL)
#define DEEP_SLEEP_WAKEUP_GPIO  21   // INT do FT6336U, ativo em LOW

// Sensor de proximidade LTR553 (acorda do dim)
#define LTR553_PROX_THRESH  80   // 0–2047
```

---

## Tela 0 — Relógio + Timer (`screen_home`)

```
┌─────────────────────────────────┐
│  dom, 05 abr 2026    [===] 85% │  ← data + ícone bateria desenhado
│                                 │
│         10:38:47                │  ← hora grande, laranja 0xFD20
│                                 │
│  ─────────────────────────────  │  ← divisor y=118
│                                 │
│  [❙❙]  04:32                   │  ← timer: ícone + MM:SS, verde=rodando
│   1  3 [5] 10  15  20  30      │  ← presets (só no estado SETTING)
│   Segurar: iniciar              │  ← dica contextual
│                                 │
│           ● ○                   │  ← indicador de tela
└─────────────────────────────────┘
```

### Estados do timer

| Estado | Cor | Ícone | Interação zona inferior |
|---|---|---|---|
| SETTING | Cinza | ▶ | Toque curto: próximo preset · Pressão longa: iniciar |
| RUNNING | Verde | ❙❙ | Toque curto: pausar · Pressão longa: zerar |
| PAUSED | Branco | ▶ | Toque curto: continuar · Pressão longa: zerar |
| DONE | Piscante vermelho/branco | — | Qualquer toque: fechar alarme |

**Presets**: 1, 3, 5, 10, 15, 20, 30 minutos (default: 5 min). Selecionado em branco, demais em cinza escuro.

**Alarme sonoro**: quando o timer chega a 0, `M5.Speaker.tone()` toca sequência 880→1100→1320 Hz a cada 1,8s. Display pisca a 400ms. Som para ao fechar o alarme (`M5.Speaker.stop()`).

### Notas de implementação
- **SHT40 não existe no CoreS3** — o hardware não tem sensor de temperatura/umidade embutido. Código de leitura I2C removido.
- Ícones ▶ e ❙❙ são desenhados como primitivas (`fillTriangle`, `fillRect`), não texto Unicode.

---

## Tela 1 — Clima externo (`screen_weather`)

```
┌─────────────────────────────────┐
│  Komatsu, Ishikawa   [===] 85% │
│                                 │
│        [ícone desenhado]        │  ← sol, nuvem, chuva, neve, raio...
│        Parcialmente nublado     │
│  ─────────────────────────────  │
│  Atual: 15.5°C                  │
│  Max: 18°C  Min: 10°C           │
│  Umidade: 65%                   │
│                                 │
│  Atualizado 10:34       ○ ●    │
└─────────────────────────────────┘
```

**Ícones de clima**: desenhados com primitivas M5GFX (não Unicode/emoji, que não renderiza nas fontes embutidas):
- Sol: círculo amarelo + 8 raios com offsets pré-calculados
- Nuvem: três `fillCircle` sobrepostos + `fillRect` na base
- Chuva: nuvem escura + linhas diagonais
- Neve: nuvem + cruzes sobrepostas
- Trovoada: nuvem escura + triângulos de raio
- Neblina: faixas horizontais com offset alternado

### API OpenMeteo

```
GET http://api.open-meteo.com/v1/forecast
  ?latitude={GEO_LATITUDE}&longitude={GEO_LONGITUDE}
  &current=temperature_2m,relative_humidity_2m,weather_code,windspeed_10m
  &daily=temperature_2m_max,temperature_2m_min,weather_code
  &timezone=Asia%2FTokyo&forecast_days=1
```

**Atenção**: a chave correta no JSON de resposta é `weather_code` (com underscore), não `weathercode`. Tanto o filtro ArduinoJson quanto o parse devem usar `weather_code`.

**Parse**: usar `http.getString()` antes de `deserializeJson()` — leitura via stream (`http.getStream()`) é instável no ESP32 com respostas chunked e retorna zeros.

---

## Anti-flickering — Sprite framebuffer

O display pisca visivelmente se redesenhado com `fillScreen()` direto a cada segundo. Solução: `LGFX_Sprite` como framebuffer off-screen.

```cpp
// Em main.cpp — inicializar APÓS M5.begin() (precisa de parent)
canvas = new LGFX_Sprite(&M5.Display);
canvas->setColorDepth(16);
canvas->createSprite(M5.Display.width(), M5.Display.height());  // 150KB PSRAM

// No loop:
screenHomeDraw(*fb);        // desenha no sprite
canvas->pushSprite(0, 0);  // empurra tudo de uma vez → sem flickering
```

- Declarar como ponteiro global (`LGFX_Sprite*`), inicializar em `setup()` — nunca como global estático sem parent
- Se `createSprite()` falhar (PSRAM indisponível), `fb` cai back para `&M5.Display` com flickering
- Todas as funções de desenho aceitam `lgfx::LovyanGFX&` (classe base de ambos display e sprite)

---

## Interação via touch

Touch tratado por evento de **release** (não press), com rastreamento de posição e duração:

```
wasPressed()  → registra touchStartY, touchStartMs, chama powerOnTouch()
wasReleased() → calcula held = millis() - touchStartMs
                longPress = (held >= 800ms)

Tela home, touchStartY >= 118 → zona do timer
  longPress → screenHomeTimerLongPress()
  toque     → screenHomeTimerTap()

Alarme ativo (DONE) → qualquer toque fecha

Fora da zona do timer, toque curto → alterna entre tela 0 e 1
Toque em dim → só acorda (touchStartedInDim bloqueia ação)
```

---

## Economia de energia (`power_manager`)

Estratégia em dois níveis:

```
POWER_ACTIVE  →  POWER_DIM após DIM_TIMEOUT_MS sem toque
POWER_DIM     →  POWER_ACTIVE no próximo toque ou detecção de proximidade (LTR553)
POWER_DIM     →  deep sleep após DEEP_SLEEP_TIMEOUT_MS (se timer não estiver ativo)
```

Em `POWER_DIM`: WiFi e atualizações de clima continuam. Display não redesenha (exceto relógio, 1×/min).

**Deep sleep**: estado RTC preserva tela atual, dados do clima e estado do timer. Wake por:
- Toque (EXT0, GPIO21, INT do FT6336U — ativo em LOW)
- Timer a cada 30 min → atualiza clima silenciosamente e volta a dormir

**LTR553** (I2C `0x23`, embutido): polled a cada 100ms quando em dim. Se `prox > LTR553_PROX_THRESH` → chama `powerOnTouch()`. Registradores usados: `PS_CONTR` (0x81), `PS_DATA_0/1` (0x8D/0x8E). API via `M5.In_I2C` (não exposto pelo M5Unified).

Bateria ≤ 10% → dim imediato. Bateria ≤ 5% → "BATERIA BAIXA!" piscante sobreposto.

---

## Indicador de bateria (`battery_ui.h`)

Ícone retangular desenhado em pixels no canto superior direito, presente em todas as telas:
- Corpo 24×12px + polo positivo (nub) 3×6px
- Fill colorido proporcional: verde >50%, laranja 20–50%, vermelho ≤20%
- Percentual numérico à esquerda do ícone em `Font0`
- "+" centralizado no corpo quando carregando
- Assinatura: `inline void drawBatteryIndicator(lgfx::LovyanGFX& display)`

---

## WiFi intermitente (`wifi_manager`)

Boot → conecta → NTP → busca clima → **desliga WiFi**.
A cada 30 min: liga → busca → desliga.

Timeout de conexão: 10s. Se falhar, mantém último dado válido e tenta no próximo ciclo.
NTP: sincroniza apenas no boot e após reconexões — RTC interno tem drift desprezível em 30 min.

---

## Tratamento de erros

| Situação | Comportamento |
|---|---|
| WiFi não conecta | "Sem conexao" na tela, tenta no próximo ciclo de 30 min |
| API do clima falha | Mantém último dado + timestamp da última leitura |
| NTP não sincroniza | Exibe `--:--:--` |
| `createSprite()` falha | Fallback para display direto (com flickering), log no serial |
| Bateria ≤ 10% | Dim imediato |
| Bateria ≤ 5% | "BATERIA BAIXA!" piscante |

---

## Notas de hardware — CoreS3

- **Sem SHT40 embutido** — o CoreS3 não tem sensor de temperatura/umidade interno. Se quiser adicionar, conectar externamente no Port A (SDA=GPIO2, SCL=GPIO1) e usar `Wire.begin(2, 1)`.
- **LTR553ALS embutido** — sensor de proximidade + luz ambiente, I2C `0x23` (barramento interno). Usado para acordar o display do dim sem toque. M5Unified não expõe API — acesso direto via `M5.In_I2C`.
- Bateria interna 900mAh, PMIC AXP2101 via `M5.Power`
- Speaker AW88298 via `M5.Speaker.tone(freq, duration_ms)` e `M5.Speaker.setVolume(0–255)`
- Touch capacitivo FT6336U — usar `M5.Touch.getDetail()` com M5Unified ≥ 0.1.6
- Display ILI9342C 320×240, landscape por padrão
- PSRAM 8MB disponível com `BOARD_HAS_PSRAM` — necessário para sprite de 150KB

```cpp
// Inicialização correta para CoreS3
auto cfg = M5.config();
cfg.serial_baudrate = 115200;
M5.begin(cfg);
M5.Speaker.setVolume(200);
```

---

## Restrições de design

- **Single loop** — sem FreeRTOS tasks
- **Sem SPIFFS/LittleFS**
- **HTTP** — OpenMeteo não requer HTTPS; se necessário, usar `client.setInsecure()`
- **Sem LVGL** — M5GFX puro
- **Sem emoji/Unicode estendido** — fontes embutidas (FreeSans, Font0) cobrem apenas Latin; ícones são primitivas desenhadas
- **Deep sleep** ativo após `DEEP_SLEEP_TIMEOUT_MS` — estado persistido em `RTC_DATA_ATTR`. Wake por EXT0 (toque) ou timer (clima). Timer não pode estar rodando.
