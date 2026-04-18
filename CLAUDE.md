# M5 CoreS3 — Cubinho

## Visão geral do projeto

Firmware para M5 CoreS3 fixado na porta da geladeira via módulo magnético.
Exibe em alternância via swipe ou toque na tela (320×240, landscape):

1. **Relógio + Timers** — hora atual via NTP, data, 3 timers regressivos de cozinha com alarme sonoro
2. **Clima externo** — temperatura + previsão do dia via OpenMeteo API (gratuita, sem chave)
3. **Sistema** — informações do dispositivo (boot count, bateria, IP, etc.)
4. **Configurações** — ajustes em runtime persistidos em NVS (brilho, dim, WiFi keep-alive, etc.)

Atualização automática do clima a cada 30 minutos (WiFi intermitente ou keep-alive).

---

## Stack técnico

- **Plataforma**: PlatformIO (não Arduino IDE)
- **Framework**: Arduino
- **Board**: M5Stack CoreS3
- **Bibliotecas**:
  - `M5Unified` (display, touch, IMU, speaker, WiFi helper)
  - `M5GFX` / `lgfx` (rendering — inclusa no M5Unified)
  - `ArduinoJson ^7` (parse JSON da OpenMeteo)
  - `FastLED` (LEDs WS2812 do M5GO3 Bottom)
  - `PubSubClient` (cliente MQTT para notificações push)
  - `WiFi.h`, `WebServer.h`, `HTTPClient.h`, `time.h`, `Preferences.h` (built-in ESP32)

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
  fastled/FastLED @ ^3.7.0
  knolleary/PubSubClient @ ^2.8

build_flags =
  -DCORE_DEBUG_LEVEL=0
  -DBOARD_HAS_PSRAM
```

---

## Estrutura de arquivos

```
src/
├── main.cpp                ← loop principal, inicialização, LTR553, acelerômetro, transições
├── config.h                ← credenciais + coordenadas + parâmetros (não commitado)
├── config.h.example        ← template de configuração
├── theme.h                 ← paleta de cores RGB565 e constantes de layout
├── logger.h                ← macros LOG_I/W/E → Serial + Telnet
├── screen_home.h/.cpp      ← tela 0: relógio + 3 timers com tabs
├── screen_weather.h/.cpp   ← tela 1: clima externo
├── screen_system.h/.cpp    ← tela 2: informações do sistema
├── screen_settings.h/.cpp  ← tela 3: configurações em runtime com scroll
├── screen_splash.h         ← tela de boot (inline)
├── status_ui.h             ← bateria + status/header helpers (inline)
├── power_manager.h/.cpp    ← dim + deep sleep + auto-brilho ALS + fade suave
├── wifi_manager.h/.cpp     ← WiFi intermitente + async + portal cativo
├── weather_api.h/.cpp      ← cliente OpenMeteo + parse JSON
├── runtime_config.h/.cpp   ← RuntimeConfig persistido em NVS
├── led_strip.h/.cpp        ← LEDs WS2812 M5GO3 Bottom (10 LEDs, GPIO5)
├── telnet_log.h/.cpp       ← log via Telnet porta 23 + SD card
├── ota_manager.h/.cpp      ← ArduinoOTA (ativo com WiFi keep-alive)
├── events.h/.cpp           ← agenda de eventos lida do SD card (/events.json)
├── notifications.h/.cpp    ← push notifications (HTTP + MQTT) + toast + gaveta
└── chime_wav.h             ← audio WAV do alarme (array PROGMEM)
```

---

## config.h

```cpp
#pragma once

#define WIFI_KEEP_ALIVE false  // true = WiFi sempre conectado (habilita OTA e Telnet)

#define GEO_LATITUDE         36.3938
#define GEO_LONGITUDE       136.4452
#define TIMEZONE_OFFSET_SEC  32400   // UTC+9 (JST)

#define CITY_NAME "Komatsu, Ishikawa"

#define NTP_SERVER_1 "ntp.nict.jp"
#define NTP_SERVER_2 "pool.ntp.org"

// Conforto térmico — coloração da temperatura na tela de clima
#define COMFORT_TEMP_MIN  18.0f
#define COMFORT_TEMP_MAX  26.0f
#define COMFORT_HUM_MIN   40.0f
#define COMFORT_HUM_MAX   70.0f

