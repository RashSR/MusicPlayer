#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include "wifi_credentials.h"
#include "DFPlayerDiagnostic.h"
#include "LedAlbumLighting.h"

HardwareSerial dfSerial(1);
DFPlayerDiagnostic dfPlayerDiagnostic(dfSerial);
LedAlbumLighting ledAlbumLighting;
WebServer server(80);
bool wasConnectedToWiFi = false;
bool wifiConfigured = false;
bool webServerStarted = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 10000;
uint32_t lastUptimeMillis = 0;
uint64_t uptimeMilliseconds = 0;
char serialCommand[32];
size_t serialCommandLength = 0;
uint32_t lastPlaybackRequestId = 0;
uint32_t lastPlaybackRequestAtMs = 0;
bool hasRecentPlaybackRequest = false;

void updateUptime() {
  const uint32_t currentMillis = millis();
  uptimeMilliseconds += static_cast<uint32_t>(currentMillis - lastUptimeMillis);
  lastUptimeMillis = currentMillis;
}

void formatUptime(uint64_t uptimeSeconds, char* buffer, size_t bufferSize) {
  const unsigned long long days = uptimeSeconds / 86400ULL;
  const unsigned long long hours = (uptimeSeconds / 3600ULL) % 24ULL;
  const unsigned long long minutes = (uptimeSeconds / 60ULL) % 60ULL;
  const unsigned long long seconds = uptimeSeconds % 60ULL;

  if (days > 0) {
    snprintf(buffer, bufferSize, "%llud %lluh %llum %llus",
             days, hours, minutes, seconds);
  } else if (hours > 0) {
    snprintf(buffer, bufferSize, "%lluh %llum %llus",
             hours, minutes, seconds);
  } else if (minutes > 0) {
    snprintf(buffer, bufferSize, "%llum %llus", minutes, seconds);
  } else {
    snprintf(buffer, bufferSize, "%llus", seconds);
  }
}

const char* httpMethodName(HTTPMethod method) {
  switch (method) {
    case HTTP_GET:
      return "GET";
    case HTTP_POST:
      return "POST";
    case HTTP_PUT:
      return "PUT";
    case HTTP_DELETE:
      return "DELETE";
    case HTTP_OPTIONS:
      return "OPTIONS";
    case HTTP_HEAD:
      return "HEAD";
    case HTTP_PATCH:
      return "PATCH";
    default:
      return "UNKNOWN";
  }
}

void logHttpResponse(int statusCode) {
  Serial.print("[HTTP] ");
  Serial.print(httpMethodName(server.method()));
  Serial.print(" ");
  Serial.print(server.uri());
  if ((server.uri() == "/play" ||
       server.uri() == "/api/play-selection") &&
      (server.hasArg("id") || server.hasArg("catalogId"))) {
    const String id = server.hasArg("id")
        ? server.arg("id")
        : server.arg("catalogId");
    Serial.print("?id=");
    bool validIdForLog = id.length() > 0 && id.length() <= 4;
    for (size_t i = 0; validIdForLog && i < id.length(); ++i) {
      validIdForLog = id[i] >= '0' && id[i] <= '9';
    }
    if (validIdForLog) {
      Serial.print(id);
    } else {
      Serial.print("<invalid>");
    }
  }
  Serial.print(" -> ");
  Serial.println(statusCode);
}

void sendHttpResponse(int statusCode, const char* contentType,
                      const String& body) {
  logHttpResponse(statusCode);
  server.send(statusCode, contentType, body);
}

void handleNotFound() {
  sendHttpResponse(404, "text/plain", "Not found: " + server.uri());
}

void handleStaticFile(const char* path, const char* contentType) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    sendHttpResponse(404, "text/plain", "Not found: " + String(path));
    return;
  }

  logHttpResponse(200);
  server.streamFile(file, contentType);
  file.close();
}

void handleLandingPage(){
  Serial.print("Client connected from: ");
  Serial.println(server.client().remoteIP());
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    Serial.println("index.html NOT FOUND in LittleFS");
    sendHttpResponse(500, "text/plain", "File not found");
    return;
  }
  logHttpResponse(200);
  server.streamFile(file, "text/html");
  file.close(); 
}

