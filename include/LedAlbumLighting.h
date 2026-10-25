#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class LedAlbumLighting {
 public:
  enum class PlaybackMode : uint8_t {
    Idle,
    Playing,
    Paused,
    Stopped
  };

  void begin() {
    strip_.begin();
    strip_.setBrightness(brightnessToNeoPixelValue(brightnessPercent_));
    Serial.println("[LED] Initialized 30 WS2812B LEDs on GPIO16");
    const uint32_t now = millis();
    phaseStartedAtMs_ = now;
    clockFrozenAtMs_ = now;
    renderFrame(now);
  }

  void setBrightness(uint8_t brightnessPercent) {
    if (brightnessPercent > 100 || brightnessPercent_ == brightnessPercent) {
      return;
    }

    brightnessPercent_ = brightnessPercent;
    strip_.setBrightness(brightnessToNeoPixelValue(brightnessPercent_));
    if (enabled_) {
      renderFrame(animationTime(millis()));
    }
  }

  uint8_t brightness() const {
    return brightnessPercent_;
  }

  void setSaturation(uint8_t saturationPercent) {
    if (saturationPercent > 150 ||
        saturationPercent_ == saturationPercent) {
      return;
    }

    saturationPercent_ = saturationPercent;
    if (enabled_) {
      renderFrame(animationTime(millis()));
    }
  }

  uint8_t saturation() const {
    return saturationPercent_;
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
      resetAnimation(millis());
      logPalette(album, color1, color2, color3, hasThirdColor);
      return;
    }

    const uint32_t now = millis();
    activeAlbum_ = album;
    paletteColorCount_ = newColorCount;
    sourceColors_[0] = color1;
    sourceColors_[1] = color2;
    sourceColors_[2] = hasThirdColor ? color3 : 0;
    targetColors_[0] = color1;
    targetColors_[1] = color2;
    targetColors_[2] = hasThirdColor ? color3 : 0;
    hasAlbum_ = true;
    resetAnimation(now);

    logPalette(activeAlbum_, color1, color2, color3, hasThirdColor);
  }

  void setPlaybackMode(PlaybackMode mode) {
    if (playbackMode_ == mode) {
      return;
    }

    const uint32_t now = millis();
    const bool wasClockRunning = isClockRunning();
    const PlaybackMode previousMode = playbackMode_;
    if (mode == PlaybackMode::Playing &&
        previousMode != PlaybackMode::Paused) {
      phaseStartedAtMs_ = now;
      clockFrozenAtMs_ = now;
    } else if (mode == PlaybackMode::Paused &&
               previousMode != PlaybackMode::Playing) {
      phaseStartedAtMs_ = now;
      clockFrozenAtMs_ = now;
    }

    playbackMode_ = mode;
    updateClock(now, wasClockRunning);
    brightnessPercent_ = mode == PlaybackMode::Playing
        ? playingBrightnessPercent_
        : idleBrightnessPercent_;
    strip_.setBrightness(brightnessToNeoPixelValue(brightnessPercent_));

    if (!enabled_) {
      return;
    }
    if (mode == PlaybackMode::Playing) {
      lastFrameAtMs_ = now - frameIntervalMs_;
      renderFrame(now);
    } else {
      renderFrame(clockFrozenAtMs_);
    }
  }

  void setEnabled(bool enabled) {
    if (enabled_ == enabled) {
      return;
    }

    const uint32_t now = millis();
    const bool wasClockRunning = isClockRunning();
    if (!enabled) {
      enabled_ = false;
      updateClock(now, wasClockRunning);
      clearStrip();
      return;
    }

    enabled_ = true;
    updateClock(now, wasClockRunning);
    if (playbackMode_ == PlaybackMode::Paused) {
      renderFrame(clockFrozenAtMs_);
    } else if (playbackMode_ == PlaybackMode::Playing) {
      lastFrameAtMs_ = now - frameIntervalMs_;
      renderFrame(now);
    } else {
      renderFrame(clockFrozenAtMs_);
    }
  }

  void update() {
    if (!enabled_ || playbackMode_ != PlaybackMode::Playing) {
      return;
    }

    const uint32_t now = millis();
    if (now - lastFrameAtMs_ < frameIntervalMs_) {
      return;
    }
    renderFrame(now);
  }

 private:
  static constexpr uint8_t ledCount_ = 30;
  static constexpr uint8_t dataPin_ = 16;
  static constexpr uint8_t idleBrightnessPercent_ = 15;
  static constexpr uint8_t playingBrightnessPercent_ = 50;
  static constexpr uint8_t defaultSaturationPercent_ = 100;
  static constexpr uint32_t frameIntervalMs_ = 33;
  static constexpr uint32_t LED_COLOR_SWAP_DURATION_MS = 1500;
  static constexpr uint32_t LED_TRANSITION_DURATION_MS = 150;
  static constexpr uint32_t defaultColor1_ = 0x315B4C;
  static constexpr uint32_t defaultColor2_ = 0xA9C7AE;

  Adafruit_NeoPixel strip_{ledCount_, dataPin_, NEO_GRB + NEO_KHZ800};
  uint32_t sourceColors_[3] = {defaultColor1_, defaultColor2_, 0};
  uint32_t targetColors_[3] = {defaultColor1_, defaultColor2_, 0};
  uint32_t phaseStartedAtMs_ = 0;
  uint32_t lastFrameAtMs_ = 0;
  uint32_t clockFrozenAtMs_ = 0;
  uint8_t paletteColorCount_ = 2;
  uint8_t brightnessPercent_ = idleBrightnessPercent_;
  uint8_t saturationPercent_ = defaultSaturationPercent_;
  String activeAlbum_;
  bool hasAlbum_ = false;
  bool enabled_ = true;
  PlaybackMode playbackMode_ = PlaybackMode::Idle;

  bool isClockRunning() const {
    return enabled_ && playbackMode_ == PlaybackMode::Playing;
  }

  uint32_t animationTime(uint32_t now) const {
    return isClockRunning() ? now : clockFrozenAtMs_;
  }

  void updateClock(uint32_t now, bool wasClockRunning) {
    const bool clockRunning = isClockRunning();
    if (wasClockRunning && !clockRunning) {
      clockFrozenAtMs_ = now;
    } else if (!wasClockRunning && clockRunning) {
      phaseStartedAtMs_ += now - clockFrozenAtMs_;
    }
  }

  void resetAnimation(uint32_t now) {
    phaseStartedAtMs_ = now;
    if (!isClockRunning()) {
      clockFrozenAtMs_ = now;
    }
    if (enabled_) {
      renderFrame(animationTime(now));
    }
  }

  void clearStrip() {
    for (uint16_t pixel = 0; pixel < ledCount_; ++pixel) {
      strip_.setPixelColor(pixel, 0);
    }
    strip_.show();
  }

  static uint8_t brightnessToNeoPixelValue(uint8_t brightnessPercent) {
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(brightnessPercent) * 255 + 50) / 100);
  }

  void renderFrame(uint32_t now) {
    lastFrameAtMs_ = millis();
    for (uint16_t pixel = 0; pixel < ledCount_; ++pixel) {
      strip_.setPixelColor(
          pixel, saturateColor(colorForPixel(pixel, now)));
    }
    strip_.show();
  }

  static uint8_t channel(uint32_t color, uint8_t shift) {
    return static_cast<uint8_t>(color >> shift);
  }

  static uint32_t interpolateColor(uint32_t from, uint32_t to, float mix) {
    const uint8_t red = static_cast<uint8_t>(
        channel(from, 16) +
        (static_cast<int>(channel(to, 16)) - channel(from, 16)) * mix + 0.5f);
    const uint8_t green = static_cast<uint8_t>(
        channel(from, 8) +
        (static_cast<int>(channel(to, 8)) - channel(from, 8)) * mix + 0.5f);
    const uint8_t blue = static_cast<uint8_t>(
        channel(from, 0) +
        (static_cast<int>(channel(to, 0)) - channel(from, 0)) * mix + 0.5f);
    return (static_cast<uint32_t>(red) << 16) |
           (static_cast<uint32_t>(green) << 8) | blue;
  }

  uint32_t saturateColor(uint32_t color) const {
    if (saturationPercent_ == 100) {
      return color;
    }

    const float red = channel(color, 16) / 255.0f;
    const float green = channel(color, 8) / 255.0f;
    const float blue = channel(color, 0) / 255.0f;
    const float maximum = red > green
        ? (red > blue ? red : blue)
        : (green > blue ? green : blue);
    const float minimum = red < green
        ? (red < blue ? red : blue)
        : (green < blue ? green : blue);
    const float delta = maximum - minimum;
    if (delta <= 0.0f) {
      return color;
    }

    float hue = 0.0f;
    if (maximum == red) {
      hue = (green - blue) / delta;
      if (hue < 0.0f) {
        hue += 6.0f;
      }
    } else if (maximum == green) {
      hue = (blue - red) / delta + 2.0f;
    } else {
      hue = (red - green) / delta + 4.0f;
    }
    hue /= 6.0f;

    float saturation = delta / maximum;
    saturation *= static_cast<float>(saturationPercent_) / 100.0f;
    if (saturation > 1.0f) {
      saturation = 1.0f;
    }

    const float chroma = maximum * saturation;
    const float hueSector = hue * 6.0f;
    const float secondary =
        chroma * (1.0f - fabsf(fmodf(hueSector, 2.0f) - 1.0f));
    float adjustedRed = 0.0f;
    float adjustedGreen = 0.0f;
    float adjustedBlue = 0.0f;

    if (hueSector < 1.0f) {
      adjustedRed = chroma;
      adjustedGreen = secondary;
    } else if (hueSector < 2.0f) {
      adjustedRed = secondary;
      adjustedGreen = chroma;
    } else if (hueSector < 3.0f) {
      adjustedGreen = chroma;
      adjustedBlue = secondary;
    } else if (hueSector < 4.0f) {
      adjustedGreen = secondary;
      adjustedBlue = chroma;
    } else if (hueSector < 5.0f) {
      adjustedRed = secondary;
      adjustedBlue = chroma;
    } else {
      adjustedRed = chroma;
      adjustedBlue = secondary;
    }

    const float offset = maximum - chroma;
    const uint8_t outputRed = static_cast<uint8_t>(
        (adjustedRed + offset) * 255.0f + 0.5f);
    const uint8_t outputGreen = static_cast<uint8_t>(
        (adjustedGreen + offset) * 255.0f + 0.5f);
    const uint8_t outputBlue = static_cast<uint8_t>(
        (adjustedBlue + offset) * 255.0f + 0.5f);
    return (static_cast<uint32_t>(outputRed) << 16) |
           (static_cast<uint32_t>(outputGreen) << 8) | outputBlue;
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
    Serial.print("[LED] Color swap interval: ");
    Serial.print(LED_COLOR_SWAP_DURATION_MS);
    Serial.println(" ms");
    Serial.print("[LED] Transition duration: ");
    Serial.print(LED_TRANSITION_DURATION_MS);
    Serial.println(" ms");
  }

  uint32_t colorForPixel(uint16_t pixel, uint32_t now) const {
    const uint32_t elapsed = now - phaseStartedAtMs_;
    const uint32_t fullCycleMs_ =
        LED_COLOR_SWAP_DURATION_MS * paletteColorCount_;
    const uint32_t cyclePosition = elapsed % fullCycleMs_;
    const uint8_t rotation = cyclePosition / LED_COLOR_SWAP_DURATION_MS;
    const uint8_t position = pixel % paletteColorCount_;
    const uint8_t fromIndex = (position + rotation) % paletteColorCount_;
    const uint32_t targetColor = targetColors_[fromIndex];
    const uint32_t timeIntoSwap =
        cyclePosition % LED_COLOR_SWAP_DURATION_MS;
    if (elapsed < LED_COLOR_SWAP_DURATION_MS ||
        timeIntoSwap >= LED_TRANSITION_DURATION_MS) {
      return targetColor;
    }

    const uint8_t previousIndex =
        (fromIndex + paletteColorCount_ - 1) % paletteColorCount_;
    const float progress =
        static_cast<float>(timeIntoSwap) / LED_TRANSITION_DURATION_MS;
    const float easedProgress = progress * progress * (3.0f - 2.0f * progress);
    return interpolateColor(targetColors_[previousIndex], targetColor,
                            easedProgress);
  }
};
