#pragma once

#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>
#include <HardwareSerial.h>
#include <string.h>

class DFPlayerDiagnostic {
 public:
  explicit DFPlayerDiagnostic(HardwareSerial& serial)
      : serial_(serial) {}

  void begin() {
    Serial.println("[DFPlayer] Serial diagnostics use 115200 baud.");
    Serial.println("[DFPlayer] Initializing UART1...");
    serial_.begin(9600, SERIAL_8N1, 18, 17);
    Serial.println("[DFPlayer] UART initialized");
    Serial.println("[DFPlayer] RX=18 TX=17 Baud=9600");
    Serial.println("[DFPlayer] Initializing player without reset or ACK wait.");
    initializePlayer();
    Serial.println("[DFPlayer] Allowing 2 seconds for module and SD initialization.");
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

    if (frameLength_ > 0 && now - frameStartedAtMs_ >= frameTimeoutMs) {
      printFrameBytes();
      Serial.print("[DFPlayer] Incomplete frame (received ");
      Serial.print(frameLength_);
      Serial.println(" of 10 bytes).");
      frameLength_ = 0;
    }

    if (!started_ && now - startedAtMs_ >= startupWaitMs) {
      started_ = true;
      sendCurrentQuery(now);
    } else if (queryInFlight_ && now - querySentAtMs_ >= responseTimeoutMs) {
      Serial.println("[DFPlayer] No response within timeout.");
      failCurrentQuery(now);
    } else if (!queryInFlight_ && started_ && !diagnosticComplete_ &&
               now - lastQueryFinishedAtMs_ >= commandIntervalMs) {
      sendCurrentQuery(now);
    }
  }

  bool playMp3Folder(uint16_t catalogId) {
    if (!playerInitialized_ || catalogId == 0 || catalogId > 9999) {
      return false;
    }
    char path[16];
    snprintf(path, sizeof(path), "/mp3/%04u.mp3",
             static_cast<unsigned int>(catalogId));
    Serial.print("[DFPlayer] Playing ");
    Serial.println(path);
    player_.playMp3Folder(catalogId);
    Serial.print("[DFPlayer] playMp3Folder(");
    Serial.print(catalogId);
    Serial.println(") sent.");
    playbackStatus_ = "unknown";
    return true;
  }

  void initialize() {
    initializePlayer();
  }

  bool setVolume(uint8_t volume) {
    if (!playerInitialized_ || volume > 30) {
      return false;
    }
    player_.volume(volume);
    volume_ = volume;
    Serial.print("[DFPlayer] Volume set to ");
    Serial.println(volume);
    return true;
  }

  int8_t volume() const {
    return volume_;
  }

  bool pause() {
    if (!playerInitialized_) {
      return false;
    }
    player_.pause();
    playbackStatus_ = "paused";
    Serial.println("[DFPlayer] Pause command sent.");
    return true;
  }

  bool resume() {
    if (!playerInitialized_) {
      return false;
    }
    player_.start();
    playbackStatus_ = "playing";
    Serial.println("[DFPlayer] Resume command sent.");
    return true;
  }

  bool stop() {
    if (!playerInitialized_) {
      return false;
    }
    player_.stop();
    playbackStatus_ = "stopped";
    Serial.println("[DFPlayer] Stop command sent.");
    return true;
  }

  bool next() {
    if (!playerInitialized_) {
      return false;
    }
    player_.next();
    playbackStatus_ = "unknown";
    Serial.println("[DFPlayer] Next-track command sent.");
    return true;
  }

  bool previous() {
    if (!playerInitialized_) {
      return false;
    }
    player_.previous();
    playbackStatus_ = "unknown";
    Serial.println("[DFPlayer] Previous-track command sent.");
    return true;
  }

  void requestStatus() {
    if (queryInFlight_) {
      Serial.println("[DFPlayer] A status query is already in progress.");
      return;
    }
    queryStep_ = QueryStorage;
    attempts_ = 0;
    diagnosticComplete_ = false;
    lastQueryFinishedAtMs_ = 0;
    Serial.println("[DFPlayer] Status query scheduled.");
  }

