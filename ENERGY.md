# Guia de Eficiência Energética — Cubinho

Princípio central: **economizar energia onde o usuário não percebe; não economizar onde percebe.**

O Cubinho é fixado na porta da geladeira. O uso é efêmero: alguém chega, olha, interage por 3–15 segundos, vai embora. A bateria de 900 mAh precisa durar dias sem recarregar. Toda decisão de design de recurso deve passar por essa lente.

---

## Orçamento de consumo estimado

| Componente | Ativo | Dim | Deep sleep |
|---|---|---|---|
| Display (brilho 150) | ~90 mA | ~17 mA (brilho 50) | 0 mA |
| CPU ESP32-S3 a 80 MHz | ~30 mA | ~15 mA (light sleep) | ~0.01 mA |
| WiFi (conectado) | +80–120 mA | — | — |
| LEDs WS2812 (10×, breathing) | ~15 mA (BREATH_MAX 80) | 0 mA | 0 mA |
| LTR553 + IMU (polling) | ~3 mA | ~1 mA | 0 mA |
| **Total sem WiFi** | **~138 mA** | **~33 mA** | **~0.01 mA** |
| **Total com WiFi** | **~220–260 mA** | — | — |

Com uso típico (5 min/h ativo + 55 min dim + deep sleep noturno), a autonomia estimada é de **2–4 dias**. WiFi keep-alive permanente reduz isso para **8–12 horas**.

---

## Arquitetura atual de economia

### Dois níveis de inatividade

```
POWER_ACTIVE  →  POWER_DIM após dimTimeoutMs (padrão: 30s)
POWER_DIM     →  deep sleep após deepSleepTimeoutMs (padrão: 10 min)
```

O dim não desliga o sistema — WiFi e timers continuam. Só o display vai para brilho 50 e o redesenho cai para 1×/min (apenas relógio na tela 0).

### WiFi intermitente (padrão)

Boot → conecta → NTP → clima → desliga WiFi. Repete a cada 30 min via timer do deep sleep. O WiFi keep-alive é opt-in pelo menu e habilita OTA e Telnet como efeito colateral.

### Deep sleep com estado preservado

Estado crítico em `RTC_DATA_ATTR`: tela atual, dados do clima, estado dos 3 timers. Wake silencioso por timer não acende o display — busca o clima e volta a dormir.

### Freq CPU + light sleep automático

CPU fixo em 80 MHz com `esp_pm_configure` habilitando light sleep durante `delay()` e idle do RTOS. WiFi acquires lock e sobe para 240 MHz sozinho quando necessário.

### LEDs apagados em dim e standby

`ledUpdate()` apaga os LEDs sempre que `isDim == true` ou quando não há timer rodando nem alarme ativo.

---

## Parâmetros atuais (pós-otimização)

| Parâmetro | Valor | Arquivo |
|---|---|---|
| `BRIGHTNESS_ACTIVE` | 150 | `config.h` |
| `BRIGHTNESS_DIM` | 50 | `config.h` |
| `AUTO_BRIGHTNESS_MIN` | `BRIGHTNESS_MIN_FLOOR` (80) | `config.h` |
| `AUTO_BRIGHTNESS_MAX` | 200 | `config.h` |
| `DIM_TIMEOUT_MS` | 30 000 ms | `config.h` |
| `DEEP_SLEEP_TIMEOUT_MS` | 10 min | `config.h` |
| `BREATH_MAX` (LEDs) | 80 | `led_strip.cpp` |
| Poll acelerômetro (dim) | 500 ms | `main.cpp` |
| Poll orientação (ativo) | 2 000 ms | `main.cpp` |
| Redraw tela 0 (relógio) | 1 000 ms | `main.cpp` |
| Redraw telas 1–3 | 5 000 ms | `main.cpp` |
| `delay()` loop normal | 50 ms | `main.cpp` |
| `delay()` loop alarme | 20 ms | `main.cpp` |
| `delay()` loop dim sem redraw | 150 ms | `main.cpp` |
| Cache bateria | 30 000 ms | `power_manager.cpp` |
| Log de sensores | 300 000 ms | `main.cpp` |

Esses valores são os defaults compilados. Os configuráveis pelo menu têm precedência via NVS.

---

## Regras para novos recursos

### 1. Classifique o recurso antes de implementar

| Tipo | Exemplos | Restrição energética |
|---|---|---|
| **Passivo** | redesenho de tela, animação | Só roda quando display está ativo |
| **Periódico** | fetch de API, log, sensor | Polling lento; respeita dim/sleep |
| **Contínuo** | microfone, Bluetooth, câmera | Exige justificativa explícita; desliga em dim |
| **On-demand** | QR scanner, OTA, Telnet | Só ativo enquanto usuário solicita |

### 2. Recursos contínuos exigem gate explícito