bool parsePositiveDecimal(const String& text, uint32_t maxValue,
                          uint32_t& value) {
  if (text.length() == 0) {
    return false;
  }

  value = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const char digit = text[i];
    if (digit < '0' || digit > '9') {
      return false;
    }

    const uint8_t digitValue = static_cast<uint8_t>(digit - '0');
    if (value > (maxValue - digitValue) / 10) {
      return false;
    }
    value = value * 10 + digitValue;
  }

  return value > 0;
}

bool parseRgbColor(const String& text, uint32_t& color) {
  if (text.length() != 7 || text[0] != '#') {
    return false;
  }

  color = 0;
  for (size_t i = 1; i < text.length(); ++i) {
    const char digit = text[i];
    uint8_t value = 0;
    if (digit >= '0' && digit <= '9') {
      value = static_cast<uint8_t>(digit - '0');
    } else if (digit >= 'A' && digit <= 'F') {
      value = static_cast<uint8_t>(digit - 'A' + 10);
    } else if (digit >= 'a' && digit <= 'f') {
      value = static_cast<uint8_t>(digit - 'a' + 10);
    } else {
      return false;
    }
    color = (color << 4) | value;
  }
  return true;
}

struct CatalogSong {
  String album;
  String title;
  uint32_t id = 0;
  uint32_t color1 = 0x315B4C;
  uint32_t color2 = 0xA9C7AE;
  uint32_t color3 = 0;
  bool hasThirdColor = false;
  bool usedFallbackColors = true;
};

enum class CatalogLookupResult {
  Found,
  NotFound,
  DuplicateId,
  Unavailable,
  Invalid
};

CatalogLookupResult findCatalogSong(uint32_t requestedId,
                                    CatalogSong& selection) {
  File file = LittleFS.open("/songs.json", "r");
  if (!file) {
    Serial.println("[Catalog] Could not open /songs.json.");
    return CatalogLookupResult::Unavailable;
  }

  DynamicJsonDocument catalog(32768);
  const DeserializationError error = deserializeJson(catalog, file);
  file.close();
  if (error) {
    Serial.print("[Catalog] Could not parse /songs.json: ");
    Serial.println(error.c_str());
    return CatalogLookupResult::Invalid;
  }

  const JsonArrayConst albums = catalog["albums"].as<JsonArrayConst>();
  if (albums.isNull()) {
    Serial.println("[Catalog] /songs.json has no albums array.");
    return CatalogLookupResult::Invalid;
  }

  uint8_t matches = 0;
  for (JsonObjectConst album : albums) {
    const char* albumName = album["name"].as<const char*>();
    const JsonArrayConst tracks = album["tracks"].as<JsonArrayConst>();
    if (albumName == nullptr || tracks.isNull()) {
      Serial.println("[Catalog] Album entry is missing its name or tracks.");
      return CatalogLookupResult::Invalid;
    }

    uint32_t colors[3] = {0x315B4C, 0xA9C7AE, 0};
    bool hasThirdColor = false;
    bool usedFallbackColors = true;
    const JsonArrayConst ledColors =
        album["ledColors"].as<JsonArrayConst>();
    if (ledColors.size() == 2 || ledColors.size() == 3) {
      uint32_t parsedColors[3] = {};
      bool validColors = true;
      for (size_t i = 0; i < ledColors.size(); ++i) {
        const char* colorText = ledColors[i].as<const char*>();
        if (colorText == nullptr ||
            !parseRgbColor(String(colorText), parsedColors[i])) {
          validColors = false;
          break;
        }
      }
      if (validColors) {
        colors[0] = parsedColors[0];
        colors[1] = parsedColors[1];
        colors[2] = parsedColors[2];
        hasThirdColor = ledColors.size() == 3;
        usedFallbackColors = false;
      }
    }

    for (JsonObjectConst song : tracks) {
      const JsonVariantConst idValue = song["id"];
      const char* title = song["title"].as<const char*>();
      if (!idValue.is<uint32_t>() || title == nullptr || title[0] == '\0') {
        Serial.println("[Catalog] Song entry has an invalid ID or title.");
        return CatalogLookupResult::Invalid;
      }
      const uint32_t songId = idValue.as<uint32_t>();
      if (songId == 0 || songId > 9999) {
        Serial.println("[Catalog] Song ID is outside the range 1-9999.");
        return CatalogLookupResult::Invalid;
      }
      if (songId != requestedId) {
        continue;
      }

      ++matches;
      selection.album = albumName;
      selection.title = title;
      selection.id = songId;
      selection.color1 = colors[0];
      selection.color2 = colors[1];
      selection.color3 = colors[2];
      selection.hasThirdColor = hasThirdColor;
      selection.usedFallbackColors = usedFallbackColors;
    }
  }

  if (matches > 1) {
    Serial.print("[Catalog] Duplicate song ID: ");
    Serial.println(requestedId);
    return CatalogLookupResult::DuplicateId;
  }
  return matches == 1 ? CatalogLookupResult::Found
                      : CatalogLookupResult::NotFound;
}

