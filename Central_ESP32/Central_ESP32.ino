#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

const char *ap_ssid = "Landslide_Central";
const char *ap_password = "12345678";

const char *sta_ssid = "Chidambaram's iPhone";
const char *sta_password = "bakemono";

const char *fast2sms_api_key = "NT2deLKIMAi4ZFo0prcbVCUmx9fulqBH6DaO8nyJg7QWv3s"
                               "zwSLQj21RBymsAT0goFpnSUe7IVG6Z4t3";

WebServer server(80);

const String emergency_numbers = "7010596459";

unsigned long lastSmsTime = 0;
const unsigned long SMS_COOLDOWN = 300000;
int lastSentRiskLevel = 0;
int smsTriggerThreshold = 2;

struct SmsLogEntry {
  String recipient;
  String text;
};
SmsLogEntry smsLogs[5];
int smsLogCount = 0;

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

struct NodeState {
  NodeDataPacket data;
  unsigned long lastSeen;
  bool isOnline;
};

NodeState nodes[4];
const unsigned long NODE_TIMEOUT = 10000;
bool localSirenActive = false;

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void onDataRecv(const esp_now_recv_info_t *recv_info,
                const uint8_t *incomingData, int len);
#else
void onDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
#endif

void checkNodeTimeouts();
int calculateOverallRisk();
void sendSmsAlert(int riskLevel);
void handleRoot();
void handleApiStatus();
void handleToggleSiren();
void handleMockTrigger();
void handleUpdateSim();
void handleGetNode();
void handleRandomize();
void handleSubscribe();
void handleApiSubscribe();
void handleSetSmsThreshold();
void handleSerialCommands();

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void onDataRecv(const esp_now_recv_info_t *recv_info,
                const uint8_t *incomingData, int len) {
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

      Serial.printf("esp-now packet from node %d received\n", packet.nodeId);
    }
  } else {
    Serial.printf("received raw data length: %d\n", len);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("esp32 gateway starting");

  for (int i = 0; i < 4; i++) {
    nodes[i].data.nodeId = i + 1;
    nodes[i].data.potValue = 0;
    nodes[i].data.soilValue = 0;
    nodes[i].data.acceleration = 9.81f;
    nodes[i].data.pitch = 0.0f;
    nodes[i].data.roll = 0.0f;
    nodes[i].data.vibrationValue = 0;
    nodes[i].data.abnormal = 0;
    nodes[i].data.batteryMv = 0;
    nodes[i].lastSeen = 0;
    nodes[i].isOnline = false;
  }

  WiFi.mode(WIFI_AP_STA);
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ap_ssid, ap_password, 1);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  Serial.print("ap started: ");
  Serial.println(ap_ssid);
  Serial.print("dashboard url: http://");
  Serial.println(WiFi.softAPIP());

  if (esp_now_init() != ESP_OK) {
    Serial.println("esp-now init failed");
  } else {
    Serial.println("esp-now ready");
    esp_now_register_recv_cb((esp_now_recv_cb_t)onDataRecv);
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/subscribe", HTTP_GET, handleSubscribe);
  server.on("/api/subscribe", HTTP_POST, handleApiSubscribe);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/siren", HTTP_POST, handleToggleSiren);
  server.on("/api/mock", HTTP_POST, handleMockTrigger);
  server.on("/api/update-sim", HTTP_POST, handleUpdateSim);
  server.on("/api/get-node", HTTP_GET, handleGetNode);
  server.on("/api/randomize", HTTP_POST, handleRandomize);
  server.on("/api/set-sms-threshold", HTTP_POST, handleSetSmsThreshold);
  server.on("/api/set-sms-threshold", HTTP_GET, handleSetSmsThreshold);
  server.begin();
  Serial.println("server ready");
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      int val = 0;
      if (input.startsWith("SMS") || input.startsWith("sms")) {
        int spaceIdx = input.indexOf(' ');
        int eqIdx = input.indexOf('=');
        int startIdx = (spaceIdx != -1) ? spaceIdx + 1 : ((eqIdx != -1) ? eqIdx + 1 : 3);
        val = input.substring(startIdx).toInt();
      } else if (input.length() == 1 && isDigit(input.charAt(0))) {
        val = input.toInt();
      }
      if (val >= 1 && val <= 4) {
        smsTriggerThreshold = val;
        Serial.printf("[CONFIG] SMS Trigger Node Threshold set to %d node(s)\n", smsTriggerThreshold);
      } else {
        Serial.println("[CONFIG] Unknown command. Usage: 'SMS 1', 'SMS 2', 'SMS 3', 'SMS 4' or type 1-4.");
      }
    }
  }
}

void loop() {
  server.handleClient();
  checkNodeTimeouts();
  handleSerialCommands();

  int riskLevel = calculateOverallRisk();
  if (riskLevel >= smsTriggerThreshold && (riskLevel > lastSentRiskLevel ||
                         (millis() - lastSmsTime > SMS_COOLDOWN))) {
    sendSmsAlert(riskLevel);
  }

  delay(10);
}

void checkNodeTimeouts() {
  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    if (nodes[i].lastSeen > 0 && (now - nodes[i].lastSeen > NODE_TIMEOUT)) {
      nodes[i].isOnline = false;
      nodes[i].data.abnormal = 0;
    }
  }
}

int calculateOverallRisk() {
  int abnormalCount = 0;
  for (int i = 0; i < 4; i++) {
    if (nodes[i].isOnline && nodes[i].data.abnormal) {
      abnormalCount++;
    }
  }
  return abnormalCount;
}