  void printStatus() {
    Serial.print("[DFPlayer] Command interface ready: ");
    Serial.println(playerInitialized_ ? "yes" : "no");
    Serial.print("[DFPlayer] UART: ");
    Serial.println(connectionStatus());
    Serial.print("[DFPlayer] SD card: ");
    Serial.println(sdStatus_);
    Serial.print("[DFPlayer] Recognized tracks: ");
    if (hasTrackCount_) {
      Serial.println(trackCount_);
    } else {
      Serial.println("unknown");
    }
    Serial.print("[DFPlayer] Playback: ");
    Serial.println(playbackStatus_);
    Serial.print("[DFPlayer] Volume: ");
    if (volume_ >= 0) {
      Serial.println(volume_);
    } else {
      Serial.println("unknown");
    }
    requestStatus();
  }

  const char* connectionStatus() const {
    const uint32_t now = millis();
    if (hasValidResponse_ && now - lastResponseMs_ <= recentResponseMs) {
      return "connected";
    }
    if (queryInFlight_ || !hasSentQuery_) {
      return "waiting";
    }
    return "no_response";
  }

  uint32_t lastResponseMs() const {
    return lastResponseMs_;
  }

  bool takeTrackFinishedEvent(uint16_t& trackNumber) {
    if (!trackFinishedEvent_) {
      return false;
    }
    trackNumber = finishedTrackNumber_;
    trackFinishedEvent_ = false;
    return true;
  }

  const char* sdStatus() const {
    return sdStatus_;
  }

  bool hasTrackCount() const {
    return hasTrackCount_;
  }

  uint16_t trackCount() const {
    return trackCount_;
  }

  const char* playbackStatus() const {
    return playbackStatus_;
  }

 private:
  enum QueryStep : uint8_t {
    QueryStorage,
    QueryTrackCount,
    QueryPlayback
  };

  static const uint32_t startupWaitMs = 2000;
  static const uint32_t commandIntervalMs = 500;
  static const uint32_t responseTimeoutMs = 2000;
  static const uint32_t frameTimeoutMs = 250;
  static const uint32_t recentResponseMs = 15000;
  static const uint8_t maxAttempts = 3;
  static const size_t frameSize = 10;

  HardwareSerial& serial_;
  DFRobotDFPlayerMini player_;
  uint8_t frame_[frameSize] = {};
  size_t frameLength_ = 0;
  uint32_t startedAtMs_ = 0;
  uint32_t lastQueryFinishedAtMs_ = 0;
  uint32_t querySentAtMs_ = 0;
  uint32_t frameStartedAtMs_ = 0;
  uint32_t lastResponseMs_ = 0;
  uint16_t trackCount_ = 0;
  int8_t volume_ = -1;
  uint8_t attempts_ = 0;
  QueryStep queryStep_ = QueryStorage;
  bool started_ = false;
  bool hasSentQuery_ = false;
  bool queryInFlight_ = false;
  bool diagnosticComplete_ = false;
  bool hasValidResponse_ = false;
  bool hasTrackCount_ = false;
  bool playerInitialized_ = false;
  bool trackFinishedEvent_ = false;
  uint16_t finishedTrackNumber_ = 0;
  const char* sdStatus_ = "unknown";
  const char* playbackStatus_ = "unknown";

  void initializePlayer() {
    playerInitialized_ = player_.begin(serial_, false, false);
    if (playerInitialized_) {
      Serial.println("[DFPlayer] Command interface ready; checking module presence via UART.");
      setVolume(8);
    } else {
      Serial.println("[DFPlayer] DFRobotDFPlayerMini command interface initialization failed.");
    }
  }

  uint8_t expectedCommand() const {
    switch (queryStep_) {
      case QueryStorage:
        return 0x3F;
      case QueryTrackCount:
        return 0x48;
      case QueryPlayback:
        return 0x42;
    }
    return 0;
  }

  const char* queryDescription() const {
    switch (queryStep_) {
      case QueryStorage:
        return "storage devices";
      case QueryTrackCount:
        return "SD card file count";
      case QueryPlayback:
        return "playback status";
    }
    return "DFPlayer";
  }

