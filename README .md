

# 📖 Description

This project implements a **centralized multi-node emergency alert network** using **ESP32 + LoRa**.

* **Member nodes** periodically transmit status (heartbeat) and SOS signals
* A **Leader node**:

  * Receives messages
  * Sends acknowledgments (ACK)
  * Broadcasts alerts to all nodes

If a direct link fails, the system **automatically switches to mesh relay mode**, ensuring message delivery via a peer node.

---

# 🚀 Features

### 🔴 Emergency Handling

* **3-Level Manual SOS**

  * NEED HELP
  * PERSON INJURED
  * CRITICAL CONDITION

### 📡 Communication Reliability

* **ACK-Confirmed Delivery**
* **Automatic Mesh Relay (fallback)**
* **Collision Avoidance**

  * Staggered heartbeats
  * Random transmission delays

### 📊 Intelligence & Monitoring

* **RSSI-Based Proximity Detection**
* **Signal Loss Detection (>60 sec → LOST alert)**
* **Battery Monitoring (Voltage + Percentage)**

### 🖥️ User Interface

* **OLED Display (scrolling for long messages)**
* **Buzzer Alerts with distinct patterns**

### ⚙️ Scalability

* Easy addition of new nodes via ID assignment

---

# 🧠 System Architecture

```
[Member Node ID1] ─────▶
                         \
                          ▶ [Leader Node] ───▶ Broadcast to all members
                         /
[Member Node ID2] ─────▶

Mesh fallback (if direct fails):
ID1 ─▶ ID2 ─▶ Leader
```

---

# 🔌 Hardware Requirements

## 🔹 Per Node (×3 total)

* ESP32 DevKit
* LoRa Module (SX1278 / Ra-02, 433 MHz)
* OLED Display (SSD1306, 128×64, I2C)
* Active Buzzer
* LED + 220Ω resistor
* 18650 LiPo Battery (3.7V)
* TP4056 Charging Module
* Buck-Boost Converter (5V output)

---

## 🔹 Member Nodes Only

* 3 Push Buttons (SOS types)
* 2 × 100kΩ resistors (battery voltage divider)

---

# 🔗 Pin Connections

## 📡 LoRa → ESP32

| LoRa | ESP32   |
| ---- | ------- |
| SCK  | GPIO 18 |
| MISO | GPIO 19 |
| MOSI | GPIO 23 |
| NSS  | GPIO 5  |
| RST  | GPIO 14 |
| DIO0 | GPIO 26 |

---

## 📺 OLED → ESP32

| OLED | ESP32   |
| ---- | ------- |
| SDA  | GPIO 21 |
| SCL  | GPIO 22 |

---

## 🔔 Buzzer & LED

| Component | Pin     |
| --------- | ------- |
| Buzzer    | GPIO 25 |
| LED       | GPIO 33 |

---

## 🔘 Buttons (Members Only)

| Function  | Pin     |
| --------- | ------- |
| NEED HELP | GPIO 13 |
| INJURED   | GPIO 12 |
| CRITICAL  | GPIO 27 |

---

## 🔋 Battery Monitor

```
Battery (+)
   │
 100kΩ
   │───▶ GPIO 34 (ADC)
 100kΩ
   │
Battery (-)
```

⚠️ Tap voltage from **battery terminals**, not buck converter output.

---

# ⚡ Power Supply Chain

```
Battery → TP4056 → Buck-Boost (5V) → ESP32 VIN
                        │
                        └── Voltage Divider → ADC
```

---

# 💻 Software Setup

### 1. Install Arduino IDE

Download from: [https://www.arduino.cc](https://www.arduino.cc)

---

### 2. Add ESP32 Support

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

---

### 3. Install Libraries

* LoRa (Sandeep Mistry)
* Adafruit SSD1306
* Adafruit GFX

---

### 4. Board Settings

* Board: ESP32 Dev Module
* Upload Speed: 115200
* Flash Size: 4MB

---

# ⬆️ Uploading Code

### Leader Node

* Flash: `leader_node.ino`

---

### Member Node 1

```
#define NODE_ID "ID1"
#define STATUS_INTERVAL 5000
```

---

### Member Node 2

```
#define NODE_ID "ID2"
#define STATUS_INTERVAL 8000
```

⚠️ Different intervals prevent collisions.

---

# ⚙️ Working Principle

## 🟢 Normal Operation

* Members send **STATUS OK**
* Leader:

  * ACKs
  * Updates RSSI
  * Displays status

---

## 🔴 SOS (Direct)

1. Button press
2. Member → Leader
3. Leader → ACK
4. Leader → Broadcast ALERT
5. Members → Display + buzzer

---

## 🔁 Mesh Relay (Fallback)

1. Direct TX fails
2. Member sends RELAY message
3. Peer forwards to Leader
4. Leader ACKs
5. ACK routed back

---

# 🚨 Signal Loss Detection

* No signal for **60 sec → LOST**
* Broadcast alert to all nodes

---

# 📦 Packet Format

| Type      | Format                  |
| --------- | ----------------------- |
| Heartbeat | ID:STATUS OK            |
| SOS       | ID:PAYLOAD              |
| Relay     | RELAY:ID:PAYLOAD        |
| ACK       | ACK:ID                  |
| ALERT     | ALERT:ID:TYPE:NEAR:DIFF |

---

# 🔔 Buzzer Patterns

| Pattern | Meaning        |
| ------- | -------------- |
| 2 beeps | SOS delivered  |
| 3 beeps | Relay success  |
| 5 beeps | Alert received |
| 6 beeps | Node lost      |
| 8 beeps | Failure        |

---

# 🔋 Battery Monitoring

| Voltage | Level |
| ------- | ----- |
| 4.2V    | 100%  |
| 4.0V    | 75%   |
| 3.8V    | 50%   |
| 3.6V    | 25%   |
| 3.0V    | 0%    |

* 16 ADC samples averaged
* +0.08V calibration offset

---

# 🧪 Testing Mesh Relay

Temporary setting:

```
#define DIRECT_RETRIES 0
```

---

# 📈 Scaling

To add node:

```
#define NODE_ID "ID3"
#define STATUS_INTERVAL 11000
```

Update leader:

```
#define NUM_NODES 3
```

---

# ⚠️ Limitations

* No encryption
* No GPS (RSSI only)
* Single-hop mesh
* 433 MHz region restriction

---

# 🔮 Future Work

* AES-128 encryption
* Fall detection sensors
* GPS integration
* Multi-hop mesh
* Mobile dashboard

---

# 📜 License

MIT License

---

# 👨‍💻 Author

Embedded systems project focused on **real-world soldier safety using LoRa communication**

---


