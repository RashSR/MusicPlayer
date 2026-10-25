#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

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

  void setAlbumColors(const String& album, uint32_t color1, uint32_t color2,
                      uint32_t color3, bool hasThirdColor) {
    const uint8_t newColorCount = hasThirdColor ? 3 : 2;
    color1 &= 0xFFFFFF;
    color2 &= 0xFFFFFF;
    color3 &= 0xFFFFFF;
    if (hasAlbum_ && activeAlbum_ == album &&
        paletteColorCount_ == newColorCount && sourceColors_[0] == color1 &&
        sourceColors_[1] == color2 &&
        (!hasThirdColor || sourceColors_[2] == color3)) {
      logPalette(album, color1, color2, color3, hasThirdColor);
      return;
    }

    const uint32_t now = millis();
    const uint32_t currentColors[3] = {
        colorForPixel(0, now), colorForPixel(1, now), colorForPixel(2, now)};
    const bool transitioningFromAlbum = hasAlbum_;
    if (!enabled_) {
      disabledAtMs_ = now;
    }

    activeAlbum_ = album;
    paletteColorCount_ = newColorCount;
    sourceColors_[0] = color1;
    sourceColors_[1] = color2;
    sourceColors_[2] = hasThirdColor ? color3 : 0;
    targetColors_[0] = correctDarkColor(color1);
    targetColors_[1] = correctDarkColor(color2);
    targetColors_[2] = hasThirdColor ? correctDarkColor(color3) : 0;
    hasAlbum_ = true;
    phaseStartedAtMs_ = now;

    if (transitioningFromAlbum) {
      for (uint8_t i = 0; i < 3; ++i) {
        transitionFromColors_[i] = currentColors[i];
      }
      colorTransitionStartedAtMs_ = now;
      colorTransitionActive_ = true;
    } else {
      colorTransitionActive_ = false;
    }

    logPalette(activeAlbum_, color1, color2, color3, hasThirdColor);
  }

  void setEnabled(bool enabled) {
    if (enabled_ == enabled) {
      return;
    }

    const uint32_t now = millis();
    if (!enabled) {
      enabled_ = false;
      disabledAtMs_ = now;
      for (uint16_t pixel = 0; pixel < ledCount_; ++pixel) {
        strip_.setPixelColor(pixel, 0);
      }
      strip_.show();
      return;
    }

    const uint32_t disabledDuration = now - disabledAtMs_;
    phaseStartedAtMs_ += disabledDuration;
    if (colorTransitionActive_) {
      colorTransitionStartedAtMs_ += disabledDuration;
    }
    enabled_ = true;
    lastFrameAtMs_ = now - frameIntervalMs_;
    update();
  }

  void update() {
    if (!enabled_) {
      return;
    }

    const uint32_t now = millis();
    if (now - lastFrameAtMs_ < frameIntervalMs_) {
      return;
    }
    lastFrameAtMs_ = now;

    for (uint16_t pixel = 0; pixel < ledCount_; ++pixel) {
      strip_.setPixelColor(pixel, colorForPixel(pixel, now));
    }
    strip_.show();

    if (colorTransitionActive_ &&
        now - colorTransitionStartedAtMs_ >= LED_COLOR_SWAP_DURATION_MS) {
      colorTransitionActive_ = false;
    }
  }

 private:
  static constexpr uint8_t ledCount_ = 30;
  static constexpr uint8_t dataPin_ = 16;
  static constexpr uint8_t brightness_ = 20;
  static constexpr uint32_t frameIntervalMs_ = 33;
  static constexpr uint32_t LED_COLOR_SWAP_DURATION_MS = 500;
  static constexpr float DARK_COLOR_LUMINANCE_THRESHOLD = 90.0f;
  static constexpr float DARK_COLOR_TARGET_LUMINANCE = 90.0f;
  static constexpr float DARK_COLOR_MAX_GAIN = 1.35f;
  static constexpr uint32_t defaultColor1_ = 0x315B4C;
  static constexpr uint32_t defaultColor2_ = 0xA9C7AE;

  Adafruit_NeoPixel strip_{ledCount_, dataPin_, NEO_GRB + NEO_KHZ800};
  uint32_t sourceColors_[3] = {defaultColor1_, defaultColor2_, 0};
  uint32_t targetColors_[3] = {
      correctDarkColor(defaultColor1_), correctDarkColor(defaultColor2_), 0};
  uint32_t transitionFromColors_[3] = {defaultColor1_, defaultColor2_,
                                       defaultColor1_};
  uint32_t phaseStartedAtMs_ = 0;
  uint32_t colorTransitionStartedAtMs_ = 0;
  uint32_t lastFrameAtMs_ = 0;
  uint32_t disabledAtMs_ = 0;
  uint8_t paletteColorCount_ = 2;
  String activeAlbum_;
  bool hasAlbum_ = false;
  bool colorTransitionActive_ = false;
  bool enabled_ = true;

  static uint8_t channel(uint32_t color, uint8_t shift) {
    return static_cast<uint8_t>(color >> shift);
  }

  static uint32_t correctDarkColor(uint32_t color) {
    const float red = channel(color, 16);
    const float green = channel(color, 8);
    const float blue = channel(color, 0);
    const float luminance = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
    if (luminance <= 0.0f || luminance >= DARK_COLOR_LUMINANCE_THRESHOLD) {
      return color;
    }

    float gain = DARK_COLOR_TARGET_LUMINANCE / luminance;
    if (gain > DARK_COLOR_MAX_GAIN) {
      gain = DARK_COLOR_MAX_GAIN;
    }
    const float maximumChannel = red > green
        ? (red > blue ? red : blue)
        : (green > blue ? green : blue);
    if (maximumChannel > 0.0f && gain > 255.0f / maximumChannel) {
      gain = 255.0f / maximumChannel;
    }
    const uint8_t correctedRed = static_cast<uint8_t>(red * gain + 0.5f);
    const uint8_t correctedGreen = static_cast<uint8_t>(green * gain + 0.5f);
    const uint8_t correctedBlue = static_cast<uint8_t>(blue * gain + 0.5f);
    return (static_cast<uint32_t>(correctedRed) << 16) |
           (static_cast<uint32_t>(correctedGreen) << 8) | correctedBlue;
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

  static void printColor(uint32_t color) {
    char formatted[8];
    snprintf(formatted, sizeof(formatted), "#%06lX",
             static_cast<unsigned long>(color));
    Serial.print(formatted);
  }

  static void logPalette(const String& album, uint32_t color1,
                         uint32_t color2, uint32_t color3,
                         bool hasThirdColor) {
    Serial.print("[LED] Album: ");
    Serial.println(album);
    Serial.print("[LED] Colors: ");
    printColor(color1);
    Serial.print(", ");
    printColor(color2);
    if (hasThirdColor) {
      Serial.print(", ");
      printColor(color3);
    }
    Serial.println();
    Serial.print("[LED] Mode: ");
    Serial.println(hasThirdColor ? "3-color rotating" : "2-color alternating");
    Serial.print("[LED] Transition duration: ");
    Serial.print(LED_COLOR_SWAP_DURATION_MS);
    Serial.println(" ms");
  }

  uint32_t colorForPixel(uint16_t pixel, uint32_t now) const {
    const uint32_t fullCycleMs_ =
        LED_COLOR_SWAP_DURATION_MS * paletteColorCount_;
    const uint32_t cyclePosition = (now - phaseStartedAtMs_) % fullCycleMs_;
    const uint8_t rotation = cyclePosition / LED_COLOR_SWAP_DURATION_MS;
    const float blend =
        static_cast<float>(cyclePosition % LED_COLOR_SWAP_DURATION_MS) /
        LED_COLOR_SWAP_DURATION_MS;
    const uint8_t position = pixel % paletteColorCount_;
    const uint8_t fromIndex = (position + rotation) % paletteColorCount_;
    const uint8_t toIndex = (fromIndex + 1) % paletteColorCount_;
    const uint32_t targetColor = interpolateColor(
        targetColors_[fromIndex], targetColors_[toIndex], blend);

    if (!colorTransitionActive_) {
      return targetColor;
    }

    const uint32_t transitionElapsed = now - colorTransitionStartedAtMs_;
    const float transitionBlend =
        transitionElapsed >= LED_COLOR_SWAP_DURATION_MS
            ? 1.0f
            : static_cast<float>(transitionElapsed) /
                  LED_COLOR_SWAP_DURATION_MS;
    return interpolateColor(transitionFromColors_[pixel % 3], targetColor,
                            transitionBlend);
  }
};
