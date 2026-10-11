#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>

// Forward declarations for Arduino main entry points
void setup();
void loop();

// Disable MPU6050 since hardware is not connected right now
#define USE_MPU6050 false

// Solar Tracker Servo Motor
Servo solarServo;4

// =====================================================
// NODE IDENTIFICATION
// =====================================================
const uint8_t NODE_ID = 1;

// ESP-NOW Broadcast Address (transmits to any receiving Gateway)
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// =====================================================
// DATA PACKET STRUCTURE (MATCHES CENTRAL GATEWAY EXACTLY)
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
// PIN DEFINITIONS (Check your physical wiring against these!)
// =====================================================
const int potPin       = 34; // Physical Potentiometer Signal (Middle pin of Pot)
const int vibrationPin = 35; // SW-420 Shock / Vibration Sensor
const int ldrPin       = 32; // LDR Light Sensor
const int soilPin      = 33; // Soil Moisture Sensor
const int batteryPin   = 36; // Battery Voltage ADC Input

const int ledPin       = 13; // External Alarm LED (Long leg to pin 13, short to GND)
const int onboardLedPin= 2;  // Onboard ESP32 Blue LED
const int buzzerPin    = 4;  // Alarm Buzzer (Positive to pin 4, Negative to GND)
const int servoPin     = 27; // Solar Tracker Servo Motor

// Multi-Pin Buzzer Candidates - In case your board maps pins differently
const int candidateBuzzerPins[] = {4, 13, 14, 25, 26, 27};
const int numBuzzerPins = 6;

// =====================================================
// THRESHOLDS & SETTINGS
// =====================================================
const int POT_THRESHOLD   = 2500; // Trigger alarm when Potentiometer >= 2500
const int SOIL_THRESHOLD  = 2500;
const float ACCELERATION_THRESHOLD = 12.0;

int currentSolarAngle = -1; // Track current solar panel angle

// =====================================================
// HYBRID POTENTIOMETER CONTROL (PHYSICAL + SERIAL DIAL)
// =====================================================
bool useManualSerialPot = false;
int  manualPotValue     = 500;

void checkSerialInput() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.equalsIgnoreCase("auto")) {
      useManualSerialPot = false;
      Serial.println("\n>>> MODE: PHYSICAL POTENTIOMETER KNOB (GPIO 34) <<<\n");
    } else {
      int val = input.toInt();
      if (val >= 0 && val <= 4095) {
        useManualSerialPot = true;
        manualPotValue = val;
        Serial.printf("\n>>> MANUAL SERIAL OVERRIDE: Set Potentiometer to %d / 4095 <<<\n", manualPotValue);
      }
    }
  }
}

// =====================================================
// ADC MULTI-SAMPLE SMOOTHING & NOISE FILTER
// =====================================================
int readAnalogSmoothed(int pin, int samples = 32) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delayMicroseconds(50);
  }
  return sum / samples;
}

// Low-pass Exponential Moving Average filter for physical potentiometer
float filteredPot = -1.0;

int getFilteredPotValue(int pin) {
  int raw = readAnalogSmoothed(pin, 32);
  if (filteredPot < 0) {
    filteredPot = raw;
  } else {
    filteredPot = (0.2f * raw) + (0.8f * filteredPot);
  }
  return (int)filteredPot;
}

// =====================================================
// POWER-SAVING SOLAR TRACKER SERVO CONTROL
// =====================================================
void adjustSolarPanelAngle(int lightLevel) {
  int targetAngle = map(lightLevel, 0, 4095, 10, 170);
  targetAngle = constrain(targetAngle, 0, 180);

  if (abs(currentSolarAngle - targetAngle) < 5) return; 

  solarServo.attach(servoPin, 500, 2400);
  solarServo.write(targetAngle);
  delay(500);
  solarServo.detach(); // Detach PWM to save power

  currentSolarAngle = targetAngle;
}

// =====================================================
// ESP-NOW SEND CALLBACK
// =====================================================
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Silent to avoid cluttering monitor
}

