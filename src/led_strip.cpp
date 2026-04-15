#include "led_strip.h"
#include <FastLED.h>
#include <math.h>

#define LED_PIN     5
#define LED_COUNT   10
#define LED_TYPE    WS2812
#define COLOR_ORDER GRB

#define BREATH_MIN   15
#define BREATH_MAX   80

static CRGB leds[LED_COUNT];

void ledInit() {
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, LED_COUNT);
    FastLED.setBrightness(0);
    FastLED.show();
}

void ledOff() {
    FastLED.setBrightness(0);
    FastLED.show();
}

void ledUpdate(bool isDim, bool alarmActive, bool timerRunning) {
    if (isDim || (!alarmActive && !timerRunning)) { ledOff(); return; }

    uint32_t now = millis();

    if (alarmActive) {
        // Pisca vermelho em sincronia com o display
        bool on = (now / 400) % 2 == 0;
        fill_solid(leds, LED_COUNT, CRGB::Red);
        FastLED.setBrightness(on ? 200 : 0);
        FastLED.show();
        return;
    }

    // Breathing suave
    uint32_t period = timerRunning ? 2000UL : 3000UL;
    float phase = (float)(now % period) / (float)period;
    float t = (sinf(phase * 2.0f * (float)M_PI - (float)M_PI / 2.0f) + 1.0f) / 2.0f;
    uint8_t brightness = (uint8_t)(BREATH_MIN + t * (BREATH_MAX - BREATH_MIN));

    CRGB color = timerRunning ? CRGB(0, 220, 60) : CRGB(255, 110, 20);

    fill_solid(leds, LED_COUNT, color);
    FastLED.setBrightness(brightness);
    FastLED.show();
}
