#pragma once
#include <stdarg.h>
#include <stdint.h>

#ifndef TELNET_LOG_PORT
#define TELNET_LOG_PORT 23
#endif

void telnetLogInit();
void telnetLogSetBoot(uint8_t bootNum);
void telnetLogUpdate();
bool telnetIsConnected();
void logPrint(char level, const char* tag, const char* fmt, ...);

// Retorna true se o SD card está disponível
bool sdIsAvailable();
