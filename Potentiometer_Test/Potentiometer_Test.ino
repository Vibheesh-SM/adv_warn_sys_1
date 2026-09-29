#include <Arduino.h>

// Virtual Potentiometer Value controlled purely via Serial Monitor
int virtualPotValue = 500;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=================================================");
  Serial.println("  SERIAL-CONTROLLED VIRTUAL POTENTIOMETER TEST   ");
  Serial.println("=================================================");
  Serial.println("Type a number '0' to '4095' in the Serial Monitor");
  Serial.println("to change the virtual potentiometer reading.");
  Serial.println("=================================================\n");
}

void loop() {
  // Read Serial Monitor Input
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    int val = input.toInt();
    if (val >= 0 && val <= 4095) {
      virtualPotValue = val;
      Serial.printf("\n>>> UPDATED VIRTUAL POTENTIOMETER: %d / 4095 <<<\n", virtualPotValue);
    }
  }

  // Convert value to equivalent voltage (0.0V - 3.3V)
  float voltage = (virtualPotValue / 4095.0) * 3.3;

  // Visual Bar Graph representation
  int barLength = map(virtualPotValue, 0, 4095, 0, 30);
  String bar = "[";
  for (int i = 0; i < 30; i++) {
    if (i < barLength) bar += "=";
    else bar += " ";
  }
  bar += "]";

  Serial.printf("Virtual Pot Value: %4d / 4095  |  Voltage: %.2fV  |  %s\n", 
                virtualPotValue, voltage, bar.c_str());

  delay(500); // 500ms update rate
}
