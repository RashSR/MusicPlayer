#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "MusicPlayer";
const char* password = "12345678";
WebServer server(80);

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

void setup() {
  Serial.begin(115200);
  delay(500);
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    return;
  } 

  WiFi.softAP(ssid, password);

  Serial.println();
  Serial.println("WiFi started");
  Serial.println(WiFi.softAPIP());

  setupRoutes();
  server.serveStatic("/", LittleFS, "/");
  server.begin();
}

void loop() {
  server.handleClient();
}