// AgriLED.h — single-pixel WS2812 status LED on the M5 ATOM Lite (G27).
//
// State semantics shared across nodes:
//   BOOT       — booting / pre-network
//   NO_LINK    — Ethernet link down or no DHCP lease
//   NO_SENSOR  — sensor not detected (optional)
//   NO_MQTT    — MQTT host configured but not connected
//   OK         — link + lease + (MQTT connected if configured)
//   PUB        — momentary on each successful publish

#pragma once

#include <Arduino.h>
#include <FastLED.h>

namespace agri {

enum LedState {
  LED_BOOT,
  LED_NO_LINK,
  LED_NO_SENSOR,
  LED_NO_MQTT,
  LED_OK,
  LED_PUB
};

struct LedColors {
  CRGB boot      = CRGB(0, 0, 50);
  CRGB no_link   = CRGB(80, 0, 0);
  CRGB no_sensor = CRGB(60, 0, 60);
  CRGB no_mqtt   = CRGB(60, 40, 0);
  CRGB ok        = CRGB(0, 30, 0);
  CRGB pub       = CRGB(60, 60, 60);
};

struct Led {
  static CRGB       pixel[1];
  static LedState   state;
  static LedColors  colors;

  // Pin 27 is the on-board WS2812 on the M5 ATOM Lite — FastLED requires
  // the data pin as a compile-time template arg, so this is hardcoded.
  // For a different board, replace this header or wrap your own FastLED
  // controller alongside.
  static void begin(uint8_t brightness = 30) {
    FastLED.addLeds<WS2812, 27, GRB>(pixel, 1);
    FastLED.setBrightness(brightness);
    state = LED_BOOT;
    apply();
  }

  static void apply() {
    switch (state) {
      case LED_BOOT:      pixel[0] = colors.boot;      break;
      case LED_NO_LINK:   pixel[0] = colors.no_link;   break;
      case LED_NO_SENSOR: pixel[0] = colors.no_sensor; break;
      case LED_NO_MQTT:   pixel[0] = colors.no_mqtt;   break;
      case LED_OK:        pixel[0] = colors.ok;        break;
      case LED_PUB:       pixel[0] = colors.pub;       break;
    }
    FastLED.show();
  }

  static void set(LedState s) {
    if (s != state) { state = s; apply(); }
  }

  static void flashPublish() {
    LedState prev = state;
    state = LED_PUB; apply();
    delay(40);
    state = prev; apply();
  }
};

inline CRGB      Led::pixel[1] = {};
inline LedState  Led::state    = LED_BOOT;
inline LedColors Led::colors   = LedColors{};

} // namespace agri