void sendSmsAlert(int riskLevel) {
  lastSmsTime = millis();
  lastSentRiskLevel = riskLevel;

  String alertMsg = "";
  if (riskLevel == 1)
    alertMsg = "[LOW WARNING]";
  else if (riskLevel == 2)
    alertMsg = "[WARNING]";
  else if (riskLevel == 3)
    alertMsg = "[HIGH RISK]";
  else if (riskLevel == 4)
    alertMsg = "[CRITICAL ALERT]";
  else
    return;

  alertMsg += " Earthquake/Landslide Warning! ";
  alertMsg += "Active Abnormal Nodes: " + String(riskLevel) + "/4. ";

  for (int i = 0; i < 4; i++) {
    if (nodes[i].isOnline && nodes[i].data.abnormal) {
      alertMsg += "N" + String(i + 1) + " Triggered. ";
    }
  }

  String targetNumbers = emergency_numbers;
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String fbUrl = "https://landslide-central-default-rtdb.asia-southeast1.firebasedatabase.app/subscribers.json";
    http.begin(fbUrl);
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      String fbNumbers = "";
      int searchIdx = 0;
      while ((searchIdx = payload.indexOf("\"", searchIdx)) != -1) {
        int endIdx = payload.indexOf("\"", searchIdx + 1);
        if (endIdx != -1) {
          String key = payload.substring(searchIdx + 1, endIdx);
          if (key.length() == 10) {
            bool isNum = true;
            for (int k = 0; k < 10; k++) {
              if (!isDigit(key.charAt(k))) { isNum = false; break; }
            }
            if (isNum) {
              if (fbNumbers.length() > 0) fbNumbers += ",";
              fbNumbers += key;
            }
          }
          searchIdx = endIdx + 1;
        } else {
          break;
        }
      }
      if (fbNumbers.length() > 0) {
        targetNumbers = fbNumbers;
      }
    }
    http.end();
  }

  if (WiFi.status() == WL_CONNECTED && targetNumbers.length() > 0) {
    HTTPClient http;
    String msgEncoded = alertMsg;
    msgEncoded.replace(" ", "%20");
    msgEncoded.replace("[", "%5B");
    msgEncoded.replace("]", "%5D");

    String url = "https://www.fast2sms.com/dev/bulkV2?authorization=" + String(fast2sms_api_key) +
                 "&route=q&message=" + msgEncoded +
                 "&language=english&flash=0&numbers=" + targetNumbers;

    http.begin(url);
    int httpCode = http.GET();
    http.end();
  }

  SmsLogEntry logEntry;
  logEntry.recipient = targetNumbers;
  logEntry.text = alertMsg;

  if (smsLogCount < 5) {
    smsLogs[smsLogCount++] = logEntry;
  } else {
    for (int i = 0; i < 4; i++)
      smsLogs[i] = smsLogs[i + 1];
    smsLogs[4] = logEntry;
  }
}

