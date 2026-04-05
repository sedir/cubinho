# M5 CoreS3 — Kitchen Dashboard

Firmware para M5Stack CoreS3 fixado na porta da geladeira. Exibe em alternância (toque na tela):

- **Tela 0** — Relógio + temperatura/umidade da cozinha (sensor SHT40 interno)
- **Tela 1** — Clima externo via API OpenMeteo (gratuita, sem cadastro)

WiFi intermitente: conecta apenas a cada 30 minutos para buscar o clima e desliga em seguida — economiza ~100mA na maior parte do tempo.

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

## Calibração do sensor de temperatura

O chip ESP32 gera calor internamente e pode elevar a leitura do SHT40 em **+1 a +3°C** dependendo da carga de trabalho e ventilação.

Para calibrar:

1. Deixe o dispositivo ligado por 15 minutos (estabilização térmica)
2. Meça a temperatura real com um termômetro externo confiável
3. Calcule o offset: `offset = leitura_real - leitura_sensor`
4. Ajuste em `config.h`:

```cpp
#define TEMP_OFFSET  -1.5f   // exemplo: sensor lê 24°C, real é 22.5°C → offset -1.5
```

---

## Estrutura do projeto

```
src/
├── main.cpp            — loop principal e inicialização
├── config.h            — suas credenciais (não commitado)
├── config.h.example    — template de configuração
├── screen_home.h/.cpp  — tela 0: relógio + ambiente
├── screen_weather.h/.cpp — tela 1: clima externo
├── battery_ui.h        — indicador de bateria (inline)
├── power_manager.h/.cpp — dim automático por inatividade
├── wifi_manager.h/.cpp — WiFi intermitente
└── weather_api.h/.cpp  — cliente API OpenMeteo + parse JSON
```

---

## Notas de hardware

- **Bateria**: 900mAh interna, gerenciada pelo AXP2101 (acessível via `M5.Power`)
- **Sensor SHT40**: I2C endereço `0x44`, integrado ao CoreS3
- **Touch**: capacitivo FT6336U — toque suave é suficiente
- **Display**: ILI9342C 320×240, landscape
- **WiFi**: desligado entre as buscas para economizar energia