Qualquer recurso que mantém hardware ativo entre interações do usuário (microfone, câmera, BLE) **deve ser desabilitado por padrão** e requer opt-in pelo menu de configurações. O custo de consumo deve ser documentado no item de configuração.

Exemplo: `voiceEnabled` (mic) é `false` por padrão. Quando `powerIsDim()` for verdadeiro, `voiceCmdSuspend()` é chamado.

### 3. Polling: escolha o intervalo pelo impacto real

| Periodicidade | Usar quando |
|---|---|
| < 500 ms | Alarme ativo, animação em progresso |
| 500 ms – 2 s | Sensores de wake (proximidade, acelerômetro) |
| 2 s – 10 s | Auto-brilho, detecção de orientação |
| 10 s – 60 s | Status de bateria, RSSI |
| > 60 s | Logs, métricas não críticas |
| Só em wake | Qualquer coisa que não precisa em dim |

### 4. Redes: intermitente por padrão, keep-alive opt-in

Novas funcionalidades que dependem de rede (notificações push, sync de dados, calendário remoto) devem:

- Funcionar no modelo batch: acumular necessidade, executar no próximo ciclo WiFi (30 min)
- Ou usar o mecanismo de `wifiForceRefresh()` disparado por ação do usuário
- **Nunca** habilitar keep-alive silenciosamente como efeito colateral

### 5. Redesenho: dirty flag, não timer fixo

Todo redesenho deve ser controlado por `needsRedraw = true`. Evite redesenho por tempo decorrido exceto para telas com conteúdo dinâmico inerente (relógio). Para telas estáticas após carregamento de dados, `needsRedraw` setado uma vez no evento de update é suficiente.

Taxa máxima por tipo de conteúdo:

| Conteúdo | Taxa máxima de redraw |
|---|---|
| Relógio (segundos) | 1 Hz |
| Animação de alarme | 2.5 Hz (400 ms) |
| Animação de transição | 60 Hz (durante ~190 ms) |
| Clima, sistema, settings | 0.2 Hz (5 s) |
| Display em dim | 1×/min (apenas tela 0) |

### 6. Som: volume diferenciado por contexto

| Contexto | Volume | Duração máxima |
|---|---|---|
| Feedback de UI (tap) | 85 | < 100 ms |
| Alarme de timer | 160 | Até usuário fechar |
| Notificação passiva | 60–80 | < 500 ms |

O speaker AW88298 consome energia enquanto ativo. Chame `M5.Speaker.stop()` sempre após o som terminar; nunca deixe um loop de playback sem condição de parada.

### 7. LEDs: apenas com significado funcional

Os WS2812 do M5GO3 Bottom devem permanecer apagados na maior parte do tempo. Use efeitos de LED apenas quando comunicam estado que o display sozinho não comunica bem (ex: alarme chamando atenção de quem está de costas). Brilho máximo recomendado: 80 para breathing, 100 para alarme.

---

## Trade-offs documentados

Cada item abaixo representa uma decisão consciente onde UX ganhou sobre eficiência — ou vice-versa.

### UX ganhou (energia sacrificada intencionalmente)

**Modo cozinha ativa (wake-lock 1h)**
Long press na área do relógio segura o display em `POWER_ACTIVE` por 1h, bloqueando dim e deep sleep. Custo: ~60 min a 138 mA = ~138 mAh (≈15% da bateria por ativação). Mantido porque o cenário de uso (cozinhar com as mãos ocupadas/sujas) exige visibilidade contínua sem tocar na tela. Usuário pode cancelar manualmente com novo long press; expira sozinho em 1h. Badge laranja "COZINHA Nmin" torna o custo visível.

**Animação de transição entre telas**
190 ms a ~60 fps, CPU a 240 MHz (WiFi lock + rendering). Custo: ~0.5 mJ por transição. Mantido porque é a principal metáfora de navegação e o tempo de uso é curto.

**Fade de brilho**
50 fps de fade ao entrar/sair do dim. Custo: CPU polling a cada 20 ms. Mantido porque o dim abrupto seria agressivo visualmente.

**Breathing nos LEDs durante timer**
LEDs em modo breathing quando timer ativo, mesmo sem alarme. Custo: ~5–10 mA constantes. Mantido porque fornece feedback visual periférico útil na cozinha.

**Deep sleep preserva tela e timers**
Todos os 3 timers são salvos em RTC e restaurados. Custo: mais bytes em RTC RAM. Mantido porque perder um timer de cozinha em andamento seria grave para o usuário.

### Energia ganhou (UX levemente sacrificada)

**Auto-brilho lento (EMA 2 s)**
O auto-brilho responde devagar a mudanças de luz para evitar jitter. Resultado: ao acender a luz da cozinha, o display pode demorar 4–6 s para clareamento completo.