void printRgbColor(uint32_t color) {
  char formatted[8];
  snprintf(formatted, sizeof(formatted), "#%06lX",
           static_cast<unsigned long>(color));
  Serial.print(formatted);
}

void applyAlbumColors(const String& album, uint32_t color1, uint32_t color2,
                     uint32_t color3, bool hasThirdColor,
                     bool usedFallbackColors) {
  Serial.print("[LED] Requested colors: ");
  printRgbColor(color1);
  Serial.print(" / ");
  printRgbColor(color2);
  if (hasThirdColor) {
    Serial.print(" / ");
    printRgbColor(color3);
  }
  Serial.println();
  if (usedFallbackColors) {
    Serial.println("[LED] No album colors provided; using fallback.");
  } else {
    Serial.println("[LED] Color values accepted.");
  }
  ledAlbumLighting.setAlbumColors(album, color1, color2, color3,
                                  hasThirdColor);
}

void logPlaySelection(const CatalogSong& selection) {
  Serial.println("[MusicPlayer] Play requested");
  Serial.print("[MusicPlayer] Album: ");
  Serial.println(selection.album);
  Serial.print("[MusicPlayer] Song: ");
  Serial.println(selection.title);
  Serial.print("[MusicPlayer] Catalog ID: ");
  Serial.println(selection.id);
}

enum class PlaybackRequestResult {
  Sent,
  DuplicateRequest,
  NotFound,
  DuplicateCatalogId,
  CatalogUnavailable,
  InvalidCatalog,
  PlayerUnavailable
};

PlaybackRequestResult playCatalogSong(uint32_t catalogId) {
  CatalogSong selection;
  switch (findCatalogSong(catalogId, selection)) {
    case CatalogLookupResult::NotFound:
      return PlaybackRequestResult::NotFound;
    case CatalogLookupResult::DuplicateId:
      return PlaybackRequestResult::DuplicateCatalogId;
    case CatalogLookupResult::Invalid:
      return PlaybackRequestResult::InvalidCatalog;
    case CatalogLookupResult::Unavailable:
      return PlaybackRequestResult::CatalogUnavailable;
    case CatalogLookupResult::Found:
      break;
  }

  const uint32_t now = millis();
  if (hasRecentPlaybackRequest && lastPlaybackRequestId == catalogId &&
      now - lastPlaybackRequestAtMs < 500) {
    Serial.print("[MusicPlayer] Ignoring duplicate play request for catalog ID ");
    Serial.println(catalogId);
    return PlaybackRequestResult::DuplicateRequest;
  }

  applyAlbumColors(selection.album, selection.color1, selection.color2,
                   selection.color3, selection.hasThirdColor,
                   selection.usedFallbackColors);
  logPlaySelection(selection);
  if (!dfPlayerDiagnostic.playMp3Folder(
          static_cast<uint16_t>(selection.id))) {
    return PlaybackRequestResult::PlayerUnavailable;
  }
  hasRecentPlaybackRequest = true;
  lastPlaybackRequestId = catalogId;
  lastPlaybackRequestAtMs = now;
  return PlaybackRequestResult::Sent;
}

