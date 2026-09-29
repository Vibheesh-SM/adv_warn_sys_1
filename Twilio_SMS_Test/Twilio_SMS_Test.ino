#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid     = "Jiovib4G";
const char* password = "happyhome";

const char* api_key  = "NT2deLKIMAi4ZFo0prcbVCUmx9fulqBH6DaO8nyJg7QWv3szwSLQj21RBymsAT0goFpnSUe7IVG6Z4t3";
const char* mobile   = "8438426992"; // Your 10-digit number

void sendSMS(String messageText) {
  if (WiFi.status() != WL_CONNECTED) return;

  // Convert spaces to %20 for URL compatibility
  messageText.replace(" ", "%20");

  HTTPClient http;
  
  // Use route=q with encoded message or route=otp to bypass DND
  String url = "https://www.fast2sms.com/dev/bulkV2?authorization=" + String(api_key) +
               "&route=q&message=" + messageText +
               "&language=english&flash=0&numbers=" + String(mobile);

  http.begin(url);
  int httpCode = http.GET();
Filter your search...
Type:

All






Filter your search...
Type:

All







  Serial.print("HTTP Response Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
NEW SKETCH

    String response = http.getString();
    Serial.println("Fast2SMS Response:");
    Serial.println(response);
  } else {
    Serial.println("Error sending HTTP GET request");
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi Connected!");
  
  // Send SMS Test
  sendSMS("ALERT: Landslide Warning System Test SMS!");
}

void loop() {}