#define WEATHER_UPDATE_INTERVAL_MS  (30UL * 60UL * 1000UL)

// Brilho do display
#define BRIGHTNESS_ACTIVE          150
#define BRIGHTNESS_DIM              50
#define BRIGHTNESS_MIN_FLOOR        80   // piso absoluto para auto-brilho
#define BRIGHTNESS_FADE_STEP         2   // unidades por tick de fade (~50fps)
#define BRIGHTNESS_FADE_INTERVAL_MS 20

// Auto-brilho via sensor ALS do LTR553
#define AUTO_BRIGHTNESS_ENABLED  true
#define AUTO_BRIGHTNESS_MIN      BRIGHTNESS_MIN_FLOOR
#define AUTO_BRIGHTNESS_MAX      200

#define DIM_TIMEOUT_MS  30000

// Deep sleep
#define DEEP_SLEEP_TIMEOUT_MS   (10UL * 60UL * 1000UL)
#define DEEP_SLEEP_WAKEUP_GPIO  21   // INT do FT6336U, ativo em LOW

// Sensor de proximidade LTR553
#define LTR553_PROX_THRESH  10   // 0–2047

// Wake por acelerômetro enquanto o display está em dim
#define ACCEL_WAKE_ENABLED    true
#define ACCEL_WAKE_THRESHOLD  0.18f

// OTA
#define OTA_ENABLED   true
#define OTA_HOSTNAME  "cubinho"

// Timers de cozinha simultâneos
#define MAX_TIMERS  3

