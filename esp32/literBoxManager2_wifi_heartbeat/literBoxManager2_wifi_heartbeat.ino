/*
  literBoxManager2 WiFi heartbeat test

  Purpose:
  - Ignore DHT11, SR60, and MQ135 for now.
  - Prove the ESP32-S3 can connect to WiFi.
  - Prove the local Flask dashboard can receive data from the board.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include "arduino_secrets.h"

const char* DEVICE_ID = "esp32s3_litterbox_2";

const int LED_PIN = 2;
const unsigned long POST_INTERVAL_MS = 3000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

unsigned long lastPostAt = 0;
unsigned long heartbeatCount = 0;

void blink(int count, int delayMs) {
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_PIN, LOW);
    delay(delayMs);
  }
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long startedAt = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - startedAt >= WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println();
      Serial.println("WiFi connection timed out.");
      blink(5, 80);
      return false;
    }
  }

  Serial.println();
  Serial.print("WiFi connected. ESP32-S3 IP: ");
  Serial.println(WiFi.localIP());
  blink(2, 80);
  return true;
}

void postHeartbeat() {
  if (!connectWiFi()) return;

  heartbeatCount++;

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "";
  body += "device_id=" + String(DEVICE_ID);
  body += "&temperature_c=0";
  body += "&humidity_percent=0";
  body += "&motion=0";
  body += "&motion_event_count=" + String(heartbeatCount);
  body += "&mq135_raw=0";
  body += "&uptime_ms=" + String(millis());
  body += "&sensor_status=wifi_heartbeat";

  int code = http.POST(body);
  Serial.printf("Heartbeat #%lu -> HTTP %d\n", heartbeatCount, code);

  if (code >= 200 && code < 300) blink(1, 60);
  else blink(4, 60);

  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("literBoxManager2 WiFi heartbeat starting...");
  Serial.print("Server URL: ");
  Serial.println(SERVER_URL);

  connectWiFi();
}

void loop() {
  unsigned long now = millis();
  if (now - lastPostAt >= POST_INTERVAL_MS) {
    lastPostAt = now;
    postHeartbeat();
  }
}
