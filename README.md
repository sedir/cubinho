# M5 CoreS3 — Cubinho

Firmware para M5Stack CoreS3 fixado na porta da geladeira. Exibe em alternância via swipe ou toque na tela:

- **Tela 0** — Relógio + 3 timers regressivos de cozinha com alarme sonoro
- **Tela 1** — Clima externo via API OpenMeteo (gratuita, sem cadastro)
- **Tela 2** — Informações do sistema (bateria, IP, boot count, etc.)
- **Tela 3** — Configurações em runtime (brilho, dim, WiFi, deep sleep, etc.)

WiFi intermitente: conecta a cada 30 minutos para buscar o clima e desliga em seguida (ou keep-alive permanente se configurado). Deep sleep automático após inatividade — o sensor de proximidade LTR553 (embutido) e o acelerômetro acordam o display ao aproximar a mão ou detectar movimento.

---

## Requisitos

- [PlatformIO](https://platformio.org/install) — extensão do VS Code ou CLI:
  ```
  pip install platformio
  ```
- M5Stack CoreS3
- Rede WiFi 2.4GHz

---

## Como compilar e fazer upload

### 1. Clone o repositório

```bash
git clone <url-do-repo>
cd cidinha
```

### 2. Crie seu `config.h`

```bash
cp src/config.h.example src/config.h
```

Abra [src/config.h](src/config.h) e ajuste pelo menos as coordenadas GPS, o fuso horário e os defaults de brilho/energia se necessário.

Se quiser exibir eventos do seu calendario na home, voce pode:
- definir `CALENDAR_ICS_URL` no `config.h` como valor inicial, ou
- abrir `Configuracoes` -> `Configurar iCal` no aparelho e preencher a URL pelo navegador usando o IP mostrado na tela.

O servidor local do calendario fica ativo apenas enquanto esse modo estiver aberto. O firmware busca os eventos do dia durante a atualizacao de rede e prioriza esse resumo na tela inicial.

As credenciais WiFi nao ficam mais no firmware: no primeiro boot, ou apos limpar as credenciais, o dispositivo abre o AP `Cubinho-Setup` para configuracao via portal cativo.

### 3. Compile e envie para o CoreS3

```bash
# Apenas compilar (verifica erros)
pio run

# Compilar + upload
pio run --target upload

# Monitor serial (debug)
pio device monitor
```

Ou use os botões da extensão PlatformIO no VS Code.

---

## Estrutura do projeto

```
src/
├── main.cpp               — loop principal, inicialização, LTR553, acelerômetro
├── config.h               — configuração local do dispositivo (não commitado)
├── config.h.example       — template de configuração
├── theme.h                — paleta de cores e constantes de layout
├── logger.h               — macros LOG_I/W/E (Serial + Telnet)
├── screen_home.h/.cpp     — tela 0: relógio + 3 timers com tabs
├── screen_weather.h/.cpp  — tela 1: clima externo
├── screen_system.h/.cpp   — tela 2: informações do sistema
├── screen_settings.h/.cpp — tela 3: configurações em runtime com scroll
├── screen_splash.h        — tela de boot (inline)
├── status_ui.h            — bateria + status/header helpers (inline)
├── power_manager.h/.cpp   — dim + deep sleep + auto-brilho ALS + fade suave
├── wifi_manager.h/.cpp    — WiFi intermitente + async + portal cativo
├── weather_api.h/.cpp     — cliente API OpenMeteo + parse JSON
├── runtime_config.h/.cpp  — configurações persistidas em NVS
├── led_strip.h/.cpp       — LEDs WS2812 M5GO3 Bottom (10 LEDs, GPIO5)
├── telnet_log.h/.cpp      — log via Telnet porta 23 + SD card
├── ota_manager.h/.cpp     — atualização OTA sem fio (ArduinoOTA)
├── events.h/.cpp          — agenda de eventos do SD card (/events.json)
├── calendar_feed.h/.cpp   — leitor de feed iCal/ICS para eventos do dia
├── notifications.h/.cpp   — push via HTTP/MQTT, toast, gaveta e endpoint /health
├── ha_discovery.h/.cpp    — Home Assistant MQTT Discovery (auto-registro)
├── i18n.h                 — strings user-facing (hoje PT-BR, pronto para i18n)
└── chime_wav.h            — audio WAV do alarme (array PROGMEM)
```

---

## Funcionalidades

### Timers de cozinha
- 3 timers simultâneos com tabs para trocar o slot focado
- Presets rápidos: 1, 3, 5, 10, 15, 20, 30 minutos
- Alarme sonoro com WAV e display piscante ao atingir zero
- Estado preservado através do deep sleep via `RTC_DATA_ATTR`

### Clima
- Temperatura atual, máxima, mínima e umidade
- Ícones desenhados com primitivas M5GFX (sol, nuvem, chuva, neve, trovoada, neblina)
- Long press na tela → força atualização imediata

### Configurações em runtime (NVS)
- WiFi keep-alive, intervalo do clima, brilho, timeout dim, auto-brilho, deep sleep, wake por acelerômetro
- Alterações salvas imediatamente e aplicadas sem reboot

### Economia de energia
- Fade suave de brilho ao entrar/sair do dim
- Auto-brilho via sensor ALS do LTR553 (ajusta ao ambiente)
- Wake por proximidade (LTR553 PS), toque ou acelerômetro
- Deep sleep com wake por toque (EXT0, GPIO21) ou timer (atualização do clima)
- Orientação automática via acelerômetro (rotações landscape 1 e 3)
- **Modo cozinha ativa**: long press na área do relógio mantém o display aceso por 1h sem tocar (ideal enquanto cozinha)

### Conectividade
- Portal cativo WiFi: após 3 falhas consecutivas, abre AP "Cubinho-Setup" para reconfiguração
- QR code de pareamento no portal — celulares modernos conectam sem digitar senha
- OTA via ArduinoOTA (hostname "cubinho") quando keep-alive ativo
- **OTA via navegador**: formulário no portal de configuração aceita upload de `.bin` pela LAN
- Log via Telnet (porta 23) e SD card quando disponível

### Integração
- **Notificações push** via HTTP (`POST /notify` na porta 8080) ou MQTT
- **Home Assistant MQTT Discovery**: entidades auto-registradas (bateria, RSSI, uptime, alarme, timer)
- **Endpoint `/health`**: JSON com telemetria (uptime, heap, RSSI, idade do clima, MQTT) para monitoração

### Robustez
- Task watchdog (30s) — reboota se o loop travar
- Jitter exponencial no reconnect MQTT para evitar thundering herd
- Boot count persistido em `RTC_DATA_ATTR` (visível na tela System e no `/health`)

---

## Notas de hardware

- **Bateria**: 900mAh interna, gerenciada pelo AXP2101 (`M5.Power`)
- **Proximidade + luz**: LTR553 embutido (I2C `0x23`) — wake por proximidade e auto-brilho; threshold via `LTR553_PROX_THRESH`
- **IMU**: acelerômetro para orientação automática e wake por movimento
- **Touch**: capacitivo FT6336U — wake do deep sleep (INT no GPIO21)
- **Display**: ILI9342C 320×240, landscape
- **LEDs**: 10× WS2812 no módulo M5GO3 Bottom (GPIO5, FastLED)
- **PSRAM**: 8MB — necessário para sprites framebuffer (~150KB cada)
