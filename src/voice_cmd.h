#pragma once
#include <M5Unified.h>

// ── Comandos por voz — monitoramento contínuo (always-on) ────────────────────
//
// O módulo fica ouvindo continuamente em background (Core 1, no main loop).
// Quando detecta som acima do limiar adaptativo, entra automaticamente na janela
// de classificação de 2 segundos — sem nenhuma interação manual necessária.
//
//  Padrão          Exemplo vocal        Ação
//  ──────────────  ──────────────────   ──────────────────────────────────────
//  • 1 pulso curto "ei" / clap          Próxima tela
//  •• 2 pulsos     "ei-ei"              Tela anterior
//  ─ 1 pulso longo "aaaa" (> 680ms)     Iniciar / pausar timer focado
//  ••• 3+ pulsos   "ei-ei-ei"           Zerar timer focado
//
// Detalhes de implementação:
//   - M5.Mic.record(..., 0) é non-blocking; drena DMA todo frame (~50fps)
//   - Calibração de ruído adaptativa via EMA durante silêncio (atualiza ao longo
//     do tempo à medida que o ambiente muda — TV, exaustor, janela aberta, etc.)
//   - Conflito speaker/mic: voiceCmdSuspend() interrompe o mic; o módulo reinicia
//     automaticamente após VOICE_RESUME_DELAY_MS ao ser notificado que o speaker parou

enum VoiceCommand {
    VCMD_NONE = 0,
    VCMD_NEXT_SCREEN,    // • 1 pulso curto
    VCMD_PREV_SCREEN,    // •• 2 pulsos curtos
    VCMD_TIMER_TOGGLE,   // ─ 1 pulso longo
    VCMD_TIMER_RESET,    // ••• 3+ pulsos curtos
    VCMD_AMBIGUOUS,      // padrão não reconhecido
};

enum VoiceState {
    VSTATE_DISABLED = 0,  // módulo desabilitado
    VSTATE_IDLE,          // monitorando (DMA drenando, esperando som)
    VSTATE_LISTENING,     // janela de classificação ativa (overlay visível)
    VSTATE_SUSPENDED,     // mic pausado (speaker/alarme ativo)
};

// Inicializa o módulo. Chame em setup() após M5.begin().
void voiceCmdInit(bool enable);

// Altera estado habilitado em runtime (ao salvar configurações).
// Inicia ou para o mic conforme necessário.
void voiceCmdSetEnabled(bool enable);
bool voiceCmdIsEnabled();

// Estado atual do módulo.
VoiceState voiceCmdGetState();

// Suspende temporariamente o mic (ex: durante alarme sonoro).
// O módulo reinicia automaticamente após VOICE_RESUME_DELAY_MS
// quando voiceCmdResume() for chamado.
void voiceCmdSuspend();
void voiceCmdResume();  // sinaliza que o speaker parou; reinício é adiado

// Chame todo frame no loop principal (qualquer que seja o estado).
// Drena DMA, atualiza calibração, detecta bursts, classifica.
// Retorna VCMD_NONE enquanto monitorando / classificando;
// retorna o comando exatamente uma vez quando a janela encerra.
VoiceCommand voiceCmdUpdate();

// Ícone de microfone mini (10×12 px) desenhado no canto superior esquerdo
// do sprite atual. Só aparece quando VSTATE_IDLE ou VSTATE_LISTENING.
void voiceCmdDrawStatusIcon(lgfx::LovyanGFX& display);

// Overlay completo sobre a tela atual durante VSTATE_LISTENING.
// Mostra barra de energia, contagem de pulsos, countdown e legenda.
void voiceCmdDrawOverlay(lgfx::LovyanGFX& display);
