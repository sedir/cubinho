#pragma once

// Inicializa OTA (ArduinoOTA). Chamar quando WiFi estiver conectado.
void otaInit();

// Poll OTA. Chamar no loop() — só processa se OTA estiver inicializado.
void otaUpdate();

// Retorna true se OTA está ativo e pronto para receber updates.
bool otaIsActive();
