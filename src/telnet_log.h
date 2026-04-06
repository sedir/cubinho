#pragma once
#include <stdarg.h>
#include <stdint.h>

// Porta Telnet padrão. Para mudar, defina TELNET_LOG_PORT em config.h antes de incluir este header.
#ifndef TELNET_LOG_PORT
#define TELNET_LOG_PORT 23
#endif

// Inicia o servidor Telnet e o SD card (chamado no setup, após M5.begin).
// O servidor Telnet só sobe de verdade quando o WiFi conectar (lazy init).
// O SD é inicializado imediatamente se o cartão estiver presente.
void telnetLogInit();

// Informa o número de boot atual — usado no nome do arquivo SD antes do NTP sincronizar.
// Chamar uma vez no setup(), após incrementar o boot counter.
void telnetLogSetBoot(uint8_t bootNum);

// Avança o servidor: aceita novos clientes, envia buffer pendente.
// Chamar a cada iteração do loop.
void telnetLogUpdate();

// Retorna true se há um cliente Telnet conectado.
bool telnetIsConnected();

// Grava uma linha no Serial e no Telnet, com timestamp e nível.
// Formato: [HH:MM:SS] L [tag] mensagem
// Não chamar diretamente — usar as macros em logger.h.
void logPrint(char level, const char* tag, const char* fmt, ...);
