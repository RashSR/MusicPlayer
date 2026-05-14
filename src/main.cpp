#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

const char* ssid = "MusicPlayer";
const char* password = "12345678";
ESP8266WebServer server(80);

void setup() {
  Serial.begin(115200);
  delay(500);
  LittleFS.begin();

  WiFi.softAP(ssid, password);

  Serial.println();
  Serial.println("WiFi started");
  Serial.println(WiFi.softAPIP());

  server.on("/", []() {
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
  });

  server.on("/play", []() {
    Serial.println("PLAY pressed");
    server.send(200, "text/plain", "Playing");
  });

  server.on("/pause", []() {
    Serial.println("PAUSE pressed");
    server.send(200, "text/plain", "Paused");
  });

  server.on("/next", []() {
    Serial.println("NEXT pressed");
    server.send(200, "text/plain", "Next");
  });

  server.on("/shuffle", []() {
    Serial.println("SHUFFLE pressed");
    server.send(200, "text/plain", "Shuffle");
  });

  server.serveStatic("/", LittleFS, "/");
  server.begin();
}

void loop() {
  server.handleClient();
}