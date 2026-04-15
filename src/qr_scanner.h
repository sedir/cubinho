#pragma once
#include <M5Unified.h>

enum QRScanMode {
    QR_SCAN_WIFI,  // espera formato WIFI:T:WPA;S:rede;P:senha;;
    QR_SCAN_ICAL,  // espera URL http(s)://... ou webcal://...
};

// Inicia sessão de scan: inicializa a câmera do CoreS3 (GC0308) e aloca quirc.
// Chame apenas uma vez; chamadas subsequentes sem qrScannerEnd() são ignoradas.
void qrScannerBegin(QRScanMode mode);

// True enquanto o scanner está ativo (entre Begin e End).
bool qrScannerIsActive();

// Chame a cada frame quando isActive() == true.
// Captura frame, exibe preview em escala de cinza e tenta decodificar QR.
// touchReleased: pass touch.wasReleased() para permitir cancelamento por toque.
// Retorna true quando terminar (sucesso, erro de câmera, ou cancelamento).
bool qrScannerUpdate(lgfx::LovyanGFX& display, bool touchReleased);

// Libera câmera e quirc. Se scan foi bem-sucedido em modo WiFi, reinicia o
// dispositivo após salvar as credenciais (não retorna nesse caso).
void qrScannerEnd();

// True se o último scan foi bem-sucedido (válido até próximo Begin).
bool qrScannerWasSuccessful();
