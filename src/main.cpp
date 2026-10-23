#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include "wifi_credentials.h"

WebServer server(80);
bool wasConnectedToWiFi = false;
bool wifiConfigured = false;
bool webServerStarted = false;

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

void setupRoutes(){
  server.on("/", handleLandingPage);
  server.on("/play", handlePlay);
  server.on("/pause", handlePause);
  server.on("/resume", handleResume);
  server.on("/next", handleNext);
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

  Serial.print("Connecting to home Wi-Fi");
  const unsigned long connectionStarted = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - connectionStarted < 30000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Could not connect; check Wi-Fi credentials. Will keep trying.");
  }
}

void loop() {
  if (!wifiConfigured) {
    delay(1000);
    return;
  }

  const bool connectedToWiFi = WiFi.status() == WL_CONNECTED;
  if (connectedToWiFi && !wasConnectedToWiFi) {
    Serial.print("Connected to home Wi-Fi. Open http://");
    Serial.println(WiFi.localIP());
    startWebServer();
  } else if (!connectedToWiFi && wasConnectedToWiFi) {
    Serial.println("Wi-Fi disconnected; waiting to reconnect");
  }
  wasConnectedToWiFi = connectedToWiFi;

  if (webServerStarted) {
    server.handleClient();
  }
}