void sendPlaybackRequestResponse(PlaybackRequestResult result) {
  switch (result) {
    case PlaybackRequestResult::Sent:
      sendHttpResponse(
          200, "text/plain",
          "Playback command sent; file availability and playback are not confirmed");
      break;
    case PlaybackRequestResult::DuplicateRequest:
      sendHttpResponse(
          200, "text/plain",
          "Duplicate selection ignored; playback is not confirmed");
      break;
    case PlaybackRequestResult::NotFound:
      sendHttpResponse(404, "text/plain", "Catalog song ID not found");
      break;
    case PlaybackRequestResult::DuplicateCatalogId:
      sendHttpResponse(409, "text/plain", "Catalog song ID is ambiguous");
      break;
    case PlaybackRequestResult::CatalogUnavailable:
    case PlaybackRequestResult::InvalidCatalog:
      sendHttpResponse(500, "text/plain", "Could not read the song catalog");
      break;
    case PlaybackRequestResult::PlayerUnavailable:
      sendHttpResponse(
          503, "text/plain",
          "DFPlayer is not initialized or the command could not be sent.");
      break;
  }
}

void handleCatalogPlay() {
  const char* idArgument = server.hasArg("id")
      ? "id"
      : (server.hasArg("catalogId") ? "catalogId" : nullptr);
  if (idArgument == nullptr) {
    sendHttpResponse(400, "text/plain", "Missing catalog ID");
    return;
  }

  uint32_t catalogId = 0;
  if (!parsePositiveDecimal(server.arg(idArgument), 9999, catalogId)) {
    sendHttpResponse(400, "text/plain",
                     "Invalid ID: expected a decimal integer from 1 to 9999");
    return;
  }
  sendPlaybackRequestResponse(playCatalogSong(catalogId));
}

void handlePlay() {
  handleCatalogPlay();
}

void handlePlaySelection() {
  handleCatalogPlay();
}

void handlePause(){
  if (!dfPlayerDiagnostic.pause()) {
    sendHttpResponse(503, "text/plain", "DFPlayer is not initialized");
    return;
  }
  sendHttpResponse(200, "text/plain", "Paused");
}

void handleResume(){
  if (!dfPlayerDiagnostic.resume()) {
    sendHttpResponse(503, "text/plain", "DFPlayer is not initialized");
    return;
  }
  sendHttpResponse(200, "text/plain", "Resumed");
}

void handleNext(){
  if (!dfPlayerDiagnostic.next()) {
    sendHttpResponse(503, "text/plain", "DFPlayer is not initialized");
    return;
  }
  sendHttpResponse(200, "text/plain", "Next");
}

void handlePrevious(){
  if (!dfPlayerDiagnostic.previous()) {
    sendHttpResponse(503, "text/plain", "DFPlayer is not initialized");
    return;
  }
  sendHttpResponse(200, "text/plain", "Previous");
}

void handleStop(){
  if (!dfPlayerDiagnostic.stop()) {
    sendHttpResponse(503, "text/plain", "DFPlayer is not initialized");
    return;
  }
  sendHttpResponse(200, "text/plain", "Stopped");
}

