#pragma once
#include <M5Unified.h>
#include "runtime_config.h"

// Altura do header da tela de configurações (pixels)
#define SETTINGS_HEADER_H  24
// Y inicial do conteúdo scrollável (logo abaixo do header)
#define SETTINGS_CONTENT_Y (SETTINGS_HEADER_H + 4)

// Desenha a tela de configurações no display/sprite fornecido.
// scrollOffset: posição de scroll em pixels (0 = topo da lista).
void screenSettingsDraw(lgfx::LovyanGFX& display, const RuntimeConfig& cfg, int scrollOffset);

// Processa tap na tela de configurações.
// tapX, tapY: coordenadas no display (não ajustadas). scrollOffset: scroll atual.
// Retorna true se a configuração foi alterada (necessita salvar e reaplicar).
bool screenSettingsHandleTap(int tapX, int tapY, RuntimeConfig& cfg, int scrollOffset);

// Processa long press na tela de configurações. Executa ações destrutivas.
// Retorna true se uma ação foi executada.
bool screenSettingsHandleLongPress(int tapX, int tapY, int scrollOffset);

// Retorna o scroll máximo em pixels (para clampar g_settingsScroll).
int screenSettingsMaxScroll();

// Retorna true se há um modal de confirmação aberto.
bool screenSettingsIsConfirmOpen();

// Tela do portal WiFi — exibida em fullscreen quando wifiIsPortalMode() == true.
void drawWifiPortalScreen(lgfx::LovyanGFX& display);
void drawCalendarConfigScreen(lgfx::LovyanGFX& display);
