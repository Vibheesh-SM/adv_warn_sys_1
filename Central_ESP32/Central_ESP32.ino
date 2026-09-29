#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <HTTPClient.h>

// =====================================================
// DUAL COMMUNICATION & NETWORK CONFIGURATION
// =====================================================
const char* ap_ssid = "Landslide_Central";
const char* ap_password = "12345678";

// Wi-Fi Station (Internet for Fast2SMS)
const char* sta_ssid     = "Jiovib4G";
const char* sta_password = "happyhome";

// Fast2SMS Configuration
const char* fast2sms_api_key = "NT2deLKIMAi4ZFo0prcbVCUmx9fulqBH6DaO8nyJg7QWv3szwSLQj21RBymsAT0goFpnSUe7IVG6Z4t3";

// WebServer on Port 80
WebServer server(80);

// =====================================================
// EMERGENCY CONTACTS
// =====================================================
const String emergency_numbers = "7010596459"; // Comma separated for Fast2SMS

// Cooldown to avoid SMS spamming (5 minutes = 300,000 ms)
unsigned long lastSmsTime = 0;
const unsigned long SMS_COOLDOWN = 300000;
int lastSentRiskLevel = 0; // 0=NORMAL, 1=LOW, 2=WARNING, 3=HIGH, 4=CRITICAL

// =====================================================
// DATA PACKET STRUCTURE (SHARED WITH SENSOR NODES)
// =====================================================
struct NodeDataPacket {
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
};

// =====================================================
// NODE TELEMETRY & SYSTEM STATE STORAGE
// =====================================================
struct NodeState {
  NodeDataPacket data;
  unsigned long lastSeen;
  bool isOnline;
};

NodeState nodes[4]; // Nodes 1, 2, 3, 4 (indexed 0..3)

const unsigned long NODE_TIMEOUT = 15000; // 15s timeout
bool localSirenActive = false;

// =====================================================
// FUNCTION PROTOTYPES
// =====================================================
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len);
void checkNodeTimeouts();
int calculateOverallRisk();
void sendSmsAlert(int riskLevel);
void handleRoot();
void handleApiStatus();
void handleToggleSiren();
void handleMockTrigger();

// =====================================================
// ESP-NOW RECEIVE CALLBACK (Compatible with ESP32 Core v2 & v3)
// =====================================================
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
#else
void onDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
#endif
  if (len == sizeof(NodeDataPacket)) {
    NodeDataPacket packet;
    memcpy(&packet, incomingData, sizeof(packet));

    if (packet.nodeId >= 1 && packet.nodeId <= 4) {
      int idx = packet.nodeId - 1;
      nodes[idx].data = packet;
      nodes[idx].lastSeen = millis();
      nodes[idx].isOnline = true;

      Serial.println();
      Serial.printf(">>> ESP-NOW PACKET FROM NODE %d RECEIVED! <<<\n", packet.nodeId);
      Serial.printf("Pot: %d | Vib: %d | Soil: %d | Acc: %.2fm/s^2 | Pitch: %.1f° | Roll: %.1f° | Bat: %dmV | Abnormal: %s\n",
                    packet.potValue, packet.vibrationValue, packet.soilValue, packet.acceleration,
                    packet.pitch, packet.roll, packet.batteryMv, packet.abnormal ? "YES" : "NO");
    }
  } else {
    Serial.printf("Received raw ESP-NOW data of length: %d (Expected: %d)\n", len, sizeof(NodeDataPacket));
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=======================================================");
  Serial.println("  ADVANCED LANDSLIDE & EARTHQUAKE GATEWAY (ESP32)      ");
  Serial.println("  Built-in ESP-NOW + Web Dashboard + GSM SMS Alert     ");
  Serial.println("=======================================================\n");

  // Initialize node states
  for (int i = 0; i < 4; i++) {
    nodes[i].data.nodeId = i + 1;
    nodes[i].data.abnormal = 0;
    nodes[i].data.batteryMv = 4000;
    nodes[i].lastSeen = 0;
    nodes[i].isOnline = false;
  }

  // 1. Initialize Wi-Fi in Pure Access Point Mode (Fixed Channel 1, Guaranteed ESP-NOW & Dashboard)
  WiFi.mode(WIFI_AP);
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ap_ssid, ap_password, 1); // Channel 1
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  Serial.println("-------------------------------------------------------");
  Serial.print("Wi-Fi Access Point Started! SSID: ");
  Serial.println(ap_ssid);
  Serial.print("Web Dashboard Access URL        : http://");
  Serial.println(WiFi.softAPIP());
  Serial.println("-------------------------------------------------------");

  // 2. Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW Initialization Failed!");
  } else {
    Serial.println("ESP-NOW Initialized Successfully!");
    esp_now_register_recv_cb(onDataRecv);
  }

  // 3. Configure WebServer Endpoints
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/siren", HTTP_POST, handleToggleSiren);
  server.on("/api/mock", HTTP_POST, handleMockTrigger);
  server.begin();
  Serial.println("HTTP Web Dashboard Server Started on Port 80!");
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
  // Handle Web Server Requests
  server.handleClient();

  // Check Node Timeouts
  checkNodeTimeouts();

  // Evaluate Risk Matrix & SMS Alert Dispatch
  int riskLevel = calculateOverallRisk();
  if (riskLevel >= 2 && (riskLevel > lastSentRiskLevel || (millis() - lastSmsTime > SMS_COOLDOWN))) {
    sendSmsAlert(riskLevel);
  }

  delay(10);
}

