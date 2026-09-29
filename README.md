# Advanced Landslide & Earthquake Early Warning System

An ESP32-based multi-sensor early warning system designed for monitoring hill slopes and detecting early signs of landslides and earthquakes.

## System Architecture

The system consists of **4 Sensor Nodes** placed across a slope and **1 Central ESP32 Gateway**.

```
[ Node 1 ] ---\
[ Node 2 ] ----+---> (Wi-Fi AP: Landslide_Central) ---> [ Central ESP32 Gateway ]
[ Node 3 ] ----|                                                |
[ Node 4 ] ---/                                      (Serial Monitor / Risk Matrix)
```

### Sensor Node Hardware Setup (Nodes 1–4)
Each node contains an ESP32 connected to:
- **MPU6050 Accelerometer/Gyroscope** (`GPIO21` SDA, `GPIO22` SCL) - Movement acceleration
- **Potentiometer** (`GPIO34`) - Simulates ground displacement / slope movement
- **SW-420 Vibration Sensor** (`GPIO35`) - Detects sudden ground vibrations
- **Soil Moisture Sensor** (`GPIO33`) - Measures soil water saturation
- **LDR Light Sensor** (`GPIO32`) - Ambient light levels
- **LED Indicator** (`GPIO2`) & **Buzzer** (`GPIO4`) - Local warning signals
- **Servo Motor** (`GPIO27`) - Physical indicator flag (0° Normal -> 90° Warning)

### Multi-Sensor Thresholding (Node Level)
A node switches to `ABNORMAL` if **2 or more** sensor thresholds are triggered simultaneously:
- Potentiometer > 2500
- Soil Moisture > 2500
- Vibration == HIGH
- MPU6050 Acceleration > 12.0 m/s²

### Central Gateway Risk Levels
- **0 Abnormal Nodes:** `SYSTEM STATUS: NORMAL`
- **1 Abnormal Node:** `SYSTEM STATUS: LOW WARNING`
- **2 Abnormal Nodes:** `SYSTEM STATUS: WARNING`
- **3 Abnormal Nodes:** `SYSTEM STATUS: HIGH RISK`
- **4 Abnormal Nodes:** `SYSTEM STATUS: CRITICAL`

---

## Directory Structure

- `Node_1/Node_1.ino` - Original firmware for Node 1
- `Node_2/Node_2.ino` - Original firmware for Node 2
- `Node_3/Node_3.ino` - Original firmware for Node 3
- `Node_4/Node_4.ino` - Original firmware for Node 4
- `Central_ESP32/Central_ESP32.ino` - Original firmware for Central ESP32 Access Point
- `COMPLETE OVERVIEW FOR BUILDING HARDWARE.pdf` - PDF document containing hardware wiring diagrams and overview
