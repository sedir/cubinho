#pragma once
#include <M5Unified.h>
#include "weather_api.h"

// Desenha a tela 1: clima externo (dados da API OpenMeteo)
void screenWeatherDraw(lgfx::LovyanGFX& display, const WeatherData& data, bool fetching);