// Portal cativo — entra em AP mode após N falhas consecutivas de WiFi
#define WIFI_PORTAL_FAIL_THRESHOLD  3
#define WIFI_PORTAL_AP_NAME         "Cubinho-Setup"
```

---

## Tela 0 — Relógio + Timers (`screen_home`)

```
┌─────────────────────────────────┐
│  dom, 05 abr 2026    [===] 85% │  ← data + ícone bateria desenhado
│                                 │
│         10:38:47                │  ← hora grande, laranja 0xFD20
│  próximo: 05/04 12:00 Almoço   │  ← próximo evento do SD card (se houver)
│  ─────────────────────────────  │  ← divisor y=118 (TIMER_ZONE_Y)
│  [ T1 ]  [ T2 ]  [ T3 ]       │  ← tabs de slot (48×20px, gap=6)
│  [❙❙]  04:32                   │  ← timer focado: ícone + MM:SS, verde=rodando
│   1  3 [5] 10  15  20  30      │  ← presets (só no estado SETTING)
│   Segurar: iniciar              │  ← dica contextual
└─────────────────────────────────┘
```

### Timers múltiplos (MAX_TIMERS = 3)

Há 3 slots de timer simultâneos. Tabs no topo da zona do timer (y=TIMER_ZONE_Y a TIMER_ZONE_Y+26)
permitem trocar o slot focado. Cada slot mantém estado independente.

### Estados do timer

| Estado | Cor | Ícone | Interação zona inferior |
|---|---|---|---|
| SETTING | Cinza | ▶ | Toque curto: próximo preset · Pressão longa: iniciar |
| RUNNING | Verde | ❙❙ | Toque curto: pausar · Pressão longa: zerar |
| PAUSED | Branco | ▶ | Toque curto: continuar · Pressão longa: zerar |
| DONE | Piscante vermelho/branco | — | Qualquer toque: fechar alarme |

**Presets**: 1, 3, 5, 10, 15, 20, 30 minutos (default: 5 min). Selecionado em branco, demais em cinza escuro.

**Alarme sonoro**: `M5.Speaker.playWav(CHIME_WAV, CHIME_WAV_LEN)` a cada 3,2s. Display pisca a 400ms. Volume alarme = 160; volume UI = 85. Som para ao fechar (`M5.Speaker.stop()`).

### Notas de implementação
- **SHT40 não existe no CoreS3** — código de leitura I2C removido.
- Ícones ▶ e ❙❙ são desenhados como primitivas (`fillTriangle`, `fillRect`), não texto Unicode.
- `TIMER_ZONE_Y = 118` e `SCREEN_COUNT = 4` definidos em `theme.h`.

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

**Long press** na tela → força atualização imediata do clima (`wifiForceRefresh()`).

**Ícones de clima**: desenhados com primitivas M5GFX (sem Unicode/emoji):
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

**Atenção**: a chave correta no JSON é `weather_code` (com underscore). Tanto o filtro ArduinoJson quanto o parse devem usar `weather_code`.

**Parse**: usar `http.getString()` antes de `deserializeJson()` — leitura via stream (`http.getStream()`) é instável no ESP32 com respostas chunked.

---

## Tela 2 — Sistema (`screen_system`)

Exibe informações do dispositivo: boot count, nível de bateria, IP, RSSI WiFi, uptime, etc.

---

## Tela 3 — Configurações (`screen_settings`)

Lista de opções com scroll vertical (lerp ease-out). Persistidas em NVS via `RuntimeConfig`.

| Campo | Tipo | Descrição |
|---|---|---|
| `wifiKeepAlive` | bool | WiFi permanente (habilita OTA e Telnet) |
| `weatherIntervalMin` | int | Intervalo de atualização do clima (minutos) |
| `brightnessActive` | int | Brilho em modo ativo (0–255) |
| `dimTimeoutSec` | int | Segundos de inatividade até dim |
| `autoBrightness` | bool | Auto-brilho via sensor ALS |
| `deepSleepTimeoutMin` | int | Minutos até deep sleep (0 = nunca) |
| `accelWake` | bool | Acorda do dim ao detectar movimento |
| `mqttEnabled` | bool | Habilita cliente MQTT para notificações push |
| `mqttHost` | char[64] | Host do broker (ex: `broker.hivemq.com`) |
| `mqttPort` | int | Porta do broker (default 1883) |
| `mqttUser` / `mqttPass` | char[] | Credenciais opcionais |
| `mqttTopic` | char[64] | Tópico de subscrição (ex: `cubinho/notif`) |

**Tap** → altera valor / toggle. **Long press** → ações destrutivas (ex: factory reset NVS, limpar credenciais WiFi). **Swipe direita** → volta para tela System (tela 2). **Swipe esquerda** → ignorado.

**Scroll suave**: `g_settingsScrollTarget` é o destino em px; `g_settingsScrollAnim` é a posição atual interpolada por lerp `+= diff * 0.22f` a cada frame.

---

## Anti-flickering — Sprite framebuffer

O display pisca visivelmente se redesenhado com `fillScreen()` direto a cada segundo. Solução: dois `LGFX_Sprite` como framebuffers off-screen.

```cpp
// Em main.cpp — inicializar APÓS M5.begin() (precisa de parent)
canvas = new LGFX_Sprite(&M5.Display);
canvas->setColorDepth(16);
canvas->createSprite(M5.Display.width(), M5.Display.height());  // ~150KB PSRAM

// Sprite de transição pré-alocado (forçado para PSRAM)
transSprite = new LGFX_Sprite(&M5.Display);
transSprite->setPsram(true);
transSprite->createSprite(w, h);

// No loop:
drawCurrentScreen(*fb);     // desenha no sprite
canvas->pushSprite(0, 0);  // empurra tudo de uma vez → sem flickering
```

- Declarar como ponteiros globais, inicializar em `setup()` — nunca como globais estáticos sem parent.
- Se `createSprite()` falhar (PSRAM indisponível), `fb` cai back para `&M5.Display` com flickering.
- Todas as funções de desenho aceitam `lgfx::LovyanGFX&` (classe base de display e sprite).

---

## Transição animada entre telas

`animateTransition(fromScreen, toScreen, direction)` — slide horizontal com ease-out cúbico:

- 12 frames × 16ms ≈ 190ms total (~60fps)
- Requer `canvas` (tela antiga) e `transSprite` (tela nova) alocados
- `direction >= 0` → nova tela entra pela direita (swipe left → próxima)
- `direction < 0` → nova tela entra pela esquerda (swipe right → anterior)
- Se sprites indisponíveis → troca instantânea

---

## Interação via touch

Touch tratado por evento de **release** com rastreamento de posição e duração:

```
wasPressed()  → registra touchStartX/Y, touchStartMs, chama powerOnTouch()
wasReleased() → calcula held = millis() - touchStartMs
                longPress = (held >= 800ms)
                isSwipe = (|deltaX| >= 30 && |deltaX| > |deltaY|)

