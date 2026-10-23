#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class LedStripTest {
 public:
  void begin() {
    strip_.begin();
    strip_.setBrightness(brightness_);
    Serial.println("[LED] Initialized 30 WS2812B LEDs on GPIO16");
    enterEffect(Effect::Red, millis());
  }

  void update() {
    const uint32_t now = millis();
    const uint32_t effectDuration =
        effect_ == Effect::Rainbow ? rainbowDurationMs : colorDurationMs;
    if (now - effectStartedAtMs_ >= effectDuration) {
      switch (effect_) {
        case Effect::Red:
          enterEffect(Effect::Green, now);
          break;
        case Effect::Green:
          enterEffect(Effect::Blue, now);
          break;
        case Effect::Blue:
          enterEffect(Effect::Rainbow, now);
          break;
        case Effect::Rainbow:
          enterEffect(Effect::Red, now);
          break;
      }
    }

    if (effect_ == Effect::Rainbow &&
        now - lastRainbowUpdateAtMs_ >= rainbowFrameIntervalMs) {
      lastRainbowUpdateAtMs_ = now;
      showRainbow();
    }
  }

 private:
  enum class Effect : uint8_t {
    Red,
    Green,
    Blue,
    Rainbow
  };

  static constexpr uint8_t ledCount_ = 30;
  static constexpr uint8_t dataPin_ = 16;
  static constexpr uint8_t brightness_ = 20;
  static constexpr uint32_t colorDurationMs = 2000;
  static constexpr uint32_t rainbowDurationMs = 5000;
  static constexpr uint32_t rainbowFrameIntervalMs = 20;

  Adafruit_NeoPixel strip_{ledCount_, dataPin_, NEO_GRB + NEO_KHZ800};
  Effect effect_ = Effect::Red;
  uint32_t effectStartedAtMs_ = 0;
  uint32_t lastRainbowUpdateAtMs_ = 0;
  uint16_t rainbowOffset_ = 0;

  void enterEffect(Effect effect, uint32_t now) {
    effect_ = effect;
    effectStartedAtMs_ = now;

    switch (effect_) {
      case Effect::Red:
        Serial.println("[LED] Testing RED");
        showColor(strip_.Color(255, 0, 0));
        break;
      case Effect::Green:
        Serial.println("[LED] Testing GREEN");
        showColor(strip_.Color(0, 255, 0));
        break;
      case Effect::Blue:
        Serial.println("[LED] Testing BLUE");
        showColor(strip_.Color(0, 0, 255));
        break;
      case Effect::Rainbow:
        Serial.println("[LED] Testing RAINBOW");
        rainbowOffset_ = 0;
        lastRainbowUpdateAtMs_ = now;
        showRainbow();
        break;
    }
  }

  void showColor(uint32_t color) {
    for (uint16_t pixel = 0; pixel < ledCount_; ++pixel) {
      strip_.setPixelColor(pixel, color);
    }
    strip_.show();
  }

  void showRainbow() {
    for (uint16_t pixel = 0; pixel < ledCount_; ++pixel) {
      const uint16_t hue = static_cast<uint16_t>(
          (pixel * 65536UL / ledCount_) + rainbowOffset_);
      strip_.setPixelColor(pixel, strip_.gamma32(strip_.ColorHSV(hue)));
    }
    strip_.show();
    rainbowOffset_ += 256;
  }
};
