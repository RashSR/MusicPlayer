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

  enum class StartupState : uint8_t {
    Booting,
    WifiConnecting,
    WifiConnectedHold,
    WifiConnectedFade,
    WifiFailed,
    Ready
  };

  void begin() {
    strip_.begin();
    Serial.print("[LED] Initialized ");
    Serial.print(ledCount_);
    Serial.println(" WS2812B LEDs on GPIO16");
    const uint32_t now = millis();
    phaseStartedAtMs_ = now;
    clockFrozenAtMs_ = now;
    startupStartedAtMs_ = now;
    startupStateStartedAtMs_ = now;
    startupState_ = StartupState::Booting;
    applyStripBrightness(255);
    renderStartupFrame(now);
  }

  void setWifiConnected(bool connected) {
    wifiConnected_ = connected;
    if (!connected || wifiEverConnected_) {
      return;
    }

    wifiEverConnected_ = true;
    if (startupState_ == StartupState::WifiConnecting ||
        startupState_ == StartupState::WifiFailed ||
        startupState_ == StartupState::Ready) {
      startConnectedAnimation(millis());
    }
  }

  void setBrightness(uint8_t brightnessPercent) {
    if (brightnessPercent > 100 || brightnessPercent_ == brightnessPercent) {
      return;
    }

    brightnessPercent_ = brightnessPercent;
    if (startupState_ == StartupState::Ready) {
      applyStripBrightness(brightnessToNeoPixelValue(brightnessPercent_));
    }
    if (enabled_ && startupState_ == StartupState::Ready) {
      renderFrame(animationTime(millis()));
    } else if (enabled_) {
      renderStartupFrame(millis());
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
    if (enabled_ && startupState_ == StartupState::Ready) {
      renderFrame(animationTime(millis()));
    } else if (enabled_) {
      renderStartupFrame(millis());
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
    } else if (startupState_ != StartupState::Ready &&
               (mode == PlaybackMode::Paused ||
                mode == PlaybackMode::Stopped)) {
      phaseStartedAtMs_ += now - clockFrozenAtMs_;
      clockFrozenAtMs_ = now;
    } else if (startupState_ != StartupState::Ready &&
               mode == PlaybackMode::Playing &&
               previousMode == PlaybackMode::Paused) {
      phaseStartedAtMs_ += now - clockFrozenAtMs_;
      clockFrozenAtMs_ = now;
    }

    playbackMode_ = mode;
    updateClock(now, wasClockRunning);
    brightnessPercent_ = mode == PlaybackMode::Playing
        ? playingBrightnessPercent_
        : idleBrightnessPercent_;
    if (startupState_ == StartupState::Ready) {
      applyStripBrightness(brightnessToNeoPixelValue(brightnessPercent_));
    }

    if (!enabled_) {
      return;
    }
    if (startupState_ != StartupState::Ready) {
      renderStartupFrame(now);
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
    if (startupState_ != StartupState::Ready) {
      renderStartupFrame(now);
      return;
    }
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
    if (startupState_ != StartupState::Ready) {
      updateStartup(millis());
      return;
    }
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
  static constexpr uint8_t defaultSaturationPercent_ = 130;
  static constexpr uint32_t frameIntervalMs_ = 33;
  static constexpr uint32_t LED_COLOR_SWAP_DURATION_MS = 1000;
  static constexpr uint32_t LED_TRANSITION_DURATION_MS = 300;
  static constexpr uint32_t BOOTING_DURATION_MS = 300;
  static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
  static constexpr uint32_t WIFI_FAILURE_DISPLAY_MS = 2500;
  static constexpr uint32_t WIFI_CONNECTED_HOLD_MS = 800;
  static constexpr uint32_t WIFI_CONNECTED_FADE_MS = 400;
  static constexpr uint32_t STARTUP_FRAME_INTERVAL_MS = 33;
  static constexpr uint32_t startupPurple_ = 0x8D42D6;
  static constexpr uint32_t startupAmber_ = 0xFF8C00;
  static constexpr uint32_t defaultColor1_ = 0x315B4C;
  static constexpr uint32_t defaultColor2_ = 0xA9C7AE;

  Adafruit_NeoPixel strip_{ledCount_, dataPin_, NEO_GRB + NEO_KHZ800};
  uint32_t sourceColors_[3] = {defaultColor1_, defaultColor2_, 0};
  uint32_t targetColors_[3] = {defaultColor1_, defaultColor2_, 0};
  uint32_t phaseStartedAtMs_ = 0;
  uint32_t lastFrameAtMs_ = 0;
  uint32_t clockFrozenAtMs_ = 0;
  uint32_t startupStartedAtMs_ = 0;
  uint32_t startupStateStartedAtMs_ = 0;
  uint32_t lastStartupFrameAtMs_ = 0;
  uint32_t renderedColors_[ledCount_] = {};
  uint8_t paletteColorCount_ = 2;
  uint8_t brightnessPercent_ = idleBrightnessPercent_;
  uint8_t saturationPercent_ = defaultSaturationPercent_;
  uint8_t appliedStripBrightness_ = 0;
  String activeAlbum_;
  bool hasAlbum_ = false;
  bool enabled_ = true;
  bool wifiConnected_ = false;
  bool wifiEverConnected_ = false;
  bool frameDirty_ = true;
  bool frameCacheValid_ = false;
  PlaybackMode playbackMode_ = PlaybackMode::Idle;
  StartupState startupState_ = StartupState::Booting;

  bool isClockRunning() const {
    return enabled_ && startupState_ == StartupState::Ready &&
           playbackMode_ == PlaybackMode::Playing;
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
      setPixel(pixel, 0);
    }
    showIfDirty();
  }

  static uint8_t brightnessToNeoPixelValue(uint8_t brightnessPercent) {
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(brightnessPercent) * 255 + 50) / 100);
  }

  void applyStripBrightness(uint8_t brightness) {
    if (appliedStripBrightness_ == brightness) {
      return;
    }
    strip_.setBrightness(brightness);
    appliedStripBrightness_ = brightness;
    frameDirty_ = true;
    frameCacheValid_ = false;
  }

  void setPixel(uint16_t pixel, uint32_t color) {
    if (frameCacheValid_ && renderedColors_[pixel] == color) {
      return;
    }
    strip_.setPixelColor(pixel, color);
    renderedColors_[pixel] = color;
    frameDirty_ = true;
  }

  void showIfDirty() {
    if (frameDirty_) {
      strip_.show();
      frameDirty_ = false;
      frameCacheValid_ = true;
    }
  }

  void renderFrame(uint32_t now) {
    lastFrameAtMs_ = millis();
    for (uint16_t pixel = 0; pixel < ledCount_; ++pixel) {
      setPixel(pixel, saturateColor(colorForPixel(pixel, now)));
    }
    showIfDirty();
  }

  static uint32_t scaleColor(uint32_t color, uint8_t percent) {
    const uint8_t red = static_cast<uint8_t>(
        (static_cast<uint16_t>(channel(color, 16)) * percent + 50) / 100);
    const uint8_t green = static_cast<uint8_t>(
        (static_cast<uint16_t>(channel(color, 8)) * percent + 50) / 100);
    const uint8_t blue = static_cast<uint8_t>(
        (static_cast<uint16_t>(channel(color, 0)) * percent + 50) / 100);
    return (static_cast<uint32_t>(red) << 16) |
           (static_cast<uint32_t>(green) << 8) | blue;
  }

  static uint8_t pulseBrightness(uint32_t elapsed, uint32_t periodMs,
                                 uint8_t minimum, uint8_t maximum) {
    const float phase =
        static_cast<float>(elapsed % periodMs) * 6.2831853f / periodMs;
    const float amount = (sinf(phase) + 1.0f) * 0.5f;
    return static_cast<uint8_t>(
        minimum + (maximum - minimum) * amount + 0.5f);
  }

  void renderStartupFrame(uint32_t now) {
    if (!enabled_) {
      return;
    }

    if (now - lastStartupFrameAtMs_ < STARTUP_FRAME_INTERVAL_MS &&
        startupState_ != StartupState::Booting &&
        startupState_ != StartupState::WifiConnectedHold) {
      return;
    }
    lastStartupFrameAtMs_ = now;
    const uint32_t stateElapsed = now - startupStateStartedAtMs_;
    for (uint16_t pixel = 0; pixel < ledCount_; ++pixel) {
      uint32_t color = 0;
      uint8_t brightness = 0;
      switch (startupState_) {
        case StartupState::Booting:
          if (pixel % 5 == 0) {
            color = startupPurple_;
            brightness = 15;
          }
          break;
        case StartupState::WifiConnecting:
          if (pixel % 5 == 0) {
            color = startupPurple_;
            brightness = pulseBrightness(stateElapsed, 2400, 10, 16);
          }
          break;
        case StartupState::WifiConnectedHold:
          color = startupPurple_;
          brightness = 35;
          break;
        case StartupState::WifiConnectedFade: {
          const float progress =
              static_cast<float>(stateElapsed) / WIFI_CONNECTED_FADE_MS;
          const float easedProgress =
              progress * progress * (3.0f - 2.0f * progress);
          const uint32_t normalColor =
              saturateColor(colorForPixel(pixel, animationTime(now)));
          const uint32_t mixedColor =
              interpolateColor(startupPurple_, normalColor, easedProgress);
          const float mixedBrightness =
              35.0f + (brightnessPercent_ - 35.0f) * easedProgress;
          color = scaleColor(
              mixedColor, static_cast<uint8_t>(mixedBrightness + 0.5f));
          break;
        }
        case StartupState::WifiFailed:
          if (pixel % 5 == 0) {
            color = startupAmber_;
            brightness = pulseBrightness(stateElapsed, 2200, 5, 16);
          }
          break;
        case StartupState::Ready:
          return;
      }
      if (startupState_ != StartupState::WifiConnectedFade) {
        color = scaleColor(color, brightness);
      }
      setPixel(pixel, color);
    }
    showIfDirty();
  }

  void startConnectedAnimation(uint32_t now) {
    const bool wasClockRunning = isClockRunning();
    startupState_ = StartupState::WifiConnectedHold;
    updateClock(now, wasClockRunning);
    startupStateStartedAtMs_ = now;
    lastStartupFrameAtMs_ = now - STARTUP_FRAME_INTERVAL_MS;
    applyStripBrightness(255);
    renderStartupFrame(now);
  }

  void enterReady(uint32_t now) {
    const bool wasClockRunning = isClockRunning();
    startupState_ = StartupState::Ready;
    updateClock(now, wasClockRunning);
    applyStripBrightness(brightnessToNeoPixelValue(brightnessPercent_));
    if (enabled_) {
      if (playbackMode_ == PlaybackMode::Playing) {
        lastFrameAtMs_ = now - frameIntervalMs_;
        renderFrame(now);
      } else {
        renderFrame(animationTime(now));
      }
    }
  }

  void updateStartup(uint32_t now) {
    if (startupState_ == StartupState::Booting) {
      if (now - startupStartedAtMs_ >= BOOTING_DURATION_MS) {
        startupState_ = StartupState::WifiConnecting;
        startupStateStartedAtMs_ = now;
        lastStartupFrameAtMs_ = now - STARTUP_FRAME_INTERVAL_MS;
        renderStartupFrame(now);
      }
      return;
    }

    if (startupState_ == StartupState::WifiConnecting) {
      if (wifiConnected_) {
        wifiEverConnected_ = true;
        startConnectedAnimation(now);
        return;
      }
      if (now - startupStartedAtMs_ >= WIFI_CONNECT_TIMEOUT_MS) {
        startupState_ = StartupState::WifiFailed;
        startupStateStartedAtMs_ = now;
        lastStartupFrameAtMs_ = now - STARTUP_FRAME_INTERVAL_MS;
        renderStartupFrame(now);
        return;
      }
      renderStartupFrame(now);
      return;
    }

    if (startupState_ == StartupState::WifiConnectedHold) {
      if (now - startupStateStartedAtMs_ >= WIFI_CONNECTED_HOLD_MS) {
        startupState_ = StartupState::WifiConnectedFade;
        startupStateStartedAtMs_ = now;
        lastStartupFrameAtMs_ = now - STARTUP_FRAME_INTERVAL_MS;
        renderStartupFrame(now);
      }
      return;
    }

    if (startupState_ == StartupState::WifiConnectedFade) {
      if (now - startupStateStartedAtMs_ >= WIFI_CONNECTED_FADE_MS) {
        enterReady(now);
      } else {
        renderStartupFrame(now);
      }
      return;
    }

    if (startupState_ == StartupState::WifiFailed) {
      if (wifiConnected_ && wifiEverConnected_) {
        startConnectedAnimation(now);
      } else if (now - startupStateStartedAtMs_ >=
                 WIFI_FAILURE_DISPLAY_MS) {
        enterReady(now);
      } else {
        renderStartupFrame(now);
      }
    }
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
