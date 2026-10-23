#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <string.h>

class DFPlayerDiagnostic {
 public:
  explicit DFPlayerDiagnostic(HardwareSerial& serial) : serial_(serial) {}

  void begin() {
    Serial.println("[DFPlayer] Initializing UART1...");
    serial_.begin(9600, SERIAL_8N1, 18, 17);
    Serial.println("[DFPlayer] RX=18 TX=17 Baud=9600");
    Serial.println("[DFPlayer] UART test does not require an SD card.");
    startedAtMs_ = millis();
  }

  void update() {
    uint32_t now = millis();
    while (serial_.available() > 0) {
      const int incomingByte = serial_.read();
      if (incomingByte >= 0) {
        processByte(static_cast<uint8_t>(incomingByte), now);
      }
      now = millis();
    }

    if (frameLength_ > 0 &&
        now - frameStartedAtMs_ >= responseTimeoutMs) {
      printFrameBytes();
      Serial.print("[DFPlayer] Incomplete frame (received ");
      Serial.print(frameLength_);
      Serial.println(" of 10 bytes).");
      frameLength_ = 0;
    }

    if (awaitingResponse_ && now - lastQueryMs_ >= responseTimeoutMs) {
      awaitingResponse_ = false;
      Serial.println("[DFPlayer] No response within timeout.");
      Serial.println("[DFPlayer] UART communication not confirmed.");
      Serial.println("[DFPlayer] Missing SD card may affect module behavior.");
    }

    if ((!hasSentQuery_ && now - startedAtMs_ >= startupWaitMs) ||
        (hasSentQuery_ && now - lastQueryMs_ >= queryIntervalMs)) {
      sendStatusQuery(now);
    }
  }

  const char* connectionStatus() const {
    const uint32_t now = millis();
    if (hasValidResponse_ && now - lastResponseMs_ <= recentResponseMs) {
      return "connected";
    }
    if (awaitingResponse_ || !hasSentQuery_) {
      return "waiting";
    }
    return "no_response";
  }

  uint32_t lastResponseMs() const {
    return lastResponseMs_;
  }

 private:
  static const uint32_t startupWaitMs = 1500;
  static const uint32_t queryIntervalMs = 5000;
  static const uint32_t responseTimeoutMs = 1500;
  static const uint32_t recentResponseMs = 15000;
  static const size_t frameSize = 10;

  HardwareSerial& serial_;
  uint8_t frame_[frameSize] = {};
  size_t frameLength_ = 0;
  uint32_t startedAtMs_ = 0;
  uint32_t lastQueryMs_ = 0;
  uint32_t frameStartedAtMs_ = 0;
  uint32_t lastResponseMs_ = 0;
  bool hasSentQuery_ = false;
  bool awaitingResponse_ = false;
  bool hasValidResponse_ = false;

  void sendStatusQuery(uint32_t now) {
    uint8_t query[frameSize] = {
      0x7E, 0xFF, 0x06, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEF
    };
    const uint16_t checksum = static_cast<uint16_t>(
        0 - (query[1] + query[2] + query[3] + query[4] +
             query[5] + query[6]));
    query[7] = static_cast<uint8_t>(checksum >> 8);
    query[8] = static_cast<uint8_t>(checksum & 0xFF);

    Serial.println("[DFPlayer] Sending status query (0x42)...");
    serial_.write(query, sizeof(query));
    lastQueryMs_ = now;
    hasSentQuery_ = true;
    awaitingResponse_ = true;
  }

  void processByte(uint8_t value, uint32_t now) {
    if (frameLength_ == 0) {
      if (value != 0x7E) {
        Serial.print("[DFPlayer] RX: ");
        printHexByte(value);
        Serial.println();
        Serial.println("[DFPlayer] Malformed data: invalid start byte.");
        return;
      }
      frameStartedAtMs_ = now;
    }

    frame_[frameLength_++] = value;
    if (frameLength_ == frameSize) {
      processFrame(now);
    }
  }

  void processFrame(uint32_t now) {
    printFrameBytes();
    const char* error = validateFrame();
    if (error != nullptr) {
      Serial.print("[DFPlayer] Malformed frame: ");
      Serial.println(error);
      resynchronizeFrame(now);
      return;
    }

    Serial.println("[DFPlayer] Valid UART response received!");
    Serial.print("[DFPlayer] Command: 0x");
    printHexByte(frame_[3]);
    Serial.println();
    Serial.print("[DFPlayer] Parameter: 0x");
    printHexByte(frame_[5]);
    printHexByte(frame_[6]);
    Serial.println();

    lastResponseMs_ = now;
    hasValidResponse_ = true;
    awaitingResponse_ = false;
    frameLength_ = 0;
  }

  const char* validateFrame() const {
    if (frame_[0] != 0x7E) {
      return "invalid start byte.";
    }
    if (frame_[1] != 0xFF) {
      return "invalid protocol version.";
    }
    if (frame_[2] != 0x06) {
      return "invalid frame length.";
    }
    if (frame_[9] != 0xEF) {
      return "invalid end byte.";
    }

    uint16_t sum = 0;
    for (size_t i = 1; i <= 6; ++i) {
      sum = static_cast<uint16_t>(sum + frame_[i]);
    }
    const uint16_t expectedChecksum = static_cast<uint16_t>(0 - sum);
    const uint16_t receivedChecksum =
        static_cast<uint16_t>((frame_[7] << 8) | frame_[8]);
    if (receivedChecksum != expectedChecksum) {
      return "invalid checksum.";
    }
    return nullptr;
  }

  void resynchronizeFrame(uint32_t now) {
    size_t nextStart = 1;
    while (nextStart < frameSize && frame_[nextStart] != 0x7E) {
      ++nextStart;
    }
    if (nextStart == frameSize) {
      frameLength_ = 0;
      return;
    }

    frameLength_ = frameSize - nextStart;
    memmove(frame_, frame_ + nextStart, frameLength_);
    frameStartedAtMs_ = now;
  }

  void printFrameBytes() const {
    Serial.print("[DFPlayer] RX: ");
    for (size_t i = 0; i < frameLength_; ++i) {
      printHexByte(frame_[i]);
      if (i + 1 < frameLength_) {
        Serial.print(' ');
      }
    }
    Serial.println();
  }

  static void printHexByte(uint8_t value) {
    if (value < 0x10) {
      Serial.print('0');
    }
    Serial.print(value, HEX);
  }
};
