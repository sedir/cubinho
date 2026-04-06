# M5 CoreS3 — Kitchen Dashboard

Firmware para M5Stack CoreS3 fixado na porta da geladeira. Exibe em alternância (toque na tela):

- **Tela 0** — Relógio + timer regressivo de cozinha com alarme sonoro
- **Tela 1** — Clima externo via API OpenMeteo (gratuita, sem cadastro)

WiFi intermitente: conecta apenas a cada 30 minutos para buscar o clima e desliga em seguida. Deep sleep automático após inatividade — o sensor de proximidade LTR553 (embutido) acorda o display ao aproximar a mão.

---

## Requisitos

- [PlatformIO](https://platformio.org/install) — instale como extensão do VS Code ou via CLI:
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
cd m5cores3-kitchen-dashboard
```

### 2. Crie seu `config.h`

```bash
cp src/config.h.example src/config.h
```

Abra `src/config.h` e preencha:

```cpp
#define WIFI_SSID     "nome_da_sua_rede"
#define WIFI_PASSWORD "sua_senha"
```

Ajuste também as coordenadas GPS e o fuso horário se não estiver em Komatsu, Ishikawa (Japão, UTC+9).

### 3. Compile e envie para o CoreS3

```bash
# Apenas compilar (verifica erros)
pio run

# Compilar + upload
pio run --target upload

# Monitor serial (debug)
pio device monitor
```

Ou use os botões da extensão PlatformIO no VS Code (ícones ✓ → para compilar, → para upload).

---

## Estrutura do projeto

```
src/
├── main.cpp              — loop principal, inicialização, LTR553
├── config.h              — suas credenciais (não commitado)
├── config.h.example      — template de configuração
├── screen_home.h/.cpp    — tela 0: relógio + timer
├── screen_weather.h/.cpp — tela 1: clima externo
├── screen_splash.h       — tela de boot (inline)
├── battery_ui.h          — indicador de bateria (inline)
├── power_manager.h/.cpp  — dim + deep sleep por inatividade
├── wifi_manager.h/.cpp   — WiFi intermitente + async state machine
└── weather_api.h/.cpp    — cliente API OpenMeteo + parse JSON
```

---

## Notas de hardware

- **Bateria**: 900mAh interna, gerenciada pelo AXP2101 (acessível via `M5.Power`)
- **Proximidade**: LTR553 embutido (I2C `0x23`) — acorda o display do dim ao aproximar a mão; threshold ajustável via `LTR553_PROX_THRESH` em `config.h`
- **Touch**: capacitivo FT6336U — acorda o dispositivo do deep sleep (INT no GPIO21)
- **Display**: ILI9342C 320×240, landscape
- **WiFi**: desligado entre as buscas para economizar energia
- **Deep sleep**: ativa após `DEEP_SLEEP_TIMEOUT_MS` sem interação; wake por toque ou timer (atualização do clima)
