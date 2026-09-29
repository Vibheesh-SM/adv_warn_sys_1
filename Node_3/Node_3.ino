#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ESP32Servo.h>
#include <math.h>

Adafruit_MPU6050 mpu;
Servo warningServo;

// =====================================================
// NODE IDENTIFICATION
// =====================================================
const uint8_t NODE_ID = 3;

// ESP-NOW Broadcast Address (transmits to any receiving Gateway)
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// =====================================================
// DATA PACKET STRUCTURE (MATCHES CENTRAL GATEWAY)
// =====================================================
typedef struct __attribute__((packed)) {
  uint8_t nodeId;         // Node ID (1 to 4)
  uint16_t potValue;      // Soil Crack / Ground Displacement (0-4095)
  uint8_t vibrationValue; // SW-420 Shock (0 or 1)
  uint16_t lightValue;    // LDR Light level (0-4095)
  uint16_t soilValue;     // Soil Moisture Saturation (0-4095)
  float acceleration;     // Total acceleration vector (m/s^2)
  float pitch;            // Slope Pitch Angle (deg)
  float roll;             // Slope Roll Angle (deg)
  uint16_t batteryMv;     // Battery Voltage in mV (e.g. 3700-4200)
  uint8_t signalCount;    // Number of triggered anomaly thresholds
  uint8_t abnormal;       // 1 if Abnormal, 0 if Normal
} NodeDataPacket;

// =====================================================
// PIN DEFINITIONS
// =====================================================
const int potPin = 34;       // Ground Crack / Potentiometer
const int vibrationPin = 35; // SW-420 Shock / Vibration Sensor
const int ldrPin = 32;       // LDR Light Sensor
const int soilPin = 33;      // Soil Moisture Sensor
const int batteryPin = 36;   // Battery Voltage ADC Input (VP Pin)

const int ledPin = 2;        // Local LED Indicator
const int buzzerPin = 4;     // Local Alarm Buzzer
const int servoPin = 27;     // Warning Flag Servo Motor

// =====================================================
// THRESHOLDS & SETTINGS
// =====================================================
const int POT_THRESHOLD = 2500;
const int SOIL_THRESHOLD = 2500;
const float ACCELERATION_THRESHOLD = 12.0; // m/s^2
const int REQUIRED_SIGNALS = 2; // Need 2 or more signals to trigger Abnormal

const int SERVO_NORMAL = 0;
const int SERVO_WARNING = 90;

// Enable Deep Sleep mode (set true for ultra-low power battery operation)
const bool ENABLE_DEEP_SLEEP = false; 

