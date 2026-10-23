#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

class LedAlbumLighting {
 public:
  void begin() {
    strip_.begin();
    strip_.setBrightness(brightness_);
    Serial.println("[LED] Initialized 30 WS2812B LEDs on GPIO16");
    phaseStartedAtMs_ = millis();
    lastFrameAtMs_ = phaseStartedAtMs_;
    update();
  }

  void setAlbumColors(const String& album, uint32_t color1, uint32_t color2) {
    const uint32_t now = millis();
    color1 &= 0xFFFFFF;
    color2 &= 0xFFFFFF;
    if (hasAlbum_ && activeAlbum_ == album &&
        targetColor1_ == color1 && targetColor2_ == color2) {
      return;
    }

    const bool transitioningFromAlbum = hasAlbum_;
    const uint32_t currentEven = colorForPixel(0, now);
    const uint32_t currentOdd = colorForPixel(1, now);
    activeAlbum_ = album;
    targetColor1_ = color1;
    targetColor2_ = color2;
    hasAlbum_ = true;
    phaseStartedAtMs_ = now;

    if (transitioningFromAlbum) {
      transitionFromEven_ = currentEven;
      transitionFromOdd_ = currentOdd;
      colorTransitionStartedAtMs_ = now;
      colorTransitionActive_ = true;
    } else {
      colorTransitionActive_ = false;
    }

    Serial.print("[LED] Album changed: ");
    Serial.println(activeAlbum_);
    Serial.println("[LED] Alternating animation active");
  }

  void update() {
    const uint32_t now = millis();
    if (now - lastFrameAtMs_ < frameIntervalMs_) {
      return;
    }
    lastFrameAtMs_ = now;

    for (uint16_t pixel = 0; pixel < ledCount_; ++pixel) {
      strip_.setPixelColor(pixel, colorForPixel(pixel, now));
    }
    strip_.show();
  }

 private:
  static constexpr uint8_t ledCount_ = 30;
  static constexpr uint8_t dataPin_ = 16;
  static constexpr uint8_t brightness_ = 20;
  static constexpr uint32_t frameIntervalMs_ = 33;
  static constexpr uint32_t LED_COLOR_SWAP_DURATION_MS = 500;
  static constexpr uint32_t colorTransitionMs_ = LED_COLOR_SWAP_DURATION_MS;
  static constexpr uint32_t halfCycleMs_ = LED_COLOR_SWAP_DURATION_MS;
  static constexpr uint32_t fullCycleMs_ = halfCycleMs_ * 2;
  static constexpr uint32_t defaultColor1_ = 0x315B4C;
  static constexpr uint32_t defaultColor2_ = 0xA9C7AE;

  Adafruit_NeoPixel strip_{ledCount_, dataPin_, NEO_GRB + NEO_KHZ800};
  uint32_t targetColor1_ = defaultColor1_;
  uint32_t targetColor2_ = defaultColor2_;
  uint32_t transitionFromEven_ = defaultColor1_;
  uint32_t transitionFromOdd_ = defaultColor2_;
  uint32_t phaseStartedAtMs_ = 0;
  uint32_t colorTransitionStartedAtMs_ = 0;
  uint32_t lastFrameAtMs_ = 0;
  String activeAlbum_;
  bool hasAlbum_ = false;
  bool colorTransitionActive_ = false;

  uint32_t colorForPixel(uint16_t pixel, uint32_t now) {
    const uint32_t elapsed = now - phaseStartedAtMs_;
    const uint32_t phase = elapsed % fullCycleMs_;
    const float blend = phase <= halfCycleMs_
        ? static_cast<float>(phase) / halfCycleMs_
        : static_cast<float>(fullCycleMs_ - phase) / halfCycleMs_;
    const bool evenPixel = pixel % 2 == 0;
    const uint32_t targetColor = evenPixel
        ? interpolateColor(targetColor1_, targetColor2_, blend)
        : interpolateColor(targetColor2_, targetColor1_, blend);

    if (!colorTransitionActive_) {
      return targetColor;
    }

    const uint32_t transitionElapsed = now - colorTransitionStartedAtMs_;
    if (transitionElapsed >= colorTransitionMs_) {
      colorTransitionActive_ = false;
      return targetColor;
    }

    const float transitionBlend =
        static_cast<float>(transitionElapsed) / colorTransitionMs_;
    const uint32_t transitionFrom =
        evenPixel ? transitionFromEven_ : transitionFromOdd_;
    return interpolateColor(transitionFrom, targetColor, transitionBlend);
  }

  static uint8_t channel(uint32_t color, uint8_t shift) {
    return static_cast<uint8_t>(color >> shift);
  }

  static uint32_t interpolateColor(uint32_t from, uint32_t to, float mix) {
    const uint8_t red = static_cast<uint8_t>(channel(from, 16) +
        (static_cast<int>(channel(to, 16)) - channel(from, 16)) * mix);
    const uint8_t green = static_cast<uint8_t>(channel(from, 8) +
        (static_cast<int>(channel(to, 8)) - channel(from, 8)) * mix);
    const uint8_t blue = static_cast<uint8_t>(channel(from, 0) +
        (static_cast<int>(channel(to, 0)) - channel(from, 0)) * mix);
    return (static_cast<uint32_t>(red) << 16) |
           (static_cast<uint32_t>(green) << 8) | blue;
  }

};