// =====================================================
// NODE TIMEOUT CHECK
// =====================================================
void checkNodeTimeouts() {
  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    if (nodes[i].lastSeen > 0 && (now - nodes[i].lastSeen > NODE_TIMEOUT)) {
      nodes[i].isOnline = false;
      nodes[i].data.abnormal = 0; // Reset abnormal flag if node drops offline
    }
  }
}

// =====================================================
// RISK CALCULATOR
// =====================================================
int calculateOverallRisk() {
  int abnormalCount = 0;
  for (int i = 0; i < 4; i++) {
    if (nodes[i].isOnline && nodes[i].data.abnormal) {
      abnormalCount++;
    }
  }
  return abnormalCount; // 0=NORMAL, 1=LOW, 2=WARNING, 3=HIGH, 4=CRITICAL
}

// =====================================================
// FAST2SMS HTTP SMS DISPATCH SYSTEM
// =====================================================
void sendSmsAlert(int riskLevel) {
  lastSmsTime = millis();
  lastSentRiskLevel = riskLevel;

  String alertMsg = "";
  if (riskLevel == 2) alertMsg = "[WARNING]";
  else if (riskLevel == 3) alertMsg = "[HIGH RISK]";
  else if (riskLevel == 4) alertMsg = "[CRITICAL ALERT]";
  else return;

  alertMsg += " Earthquake/Landslide Warning! ";
  alertMsg += "Active Abnormal Nodes: " + String(riskLevel) + "/4. ";
  
  for (int i = 0; i < 4; i++) {
    if (nodes[i].isOnline && nodes[i].data.abnormal) {
      alertMsg += "N" + String(i + 1) + " Triggered. ";
    }
  }

  Serial.println(">>> DISPATCHING EMERGENCY SMS ALERTS VIA FAST2SMS <<<");
  Serial.println(alertMsg);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERROR: Wi-Fi STA not connected. Cannot send SMS!");
    return;
  }

  // Format message for URL
  alertMsg.replace(" ", "%20");
  alertMsg.replace("\n", "%0A");

  HTTPClient http;
  String url = "https://www.fast2sms.com/dev/bulkV2?authorization=" + String(fast2sms_api_key) +
               "&route=q&message=" + alertMsg +
               "&language=english&flash=0&numbers=" + emergency_numbers;

  http.begin(url);
  int httpCode = http.GET();

  Serial.print("Fast2SMS HTTP Response Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Response Payload:");
    Serial.println(response);
  } else {
    Serial.println("Error sending HTTP GET request to Fast2SMS");
  }

  http.end();
}

