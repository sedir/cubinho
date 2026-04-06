#pragma once

// Inicializa os 10x WS2812 do M5GO3 Bottom (GPIO5).
void ledInit();

// Atualiza o efeito dos LEDs. Chame no loop() a cada iteração.
//   isDim        — display em modo dim (LEDs apagados)
//   alarmActive  — timer DONE (pisca vermelho junto com o display)
//   timerRunning — timer RUNNING (breathing verde)
void ledUpdate(bool isDim, bool alarmActive, bool timerRunning);

// Apaga todos os LEDs imediatamente (usar antes do deep sleep).
void ledOff();