void handleSubscribe() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Enter Phone Number</title>
    <style>
        body { font-family: monospace, sans-serif; background: #000; color: #fff; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; padding: 20px; box-sizing: border-box; }
        .card { width: 100%; max-width: 320px; text-align: center; }
        label { display: block; margin-bottom: 12px; font-size: 16px; font-weight: bold; }
        input { width: 100%; padding: 12px; font-size: 16px; border-radius: 6px; border: 1px solid #333; background: #111; color: #fff; box-sizing: border-box; text-align: center; outline: none; margin-bottom: 12px; }
        button { width: 100%; padding: 12px; font-size: 16px; font-weight: bold; background: #fff; color: #000; border: none; border-radius: 6px; cursor: pointer; }
        #msg { margin-top: 15px; font-size: 14px; }
    </style>
</head>
<body>
    <div class="card">
        <form id="subForm">
            <label for="phone">Enter your phone number</label>
            <input type="tel" id="phone" placeholder="10-digit number" pattern="[0-9]{10}" maxlength="10" required>
            <button type="submit">Submit</button>
        </form>
        <div id="msg"></div>
    </div>
    <script>
        document.getElementById('subForm').onsubmit = async (e) => {
            e.preventDefault();
            const phone = document.getElementById('phone').value;
            const res = await fetch('/api/subscribe', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ phone: phone })
            });
            const data = await res.json();
            document.getElementById('msg').innerText = data.message;
            if (res.ok) {
                document.getElementById('phone').value = '';
                document.getElementById('msg').style.color = '#4ade80';
            } else {
                document.getElementById('msg').style.color = '#f87171';
            }
        };
    </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleApiSubscribe() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    int phoneIdx = body.indexOf("\"phone\":");
    if (phoneIdx != -1) {
      int startQuote = body.indexOf("\"", phoneIdx + 8);
      int endQuote = body.indexOf("\"", startQuote + 1);
      if (startQuote != -1 && endQuote != -1) {
        String phone = body.substring(startQuote + 1, endQuote);
        phone.trim();
        if (phone.length() == 10) {
          if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            String url = "https://landslide-central-default-rtdb.asia-southeast1.firebasedatabase.app/subscribers/" + phone + ".json";
            http.begin(url);
            http.addHeader("Content-Type", "application/json");
            http.PUT("{\"timestamp\":1}");
            http.end();
          }
          server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Phone number registered successfully!\"}");
          return;
        }
      }
    }
  }
  server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Please enter a valid 10-digit number.\"}");
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>btop - Central ESP32 Telemetry Command Gateway</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600;700;800&family=Fira+Code:wght@400;600;700&display=swap" rel="stylesheet">
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'JetBrains Mono', 'Fira Code', monospace; }
    body { background-color: #000000; color: #c9d1d9; padding: 12px; font-size: 13px; line-height: 1.45; text-rendering: optimizeLegibility; -webkit-font-smoothing: antialiased; }
    
    .terminal-box { border: 1px solid #21262d; background: #050505; margin-bottom: 12px; border-radius: 4px; overflow: hidden; transition: border-color 0.25s ease, box-shadow 0.25s ease; }
    .terminal-box:hover { border-color: #30363d; box-shadow: 0 2px 8px rgba(0, 0, 0, 0.6); }
    .title-bar { background: #161b22; color: #58a6ff; padding: 5px 12px; font-weight: bold; border-bottom: 1px solid #21262d; display: flex; justify-content: space-between; align-items: center; text-transform: uppercase; font-size: 12px; }
    .content { padding: 10px; }

    header { display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid #21262d; padding-bottom: 8px; margin-bottom: 12px; }
    h1 { font-size: 16px; color: #58a6ff; text-transform: uppercase; letter-spacing: 1px; }

    .main-container { display: flex; flex-direction: column; gap: 12px; }

    .btn { background: #161b22; color: #c9d1d9; border: 1px solid #8b949e; padding: 4px 12px; font-family: monospace; font-size: 12px; font-weight: bold; cursor: pointer; border-radius: 3px; transition: all 0.15s ease; }
    .btn:hover { background: #21262d; color: #ffffff; border-color: #58a6ff; transform: translateY(-1px); }
    .btn:active { transform: translateY(0); }
    .btn-alert { border-color: #f85149; color: #f85149; }
    .btn-alert:hover { background: #f85149; color: #000000; }

    .status-panel { display: flex; justify-content: space-between; align-items: center; padding: 8px 12px; background: #000000; border: 1px solid #21262d; border-radius: 3px; }
    .status-txt { font-weight: bold; font-size: 15px; text-transform: uppercase; transition: color 0.3s ease; }

    .node-grid-container { display: grid; grid-template-columns: repeat(2, 1fr); gap: 12px; }

    .row { display: flex; justify-content: space-between; margin-bottom: 4px; font-size: 12px; }
    .lbl { color: #8b949e; }
    .val { font-weight: bold; color: #58a6ff; transition: color 0.2s ease; }
    
    .meter-container { display: flex; align-items: center; gap: 6px; margin-bottom: 8px; }
    .meter-bar { font-family: monospace; letter-spacing: 1px; flex-grow: 1; background: #000000; border: 1px solid #21262d; padding: 2px 5px; font-size: 11px; white-space: pre; border-radius: 2px; }
    
    .bottom-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
    .control-group { margin-bottom: 10px; }
    .control-group label { display: flex; justify-content: space-between; font-size: 11px; color: #8b949e; margin-bottom: 4px; font-weight: 600; }
    .control-group select { width: 100%; background: #000000; color: #c9d1d9; border: 1px solid #21262d; padding: 5px 8px; font-family: monospace; font-size: 12px; border-radius: 3px; outline: none; }
    
    .sms-log-item { font-size: 11px; background: #000000; border-left: 3px solid #58a6ff; padding: 6px 8px; margin-bottom: 8px; font-family: monospace; color: #7ee787; border-top: 1px solid #21262d; border-right: 1px solid #21262d; border-bottom: 1px solid #21262d; border-radius: 0 3px 3px 0; }
    
    .status-normal { color: #3fb950; }
    .status-warn { color: #d29922; }
    .status-crit { color: #f85149; }

    @keyframes blink {
      0%, 49% { opacity: 1; visibility: visible; }
      50%, 100% { opacity: 0; visibility: hidden; }
    }
    .blink-alert { animation: blink 0.5s steps(1, start) infinite; border-color: #f85149 !important; font-weight: bold; }
  </style>
</head>
<body>

  <header>
    <div>
      <h1>Central ESP32 Telemetry Command Gateway</h1>
      <div style="color: #8b949e; font-size: 10px; margin-top: 1px;">ESP-NOW Radio Telemetry & Multi-Node Hardware Monitor</div>
    </div>
    <div style="display: flex; gap: 8px; align-items: center;">
      <button class="btn btn-alert" onclick="toggleSiren()">[Mute Siren]</button>
    </div>
  </header>

  <div class="main-container">
    <!-- SYSTEM THREAT & DIAGNOSTICS -->
    <div class="terminal-box">
      <div class="title-bar">
        <span>System Threat & Diagnostics</span>
        <div style="display: flex; gap: 10px; align-items: center;">
          <span id="sms-thresh-display" style="color: #8b949e; font-size: 11px; font-weight: normal;">[SMS REQ: >= 2 NODES]</span>
          <span id="risk-raw">[0 / 4 ABNORMAL NODES]</span>
        </div>
      </div>
      <div class="content">
        <div class="status-panel" style="margin-bottom: 6px;">
          <div>
            <span class="lbl">Threat Level: </span>
            <span id="status-text" class="status-txt status-normal">SYSTEM NORMAL</span>
          </div>
          <div>
            <button class="btn" id="audio-toggle-btn" onclick="toggleAudioAlert()">[AUDIO ALARM: ENABLED]</button>
          </div>
        </div>
        <div class="row" style="font-size: 10px; border-top: 1px solid #21262d; padding-top: 5px; margin-bottom: 0;">
          <div><span class="lbl">Poll Rate: </span><span class="val" id="diag-rate">100ms (10Hz)</span></div>
          <div><span class="lbl">Radio Status: </span><span class="val" style="color: #3fb950;">ESP-NOW 100% OK</span></div>
          <div><span class="lbl">Packets: </span><span class="val" id="diag-packets">0 PKTS</span></div>
          <div><span class="lbl">Peak Accel: </span><span class="val" id="diag-peak-acc" style="color: #d29922;">9.81 m/s²</span></div>
        </div>
      </div>
    </div>

    <!-- 3D MOUNTAIN TOPOLOGY & NODE SPATIAL MAP -->
    <div class="terminal-box" id="map-terminal-box">
      <div class="title-bar">
        <span>3D Mountain Topology & Node Spatial Map (Interactive Orbit)</span>
        <div style="display: flex; gap: 4px; align-items: center;">
          <button class="btn" onclick="setMapHeight(180)" style="padding: 1px 4px; font-size: 9px;">[S: 180px]</button>
          <button class="btn" onclick="setMapHeight(280)" style="padding: 1px 4px; font-size: 9px;">[M: 280px]</button>
          <button class="btn" onclick="setMapHeight(420)" style="padding: 1px 4px; font-size: 9px;">[L: 420px]</button>
          <span style="color: #30363d;">|</span>
          <button class="btn" onclick="zoomMap(-0.1)" style="padding: 1px 5px; font-size: 9px;">[Zoom Out -]</button>
          <button class="btn" onclick="zoomMap(0.1)" style="padding: 1px 5px; font-size: 9px;">[Zoom In +]</button>
          <button class="btn" onclick="resetMapView()" style="padding: 1px 5px; font-size: 9px;">[Reset]</button>
          <span id="zoom-val" style="font-size: 10px; color: #8b949e;">[ZOOM: 100%]</span>
        </div>
      </div>
      <div id="map-canvas-container" class="content" style="padding: 4px; resize: vertical; overflow: hidden; min-height: 160px; height: 280px; max-height: 800px; position: relative;">
        <canvas id="topo-canvas" style="background: #000; border: 1px solid #161b22; display: block; width: 100%; height: 100%; cursor: grab; border-radius: 2px;"></canvas>
      </div>
      <div id="map-drag-handle" style="background: #161b22; border-top: 1px solid #21262d; text-align: center; color: #8b949e; font-size: 10px; cursor: ns-resize; padding: 3px 0; font-weight: bold; user-select: none; letter-spacing: 2px;">
        ░░░ DRAG TO RESIZE MAP HEIGHT ░░░
      </div>
    </div>

    <!-- 4-NODE HARDWARE TELEMETRY GRID -->
    <div class="node-grid-container" id="node-grid"></div>

    <!-- BOTTOM ROW: CONFIG & SMS DISPATCH LOG -->
    <div class="bottom-grid">
      <div class="terminal-box">
        <div class="title-bar">Oscilloscope & Poll Config</div>
        <div class="content">
          <div class="control-group">
            <label>Telemetry Poll Frequency:</label>
            <select id="set-poll-rate" onchange="applySettings()">
              <option value="50">50 ms (20.0 Hz - Ultra High Speed)</option>
              <option value="100" selected>100 ms (10.0 Hz - Realtime Oscilloscope)</option>
              <option value="250">250 ms (4.0 Hz - Standard Telemetry)</option>
              <option value="500">500 ms (2.0 Hz - Normal Bandwidth)</option>
            </select>
          </div>
          <div class="control-group">
            <label>Waveform History Window:</label>
            <select id="set-buffer-len" onchange="applySettings()">
              <option value="15">15 Samples (Short Window)</option>
              <option value="30" selected>30 Samples (Standard Window)</option>
              <option value="60">60 Samples (Long Historical Window)</option>
            </select>
          </div>
          <div class="control-group">
            <label>SMS Trigger Node Threshold:</label>
            <select id="set-sms-threshold" onchange="applySmsThreshold()">
              <option value="1">1 Node Triggered (Low Warning / Demo)</option>
              <option value="2" selected>2 Nodes Triggered (Standard Warning)</option>
              <option value="3">3 Nodes Triggered (High Risk)</option>
              <option value="4">4 Nodes Triggered (Critical)</option>
            </select>
          </div>
        </div>
      </div>

      <div class="terminal-box">
        <div class="title-bar">Transmission & SMS Dispatch Log</div>
        <div class="content" id="sms-logs-container" style="max-height: 160px; overflow-y: auto;">
          <p style="color: #484f58; font-size: 10px;">No SMS dispatches recorded.</p>
        </div>
      </div>
    </div>
  </div>

  <script>
    let audioEnabled = true;
    let audioCtx = null;
    let totalPacketsReceived = 0;
    let globalPeakAccel = 9.81;
    let pollIntervalMs = 100;
    let currentBufferLen = 30;
    let pollTimer = null;

    function toggleAudioAlert() {
      audioEnabled = !audioEnabled;
      document.getElementById('audio-toggle-btn').innerText = audioEnabled ? '[AUDIO ALARM: ENABLED]' : '[AUDIO ALARM: MUTED]';
    }

    function playBeep(targetFreq = 880, duration = 0.15) {
      if (!audioEnabled) return;
      try {
        if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        if (audioCtx.state === 'suspended') audioCtx.resume();
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        osc.type = 'sine';
        osc.frequency.setValueAtTime(targetFreq * 0.88, audioCtx.currentTime);
        osc.frequency.exponentialRampToValueAtTime(targetFreq, audioCtx.currentTime + 0.05);
        gain.gain.setValueAtTime(0.08, audioCtx.currentTime);
        gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + duration);
        osc.connect(gain);
        gain.connect(audioCtx.destination);
        osc.start();
        osc.stop(audioCtx.currentTime + duration);
      } catch (e) {}
    }

    function renderAsciiBar(val, max, width = 16, fillChar = '■', emptyChar = '░') {
      if (max <= 0) return `[${emptyChar.repeat(width)}]`;
      const filledLen = Math.round((val / max) * width);
      const filled = fillChar.repeat(Math.min(width, Math.max(0, filledLen)));
      const empty = emptyChar.repeat(Math.max(0, width - filled.length));
      return `[${filled}${empty}]`;
    }

    async function toggleSiren() {
      try { await fetch('/api/siren', { method: 'POST' }); } catch (e) {}
    }

    async function applySmsThreshold() {
      const elem = document.getElementById('set-sms-threshold');
      if (!elem) return;
      const val = parseInt(elem.value);
      try {
        await fetch('/api/set-sms-threshold', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ threshold: val })
        });
      } catch (e) { console.error(e); }
    }

    function applySettings() {
      const pElem = document.getElementById('set-poll-rate');
      const bElem = document.getElementById('set-buffer-len');
      if (pElem) pollIntervalMs = parseInt(pElem.value);
      if (bElem) currentBufferLen = parseInt(bElem.value);
      
      const diagElem = document.getElementById('diag-rate');
      if (diagElem) diagElem.innerText = `${pollIntervalMs}ms (${(1000/pollIntervalMs).toFixed(1)}Hz)`;

      if (pollTimer) clearInterval(pollTimer);
      pollTimer = setInterval(updateDashboard, pollIntervalMs);
    }

    async function updateDashboard() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        latestNodesData = data.nodes;
        
        const onlineCount = data.nodes.filter(n => n.online).length;
        totalPacketsReceived += onlineCount;
        const packetsElem = document.getElementById('diag-packets');
        if (packetsElem) packetsElem.innerText = `${totalPacketsReceived} PKTS`;

        if (data.smsTriggerThreshold !== undefined) {
          const threshElem = document.getElementById('sms-thresh-display');
          if (threshElem) threshElem.innerText = `[SMS REQ: >= ${data.smsTriggerThreshold} NODE${data.smsTriggerThreshold > 1 ? 'S' : ''}]`;
          const selectElem = document.getElementById('set-sms-threshold');
          if (selectElem && document.activeElement !== selectElem) {
            selectElem.value = data.smsTriggerThreshold;
          }
        }

        const statusText = document.getElementById('status-text');
        const riskRaw = document.getElementById('risk-raw');
        if (riskRaw) riskRaw.innerText = `[${data.riskLevel} / 4 ABNORMAL NODES]`;

        const hasVibration = data.nodes.some(n => n.online && n.vibration === 1);

        if (statusText) {
          if (data.riskLevel === 0 && !hasVibration) {
            statusText.className = 'status-txt status-normal'; statusText.innerText = 'SYSTEM NORMAL (0/4 ACTIVE)';
          } else if (hasVibration && data.riskLevel < 2) {
            statusText.className = 'status-txt status-warn blink-alert'; statusText.innerText = 'SEISMIC TREMOR DETECTED (SW-420)';
            playBeep(950, 0.15);
          } else if (data.riskLevel === 1) {
            statusText.className = 'status-txt status-warn'; statusText.innerText = 'LOW THREAT (1 NODE TRIGGERED)';
            playBeep(600, 0.1);
          } else if (data.riskLevel === 2) {
            statusText.className = 'status-txt status-warn'; statusText.innerText = 'WARNING (2 NODES TRIGGERED)';
            playBeep(800, 0.15);
          } else if (data.riskLevel === 3) {
            statusText.className = 'status-txt status-crit'; statusText.innerText = 'HIGH RISK (3 NODES TRIGGERED)';
            playBeep(1000, 0.2);
          } else {
            statusText.className = 'status-txt status-crit'; statusText.innerText = 'CRITICAL LANDSLIDE / EARTHQUAKE EMERGENCY!';
            playBeep(1200, 0.3);
          }
        }

        const grid = document.getElementById('node-grid');
        const sectors = ["Crest Sector Alpha", "Summit Sector Beta", "Ridge Sector Gamma", "Base Sector Delta"];

        data.nodes.forEach(n => {
          const isOnline = n.online;
          const isAbnormal = n.abnormal === 1;

          let statusTag = '';
          let headClass = '';
          if (!isOnline) {
            statusTag = '<span style="color:#8b949e; font-weight:bold;">[OFFLINE]</span>';
            headClass = 'color: #8b949e;';
          } else if (isAbnormal) {
            statusTag = '<span class="status-crit blink-alert">[ABNORMAL ALARM]</span>';
            headClass = 'color: #f85149;';
          } else {
            statusTag = '<span class="status-normal" style="color:#3fb950; font-weight:bold;">[ONLINE]</span>';
            headClass = 'color: #3fb950;';
          }

          if (isOnline && n.acc > globalPeakAccel) {
            globalPeakAccel = n.acc;
            const peakElem = document.getElementById('diag-peak-acc');
            if (peakElem) peakElem.innerText = `${globalPeakAccel.toFixed(2)} m/s²`;
          }

          const potVal = isOnline ? n.pot : 0;
          const soilVal = isOnline ? n.soil : 0;
          const accVal = isOnline ? n.acc : 0;

          const potBar = renderAsciiBar(potVal, 4095, 14);
          const soilBar = renderAsciiBar(soilVal, 4095, 14);
          const potOver = isOnline && potVal >= 2500;
          const soilOver = isOnline && soilVal >= 2500;

          if (!window.nodeHistory) window.nodeHistory = {};
          if (!window.nodeHistory[n.id]) window.nodeHistory[n.id] = { pot: [], soil: [], acc: [], maxPot: 0, maxSoil: 0, maxAcc: 0 };

          const hist = window.nodeHistory[n.id];
          
          if (isOnline) {
            hist.pot.push(n.pot); if (hist.pot.length > currentBufferLen) hist.pot.shift();
            hist.soil.push(n.soil); if (hist.soil.length > currentBufferLen) hist.soil.shift();
            hist.acc.push(n.acc); if (hist.acc.length > currentBufferLen) hist.acc.shift();

            if (n.pot > hist.maxPot) hist.maxPot = n.pot;
            if (n.soil > hist.maxSoil) hist.maxSoil = n.soil;
            if (n.acc > hist.maxAcc) hist.maxAcc = n.acc;
          }

          let card = document.getElementById(`sim-card-${n.id}`);
          if (!card) {
            card = document.createElement('div');
            card.id = `sim-card-${n.id}`;
            grid.appendChild(card);
          }

          card.className = "terminal-box";
          if (isOnline && isAbnormal) card.style.borderColor = "#f85149";
          else if (isOnline) card.style.borderColor = "#30363d";
          else card.style.borderColor = "#21262d";

          const sectorLabel = sectors[n.id - 1] || "Sector";

          card.innerHTML = `
            <div class="title-bar">
              <span style="${headClass}">Node ${n.id} (${sectorLabel})</span>
              ${statusTag}
            </div>
            <div class="content" style="padding: 6px 8px;">
              <div class="row"><span class="lbl">Potentiometer (Crack):</span><span class="val" style="${potOver ? 'color: #f85149;' : ''}">${isOnline ? `${n.pot} / 4095 (Max: ${hist.maxPot})` : '-- / 4095'}</span></div>
              <div class="meter-container">
                <div class="meter-bar ${potOver ? 'blink-alert' : ''}" style="color: ${potOver ? '#f85149' : (isOnline ? '#58a6ff' : '#484f58')};">${potBar}</div>
              </div>

              <div class="row"><span class="lbl">Soil Moisture:</span><span class="val" style="${soilOver ? 'color: #f85149;' : ''}">${isOnline ? `${n.soil} / 4095 (Max: ${hist.maxSoil})` : '-- / 4095'}</span></div>
              <div class="meter-container">
                <div class="meter-bar ${soilOver ? 'blink-alert' : ''}" style="color: ${soilOver ? '#f85149' : (isOnline ? '#38d430' : '#484f58')};">${soilBar}</div>
              </div>

              <div class="row" style="font-size: 11.5px;"><span class="lbl">MPU Acceleration:</span><span class="val">${isOnline ? `${n.acc.toFixed(2)} m/s² (Peak: ${hist.maxAcc.toFixed(1)})` : '-- m/s²'}</span></div>
              <div class="row" style="font-size: 11.5px;"><span class="lbl">Tilt (Pitch/Roll):</span><span class="val">${isOnline ? `${n.pitch.toFixed(1)}° / ${n.roll.toFixed(1)}°` : '--° / --°'}</span></div>
              <div class="row" style="font-size: 11.5px;"><span class="lbl">SW-420 Vibration:</span><span class="val" style="color: ${isOnline ? (n.vibration ? '#f85149' : '#c9d1d9') : '#484f58'};">${isOnline ? (n.vibration ? 'SHOCK DETECTED' : 'STABLE') : 'OFFLINE'}</span></div>
              <div class="row" style="font-size: 11.5px; margin-bottom: 4px;"><span class="lbl">Node Battery Voltage:</span><span class="val" style="color: ${isOnline ? (n.batteryMv < 3500 ? '#f85149' : '#3fb950') : '#484f58'};">${isOnline ? `${n.batteryMv} mV` : '-- mV'}</span></div>

              <div style="border-top: 1px solid #161b22; padding-top: 5px; margin-top: 5px;">
                <div class="row" style="font-size: 11px; margin-bottom: 3px;">
                  <span style="color: #8b949e; font-weight: bold;">Waveform Oscilloscope History</span>
                  <span style="color: ${isOnline ? (isAbnormal ? '#f85149' : '#3fb950') : '#484f58'};">${isOnline ? (isAbnormal ? '[TREMOR ALARM]' : '[LIVE STABLE]') : '[OFFLINE]'}</span>
                </div>
                <canvas id="graph-sim-${n.id}" width="420" height="75" style="background: #000; border: 1px solid #161b22; display: block; width: 100%; height: 75px; border-radius: 2px;"></canvas>
              </div>
            </div>
          `;

          const canvas = document.getElementById(`graph-sim-${n.id}`);
          if (canvas && isOnline) {
            const ctx = canvas.getContext('2d');
            const w = canvas.width, h = canvas.height;
            ctx.clearRect(0, 0, w, h);

            ctx.strokeStyle = '#161b22';
            ctx.lineWidth = 1;
            ctx.beginPath(); ctx.moveTo(0, h/2); ctx.lineTo(w, h/2); ctx.stroke();

            const drawSeries = (arr, maxVal, color) => {
              if (arr.length < 2) return;
              ctx.strokeStyle = color;
              ctx.lineWidth = 1.5;
              ctx.beginPath();
              for (let i = 0; i < arr.length; i++) {
                const x = (i / (arr.length - 1)) * w;
                const y = h - ((arr[i] / maxVal) * (h - 8)) - 4;
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
              }
              ctx.stroke();
            };

            drawSeries(hist.pot, 4095, potOver ? '#f85149' : '#58a6ff');
            drawSeries(hist.soil, 4095, soilOver ? '#f85149' : '#38d430');
            drawSeries(hist.acc, 30, '#f59e0b');
          }
        });

        draw3DSpatialMap(data.nodes);

        const logsContainer = document.getElementById('sms-logs-container');
        if (logsContainer) {
          if (!data.smsLogs || data.smsLogs.length === 0) {
            logsContainer.innerHTML = '<p style="color: #484f58; font-size: 10px;">No SMS dispatches recorded.</p>';
          } else {
            logsContainer.innerHTML = data.smsLogs.map(log => `
              <div class="sms-log-item">
                <strong>[SMS DISPATCH -> ${log.recipient}]</strong><br>
                ${log.text.replace(/\n/g, '<br>')}
              </div>
            `).join('');
          }
        }
      } catch (err) { console.error(err); }
    }

    let mapZoom = 1.0;
    let rotX = 0.45;
    let rotY = -0.32;
    let isPanning = false;
    let startPanX = 0, startPanY = 0;
    let latestNodesData = [];

    function setMapHeight(pxHeight) {
      const container = document.getElementById('map-canvas-container');
      if (container) {
        container.style.height = pxHeight + 'px';
        updateCanvasDimensions();
        draw3DSpatialMap(latestNodesData);
      }
    }

    function updateCanvasDimensions() {
      const cvs = document.getElementById('topo-canvas');
      const container = document.getElementById('map-canvas-container');
      if (cvs && container) {
        const rect = container.getBoundingClientRect();
        if (rect.width > 0 && rect.height > 0) {
          cvs.width = Math.floor(rect.width - 12);
          cvs.height = Math.floor(rect.height - 12);
        }
      }
    }

    function resetMapView() {
      mapZoom = 1.0;
      rotX = 0.45;
      rotY = -0.32;
      const zElem = document.getElementById('zoom-val');
      if (zElem) zElem.innerText = `[ZOOM: 100%]`;
      draw3DSpatialMap(latestNodesData);
    }

    function zoomMap(delta) {
      mapZoom = Math.max(0.4, Math.min(2.5, mapZoom + delta));
      const zElem = document.getElementById('zoom-val');
      if (zElem) zElem.innerText = `[ZOOM: ${Math.round(mapZoom * 100)}%]`;
      draw3DSpatialMap(latestNodesData);
    }

    function initMapControls() {
      const cvs = document.getElementById('topo-canvas');
      const container = document.getElementById('map-canvas-container');
      const dragHandle = document.getElementById('map-drag-handle');
      if (!cvs) return;

      updateCanvasDimensions();

      if (window.ResizeObserver && container) {
        const observer = new ResizeObserver(() => {
          updateCanvasDimensions();
          draw3DSpatialMap(latestNodesData);
        });
        observer.observe(container);
      }

      if (dragHandle && container) {
        let isResizing = false;
        let startY = 0;
        let startH = 0;

        dragHandle.addEventListener('mousedown', (e) => {
          isResizing = true;
          startY = e.clientY;
          startH = container.clientHeight;
          document.body.style.cursor = 'ns-resize';
          e.preventDefault();
        });

        window.addEventListener('mousemove', (e) => {
          if (!isResizing) return;
          const deltaY = e.clientY - startY;
          const newH = Math.max(160, Math.min(800, startH + deltaY));
          container.style.height = newH + 'px';
          updateCanvasDimensions();
          draw3DSpatialMap(latestNodesData);
        });

        window.addEventListener('mouseup', () => {
          if (isResizing) {
            isResizing = false;
            document.body.style.cursor = 'default';
          }
        });
      }

      cvs.addEventListener('wheel', (e) => {
        e.preventDefault();
        zoomMap(e.deltaY < 0 ? 0.08 : -0.08);
      }, { passive: false });

      cvs.addEventListener('mousedown', (e) => {
        isPanning = true;
        startPanX = e.clientX;
        startPanY = e.clientY;
        cvs.style.cursor = 'grabbing';
      });

      window.addEventListener('mousemove', (e) => {
        if (!isPanning) return;
        const dx = e.clientX - startPanX;
        const dy = e.clientY - startPanY;
        rotY += dx * 0.005;
        rotX += dy * 0.005;
        rotX = Math.max(-1.2, Math.min(1.2, rotX));
        startPanX = e.clientX;
        startPanY = e.clientY;
        draw3DSpatialMap(latestNodesData);
      });

      window.addEventListener('mouseup', () => {
        if (isPanning) {
          isPanning = false;
          if (cvs) cvs.style.cursor = 'grab';
        }
      });
    }

    function project3D(x, y, z, cx, cy) {
      let x1 = x * Math.cos(rotY) - z * Math.sin(rotY);
      let z1 = x * Math.sin(rotY) + z * Math.cos(rotY);
      let y2 = y * Math.cos(rotX) - z1 * Math.sin(rotX);
      let z2 = y * Math.sin(rotX) + z1 * Math.cos(rotX);

      const scale = (260 * mapZoom) / (z2 + 3.2);
      return {
        x: cx + x1 * scale,
        y: cy - y2 * scale
      };
    }

    function draw3DSpatialMap(nodesData) {
      const cvs = document.getElementById('topo-canvas');
      if (!cvs) return;
      const ctx = cvs.getContext('2d');
      const w = cvs.width, h = cvs.height;
      if (w === 0 || h === 0) return;

      ctx.clearRect(0, 0, w, h);

      ctx.fillStyle = '#000000';
      ctx.fillRect(0, 0, w, h);

      const cx = w / 2;
      const cy = h / 2 + 10;

      // Base cardboard plate
      const baseCorners = [
        project3D(-1.05, 0, -0.95, cx, cy),
        project3D(1.05, 0, -0.95, cx, cy),
        project3D(1.05, 0, 0.95, cx, cy),
        project3D(-1.05, 0, 0.95, cx, cy)
      ];
      ctx.fillStyle = '#11161d';
      ctx.strokeStyle = '#30363d';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(baseCorners[0].x, baseCorners[0].y);
      for (let i = 1; i < 4; i++) ctx.lineTo(baseCorners[i].x, baseCorners[i].y);
      ctx.closePath();
      ctx.fill();
      ctx.stroke();

      // Base Grid
      ctx.strokeStyle = 'rgba(48, 54, 61, 0.4)';
      ctx.lineWidth = 1;
      for (let g = -0.8; g <= 0.8; g += 0.4) {
        let p1 = project3D(g, 0, -0.95, cx, cy), p2 = project3D(g, 0, 0.95, cx, cy);
        ctx.beginPath(); ctx.moveTo(p1.x, p1.y); ctx.lineTo(p2.x, p2.y); ctx.stroke();
      }

      // Green Mat (Valley Floor under Central ESP)
      const greenCorners = [
        project3D(-0.35, 0.02, -0.75, cx, cy),
        project3D(0.35, 0.02, -0.75, cx, cy),
        project3D(0.35, 0.02, -0.20, cx, cy),
        project3D(-0.35, 0.02, -0.20, cx, cy)
      ];
      ctx.fillStyle = 'rgba(46, 160, 67, 0.25)';
      ctx.strokeStyle = '#2ea043';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(greenCorners[0].x, greenCorners[0].y);
      for (let i = 1; i < 4; i++) ctx.lineTo(greenCorners[i].x, greenCorners[i].y);
      ctx.closePath();
      ctx.fill();
      ctx.stroke();

      // 3D Curved Mountain Surface Mesh
      const resX = 14, resZ = 12;
      const getMountainY = (z) => {
        if (z < -0.15) return 0.05;
        if (z > 0.45) return 0.62;
        const t = (z + 0.15) / 0.60;
        return 0.05 + 0.57 * (0.5 - 0.5 * Math.cos(Math.PI * t));
      };

      for (let i = 0; i < resX; i++) {
        const x1 = -0.9 + (i / resX) * 1.8;
        const x2 = -0.9 + ((i + 1) / resX) * 1.8;
        for (let j = 0; j < resZ; j++) {
          const z1 = -0.9 + (j / resZ) * 1.8;
          const z2 = -0.9 + ((j + 1) / resZ) * 1.8;

          const y11 = getMountainY(z1), y12 = getMountainY(z2);
          const y21 = getMountainY(z1), y22 = getMountainY(z2);

          const p11 = project3D(x1, y11, z1, cx, cy);
          const p12 = project3D(x1, y12, z2, cx, cy);
          const p22 = project3D(x2, y22, z2, cx, cy);
          const p21 = project3D(x2, y21, z1, cx, cy);

          ctx.fillStyle = (j > resZ * 0.65) ? 'rgba(56, 139, 253, 0.06)' : 'rgba(255, 255, 255, 0.03)';
          ctx.strokeStyle = 'rgba(139, 148, 158, 0.2)';
          ctx.lineWidth = 0.8;

          ctx.beginPath();
          ctx.moveTo(p11.x, p11.y);
          ctx.lineTo(p12.x, p12.y);
          ctx.lineTo(p22.x, p22.y);
          ctx.lineTo(p21.x, p21.y);
          ctx.closePath();
          ctx.fill();
          ctx.stroke();
        }
      }

      // Physical Node Towers
      const physicalNodes = [
        { id: 1, label: "Node 1", x: -0.50, z: 0.60, yBase: 0.62, yTop: 0.88 },
        { id: 2, label: "Node 2", x: 0.50, z: 0.60, yBase: 0.62, yTop: 0.88 },
        { id: 3, label: "Node 3", x: -0.65, z: -0.48, yBase: 0.05, yTop: 0.28 },
        { id: 4, label: "Node 4", x: 0.65, z: -0.48, yBase: 0.05, yTop: 0.28 }
      ];
      const gatewayPos = { label: "Central ESP32", x: 0.00, z: -0.48, yBase: 0.06, yTop: 0.29 };

      const gwPTop = project3D(gatewayPos.x, gatewayPos.yTop, gatewayPos.z, cx, cy);

      // ESP-NOW Wireless Radio Beams (Only drawn for ONLINE nodes)
      const dashOffset = (Date.now() / 35) % 8;
      physicalNodes.forEach(pt => {
        const nData = (nodesData || []).find(n => n.id === pt.id) || { abnormal: 0, online: false };
        if (nData.online) {
          const isAbnormal = nData.abnormal === 1;
          const nPTop = project3D(pt.x, pt.yTop, pt.z, cx, cy);

          ctx.strokeStyle = isAbnormal ? 'rgba(248, 81, 73, 0.85)' : 'rgba(88, 166, 255, 0.6)';
          ctx.lineWidth = isAbnormal ? 2 : 1.5;
          ctx.setLineDash([4, 4]);
          ctx.lineDashOffset = -dashOffset;
          ctx.beginPath();
          ctx.moveTo(nPTop.x, nPTop.y);
          ctx.lineTo(gwPTop.x, gwPTop.y);
          ctx.stroke();
          ctx.setLineDash([]);
        }
      });

      // Helper function to draw wooden dual-tier tower
      const drawWoodenTower = (x, z, yBase, yTop, labelText, colorHex, isGw, isOnline, isAbnormal) => {
        const pTop = project3D(x, yTop, z, cx, cy);
        const w = 0.08, d = 0.06;

        const legOffsets = [
          { dx: -w, dz: -d }, { dx: w, dz: -d },
          { dx: w, dz: d }, { dx: -w, dz: d }
        ];

        ctx.strokeStyle = isOnline ? '#8b6b45' : '#30363d';
        ctx.lineWidth = 1.8;
        legOffsets.forEach(off => {
          let legB = project3D(x + off.dx, yBase, z + off.dz, cx, cy);
          let legT = project3D(x + off.dx, yTop, z + off.dz, cx, cy);
          ctx.beginPath(); ctx.moveTo(legB.x, legB.y); ctx.lineTo(legT.x, legT.y); ctx.stroke();
        });

        [yBase + (yTop - yBase) * 0.5, yTop].forEach((yLvl, idx) => {
          let deckCorners = [
            project3D(x - w, yLvl, z - d, cx, cy),
            project3D(x + w, yLvl, z - d, cx, cy),
            project3D(x + w, yLvl, z + d, cx, cy),
            project3D(x - w, yLvl, z + d, cx, cy)
          ];
          ctx.fillStyle = isOnline ? (idx === 1 ? '#d29922' : '#8b6b45') : '#161b22';
          ctx.strokeStyle = isOnline ? '#fae17d' : '#21262d';
          ctx.lineWidth = 1;
          ctx.beginPath();
          ctx.moveTo(deckCorners[0].x, deckCorners[0].y);
          for (let c = 1; c < 4; c++) ctx.lineTo(deckCorners[c].x, deckCorners[c].y);
          ctx.closePath();
          ctx.fill();
          ctx.stroke();
        });

        // Beacon LED Sphere
        ctx.fillStyle = colorHex;
        ctx.beginPath();
        ctx.arc(pTop.x, pTop.y - 6, isGw ? 5 : 4, 0, Math.PI * 2);
        ctx.fill();

        if (isOnline || isGw) {
          ctx.fillStyle = colorHex;
          ctx.globalAlpha = 0.25;
          ctx.beginPath();
          ctx.arc(pTop.x, pTop.y - 6, isGw ? 12 : 10, 0, Math.PI * 2);
          ctx.fill();
          ctx.globalAlpha = 1.0;
        }

        // Label Badge
        ctx.font = 'bold 11px monospace';
        ctx.textAlign = 'center';
        ctx.fillStyle = '#000000';
        const txtW = ctx.measureText(labelText).width + 8;
        ctx.fillRect(pTop.x - txtW/2, pTop.y - 24, txtW, 14);
        ctx.strokeStyle = colorHex;
        ctx.strokeRect(pTop.x - txtW/2, pTop.y - 24, txtW, 14);
        ctx.fillStyle = colorHex;
        ctx.fillText(labelText, pTop.x, pTop.y - 13);
      };

      // Draw Gateway
      drawWoodenTower(gatewayPos.x, gatewayPos.z, gatewayPos.yBase, gatewayPos.yTop, gatewayPos.label, '#3fb950', true, true, false);

      // Draw Nodes
      physicalNodes.forEach(pt => {
        const nData = (nodesData || []).find(n => n.id === pt.id) || { abnormal: 0, vibration: 0, online: false };
        let nodeColor = '#484f58'; // Muted grey if OFFLINE
        if (nData.online) {
          nodeColor = (nData.abnormal === 1) ? '#f85149' : (nData.vibration === 1 ? '#f59e0b' : '#58a6ff');
        }
        const badgeLabel = nData.online ? pt.label : `${pt.label} [OFF]`;
        drawWoodenTower(pt.x, pt.z, pt.yBase, pt.yTop, badgeLabel, nodeColor, false, nData.online, nData.abnormal === 1);

        // Draw Animated 3D Seismic Ground Shockwaves if SW-420 Vibration is active
        if (nData.online && nData.vibration === 1) {
          const groundPT = project3D(pt.x, pt.yBase, pt.z, cx, cy);
          const phase = (Date.now() % 1200) / 1200;
          
          ctx.save();
          ctx.strokeStyle = '#f59e0b';
          ctx.lineWidth = 2;
          ctx.globalAlpha = 1.0 - phase;
          ctx.beginPath();
          ctx.ellipse(groundPT.x, groundPT.y, 14 + phase * 32, 6 + phase * 14, 0, 0, Math.PI * 2);
          ctx.stroke();

          ctx.strokeStyle = '#f85149';
          const phase2 = (phase + 0.5) % 1.0;
          ctx.globalAlpha = 1.0 - phase2;
          ctx.beginPath();
          ctx.ellipse(groundPT.x, groundPT.y, 14 + phase2 * 32, 6 + phase2 * 14, 0, 0, Math.PI * 2);
          ctx.stroke();
          ctx.restore();
        }
      });
    }

    window.addEventListener('DOMContentLoaded', () => {
      initMapControls();
      applySettings();
    });
  </script>
</body>
</html>
  )rawliteral";

  server.send(200, "text/html", html);
}

void handleApiStatus() {
  int riskLevel = calculateOverallRisk();
  String json = "{";
  json += "\"riskLevel\":" + String(riskLevel) + ",";
  json += "\"smsTriggerThreshold\":" + String(smsTriggerThreshold) + ",";
  json +=
      "\"sirenActive\":" + String(localSirenActive ? "true" : "false") + ",";
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
    if (i < 3)
      json += ",";
  }
  json += "],";

  json += "\"smsLogs\":[";
  for (int i = 0; i < smsLogCount; i++) {
    json += "{";
    json += "\"recipient\":\"" + smsLogs[i].recipient + "\",";
    json += "\"text\":\"" + smsLogs[i].text + "\"";
    json += "}";
    if (i < smsLogCount - 1)
      json += ",";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void handleToggleSiren() {
  localSirenActive = !localSirenActive;
  server.send(200, "text/plain", "OK");
}

void handleMockTrigger() {
  for (int i = 0; i < 2; i++) {
    nodes[i].isOnline = true;
    nodes[i].lastSeen = millis();
    nodes[i].data.nodeId = i + 1;
    nodes[i].data.abnormal = 1;
    nodes[i].data.potValue = 3500;
    nodes[i].data.soilValue = 2800;
    nodes[i].data.acceleration = 14.5f;
  }
  lastSmsTime = 0;
  lastSentRiskLevel = 0;
  server.send(200, "text/plain", "MOCK TRIGGERED");
}

void handleGetNode() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    int idx = id - 1;
    if (idx >= 0 && idx < 4) {
      String json = "{";
      json += "\"id\":" + String(nodes[idx].data.nodeId) + ",";
      json +=
          "\"online\":" + String(nodes[idx].isOnline ? "true" : "false") + ",";
      json += "\"soil\":" + String(nodes[idx].data.soilValue) + ",";
      json += "\"pot\":" + String(nodes[idx].data.potValue) + ",";
      json += "\"acc\":" + String(nodes[idx].data.acceleration, 2) + ",";
      json += "\"vibration\":" + String(nodes[idx].data.vibrationValue);
      json += "}";
      server.send(200, "application/json", json);
      return;
    }
  }
  server.send(400, "text/plain", "Invalid ID");
}

void handleUpdateSim() { server.send(200, "text/plain", "OK"); }

void handleRandomize() { server.send(200, "text/plain", "OK"); }

void handleSetSmsThreshold() {
  int newThreshold = -1;
  if (server.hasArg("val")) {
    newThreshold = server.arg("val").toInt();
  } else if (server.hasArg("value")) {
    newThreshold = server.arg("value").toInt();
  } else if (server.hasArg("threshold")) {
    newThreshold = server.arg("threshold").toInt();
  } else if (server.hasArg("plain")) {
    String body = server.arg("plain");
    int idx = body.indexOf("\"threshold\":");
    if (idx != -1) {
      newThreshold = body.substring(idx + 12).toInt();
    } else {
      idx = body.indexOf("\"value\":");
      if (idx != -1) {
        newThreshold = body.substring(idx + 8).toInt();
      } else {
        newThreshold = body.toInt();
      }
    }
  }

  if (newThreshold >= 1 && newThreshold <= 4) {
    smsTriggerThreshold = newThreshold;
    Serial.printf("[HTTP API] SMS Trigger Threshold updated to: %d node(s)\n", smsTriggerThreshold);
    String json = "{\"status\":\"success\",\"smsTriggerThreshold\":" + String(smsTriggerThreshold) + "}";
    server.send(200, "application/json", json);
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Threshold must be between 1 and 4\"}");
  }
}
