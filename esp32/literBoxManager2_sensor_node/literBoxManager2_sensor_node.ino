/*
  literBoxManager2 ESP32-S3 sensor node

  Based on the provided schematik_esp32.ino idea:
  - keep PIR session tracking
  - keep MQ135 warm-up and rolling session average
  - keep local Serial debug output

  Adapted for our dashboard:
  - posts readings to the Flask dashboard at SERVER_URL
  - uses DHT11 by default
  - uses only MQ135 AO, not DO

  Current safe wiring:
  - DHT11 VCC  -> 3.3V
  - DHT11 GND  -> GND
  - DHT11 DATA -> GPIO 5
  - SR60 VCC   -> 3.3V or 5V, depending on your module
  - SR60 GND   -> GND
  - SR60 OUT   -> GPIO 6
  - MQ135 VCC  -> 3.3V for first safe test, or 5V with AO voltage divider
  - MQ135 GND  -> GND
  - MQ135 AO   -> A0
  - MQ135 DO   -> not connected

  Important:
  - Do not connect MQ135 DO to GPIO0. GPIO0 is a boot-mode pin and can block upload.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_AMG88xx.h>
#include "arduino_secrets.h"

const char* DEVICE_ID = "esp32s3_litterbox_2";

const int DHT_PIN = 5;
const int PIR_PIN = 6;
const int MQ135_AO_PIN = A0;
const int LED_PIN = 2;

const int DHT_TYPE = DHT11;
const unsigned long READ_INTERVAL_MS = 3000;
const unsigned long WIFI_RETRY_DELAY_MS = 500;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long MQ135_WARMUP_MS = 90000UL;
const unsigned long PIR_DEBOUNCE_MS = 2000UL;
const unsigned long MOTION_EVENT_COOLDOWN_MS = 10000UL;
const unsigned long MIN_SESSION_MS = 5000UL;

DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_AMG88xx amg;

bool warmedUp = false;
bool amgReady = false;
bool catPresent = false;
int lastMotion = LOW;

unsigned long motionEventCount = 0;
unsigned long lastMotionEventAt = 0;
unsigned long lastReadAt = 0;
unsigned long pirHighStartAt = 0;
unsigned long sessionStartAt = 0;
unsigned long sessionCount = 0;
unsigned long lastSessionDurationMs = 0;

long mq135Accum = 0;
unsigned long mq135Samples = 0;
int lastSessionMq135Avg = 0;

float tempAtEntry = NAN;
float humidityAtEntry = NAN;
float amgPixels[AMG88xx_PIXEL_ARRAY_SIZE];
float lastAmgAmbientC = NAN;
float lastAmgMaxC = NAN;
int lastAmgHotPixelCount = 0;

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
    delay(WIFI_RETRY_DELAY_MS);
    Serial.print(".");
    if (millis() - startedAt >= WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println();
      Serial.println("WiFi not connected. Sensor reading will continue in Serial-only mode.");
      blink(5, 80);
      return false;
    }
  }

  Serial.println();
  Serial.print("Connected. ESP32-S3 IP: ");
  Serial.println(WiFi.localIP());
  blink(2, 80);
  return true;
}

String urlEncode(const String& value) {
  String encoded = "";
  char buffer[4];

  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += '+';
    } else {
      snprintf(buffer, sizeof(buffer), "%%%02X", (unsigned char)c);
      encoded += buffer;
    }
  }

  return encoded;
}

void postReading(
  String temperatureC,
  String humidityPercent,
  int motion,
  int mq135Raw,
  const char* sensorStatus
) {
  if (!connectWiFi()) {
    Serial.println("Skipping HTTP upload because WiFi is offline.");
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "";
  body += "device_id=" + urlEncode(String(DEVICE_ID));
  body += "&temperature_c=" + temperatureC;
  body += "&humidity_percent=" + humidityPercent;
  body += "&motion=" + String(motion);
  body += "&motion_event_count=" + String(motionEventCount);
  body += "&mq135_raw=" + String(mq135Raw);
  body += "&uptime_ms=" + String(millis());
  body += "&sensor_status=" + urlEncode(String(sensorStatus));

  int code = http.POST(body);

  Serial.printf(
    "POST temp=%s humidity=%s motion=%d events=%lu mq135=%d status=%s -> HTTP %d\n",
    temperatureC.c_str(),
    humidityPercent.c_str(),
    motion,
    motionEventCount,
    mq135Raw,
    sensorStatus,
    code
  );

  if (code >= 200 && code < 300) blink(1, 60);
  else blink(4, 60);

  http.end();
}

bool readAmg8833() {
  if (!amgReady) return false;

  amg.readPixels(amgPixels);

  float sum = 0;
  float maxTemp = -1000;
  int hotPixels = 0;

  for (int i = 0; i < AMG88xx_PIXEL_ARRAY_SIZE; i++) {
    float value = amgPixels[i];
    sum += value;
    if (value > maxTemp) maxTemp = value;
  }

  float ambient = sum / AMG88xx_PIXEL_ARRAY_SIZE;
  for (int i = 0; i < AMG88xx_PIXEL_ARRAY_SIZE; i++) {
    if (amgPixels[i] >= ambient + 3.0) hotPixels++;
  }

  lastAmgAmbientC = ambient;
  lastAmgMaxC = maxTemp;
  lastAmgHotPixelCount = hotPixels;
  return true;
}

void startSession(unsigned long startAt) {
  catPresent = true;
  sessionStartAt = startAt;
  mq135Accum = 0;
  mq135Samples = 0;

  tempAtEntry = dht.readTemperature();
  humidityAtEntry = dht.readHumidity();

  Serial.printf(
    "[ENTRY] T=%.1fC H=%.1f%% motion_events=%lu\n",
    isnan(tempAtEntry) ? 0 : tempAtEntry,
    isnan(humidityAtEntry) ? 0 : humidityAtEntry,
    motionEventCount
  );
}

void endSession(unsigned long now) {
  catPresent = false;
  lastSessionDurationMs = now - sessionStartAt;
  lastSessionMq135Avg = mq135Samples > 0 ? (int)(mq135Accum / (long)mq135Samples) : 0;

  if (lastSessionDurationMs >= MIN_SESSION_MS) {
    sessionCount++;
    Serial.printf(
      "[SESSION] #%lu duration=%lus mq135_avg=%d entry_T=%.1fC entry_H=%.1f%%\n",
      sessionCount,
      lastSessionDurationMs / 1000,
      lastSessionMq135Avg,
      isnan(tempAtEntry) ? 0 : tempAtEntry,
      isnan(humidityAtEntry) ? 0 : humidityAtEntry
    );
  } else {
    Serial.printf("[SESSION_IGNORED] duration=%lums too short\n", lastSessionDurationMs);
  }
}

void updateMotionSession(int motion, unsigned long now) {
  if (motion == HIGH && lastMotion == LOW) {
    if (lastMotionEventAt == 0 || now - lastMotionEventAt >= MOTION_EVENT_COOLDOWN_MS) {
      motionEventCount++;
      lastMotionEventAt = now;
      Serial.printf("Motion event #%lu\n", motionEventCount);
    } else {
      Serial.println("Motion edge ignored during cooldown.");
    }
  }
  lastMotion = motion;

  if (motion == HIGH && !catPresent) {
    if (pirHighStartAt == 0) {
      pirHighStartAt = now;
    } else if (now - pirHighStartAt >= PIR_DEBOUNCE_MS) {
      startSession(pirHighStartAt);
    }
  }

  if (motion == LOW) {
    pirHighStartAt = 0;
    if (catPresent) {
      endSession(now);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(PIR_PIN, INPUT_PULLDOWN);
  pinMode(MQ135_AO_PIN, INPUT);
  Wire.begin();
  dht.begin();
  amgReady = amg.begin();

  Serial.println("literBoxManager2 ESP32-S3 sensor node starting...");
  Serial.printf("DHT11 pin: GPIO %d\n", DHT_PIN);
  Serial.printf("SR60/PIR pin: GPIO %d\n", PIR_PIN);
  Serial.printf("MQ135 AO pin: GPIO %d\n", MQ135_AO_PIN);
  Serial.printf("MQ135 warm-up: %lus\n", MQ135_WARMUP_MS / 1000);
  Serial.printf("AMG8833: %s\n", amgReady ? "ready" : "not found");
  Serial.println("Do not connect MQ135 DO to GPIO0.");

  connectWiFi();
}

void loop() {
  unsigned long now = millis();
  int motion = digitalRead(PIR_PIN);
  updateMotionSession(motion, now);

  if (!warmedUp && now >= MQ135_WARMUP_MS) {
    warmedUp = true;
    Serial.println("MQ135 warm-up complete. Treat readings as trend data, not gas-specific ppm.");
  }

  if (now - lastReadAt < READ_INTERVAL_MS) {
    delay(40);
    return;
  }
  lastReadAt = now;

  float humidity = dht.readHumidity();
  float temperatureC = dht.readTemperature();
  bool amgOk = readAmg8833();
  int mq135Raw = analogRead(MQ135_AO_PIN);

  if (catPresent) {
    mq135Accum += mq135Raw;
    mq135Samples++;
  }

  const char* status = "ok";
  if (!warmedUp) status = "mq135_warming";
  if (isnan(humidity) || isnan(temperatureC)) status = "dht_read_failed";
  if ((isnan(humidity) || isnan(temperatureC)) && amgOk) status = "amg_ok_dht_failed";
  if (catPresent) status = warmedUp ? "session_active" : "session_active_mq135_warming";

  String uploadTemp = isnan(temperatureC) ? "" : String(temperatureC, 1);
  if (uploadTemp == "" && amgOk) uploadTemp = String(lastAmgAmbientC, 1);
  String uploadHumidity = isnan(humidity) ? "" : String(humidity, 1);

  Serial.printf(
    "[DATA] temp=%.1fC humidity=%.1f%% amg_ambient=%.1fC amg_max=%.1fC hot=%d motion=%d in_session=%s events=%lu mq135=%d samples=%lu status=%s\n",
    isnan(temperatureC) ? 0 : temperatureC,
    isnan(humidity) ? 0 : humidity,
    amgOk ? lastAmgAmbientC : 0,
    amgOk ? lastAmgMaxC : 0,
    amgOk ? lastAmgHotPixelCount : 0,
    motion,
    catPresent ? "yes" : "no",
    motionEventCount,
    mq135Raw,
    mq135Samples,
    status
  );

  postReading(
    uploadTemp,
    uploadHumidity,
    motion,
    mq135Raw,
    status
  );
}