void handleStatus(){
  const IPAddress ip = WiFi.localIP();
  const uint64_t uptimeSeconds = uptimeMilliseconds / 1000ULL;
  char uptimeFormatted[48];
  char trackCount[8];
  char response[448];
  formatUptime(uptimeSeconds, uptimeFormatted, sizeof(uptimeFormatted));
  if (dfPlayerDiagnostic.hasTrackCount()) {
    snprintf(trackCount, sizeof(trackCount), "%u",
             static_cast<unsigned int>(dfPlayerDiagnostic.trackCount()));
  } else {
    snprintf(trackCount, sizeof(trackCount), "null");
  }
  snprintf(response, sizeof(response),
           "{\"online\":true,\"device\":\"ESP32 Music Player\","
           "\"ip\":\"%u.%u.%u.%u\",\"uptime\":%llu,"
           "\"uptime_formatted\":\"%s\",\"wifi_rssi\":%d,"
           "\"free_heap\":%lu,\"dfplayer_uart\":\"%s\","
           "\"dfplayer_last_response_ms\":%lu,\"dfplayer_sd\":\"%s\","
           "\"dfplayer_tracks\":%s,\"dfplayer_playback\":\"%s\"}",
           static_cast<unsigned int>(ip[0]),
           static_cast<unsigned int>(ip[1]),
           static_cast<unsigned int>(ip[2]),
           static_cast<unsigned int>(ip[3]),
           static_cast<unsigned long long>(uptimeSeconds),
           uptimeFormatted,
           static_cast<int>(WiFi.RSSI()),
           static_cast<unsigned long>(ESP.getFreeHeap()),
           dfPlayerDiagnostic.connectionStatus(),
           static_cast<unsigned long>(dfPlayerDiagnostic.lastResponseMs()),
           dfPlayerDiagnostic.sdStatus(),
           trackCount,
           dfPlayerDiagnostic.playbackStatus());

  server.sendHeader("Cache-Control", "no-store");
  sendHttpResponse(200, "application/json", response);
}

void setupRoutes(){
  server.on("/", handleLandingPage);
  server.on("/app.js", HTTP_GET, []() {
    handleStaticFile("/app.js", "text/javascript");
  });
  server.on("/style.css", HTTP_GET, []() {
    handleStaticFile("/style.css", "text/css");
  });
  server.on("/songs.json", HTTP_GET, []() {
    handleStaticFile("/songs.json", "application/json");
  });
  server.on("/favicon.ico", HTTP_GET, []() {
    sendHttpResponse(204, "image/x-icon", "");
  });
  server.on("/play", handlePlay);
  server.on("/api/play-selection", HTTP_GET, handlePlaySelection);
  server.on("/pause", handlePause);
  server.on("/resume", handleResume);
  server.on("/next", handleNext);
  server.on("/previous", handlePrevious);
  server.on("/stop", handleStop);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);
}

void printSerialHelp() {
  Serial.println("[DFPlayer] Serial commands (end each with Enter):");
  Serial.println("  status  - print cached status and query the module");
  Serial.println("  init    - reinitialize the DFPlayer library");
  Serial.println("  volume  - set volume to 15 (range 0-30)");
  Serial.println("  play <id> - play catalog ID, e.g. play 702");
  Serial.println("  pause   - pause playback");
  Serial.println("  resume  - resume playback");
  Serial.println("  stop    - stop playback");
  Serial.println("  next    - play next SD track");
  Serial.println("  prev    - play previous SD track");
}

