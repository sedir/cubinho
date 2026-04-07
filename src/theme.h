#pragma once
#include <M5Unified.h>

// ── Paleta de cores centralizada (RGB565) ────────────────────────────────────
// Texto
#define COLOR_TEXT_PRIMARY   TFT_WHITE
#define COLOR_TEXT_DIM       0x8410  // cinza médio
#define COLOR_TEXT_SUBTLE    0x4208  // cinza escuro
#define COLOR_TEXT_ACCENT    0xFD20  // laranja (cor principal do relógio)
#define COLOR_TEXT_OFFWHITE  0xDEDB  // branco suavizado

// UI
#define COLOR_DIVIDER        0x4208
#define COLOR_BACKGROUND     TFT_BLACK

// Temperatura
#define COLOR_TEMP_COLD      0x001F  // azul
#define COLOR_TEMP_HOT       TFT_RED
#define COLOR_TEMP_COMFORT   TFT_GREEN

// Timer
#define COLOR_TIMER_RUNNING  TFT_GREEN
#define COLOR_TIMER_PAUSED   TFT_WHITE
#define COLOR_TIMER_SETTING  0x8410

// Clima (ícones)
#define COLOR_SUN            0xFFE0
#define COLOR_CLOUD          0xC618
#define COLOR_CLOUD_DARK     0x7BEF
#define COLOR_RAIN           0x5D9F
#define COLOR_SNOW           0xEF7D
#define COLOR_BOLT           0xFFE0

// Bateria
#define COLOR_BATTERY_OK     TFT_GREEN
#define COLOR_BATTERY_MED    0xFD20
#define COLOR_BATTERY_LOW    TFT_RED

// Humidade
#define COLOR_HUMIDITY       0x5D9F

// ── Layout constants ─────────────────────────────────────────────────────────
#define TIMER_ZONE_Y         118   // fronteira toque relógio / timer
#define SCREEN_COUNT         3     // home, weather, system