**`AUTO_BRIGHTNESS_MIN` mantido em `BRIGHTNESS_MIN_FLOOR` (80)**
Tentativa de baixar para 40 foi revertida: o LTR553 na porta da geladeira frequentemente lê lux < 10 (cozinha escura, sensor obstruído), fazendo o EMA convergir para o mínimo mesmo durante uso ativo. O piso de 80 evita que o display fique ilegível em condições normais de cozinha.

**Redraw de 5 s para telas 1–3**
Dados do clima não mudam em 5 s, mas timestamps ("atualizado há X min") podem parecer ligeiramente defasados.

**WIFI_KEEP_ALIVE false por padrão**
OTA e Telnet ficam indisponíveis sem opt-in. Aceitável porque o caso de uso principal (dashboard de cozinha) não precisa de conectividade contínua.

---

## Oportunidades futuras (não implementadas)

### CPU 40 MHz em dim
O ESP32-S3 suporta 40 MHz como mínimo (sem WiFi ativo). Em dim sem keep-alive, rebaixar para 40 MHz poderia economizar ~8–12 mA. Bloqueador atual: `esp_pm_configure` tem `min_freq_mhz = 80` para compatibilidade com drivers I2C. Requer validação de que LTR553 e IMU funcionam a 40 MHz.

### Wake-on-sound passivo
O AW88298 tem modo de detecção de som com threshold. Poderia acordar o display quando há barulho na cozinha (forno abrindo, etc.) sem o microfone principal ativo. Custo: ~1–2 mA em standby. Impacto UX: alto (mãos livres).

### Deep sleep mais cedo em bateria crítica
Atualmente, bateria ≤ 10% força dim imediato. Poderia também reduzir o timeout de deep sleep para 2 min nesse estado, priorizando preservar energia para acordar e mostrar o relógio quando necessário.

### Compressão de sprite em PSRAM
Os dois sprites de 320×240×16bit ocupam ~150KB cada (300KB total). Recriar com `setColorDepth(8)` nos casos em que a profundidade não importa reduziria o uso de PSRAM, liberando RAM para heap — não economiza energia diretamente, mas reduz o risco de fallback para display direto (que causa flickering e redesenhos extras).

### Bateria: curva de descarga por perfil de uso
`batteryGetEstimateMinutes()` usa taxa linear. Uma curva de descarga real do AXP2101 daria estimativas melhores especialmente abaixo de 20%, onde LiPo descarrega não-linearmente. Impacto energético: nenhum. Impacto UX: estimativa mais confiável.

### Throttle de redraw em dim por conteúdo
Atualmente em dim o relógio redesenha 1×/min (qualquer tela ativa). Poderia redesenhar apenas quando a tela ativa for a 0 (relógio), ignorando completamente o redraw em dim nas telas 1–3. Já é parcialmente feito mas a condição `currentScreen == 0` está só no gatilho do `lastDimMinute` — a lógica do `needsRedraw` tardio pode ainda escalar.

---

## Auditoria do sistema atual

Última auditoria realizada em 2026-04-16 cobrindo todos os módulos de `src/`.

### Resultado: 52/53 itens conformes

**Violação corrigida — voice command em dim** (`main.cpp`)
`voiceCmdUpdate()` era chamado independentemente do estado de energia, mantendo o microfone ativo durante dim. Corrigido adicionando guard `!powerIsDim()` na condição de entrada do bloco de voz.

### Consumos não medidos (requerem hardware)

Os itens abaixo estão corretos em implementação mas sem consumo quantificado. Medição recomendada com amperímetro em 5V+GND nos modos relevantes:

| Componente | Estado a medir | Consumo esperado |
|---|---|---|
| Microfone (voice cmd ativo) | VSTATE_IDLE listening | ~5–10 mA (estimado) |
| Câmera GC0308 (QR scanner) | Frame capture 50ms/frame | Verificar datasheet |
| bg_network task (Core 0) | Idle entre fetches | ~0 (task bloqueada em notify) |

---

## Checklist para novos recursos

Antes de mergear qualquer funcionalidade nova, verifique:

- [ ] O recurso desliga ou reduz atividade quando `powerIsDim()` for verdadeiro?
- [ ] Se usa hardware contínuo (mic, câmera, BLE, WiFi): está `false` por padrão e configurável pelo menu?
- [ ] Polling respeita a tabela de intervalos mínimos acima?
- [ ] Redesenho usa `needsRedraw = true` em vez de timer próprio?
- [ ] Som tem `M5.Speaker.stop()` garantido no caminho de saída?
- [ ] LEDs são apagados em dim e quando recurso está inativo?
- [ ] O recurso é seguro para rodar durante wake silencioso por timer (sem display)?
- [ ] Estado relevante para restore pós-deep-sleep está em `RTC_DATA_ATTR`?
- [ ] O custo estimado em mA foi considerado no contexto da autonomia de 900 mAh?