// =====================================================
// DEDICATED GLASSMORPHISM WEB DASHBOARD HTML/CSS/JS
// =====================================================
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Landslide & Earthquake Command Center</title>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600;700;800&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg: #0b0f19;
      --card-bg: rgba(22, 31, 49, 0.7);
      --card-border: rgba(255, 255, 255, 0.08);
      --accent: #3b82f6;
      --normal: #10b981;
      --low: #f59e0b;
      --warning: #f97316;
      --high: #ef4444;
      --critical: #dc2626;
      --text: #f3f4f6;
      --text-muted: #9ca3af;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Inter', sans-serif; }
    body { background: var(--bg); color: var(--text); min-height: 100vh; padding: 20px; }
    header { display: flex; justify-content: space-between; align-items: center; padding-bottom: 20px; border-bottom: 1px solid var(--card-border); margin-bottom: 25px; }
    h1 { font-size: 1.6rem; font-weight: 800; background: linear-gradient(90deg, #60a5fa, #a78bfa); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
    .status-banner { padding: 18px 24px; border-radius: 16px; margin-bottom: 30px; display: flex; justify-content: space-between; align-items: center; backdrop-filter: blur(12px); border: 1px solid var(--card-border); transition: all 0.3s ease; }
    .status-title { font-size: 1.4rem; font-weight: 700; text-transform: uppercase; letter-spacing: 1px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 20px; }
    .card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 20px; padding: 20px; backdrop-filter: blur(16px); box-shadow: 0 10px 30px rgba(0,0,0,0.3); }
    .card-header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--card-border); padding-bottom: 12px; margin-bottom: 16px; }
    .node-title { font-weight: 700; font-size: 1.1rem; }
    .badge { padding: 4px 10px; border-radius: 20px; font-size: 0.75rem; font-weight: 700; text-transform: uppercase; }
    .metric { display: flex; justify-content: space-between; margin-bottom: 10px; font-size: 0.9rem; color: var(--text-muted); }
    .metric-val { font-weight: 600; color: var(--text); }
    .bar-bg { background: rgba(255,255,255,0.05); height: 6px; border-radius: 4px; overflow: hidden; margin-bottom: 14px; }
    .bar-fill { height: 100%; width: 0%; background: var(--accent); transition: width 0.4s ease; }
    .siren-btn { background: #ef4444; color: white; border: none; padding: 10px 20px; border-radius: 10px; font-weight: 700; cursor: pointer; transition: 0.2s; }
    .siren-btn:hover { transform: scale(1.04); }
  </style>
</head>
<body>
  <header>
    <div>
      <h1>EARTHQUAKE & LANDSLIDE COMMAND CENTER</h1>
      <p style="color: var(--text-muted); font-size: 0.85rem; margin-top: 4px;">ESP32 Advanced Telemetry & GSM SMS Alert Gateway</p>
    </div>
    <div>
      <button class="siren-btn" style="background: #f59e0b; margin-right: 10px;" onclick="triggerMock()">TEST MOCK SMS</button>
      <button class="siren-btn" onclick="toggleSiren()">TEST ALARM SIREN</button>
    </div>
  </header>

  <div id="banner" class="status-banner" style="background: rgba(16, 185, 129, 0.15); border-color: var(--normal);">
    <div>
      <div style="font-size: 0.8rem; color: var(--text-muted); text-transform: uppercase; letter-spacing: 1px;">Overall System Threat Level</div>
      <div id="status-text" class="status-title" style="color: var(--normal);">SYSTEM NORMAL</div>
    </div>
    <div id="abnormal-count-badge" style="font-size: 2.2rem; font-weight: 800; color: var(--normal);">0/4</div>
  </div>

  <div class="grid" id="node-grid">
    <!-- 4 Node Cards dynamically updated -->
  </div>

  <script>
    async function updateDashboard() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        
        // Update Banner
        const banner = document.getElementById('banner');
        const statusText = document.getElementById('status-text');
        const countBadge = document.getElementById('abnormal-count-badge');
        countBadge.innerText = `${data.riskLevel}/4`;

        if (data.riskLevel === 0) {
          banner.style.background = 'rgba(16, 185, 129, 0.15)'; banner.style.borderColor = 'var(--normal)';
          statusText.style.color = 'var(--normal)'; statusText.innerText = 'SYSTEM NORMAL';
        } else if (data.riskLevel === 1) {
          banner.style.background = 'rgba(245, 158, 11, 0.15)'; banner.style.borderColor = 'var(--low)';
          statusText.style.color = 'var(--low)'; statusText.innerText = 'LOW WARNING (1 Node Triggered)';
        } else if (data.riskLevel === 2) {
          banner.style.background = 'rgba(249, 115, 22, 0.2)'; banner.style.borderColor = 'var(--warning)';
          statusText.style.color = 'var(--warning)'; statusText.innerText = 'WARNING (2 Nodes Triggered)';
        } else if (data.riskLevel === 3) {
          banner.style.background = 'rgba(239, 68, 68, 0.25)'; banner.style.borderColor = 'var(--high)';
          statusText.style.color = 'var(--high)'; statusText.innerText = 'HIGH RISK (3 Nodes Triggered!)';
        } else {
          banner.style.background = 'rgba(220, 38, 38, 0.35)'; banner.style.borderColor = 'var(--critical)';
          statusText.style.color = 'var(--critical)'; statusText.innerText = 'CRITICAL LANDSLIDE WARNING!';
        }

        // Render Node Cards
        const grid = document.getElementById('node-grid');
        grid.innerHTML = '';

        data.nodes.forEach(n => {
          const isAbnormal = n.abnormal === 1;
          const badgeColor = !n.online ? '#6b7280' : (isAbnormal ? '#ef4444' : '#10b981');
          const statusLbl = !n.online ? 'OFFLINE' : (isAbnormal ? 'ABNORMAL' : 'NORMAL');
          
          grid.innerHTML += `
            <div class="card" style="${isAbnormal ? 'border-color: #ef4444;' : ''}">
              <div class="card-header">
                <span class="node-title">NODE ${n.id}</span>
                <span class="badge" style="background: ${badgeColor}22; color: ${badgeColor}; border: 1px solid ${badgeColor};">${statusLbl}</span>
              </div>
              <div class="metric"><span>Slope Crack / Pot</span><span class="metric-val">${n.pot} / 4095</span></div>
              <div class="bar-bg"><div class="bar-fill" style="width: ${(n.pot/4095)*100}%;"></div></div>
              <div class="metric"><span>Soil Moisture</span><span class="metric-val">${n.soil} / 4095</span></div>
              <div class="bar-bg"><div class="bar-fill" style="width: ${(n.soil/4095)*100}%; background: #3b82f6;"></div></div>
              <div class="metric"><span>MPU Acceleration</span><span class="metric-val">${n.acc.toFixed(1)} m/s²</span></div>
              <div class="metric"><span>Tilt Angle (Pitch/Roll)</span><span class="metric-val">${n.pitch.toFixed(1)}° / ${n.roll.toFixed(1)}°</span></div>
              <div class="metric"><span>Vibration Sensor</span><span class="metric-val">${n.vibration ? '⚡ SHOCK DETECTED' : 'NORMAL'}</span></div>
              <div class="metric"><span>Battery Voltage</span><span class="metric-val" style="color: ${n.batteryMv < 3500 ? '#ef4444' : '#10b981'};">${n.batteryMv} mV</span></div>
            </div>
          `;
        });
      } catch (err) { console.error("Error fetching status", err); }
    }

    async function toggleSiren() {
      await fetch('/api/siren', { method: 'POST' });
      alert("Emergency Siren Toggled!");
    }

    async function triggerMock() {
      await fetch('/api/mock', { method: 'POST' });
      alert("Mock Nodes Triggered! Check your phone for SMS.");
    }

    setInterval(updateDashboard, 1000);
    updateDashboard();
  </script>
