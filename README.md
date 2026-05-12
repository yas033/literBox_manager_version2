# literBoxManager2

ESP32-S3 hardware test project for the SniffKitty / litter-box monitor prototype.

This project is intentionally separate from the first prototype. It focuses on testing the current connected components:

- Adafruit Feather ESP32-S3 2MB PSRAM sensor node
- AMG8833 thermal array temperature / presence sensor
- DHT11 temperature / humidity sensor
- SR60 small PIR / infrared motion sensor
- MQ135 broad odor / VOC sensor
- Local browser dashboard

The current goal is not deployment. The goal is to verify that the board can read sensors, send data to the computer, and update a local webpage in real time.

## Hardware Wiring

Default pins in the Arduino sketch:

```text
DHT11 VCC  -> 3.3V
DHT11 GND  -> GND
DHT11 DATA -> GPIO 5

SR60 VCC   -> 3.3V or 5V, depending on your module label
SR60 GND   -> GND
SR60 OUT   -> GPIO 6

MQ135 VCC  -> 3.3V for first safe test, or 5V with AO voltage divider
MQ135 GND  -> GND
MQ135 AO   -> A0
MQ135 DO   -> not connected

AMG8833 VIN -> 3.3V
AMG8833 GND -> GND
AMG8833 SDA -> SDA
AMG8833 SCL -> SCL
```

If your wiring uses different pins, change these lines in the sketch:

```cpp
const int DHT_PIN = 5;
const int PIR_PIN = 6;
const int MQ135_AO_PIN = A0;
```

MQ135 note: `AO` is the useful analog trend output. `DO` is only a threshold on/off output and is not used in this prototype. MQ135 is a broad air-quality / VOC trend sensor here, not a gas-specific ammonia sensor.

For DHT11, use a 4.7k-10k pull-up resistor between DATA and 3.3V if your DHT11 module does not already include one.

## Arduino Libraries

Install these in Arduino IDE Library Manager:

```text
DHT sensor library by Adafruit
Adafruit Unified Sensor
Adafruit AMG88xx Library
Adafruit BusIO
```

ESP32 board support must also be installed in Arduino IDE. Select a compatible ESP32-S3 board, such as `ESP32S3 Dev Module`, if your exact board name is not listed.

## Configure Wi-Fi And Server

Open:

```text
esp32/literBoxManager2_sensor_node/literBoxManager2_sensor_node.ino
```

Copy the example secrets file:

```bash
cp esp32/literBoxManager2_sensor_node/arduino_secrets.example.h \
  esp32/literBoxManager2_sensor_node/arduino_secrets.h
```

Then edit `arduino_secrets.h` with your Wi-Fi and Mac server URL. This file is ignored by Git so passwords are not pushed.

Important: ESP32 cannot use `localhost` or `127.0.0.1`. Use your Mac's Wi-Fi IP address.

## Run Local Server

From this folder:

```bash
python3 server/app.py
```

Then open:

```text
http://127.0.0.1:5052
```

## API Fields

ESP32 sends:

```text
device_id
temperature_c
humidity_percent
motion
motion_event_count
mq135_raw
uptime_ms
sensor_status
```

The dashboard shows:

- Current temperature
- Current humidity
- Current MQ135 odor/VOC raw value
- Motion state
- Motion event count
- Last update time
- Recent trend table

## What This Tests

This version proves:

- AMG8833 can provide near-box temperature fallback when DHT11 is unavailable.
- DHT11 can capture humidity when wiring and pull-up are correct.
- SR60 can detect motion / presence events when wired to the configured GPIO.
- ESP32-S3 can upload sensor readings to a local server.
- A local browser dashboard can update from real hardware data.

This version does not detect ammonia, stool shape/color, disease, or cat identity.