// =====================================================
// MULTI-PIN BUZZER DRIVER
// =====================================================
void soundAlarmBuzzer(bool enable) {
  for (int p = 0; p < numBuzzerPins; p++) {
    digitalWrite(candidateBuzzerPins[p], enable ? HIGH : LOW);
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=================================================");
  Serial.printf("         SENSOR NODE %d ONLINE                  \n", NODE_ID);
  Serial.println("=================================================");
  Serial.println("   POTENTIOMETER DUAL MODE INSTRUCTIONS:        ");
  Serial.println("   - Physical Knob on GPIO 34 works by default  ");
  Serial.println("   - Type '0'-'4095' in Serial Monitor to override ");
  Serial.println("   - Type 'auto' to return to physical Knob     ");
  Serial.println("   - Values >= 2500 TRIGGER ALARM               ");
  Serial.println("=================================================");

  // Pin Configuration
  pinMode(potPin, INPUT);
  pinMode(vibrationPin, INPUT_PULLDOWN);
  pinMode(ledPin, OUTPUT);
  pinMode(onboardLedPin, OUTPUT);
  
  for (int p = 0; p < numBuzzerPins; p++) {
    pinMode(candidateBuzzerPins[p], OUTPUT);
    digitalWrite(candidateBuzzerPins[p], LOW);
  }

  // Initialize Servo
  solarServo.setPeriodHertz(50);
  solarServo.attach(servoPin, 500, 2400);
  solarServo.write(90);
  delay(500);
  solarServo.detach();
  currentSolarAngle = 90;

  // Wi-Fi Station Mode for ESP-NOW (Lock to Wi-Fi Channel 1 to match Central Gateway AP)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  // Initialize ESP-NOW
  if (esp_now_init() == ESP_OK) {
    esp_now_register_send_cb((esp_now_send_cb_t)onDataSent);
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1; // Explicitly set channel 1
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  }
}

// =====================================================
// MAIN LOOP
// Alarm latch timer (keeps alarm active for at least 3 seconds when vibration is triggered)
unsigned long lastVibrationTime = 0;
const unsigned long ALARM_HOLD_TIME = 3000; // Hold alarm for 3 seconds after shock

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
  // Check for incoming Serial commands from Serial Monitor
  checkSerialInput();

  // 1. Read Potentiometer (Physical GPIO 34 OR Serial Override)
  int potValue = useManualSerialPot ? manualPotValue : getFilteredPotValue(potPin);

  // SW-420 Vibration Sensing: Detect pulse transitions relative to initial state
  // SW-420 modules come in 2 varieties: Active HIGH (normally LOW) or Active LOW (normally HIGH).
  // Also, onboard potentiometer on SW-420 module sets sensitivity threshold.
  int restingState = digitalRead(vibrationPin);
  int vibrationValue = 0;

  // Sample over 30ms for any pulse deviation from resting state
  for (int i = 0; i < 15; i++) {
    if (digitalRead(vibrationPin) != restingState) {
      vibrationValue = 1; // Pulse/Shock detected!
      break;
    }
    delay(2);
  }

  int lightValue = readAnalogSmoothed(ldrPin, 16);
  int soilValue  = readAnalogSmoothed(soilPin, 16);

  // 2. Solar Panel Sunlight Tracking
  adjustSolarPanelAngle(lightValue);

  // 3. Threshold Evaluation
  bool potTriggered = (potValue >= POT_THRESHOLD);

  // Alarm is ACTIVE INSTANTLY whenever Potentiometer reaches 2500
  bool abnormal = potTriggered;

  // 4. Local Alarm Actuators (LED & Buzzer)
  if (abnormal) {
    digitalWrite(ledPin, HIGH);        // Turn ON External LED (GPIO 13)
    digitalWrite(onboardLedPin, HIGH); // Turn ON Onboard Blue LED (GPIO 2)
    soundAlarmBuzzer(true);            // Sound Alarm Buzzer (GPIO 4)
  } else {
    digitalWrite(ledPin, LOW);         // Turn OFF External LED
    digitalWrite(onboardLedPin, LOW);  // Turn OFF Onboard Blue LED
    soundAlarmBuzzer(false);           // Turn OFF Buzzer
  }

  // 5. Build and Send ESP-NOW Packet
  NodeDataPacket packet;
  packet.nodeId         = NODE_ID;
  packet.potValue       = potValue;
  packet.vibrationValue = vibrationValue;
  packet.lightValue     = lightValue;
  packet.soilValue      = soilValue;
  packet.acceleration   = 9.81;
  packet.pitch          = 0.0;
  packet.roll           = 0.0;
  packet.batteryMv      = 4000;
  packet.signalCount    = (potTriggered ? 1 : 0) + (vibrationValue == 1 ? 1 : 0);
  packet.abnormal       = abnormal ? 1 : 0;

  esp_now_send(broadcastAddress, (uint8_t *)&packet, sizeof(packet));

  // 6. Complete Serial Telemetry & Alarm Status Report
  Serial.println("\n------------------------------------------------");
  Serial.printf(" [NODE %d TELEMETRY REPORT]\n", NODE_ID);
  Serial.printf(" Potentiometer (GPIO 34 / %s) : %4d / 4095  [%s]\n", 
                useManualSerialPot ? "SERIAL OVERRIDE" : "PHYSICAL KNOB", 
                potValue, potTriggered ? "ALARM TRIGGERED" : "NORMAL");
  Serial.printf(" SW-420 Vibration (GPIO 35)      : %d          [%s]\n", vibrationValue, (vibrationValue == 1) ? "SHOCK / VIBRATION DETECTED!" : "STABLE");
  Serial.printf(" LDR Light Sensor (GPIO 32)      : %4d / 4095  (Solar Angle: %d°)\n", lightValue, currentSolarAngle);
  Serial.printf(" Soil Moisture (GPIO 33)        : %4d / 4095\n", soilValue);
  Serial.printf(" SYSTEM ALARM STATUS             : [%s]\n", abnormal ? ">>> ALARM ACTIVE (LED & BUZZER ON) <<<" : "NORMAL (ALL OFF)");
  Serial.println("------------------------------------------------");

  delay(250); // 250ms update rate for faster shock response
}