</body>
</html>
  )rawliteral";
  server.send(200, "text/html", html);
}

// =====================================================
// JSON API STATUS ENDPOINT
// =====================================================
void handleApiStatus() {
  int riskLevel = calculateOverallRisk();
  String json = "{";
  json += "\"riskLevel\":" + String(riskLevel) + ",";
  json += "\"sirenActive\":" + String(localSirenActive ? "true" : "false") + ",";
  json += "\"nodes\":[";
  
  for (int i = 0; i < 4; i++) {
    json += "{";
    json += "\"id\":" + String(nodes[i].data.nodeId) + ",";
    json += "\"online\":" + String(nodes[i].isOnline ? "true" : "false") + ",";
    json += "\"pot\":" + String(nodes[i].data.potValue) + ",";
    json += "\"vibration\":" + String(nodes[i].data.vibrationValue) + ",";
    json += "\"soil\":" + String(nodes[i].data.soilValue) + ",";
    json += "\"acc\":" + String(nodes[i].data.acceleration, 2) + ",";
    json += "\"pitch\":" + String(nodes[i].data.pitch, 2) + ",";
    json += "\"roll\":" + String(nodes[i].data.roll, 2) + ",";
    json += "\"batteryMv\":" + String(nodes[i].data.batteryMv) + ",";
    json += "\"abnormal\":" + String(nodes[i].data.abnormal);
    json += "}";
    if (i < 3) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleToggleSiren() {
  localSirenActive = !localSirenActive;
  server.send(200, "text/plain", "OK");
}

void handleMockTrigger() {
  // Simulate Node 1 and 2 triggering an earthquake
  for (int i = 0; i < 2; i++) {
    nodes[i].isOnline = true;
    nodes[i].lastSeen = millis();
    nodes[i].data.nodeId = i + 1;
    nodes[i].data.abnormal = 1;
    nodes[i].data.potValue = 3500;
    nodes[i].data.soilValue = 2800;
    nodes[i].data.acceleration = 14.5;
    nodes[i].data.pitch = 12.0;
  }
  
  // Force reset cooldown so it sends immediately for testing
  lastSmsTime = 0; 
  lastSentRiskLevel = 0;
  
  server.send(200, "text/plain", "MOCK TRIGGERED");
}