void processSerialCommand() {
  serialCommand[serialCommandLength] = '\0';
  if (strcmp(serialCommand, "help") == 0) {
    printSerialHelp();
  } else if (strcmp(serialCommand, "status") == 0) {
    dfPlayerDiagnostic.printStatus();
  } else if (strcmp(serialCommand, "init") == 0) {
    dfPlayerDiagnostic.initialize();
  } else if (strcmp(serialCommand, "volume") == 0) {
    if (!dfPlayerDiagnostic.setVolume(15)) {
      Serial.println("[DFPlayer] Cannot set volume: player is not initialized.");
    }
  } else if (strcmp(serialCommand, "play") == 0) {
    Serial.println("[DFPlayer] Usage: play <catalog ID from 1 to 9999>");
  } else if (strncmp(serialCommand, "play ", 5) == 0) {
    uint32_t catalogId = 0;
    if (!parsePositiveDecimal(String(serialCommand + 5), 9999, catalogId)) {
      Serial.println("[DFPlayer] Invalid catalog ID; use play <ID> from 1 to 9999.");
    } else {
      const PlaybackRequestResult result = playCatalogSong(catalogId);
      if (result == PlaybackRequestResult::DuplicateRequest) {
        Serial.println("[DFPlayer] Duplicate playback request ignored.");
      } else if (result == PlaybackRequestResult::NotFound) {
        Serial.println("[DFPlayer] Catalog ID was not found in /songs.json.");
      } else if (result == PlaybackRequestResult::DuplicateCatalogId) {
        Serial.println("[DFPlayer] Catalog ID occurs more than once.");
      } else if (result == PlaybackRequestResult::CatalogUnavailable ||
                 result == PlaybackRequestResult::InvalidCatalog) {
        Serial.println("[DFPlayer] Could not resolve ID from /songs.json.");
      } else if (result == PlaybackRequestResult::PlayerUnavailable) {
        Serial.println("[DFPlayer] Cannot play: player is not initialized.");
      }
    }
  } else if (strcmp(serialCommand, "pause") == 0) {
    if (!dfPlayerDiagnostic.pause()) {
      Serial.println("[DFPlayer] Cannot pause: player is not initialized.");
    }
  } else if (strcmp(serialCommand, "resume") == 0) {
    if (!dfPlayerDiagnostic.resume()) {
      Serial.println("[DFPlayer] Cannot resume: player is not initialized.");
    }
  } else if (strcmp(serialCommand, "stop") == 0) {
    if (!dfPlayerDiagnostic.stop()) {
      Serial.println("[DFPlayer] Cannot stop: player is not initialized.");
    }
  } else if (strcmp(serialCommand, "next") == 0) {
    if (!dfPlayerDiagnostic.next()) {
      Serial.println("[DFPlayer] Cannot skip: player is not initialized.");
    }
  } else if (strcmp(serialCommand, "prev") == 0) {
    if (!dfPlayerDiagnostic.previous()) {
      Serial.println("[DFPlayer] Cannot go back: player is not initialized.");
    }
  } else if (serialCommandLength > 0) {
    Serial.print("[DFPlayer] Unknown command: ");
    Serial.println(serialCommand);
    printSerialHelp();
  }
  serialCommandLength = 0;
}

void updateSerialCommands() {
  while (Serial.available() > 0) {
    const char character = static_cast<char>(Serial.read());
    if (character == '\r') {
      continue;
    }
    if (character == '\n') {
      processSerialCommand();
      continue;
    }
    if (serialCommandLength + 1 < sizeof(serialCommand)) {
      serialCommand[serialCommandLength++] = character;
    } else {
      serialCommandLength = 0;
      Serial.println("[DFPlayer] Command too long; input discarded.");
    }
  }
}

void startWebServer() {
  setupRoutes();
  server.serveStatic("/", LittleFS, "/");
  server.begin();
  webServerStarted = true;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  dfPlayerDiagnostic.begin();
  printSerialHelp();
  ledAlbumLighting.begin();
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    return;
  } 

  if (strlen(WIFI_SSID) == 0 || strlen(WIFI_PASSWORD) == 0 ||
      strcmp(WIFI_SSID, "YOUR_HOME_WIFI_NAME") == 0 ||
      strcmp(WIFI_PASSWORD, "YOUR_HOME_WIFI_PASSWORD") == 0) {
    Serial.println("Set your home Wi-Fi name and password in include/wifi_credentials.h");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiConfigured = true;
  lastReconnectAttempt = millis();
  Serial.println("Connecting to home Wi-Fi");
}

void loop() {
  updateUptime();
  dfPlayerDiagnostic.update();
  updateSerialCommands();
  ledAlbumLighting.update();
  if (!wifiConfigured) {
    delay(1);
    return;
  }

  const bool connectedToWiFi = WiFi.status() == WL_CONNECTED;
  if (connectedToWiFi && !wasConnectedToWiFi) {
    Serial.println("Connected to Wi-Fi!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Status endpoint: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/api/status");
    Serial.print("Wi-Fi RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    if (!webServerStarted) {
      startWebServer();
    }
  } else if (!connectedToWiFi && wasConnectedToWiFi) {
    Serial.println("Wi-Fi disconnected; waiting to reconnect");
  }
  wasConnectedToWiFi = connectedToWiFi;

  if (!connectedToWiFi &&
      millis() - lastReconnectAttempt >= reconnectInterval) {
    Serial.println("Retrying Wi-Fi connection");
    WiFi.reconnect();
    lastReconnectAttempt = millis();
  }

  if (webServerStarted && connectedToWiFi) {
    server.handleClient();
  }
}