  void sendCurrentQuery(uint32_t now) {
    if (attempts_ == 0) {
      Serial.print("[DFPlayer] Querying ");
      Serial.print(queryDescription());
      Serial.println("...");
    } else {
      Serial.print("[DFPlayer] Retrying ");
      Serial.print(queryDescription());
      Serial.print(" (attempt ");
      Serial.print(attempts_ + 1);
      Serial.println(" of 3)...");
    }

    sendCommand(expectedCommand(), 0);
    ++attempts_;
    querySentAtMs_ = now;
    hasSentQuery_ = true;
    queryInFlight_ = true;
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

    hasValidResponse_ = true;
    lastResponseMs_ = now;
    const uint8_t command = frame_[3];
    const uint16_t parameter =
        static_cast<uint16_t>((frame_[5] << 8) | frame_[6]);

    if (command == 0x40) {
      Serial.print("[DFPlayer] Player error code: ");
      Serial.println(parameter);
      switch (parameter) {
        case 1:
          Serial.println("[DFPlayer] Error: module is busy.");
          break;
        case 2:
          Serial.println("[DFPlayer] Error: module is sleeping.");
          break;
        case 3:
          Serial.println("[DFPlayer] Error: serial receive error.");
          break;
        case 4:
          Serial.println("[DFPlayer] Error: checksum mismatch.");
          break;
        case 5:
          Serial.println("[DFPlayer] Error: requested track is out of range.");
          break;
        case 6:
          Serial.println("[DFPlayer] Error: sleep command received.");
          break;
        case 7:
          Serial.println("[DFPlayer] Error: invalid serial command.");
          break;
        case 8:
          Serial.println("[DFPlayer] Error: serial checksum error.");
          break;
        case 9:
          Serial.println("[DFPlayer] Error: file not found on the SD card.");
          break;
        case 11:
          Serial.println("[DFPlayer] Error: SD card not found.");
          sdStatus_ = "not_detected";
          break;
        default:
          break;
      }
      playbackStatus_ = "error";
      if (queryInFlight_) {
        failCurrentQuery(now);
      }
      frameLength_ = 0;
      return;
    }

    if (command == 0x3A) {
      sdStatus_ = "detected";
      Serial.println("[DFPlayer] SD card inserted.");
      frameLength_ = 0;
      return;
    }
    if (command == 0x3B) {
      sdStatus_ = "not_detected";
      Serial.println("[DFPlayer] SD card removed.");
      frameLength_ = 0;
      return;
    }
    if (command == 0x3C) {
      Serial.println("[DFPlayer] Module is online.");
      frameLength_ = 0;
      return;
    }

    if (command == 0x3D) {
      finishedTrackNumber_ = parameter;
      trackFinishedEvent_ = true;
      Serial.print("[DFPlayer] Track-finished event: ");
      Serial.println(finishedTrackNumber_);
      frameLength_ = 0;
      return;
    }

    if (command == 0x41) {
      Serial.println("[DFPlayer] Acknowledgement received; awaiting query result.");
      frameLength_ = 0;
      return;
    }

    if (!queryInFlight_ || command != expectedCommand()) {
      Serial.print("[DFPlayer] Unexpected response command: 0x");
      printHexByte(command);
      Serial.println();
      frameLength_ = 0;
      return;
    }

    Serial.println("[DFPlayer] Valid query response received.");
    Serial.print("[DFPlayer] Command: 0x");
    printHexByte(command);
    Serial.println();
    Serial.print("[DFPlayer] Parameter: 0x");
    printHexByte(frame_[5]);
    printHexByte(frame_[6]);
    Serial.println();

    switch (queryStep_) {
      case QueryStorage:
        processStorageResponse(parameter, now);
        break;
      case QueryTrackCount:
        trackCount_ = parameter;
        hasTrackCount_ = true;
        Serial.print("[DFPlayer] Recognized tracks: ");
        Serial.println(trackCount_);
        finishCurrentQuery(now);
        break;
      case QueryPlayback:
        processPlaybackResponse(parameter);
        finishCurrentQuery(now);
        break;
    }
    frameLength_ = 0;
  }

  void processStorageResponse(uint16_t devices, uint32_t now) {
    Serial.print("[DFPlayer] Available storage mask: 0x");
    printHexByte(static_cast<uint8_t>(devices >> 8));
    printHexByte(static_cast<uint8_t>(devices));
    Serial.println();

    if ((devices & 0x0002) != 0) {
      sdStatus_ = "detected";
      Serial.println("[DFPlayer] SD card detected!");
    } else {
      sdStatus_ = "not_detected";
      Serial.println("[DFPlayer] SD card detection not confirmed.");
    }
    finishCurrentQuery(now);
  }