Gaveta de notificações (prioridade máxima, antes de qualquer tela):
  Aberta → notifDrawerHandleRelease() consome o evento
  Fechada + swipe da borda superior (notifShouldOpenFromSwipe) → notifDrawerOpen()
  Toast ativo + tap no topo → dismiss + abre gaveta

Swipe horizontal → animateTransition() para tela anterior/próxima
  Tela 3 (Settings): swipe direita → volta para System (tela 2); esquerda ignorado

Tela 3, sem swipe:
  Swipe vertical (|deltaY| >= 20) → scroll da lista de configurações
  Long press → screenSettingsHandleLongPress()
  Tap → screenSettingsHandleTap() → salva + aplica se mudou

Tela 0, touchStartY em [TIMER_ZONE_Y, TIMER_ZONE_Y+26):
  Tap nas tabs → screenHomeTimerSwitchSlot(i)

Tela 0, touchStartY >= TIMER_ZONE_Y+26:
  Long press → screenHomeTimerLongPress()
  Tap → screenHomeTimerTap(touchStartX)

Tela 0, alarme DONE: qualquer toque fecha o alarme

Tela 1, long press → wifiForceRefresh() (atualização imediata do clima)

Tap fora dessas zonas → animateTransition() para próxima tela

Toque em dim → só acorda (touchStartedInDim bloqueia ação, exceto alarme ativo)
```

---

## Economia de energia (`power_manager`)

Estratégia em dois níveis:

```
POWER_ACTIVE  →  POWER_DIM após dimTimeoutMs sem toque
POWER_DIM     →  POWER_ACTIVE no próximo toque, proximidade (LTR553) ou acelerômetro
POWER_DIM     →  deep sleep após deepSleepTimeoutMs (se timer não estiver ativo)
```

**Fade suave de brilho**: `_currentBrightness` caminha em direção a `_targetBrightness` em steps de `BRIGHTNESS_FADE_STEP` a cada `BRIGHTNESS_FADE_INTERVAL_MS` (~50fps).

**Auto-brilho ALS**: quando habilitado, lê o canal CH0 do LTR553 e mapeia linearmente para `AUTO_BRIGHTNESS_MIN`–`AUTO_BRIGHTNESS_MAX`.

**Acelerômetro** (`accelWake`): polled a cada 200ms em dim. Se `|mag_atual - mag_anterior| > ACCEL_WAKE_THRESHOLD` → `powerOnTouch()`.

**Orientação automática**: polled a cada 500ms quando ativo. `ay > 0.4` → rotação 1; `ay < -0.4` → rotação 3 (apenas landscape, dimensões do sprite não mudam).

**Deep sleep**: estado RTC preserva tela atual, dados do clima e estado dos 3 timers. Wake por:
- Toque (EXT0, GPIO21, INT do FT6336U — ativo em LOW)
- Timer a cada 30 min → atualiza clima silenciosamente e volta a dormir

**Wake-lock por fonte externa**: `powerIsOnExternalPower()` retorna true quando `M5.Power.isCharging()` ou quando `batteryLevel >= 100` (carregador plugado com bateria cheia). Nesse caso `powerShouldDeepSleep()` retorna false — o aparelho fica sempre aceso enquanto conectado. Útil para manter notificações push recebendo em tempo real.

Também bloqueiam dim/deep sleep: alarme ativo, timer ou cronômetro rodando, gaveta de notificações visível, página unificada de configuração aberta.

Em `POWER_DIM`: WiFi e atualizações de clima continuam. Display não redesenha (exceto relógio, 1×/min).

Bateria ≤ 10% → dim imediato. Bateria ≤ 5% → "BATERIA BAIXA!" piscante sobreposto.

---

## LEDs WS2812 (`led_strip`)

10 LEDs no módulo M5GO3 Bottom, controlados via GPIO5 com FastLED.

| Estado | Efeito |
|---|---|
| Dim | Apagados |
| Alarme ativo | Pisca vermelho sincronizado com o display (400ms) |
| Timer RUNNING | Breathing verde |
| Normal/ativo | Apagados |

`ledOff()` deve ser chamado antes do deep sleep.

---

## Log (`logger.h` + `telnet_log`)

Log estruturado com timestamp, nível e tag:
```
[10:32:15] I [wifi    ] Conectado — IP 192.168.1.42
[10:32:16] W [power   ] Bateria baixa: 8%
[10:32:17] E [main    ] Sprite nao alocado
```

- **Serial**: sempre ativo
- **Telnet**: porta 23, uma conexão por vez; requer WiFi keep-alive
- **SD card**: se disponível (`sdIsAvailable()`), escreve também em arquivo
- Macros: `LOG_I(tag, fmt, ...)`, `LOG_W(tag, fmt, ...)`, `LOG_E(tag, fmt, ...)`

---

## OTA (`ota_manager`)

ArduinoOTA com hostname `OTA_HOSTNAME` ("cubinho"). Ativo apenas quando WiFi keep-alive habilitado. `otaUpdate()` chamado no loop principal.

---

## Portal WiFi cativo (`wifi_manager`)

Após `WIFI_PORTAL_FAIL_THRESHOLD` falhas consecutivas de conexão:
- Abre AP com SSID `WIFI_PORTAL_AP_NAME` ("Cubinho-Setup")
- Exibe `drawWifiPortalScreen()` em fullscreen
- DNS e HTTP servidos por `wifiPortalUpdate()` no loop
- Credenciais salvas em NVS: `wifiHasStoredCredentials()` / `wifiClearStoredCredentials()`

---

## Eventos (`events`)

Agenda lida do SD card (`/events.json`), máximo `MAX_EVENTS = 10`:

```json
[{"name":"Almoço","month":4,"day":5,"hour":12,"minute":0}]
```

`eventsGetNext()` retorna o próximo evento futuro. Exibido na tela Home abaixo do relógio. Verificado a cada 30s no loop via `updateNextEvent()`.

---

## WiFi intermitente (`wifi_manager`)

**Modo intermitente** (`wifiKeepAlive = false`):
Boot → conecta → NTP → busca clima → desliga WiFi. A cada 30 min: liga → busca → desliga.

**Modo keep-alive** (`wifiKeepAlive = true`):
WiFi permanece conectado → habilita OTA, Telnet e atualizações contínuas.

Timeout de conexão: 10s. Se falhar, mantém último dado válido e tenta no próximo ciclo.
NTP: sincronizado apenas no boot e após reconexões — RTC interno tem drift desprezível em 30 min.

---

## Notificações push (`notifications`)

Dois protocolos recebem mensagens e entregam ao mesmo `notifPush()`. Ambos exigem **WiFi keep-alive** — sem ele o rádio fica desligado a maior parte do tempo e nenhum dos servidores sobe. Não funcionam em deep sleep (CPU desligada).

### HTTP server — porta 8080

`WebServer` separado do da porta 80 (que é usada pelo portal cativo e pela página unificada de config). Iniciado/parado por `notifServerPoll()` no loop conforme `WL_CONNECTED && wifiIsKeepAlive() && !wifiIsPortalMode()`.

Endpoints:
- `GET /` — formulário HTML de teste
- `POST /notify` — JSON `{title, body, icon}` ou form urlencoded (campos iguais)
- `GET /list` — JSON com histórico
- `POST /clear` — limpa tudo

Uso típico: iOS Shortcuts, `curl`, webhooks locais na LAN.

### Cliente MQTT

`PubSubClient` conecta ao broker configurado em `RuntimeConfig` (`mqttEnabled`, `mqttHost`, `mqttPort`, `mqttUser`, `mqttPass`, `mqttTopic`). Reconexão com backoff exponencial (2s → 60s). Payload aceita:
- JSON `{"title":"..","body":"..","icon":"info|ok|warn|error"}`
- Texto puro (usa como corpo, título vira "MQTT")

Vantagem sobre HTTP: atravessa NAT (outbound pro broker) — ideal pra Home Assistant, n8n, IFTTT via bridge. Configuração via página web (`/api/state` e `/api/save`).

### Storage e UI

- Histórico circular de até `NOTIF_MAX = 12` em RAM; `notifPush()` empurra novas no índice 0.
- **Toast** de 5s no topo ao receber nova notificação; chama `powerOnTouch()` pra acordar o display.
- **Gaveta** (drawer): swipe da borda superior abre. Animação ease-out cúbico em `notifDrawerUpdate()`. Exibe até 3 itens; overflow mostra "+ N mais…". Swipe-up, X ou botão Limpar fecham. Long press na lista limpa tudo.
- Gaveta visível bloqueia dim/deep sleep.

### Ícones

`NotifIcon { INFO, WARN, ERROR, OK }` — mapeados pra cores (azul/laranja/vermelho/verde) e glifos (`i/!/x/v`).

---

## UI de status (`status_ui.h`)

Helpers inline desenhados no cabeçalho e rodapé das telas:
- Corpo 24×12px + polo positivo (nub) 3×6px
- Fill colorido proporcional: verde >50%, laranja 20–50%, vermelho ≤20%
- Percentual numérico à esquerda do ícone em `Font0`
- "+" centralizado no corpo quando carregando
- `inline void drawBatteryIndicator(lgfx::LovyanGFX& display)`
- `inline void drawBatteryWarning(lgfx::LovyanGFX& display, int y = 218)`
- `inline void drawScreenIndicator(lgfx::LovyanGFX& display, int current, int total)`

---

## Tratamento de erros

| Situação | Comportamento |
|---|---|
| WiFi não conecta (< threshold) | "Sem conexao" na tela, tenta no próximo ciclo de 30 min |
| WiFi não conecta (≥ threshold) | Entra em modo portal cativo (AP "Cubinho-Setup") |
| API do clima falha | Mantém último dado + timestamp da última leitura |
| NTP não sincroniza | Exibe `--:--:--` |
| `createSprite()` falha | Fallback para display direto (com flickering), LOG_E |
| `transSprite` não alocado | Transição instantânea sem animação |
| Bateria ≤ 10% | Dim imediato |
| Bateria ≤ 5% | "BATERIA BAIXA!" piscante sobreposto |

---

## Notas de hardware — CoreS3

- **Sem SHT40 embutido** — o CoreS3 não tem sensor de temperatura/umidade interno.
- **LTR553ALS embutido** — I2C `0x23` (barramento interno). Usado para wake por proximidade (PS) e auto-brilho (ALS). M5Unified não expõe API — acesso direto via `M5.In_I2C`.
  - PS: `PS_CONTR` (0x81), `PS_DATA_0/1` (0x8D/0x8E)
  - ALS: `ALS_CONTR` (0x80), `ALS_DATA_CH0` (0x8A/0x8B)
- **IMU** — `M5.Imu.getAccel(&ax, &ay, &az)` — orientação e wake por movimento.
- Bateria interna 900mAh, PMIC AXP2101 via `M5.Power`
- Speaker AW88298 via `M5.Speaker.playWav()` e `M5.Speaker.setVolume(0–255)`
- Touch capacitivo FT6336U — `M5.Touch.getDetail()` com M5Unified ≥ 0.1.6
- Display ILI9342C 320×240, landscape por padrão
- PSRAM 8MB disponível com `BOARD_HAS_PSRAM` — necessário para sprites (~150KB cada)

```cpp
// Inicialização correta para CoreS3
auto cfg = M5.config();
cfg.serial_baudrate = 115200;
M5.begin(cfg);
M5.Speaker.setVolume(85);   // volume de UI; alarme usa 160
```

---

## Restrições de design

- **Single loop** — sem FreeRTOS tasks
- **Sem SPIFFS/LittleFS** — configurações em NVS; eventos/log em SD card
- **HTTP** — OpenMeteo não requer HTTPS; se necessário, usar `client.setInsecure()`
- **Sem LVGL** — M5GFX puro
- **Sem emoji/Unicode estendido** — fontes embutidas cobrem apenas Latin; ícones são primitivas desenhadas
- **Deep sleep** ativo após `deepSleepTimeoutMin` — estado persistido em `RTC_DATA_ATTR`. Timer não pode estar rodando.
