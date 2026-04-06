#pragma once
#include "telnet_log.h"

// Logging estruturado: Serial + Telnet com timestamp e nível.
//
// Uso:
//   LOG_I("wifi",  "Conectado — IP %s", ip);
//   LOG_W("power", "Bateria baixa: %d%%", pct);
//   LOG_E("main",  "Sprite nao alocado");
//
// Saída:
//   [10:32:15] I [wifi    ] Conectado — IP 192.168.1.42
//   [10:32:16] W [power   ] Bateria baixa: 8%
//   [10:32:17] E [main    ] Sprite nao alocado

#define LOG_I(tag, fmt, ...) logPrint('I', tag, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) logPrint('W', tag, fmt, ##__VA_ARGS__)
#define LOG_E(tag, fmt, ...) logPrint('E', tag, fmt, ##__VA_ARGS__)
