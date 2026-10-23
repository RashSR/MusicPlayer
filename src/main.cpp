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

void handleLandingPage(){
  Serial.print("Client connected from: ");
  Serial.println(server.client().remoteIP());
  Serial.println("ROOT requested");
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    Serial.println("index.html NOT FOUND in LittleFS");
    server.send(500, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close(); 
}

void handlePlay(){
  if (!server.hasArg("id")) {
      server.send(400, "text/plain", "Missing ID");
      return;
  }

  String trackId = server.arg("id");
  Serial.print("PLAY TRACK: ");
  Serial.println(trackId);
  // DFPlayer.play(trackId.toInt());
  server.send(200, "text/plain", "OK");
}

void handlePause(){
  Serial.println("PAUSE");
  // dfplayer.pause();
  server.send(200, "text/plain", "Paused");
}

void handleResume(){
  Serial.println("RESUME");
  // dfplayer.start();
  server.send(200, "text/plain", "Resumed");
}

void handleNext(){
    Serial.println("NEXT TRACK");
    server.send(200, "text/plain", "Next");
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
  server.send(200, "application/json", response);
}

void setupRoutes(){
  server.on("/", handleLandingPage);
  server.on("/play", handlePlay);
  server.on("/pause", handlePause);
  server.on("/resume", handleResume);
  server.on("/next", handleNext);
  server.on("/api/status", HTTP_GET, handleStatus);
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