# literBoxManager2 中文说明

这是 SniffKitty / 猫砂盆监测器的第二个独立硬件测试项目，专门用于测试当前硬件组合：

- Adafruit Feather ESP32-S3 2MB PSRAM 传感器节点
- AMG8833 热成像温度 / presence 传感器
- DHT11 温湿度传感器
- SR60 小型人体红外 / PIR 感应模块
- MQ135 广谱气味 / VOC 传感器
- 本地网页实时 dashboard

这一版暂时不做部署，也不做疾病判断。目标是先确认：传感器能读数，ESP32-S3 能上传，本地网页能实时更新。

## 默认接线

Arduino 代码默认使用：

```text
DHT11 VCC  -> 3.3V
DHT11 GND  -> GND
DHT11 DATA -> GPIO 5

SR60 VCC   -> 3.3V 或 5V，看你的模块标注
SR60 GND   -> GND
SR60 OUT   -> GPIO 6

MQ135 VCC  -> 第一次安全测试先接 3.3V；如果接 5V，AO 必须做分压
MQ135 GND  -> GND
MQ135 AO   -> A0
MQ135 DO   -> 暂时不接

AMG8833 VIN -> 3.3V
AMG8833 GND -> GND
AMG8833 SDA -> SDA
AMG8833 SCL -> SCL
```

如果你实际接线不同，改 Arduino 代码顶部这几行：

```cpp
const int DHT_PIN = 5;
const int PIR_PIN = 6;
const int MQ135_AO_PIN = A0;
```

MQ135 说明：这一版只用 `AO` 模拟输出，用来观察广谱气味 / VOC 趋势。`DO` 只是阈值 0/1 输出，信息太少，先不接。MQ135 不能单独证明氨气，也不能诊断疾病。

DHT11 如果是裸传感器，DATA 和 3.3V 之间建议加 4.7k-10k 上拉电阻。很多 DHT11 模块板已经自带电阻。

这一版默认用 AMG8833 热区停留来计算 event，而不是 SR60/PIR：

```cpp
const bool USE_PIR_MOTION = false;
```

当它是 `false` 时，AMG8833 连续几秒检测到热区，才会累计一次 event。只有在 SR60/PIR 的 OUT 已经稳定接到 GPIO 6 时，才建议改成 `true`。

## Arduino IDE 需要安装

Library Manager 里安装：

```text
DHT sensor library by Adafruit
Adafruit Unified Sensor
Adafruit AMG88xx Library
Adafruit BusIO
```

Boards Manager 里需要 ESP32 支持。板子可以先选 `ESP32S3 Dev Module`。

## 修改 Wi-Fi 和 server 地址

打开：

```text
esp32/literBoxManager2_sensor_node/literBoxManager2_sensor_node.ino
```

复制 example secrets 文件：

```bash
cp esp32/literBoxManager2_sensor_node/arduino_secrets.example.h \
  esp32/literBoxManager2_sensor_node/arduino_secrets.h
```

然后在 `arduino_secrets.h` 里填 Wi-Fi 和你的 Mac server 地址。这个文件被 Git 忽略，不会把密码推到 GitHub。

注意：ESP32 不能写 `localhost` 或 `127.0.0.1`。这里要写你电脑在同一个 Wi-Fi 下的 IP。

## 启动本地网页

在项目目录运行：

```bash
python3 server/app.py
```

然后浏览器打开：

```text
http://127.0.0.1:5052
```

## 当前上传字段

ESP32 会上传：

```text
device_id
temperature_c
humidity_percent
amg_ambient_c
amg_max_c
amg_hot_pixels
motion
motion_event_count
mq135_raw
uptime_ms
sensor_status
```

网页会显示：

- 当前温度
- 当前湿度
- AMG8833 远红外最高热区和热像素数量
- MQ135 气味 / VOC 原始值
- 是否检测到 presence
- presence event 次数
- 最后更新时间
- 最近数据表格

## 这一版能证明什么

这一版可以证明：

- AMG8833 可以在 DHT11 不稳定时提供温度 fallback。
- AMG8833 可以提供“远红外热区 / presence attempt”信号，例如最高热像素温度和热像素数量。
- 如果 SR60/PIR 的 GPIO motion 不稳定，AMG8833 热区停留可以代替 event 计数。
- DHT11 接线和上拉正确时可以记录湿度。
- SR60 接到配置好的 GPIO 后可以检测靠近 / 活动。
- ESP32-S3 能通过 Wi-Fi 上传数据。
- 本地网页能从真实硬件实时更新。

AMG8833 的数值不是体温，也不能作为医疗测量。

这一版暂时不能证明：

- 氨气检测
- 粪便形状/颜色识别
- 疾病诊断
- 多猫身份识别
