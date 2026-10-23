#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include "wifi_credentials.h"
#include "DFPlayerDiagnostic.h"

HardwareSerial dfSerial(1);
DFPlayerDiagnostic dfPlayerDiagnostic(dfSerial);
WebServer server(80);
bool wasConnectedToWiFi = false;
bool wifiConfigured = false;
bool webServerStarted = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 10000;
uint32_t lastUptimeMillis = 0;
uint64_t uptimeMilliseconds = 0;

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
  if (server.uri() == "/play" && server.hasArg("id")) {
    const String id = server.arg("id");
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

bool isValidMetadataText(const String& value, size_t maxLength) {
  if (value.length() == 0 || value.length() > maxLength) {
    return false;
  }

  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t character = static_cast<uint8_t>(value[i]);
    if (character < 0x20 || character == 0x7F) {
      return false;
    }
  }
  return true;
}

bool hasSelectionMetadata() {
  return server.hasArg("album") || server.hasArg("title") ||
         server.hasArg("catalogId") || server.hasArg("dfplayerTrack");
}

bool readSelectionMetadata(String& album, String& title,
                           uint32_t& catalogId, uint32_t& dfplayerTrack,
                           bool requireTrack) {
  if (!server.hasArg("album") || !server.hasArg("title") ||
      !server.hasArg("catalogId") ||
      (requireTrack && !server.hasArg("dfplayerTrack")) ||
      (!requireTrack && server.hasArg("dfplayerTrack"))) {
    return false;
  }

  album = server.arg("album");
  title = server.arg("title");
  return isValidMetadataText(album, 80) &&
         isValidMetadataText(title, 120) &&
         parsePositiveDecimal(server.arg("catalogId"), 999999999, catalogId) &&
         (!requireTrack ||
          parsePositiveDecimal(server.arg("dfplayerTrack"), 9999,
                               dfplayerTrack));
}

void logPlaySelection(const String& album, const String& title,
                      uint32_t catalogId, const String& dfplayerTrack) {
  Serial.println("[MusicPlayer] Play requested");
  Serial.print("[MusicPlayer] Album: ");
  Serial.println(album);
  Serial.print("[MusicPlayer] Song: ");
  Serial.println(title);
  Serial.print("[MusicPlayer] Catalog ID: ");
  Serial.println(catalogId);
  Serial.print("[MusicPlayer] DFPlayer track: ");
  Serial.println(dfplayerTrack);
}

void handlePlay(){
  if (!server.hasArg("id")) {
    sendHttpResponse(400, "text/plain", "Missing ID");
    return;
  }

  const String trackId = server.arg("id");
  uint16_t track = 0;
  bool validTrackId = trackId.length() > 0;
  for (size_t i = 0; i < trackId.length(); ++i) {
    const char digit = trackId[i];
    if (digit < '0' || digit > '9') {
      validTrackId = false;
      break;
    }

    const uint8_t value = static_cast<uint8_t>(digit - '0');
    if (track > (9999 - value) / 10) {
      validTrackId = false;
      break;
    }
    track = static_cast<uint16_t>(track * 10 + value);
  }

  if (!validTrackId || track == 0) {
    sendHttpResponse(400, "text/plain",
                     "Invalid ID: expected a decimal integer from 1 to 9999");
    return;
  }

  String album;
  String title;
  uint32_t catalogId = 0;
  uint32_t mappedTrack = 0;
  if (hasSelectionMetadata()) {
    if (!readSelectionMetadata(album, title, catalogId, mappedTrack, true) ||
        mappedTrack != track) {
      sendHttpResponse(400, "text/plain",
                       "Invalid or incomplete song metadata");
      return;
    }
    logPlaySelection(album, title, catalogId, String(track));
  } else {
    logPlaySelection("metadata unavailable", "metadata unavailable", 0,
                     String(track));
  }

  Serial.print("PLAY TRACK: ");
  Serial.println(track);
  if (!dfPlayerDiagnostic.playMp3Track(track)) {
    sendHttpResponse(
        503, "text/plain",
        "DFPlayer command could not be sent; a diagnostic query may be in progress.");
    return;
  }
  sendHttpResponse(200, "text/plain", "DFPlayer play command sent");
}

void handlePlaySelection(){
  String album;
  String title;
  uint32_t catalogId = 0;
  uint32_t unusedTrack = 0;
  if (!readSelectionMetadata(album, title, catalogId, unusedTrack, false)) {
    sendHttpResponse(400, "text/plain", "Invalid or incomplete song metadata");
    return;
  }

  logPlaySelection(album, title, catalogId, "not mapped");
  sendHttpResponse(200, "text/plain",
                   "Song selection logged; no play command sent");
}

void handlePause(){
  Serial.println("PAUSE");
  // dfplayer.pause();
  sendHttpResponse(200, "text/plain", "Paused");
}

void handleResume(){
  Serial.println("RESUME");
  // dfplayer.start();
  sendHttpResponse(200, "text/plain", "Resumed");
}

void handleNext(){
    Serial.println("NEXT TRACK");
    sendHttpResponse(200, "text/plain", "Next");
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
  server.on("/api/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);
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