// =====================================================
// ESP-NOW SEND CALLBACK
// =====================================================
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=========================================");
  Serial.printf("         SENSOR NODE %d ONLINE          \n", NODE_ID);
  Serial.println("   ESP-NOW + DSP Tilt + Battery Monitor  ");
  Serial.println("=========================================");

  // Pin Configuration
  pinMode(vibrationPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  digitalWrite(buzzerPin, LOW);

  // Servo Setup
  warningServo.setPeriodHertz(50);
  warningServo.attach(servoPin, 500, 2400);
  warningServo.write(SERVO_NORMAL);

  // Wire & MPU6050 Setup
  Wire.begin();
  if (!mpu.begin()) {
    Serial.println("ERROR: MPU6050 Sensor Not Found!");
  } else {
    Serial.println("MPU6050 Sensor Connected Successfully!");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  // Wi-Fi Station Mode for ESP-NOW
  WiFi.mode(WIFI_STA);
  Serial.print("Node MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW Initialization Failed!");
    return;
  }
  esp_now_register_send_cb(onDataSent);

  // Register ESP-NOW Peer
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer!");
  } else {
    Serial.println("ESP-NOW Peer Added Successfully!");
  }
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
  // 1. Read Analog & Digital Sensors
  int potValue = analogRead(potPin);
  int vibrationValue = digitalRead(vibrationPin);
  int lightValue = analogRead(ldrPin);
  int soilValue = analogRead(soilPin);

  // Battery Voltage Calculation (Divider ratio ~ 2:1 on ADC pin)
  int rawBat = analogRead(batteryPin);
  uint16_t batteryMv = map(rawBat, 0, 4095, 0, 3300) * 2; // Approx mV (e.g. 4100 mV)
  if (batteryMv < 2500) batteryMv = 3990; // Fallback default for unpopulated divider

  // 2. Read MPU6050 IMU Data
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Total Acceleration Vector Magnitude (m/s^2)
  float acceleration = sqrt(
    a.acceleration.x * a.acceleration.x +
    a.acceleration.y * a.acceleration.y +
    a.acceleration.z * a.acceleration.z
  );

  // DSP Pitch and Roll Tilt Angle Calculation (Degrees)
  float pitch = atan2(a.acceleration.y, sqrt(a.acceleration.x * a.acceleration.x + a.acceleration.z * a.acceleration.z)) * 180.0 / M_PI;
  float roll  = atan2(-a.acceleration.x, a.acceleration.z) * 180.0 / M_PI;

  // 3. Threshold Evaluation
  bool groundMovement    = (potValue > POT_THRESHOLD);
  bool vibrationDetected = (vibrationValue == HIGH);
  bool wetSoil           = (soilValue > SOIL_THRESHOLD);
  bool mpuMovement       = (acceleration > ACCELERATION_THRESHOLD);

  int signalCount = 0;
  if (groundMovement) signalCount++;
  if (vibrationDetected) signalCount++;
  if (wetSoil) signalCount++;
  if (mpuMovement) signalCount++;

  bool abnormal = (signalCount >= REQUIRED_SIGNALS);

  // 4. Local Actuator Signals
  if (abnormal) {
    digitalWrite(ledPin, HIGH);
    digitalWrite(buzzerPin, HIGH);
    warningServo.write(SERVO_WARNING);
  } else {
    digitalWrite(ledPin, LOW);
    digitalWrite(buzzerPin, LOW);
    warningServo.write(SERVO_NORMAL);
  }

  // 5. Construct & Send ESP-NOW Packet
  NodeDataPacket packet;
  packet.nodeId = NODE_ID;
  packet.potValue = potValue;
  packet.vibrationValue = vibrationValue;
  packet.lightValue = lightValue;
  packet.soilValue = soilValue;
  packet.acceleration = acceleration;
  packet.pitch = pitch;
  packet.roll = roll;
  packet.batteryMv = batteryMv;
  packet.signalCount = signalCount;
  packet.abnormal = abnormal ? 1 : 0;

  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&packet, sizeof(packet));

  // 6. Serial Debug Output
  Serial.println("\n-----------------------------------------");
  Serial.printf("NODE %d TELEMETRY REPORT\n", NODE_ID);
  Serial.printf("Pot: %d | Vib: %d | Soil: %d | Acc: %.2f m/s^2\n", potValue, vibrationValue, soilValue, acceleration);
  Serial.printf("Pitch Tilt: %.1f° | Roll Tilt: %.1f° | Battery: %d mV\n", pitch, roll, batteryMv);
  Serial.printf("Signal Count: %d | Status: %s | ESP-NOW Result: %s\n", 
                signalCount, abnormal ? "ABNORMAL" : "NORMAL", result == ESP_OK ? "SENT OK" : "SEND ERROR");
  Serial.println("-----------------------------------------");

  // 7. Battery Power Management (Deep Sleep optional)
  if (ENABLE_DEEP_SLEEP) {
    // Wake up on SW-420 shock interrupt or after 30 seconds
    esp_sleep_enable_ext0_wakeup((gpio_num_t)vibrationPin, HIGH);
    esp_sleep_enable_timer_wakeup(30 * 1000000ULL);
    Serial.println("Entering Deep Sleep (~15uA)...");
    esp_deep_sleep_start();
  }

  delay(2000);
}

