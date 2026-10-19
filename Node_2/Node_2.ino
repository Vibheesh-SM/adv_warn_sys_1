#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

void setup();
void loop();

const uint8_t NODE_ID = 2;

uint8_t broadcastAddress[] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

typedef struct __attribute__((packed)) {
  uint8_t nodeId;
  uint16_t potValue;
  uint8_t vibrationValue;
  uint16_t lightValue;
  uint16_t soilValue;
  float acceleration;
  float pitch;
  float roll;
  uint16_t batteryMv;
  uint8_t signalCount;
  uint8_t abnormal;
} NodeDataPacket;

const int potPin       = 34;
const int vibrationPin = 35;
const int ldrPin       = 25;
const int soilPin      = 33;
const int batteryPin   = 36;

const int ledPin        = 13;
const int onboardLedPin = 2;
const int buzzerPin     = 4;
const int servoPin      = 27;

const int POT_THRESHOLD = 2500;
const int SOIL_THRESHOLD = 2500;
const float ACCELERATION_THRESHOLD = 12.0;

int currentSolarAngle = -1;

bool useManualSerialPot = false;
int manualPotValue = 500;

void checkSerialInput() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.equalsIgnoreCase("auto")) {
      useManualSerialPot = false;
      Serial.println("mode: physical potentiometer knob");
    } else {
      int val = input.toInt();
      if (val >= 0 && val <= 4095) {
        useManualSerialPot = true;
        manualPotValue = val;
        Serial.printf("manual override: set potentiometer to %d / 4095\n", manualPotValue);
      }
    }
  }
}

int readAnalogSmoothed(int pin, int samples = 32) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delayMicroseconds(50);
  }
  return sum / samples;
}

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

// Hardware LEDC PWM Servo Driver (50Hz standard servo control)
const int SERVO_PWM_CHANNEL = 4;
const int SERVO_PWM_FREQ = 50;
const int SERVO_PWM_RES = 16; // 16-bit resolution (0-65535, ~20ms period)

void initServoPWM(int pin) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  ledcAttach(pin, SERVO_PWM_FREQ, SERVO_PWM_RES);
#else
  ledcSetup(SERVO_PWM_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RES);
  ledcAttachPin(pin, SERVO_PWM_CHANNEL);
#endif
}

void moveServoAngle(int pin, int targetAngle) {
  int angle = constrain(targetAngle, 0, 180);
  int pulseUs = map(angle, 0, 180, 500, 2400);
  uint32_t duty = (uint32_t)(((uint64_t)pulseUs * 65535ULL) / 20000ULL);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  ledcWrite(pin, duty);
#else
  ledcWrite(SERVO_PWM_CHANNEL, duty);
#endif
}

void adjustSolarPanelAngle(int lightLevel) {
  int targetAngle = (lightLevel == HIGH) ? 120 : 60;
  targetAngle = constrain(targetAngle, 0, 180);

  if (currentSolarAngle == targetAngle)
    return;

  moveServoAngle(servoPin, targetAngle);
  currentSolarAngle = targetAngle;
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
}

// buzzer control (inverted for 5v direct drive)
void soundAlarmBuzzer(bool enable) {
  // true pulls pin low to sink current
  digitalWrite(buzzerPin, enable ? LOW : HIGH);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.printf("sensor node %d online\n", NODE_ID);

  pinMode(potPin, INPUT);
  pinMode(vibrationPin, INPUT_PULLDOWN);
  pinMode(ldrPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(onboardLedPin, OUTPUT);
  
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, HIGH);

  initServoPWM(servoPin);
  moveServoAngle(servoPin, 90);
  currentSolarAngle = 90;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_send_cb((esp_now_send_cb_t)onDataSent);
    
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    
    esp_now_add_peer(&peerInfo);
  }
}

void loop() {
  checkSerialInput();

  int potValue = useManualSerialPot ? manualPotValue : getFilteredPotValue(potPin);

  int restingState = digitalRead(vibrationPin);
  int vibrationValue = 0;
  for (int i = 0; i < 15; i++) {
    if (digitalRead(vibrationPin) != restingState) {
      vibrationValue = 1;
      break;
    }
    delay(2);
  }

  int lightValue = digitalRead(ldrPin);
  int soilValue = readAnalogSmoothed(soilPin, 16);

  adjustSolarPanelAngle(lightValue);

  bool potTriggered = (potValue >= POT_THRESHOLD);
  
  if (potTriggered) {
    soundAlarmBuzzer(true);
    digitalWrite(onboardLedPin, HIGH);
    digitalWrite(ledPin, HIGH);    
  } else {
    soundAlarmBuzzer(false);
    digitalWrite(onboardLedPin, LOW);
    digitalWrite(ledPin, LOW);
  }

  NodeDataPacket packet;
  packet.nodeId         = NODE_ID;
  packet.potValue       = (uint16_t)potValue;
  packet.vibrationValue = (uint8_t)vibrationValue;
  packet.lightValue     = (uint16_t)(lightValue == HIGH ? 4095 : 100);
  packet.soilValue      = (uint16_t)soilValue;
  packet.acceleration   = 9.81f;
  packet.pitch          = 0.0f;
  packet.roll           = 0.0f;
  packet.batteryMv      = 3700;
  packet.signalCount    = potTriggered ? 1 : 0;
  packet.abnormal       = potTriggered ? 1 : 0;

  esp_now_send(broadcastAddress, (uint8_t *)&packet, sizeof(packet));

  Serial.printf("node %d | pot: %d | ldr: %d | soil: %d | alarm: %s\n", 
                NODE_ID, potValue, lightValue, soilValue, potTriggered ? "active" : "off");

  delay(200);
}