  void processPlaybackResponse(uint16_t status) {
    const uint8_t device = static_cast<uint8_t>(status >> 8);
    const uint8_t state = static_cast<uint8_t>(status);
    if (device != 0) {
      Serial.print("[DFPlayer] Playback response device: 0x");
      printHexByte(device);
      Serial.println();
    }

    switch (state) {
      case 0:
        playbackStatus_ = "stopped";
        Serial.println("[DFPlayer] Playback status: Stopped");
        break;
      case 1:
        playbackStatus_ = "playing";
        Serial.println("[DFPlayer] Playback status: Playing");
        break;
      case 2:
        playbackStatus_ = "paused";
        Serial.println("[DFPlayer] Playback status: Paused");
        break;
      default:
        playbackStatus_ = "unknown";
        Serial.print("[DFPlayer] Unexpected playback state: 0x");
        printHexByte(state);
        Serial.println();
        break;
    }
  }

  bool sendCommand(uint8_t command, uint16_t parameter) {
    uint8_t frame[frameSize] = {
      0x7E, 0xFF, 0x06, command, 0x00,
      static_cast<uint8_t>(parameter >> 8),
      static_cast<uint8_t>(parameter & 0xFF),
      0x00, 0x00, 0xEF
    };
    const uint16_t checksum = static_cast<uint16_t>(
        0 - (frame[1] + frame[2] + frame[3] + frame[4] +
             frame[5] + frame[6]));
    frame[7] = static_cast<uint8_t>(checksum >> 8);
    frame[8] = static_cast<uint8_t>(checksum & 0xFF);

    if (frame[0] != 0x7E || frame[1] != 0xFF ||
        frame[2] != 0x06 || frame[9] != 0xEF ||
        readChecksum(frame) != calculateChecksum(frame)) {
      Serial.println("[DFPlayer] Refusing to send command with invalid frame/checksum.");
      return false;
    }

    Serial.print("[DFPlayer] TX: ");
    for (size_t i = 0; i < frameSize; ++i) {
      printHexByte(frame[i]);
      if (i + 1 < frameSize) {
        Serial.print(' ');
      }
    }
    Serial.println();
    return serial_.write(frame, sizeof(frame)) == sizeof(frame);
  }

  static uint16_t calculateChecksum(const uint8_t* frame) {
    uint16_t sum = 0;
    for (size_t i = 1; i <= 6; ++i) {
      sum = static_cast<uint16_t>(sum + frame[i]);
    }
    return static_cast<uint16_t>(0 - sum);
  }

  static uint16_t readChecksum(const uint8_t* frame) {
    return static_cast<uint16_t>((frame[7] << 8) | frame[8]);
  }

  void finishCurrentQuery(uint32_t now) {
    queryInFlight_ = false;
    attempts_ = 0;
    lastQueryFinishedAtMs_ = now;
    if (queryStep_ == QueryPlayback) {
      if (!diagnosticComplete_) {
        diagnosticComplete_ = true;
        Serial.println("[DFPlayer] SD card diagnostic completed.");
      }
      return;
    }
    queryStep_ = static_cast<QueryStep>(queryStep_ + 1);
  }

  void failCurrentQuery(uint32_t now) {
    queryInFlight_ = false;
    lastQueryFinishedAtMs_ = now;
    if (attempts_ < maxAttempts) {
      return;
    }

    if (queryStep_ == QueryStorage) {
      Serial.println("[DFPlayer] SD card detection not confirmed.");
    } else if (queryStep_ == QueryTrackCount) {
      Serial.println("[DFPlayer] File count unavailable.");
    }
    attempts_ = 0;
    if (queryStep_ == QueryPlayback) {
      if (!diagnosticComplete_) {
        diagnosticComplete_ = true;
        Serial.println("[DFPlayer] SD card diagnostic completed.");
      }
      return;
    }
    queryStep_ = static_cast<QueryStep>(queryStep_ + 1);
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

    if (readChecksum(frame_) != calculateChecksum(frame_)) {
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
