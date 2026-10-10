import http.server
import socketserver
import json
import random
import threading
import time
import socket
import urllib.parse

PORT = 8080

def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


# Initial mock state for nodes 1-4
nodes = [
    {
        "id": 1,
        "online": True,
        "pot": 1200,
        "vibration": 0,
        "soil": 1100,
        "acc": 9.8,
        "pitch": 0.0,
        "roll": 0.0,
        "batteryMv": 4100,
        "abnormal": 0
    },
    {
        "id": 2,
        "online": True,
        "pot": 900,
        "vibration": 0,
        "soil": 1300,
        "acc": 9.8,
        "pitch": 0.0,
        "roll": 0.0,
        "batteryMv": 4050,
        "abnormal": 0
    },
    {
        "id": 3,
        "online": True,
        "pot": 1500,
        "vibration": 0,
        "soil": 1050,
        "acc": 9.8,
        "pitch": 0.0,
        "roll": 0.0,
        "batteryMv": 4080,
        "abnormal": 0
    },
    {
        "id": 4,
        "online": True,
        "pot": 1000,
        "vibration": 0,
        "soil": 1200,
        "acc": 9.8,
        "pitch": 0.0,
        "roll": 0.0,
        "batteryMv": 4120,
        "abnormal": 0
    }
]

sms_logs = []
siren_active = False

def calculate_risk():
    abnormal_count = 0
    for node in nodes:
        if node["online"] and node["abnormal"]:
            abnormal_count += 1
    return abnormal_count

class SimulatorHandler(http.server.SimpleHTTPRequestHandler):
    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        global nodes, sms_logs
        
        # API Status endpoint
        if self.path == '/api/status':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            
            response = {
                "riskLevel": calculate_risk(),
                "sirenActive": siren_active,
                "nodes": nodes,
                "smsLogs": sms_logs[-5:] # Return last 5 SMS logs
            }
            self.wfile.write(json.dumps(response).encode())
            return
            
        # Serve the Dashboard HTML
        elif self.path == '/' or self.path == '/index.html':
            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            
            html = R"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>btop - Landslide Telemetry Command Gateway</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600;700;800&family=Fira+Code:wght@400;600;700&display=swap" rel="stylesheet">
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'JetBrains Mono', 'Fira Code', 'Cascadia Code', 'Consolas', monospace; }
    body { background-color: #000000; color: #c9d1d9; padding: 12px; font-size: 13px; line-height: 1.45; text-rendering: optimizeLegibility; -webkit-font-smoothing: antialiased; }
    
    .terminal-box { border: 1px solid #21262d; background: #050505; margin-bottom: 12px; border-radius: 4px; overflow: hidden; transition: border-color 0.25s ease, box-shadow 0.25s ease; }
    .terminal-box:hover { border-color: #30363d; box-shadow: 0 2px 8px rgba(0, 0, 0, 0.6); }
    .title-bar { background: #161b22; color: #58a6ff; padding: 5px 12px; font-weight: bold; border-bottom: 1px solid #21262d; display: flex; justify-content: space-between; align-items: center; text-transform: uppercase; font-size: 12px; }
    .content { padding: 10px; }

    header { display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid #21262d; padding-bottom: 8px; margin-bottom: 12px; }
    h1 { font-size: 16px; color: #58a6ff; text-transform: uppercase; letter-spacing: 1px; }

    .main-container { display: grid; grid-template-columns: 1fr 370px; gap: 12px; }

    .btn { background: #161b22; color: #c9d1d9; border: 1px solid #8b949e; padding: 4px 12px; font-family: monospace; font-size: 12px; font-weight: bold; cursor: pointer; border-radius: 3px; transition: all 0.15s ease; }
    .btn:hover { background: #21262d; color: #ffffff; border-color: #58a6ff; transform: translateY(-1px); }
    .btn:active { transform: translateY(0); }
    .btn-alert { border-color: #f85149; color: #f85149; }
    .btn-alert:hover { background: #f85149; color: #000000; border-color: #f85149; }
    .btn-warn { border-color: #d29922; color: #d29922; }
    .btn-warn:hover { background: #d29922; color: #000000; border-color: #d29922; }
    .btn-success { border-color: #3fb950; color: #3fb950; }
    .btn-success:hover { background: #3fb950; color: #000000; }

    .status-panel { display: flex; justify-content: space-between; align-items: center; padding: 8px 12px; background: #000000; border: 1px solid #21262d; border-radius: 3px; }
    .status-txt { font-weight: bold; font-size: 15px; text-transform: uppercase; transition: color 0.3s ease; }

    .node-grid-container { display: grid; grid-template-columns: repeat(2, 1fr); gap: 12px; margin-top: 12px; }

    .row { display: flex; justify-content: space-between; margin-bottom: 4px; font-size: 12px; }
    .lbl { color: #8b949e; }
    .val { font-weight: bold; color: #58a6ff; transition: color 0.2s ease; }
    
    .meter-container { display: flex; align-items: center; gap: 6px; margin-bottom: 8px; }
    .meter-bar { font-family: monospace; letter-spacing: 1px; flex-grow: 1; background: #000000; border: 1px solid #21262d; padding: 2px 5px; font-size: 11px; white-space: pre; border-radius: 2px; }
    
    .sidebar { display: flex; flex-direction: column; gap: 12px; }
    .control-group { margin-bottom: 10px; }
    .control-group label { display: flex; justify-content: space-between; font-size: 11px; color: #8b949e; margin-bottom: 4px; font-weight: 600; }
    .control-group select { width: 100%; background: #000000; color: #c9d1d9; border: 1px solid #21262d; padding: 5px 8px; font-family: monospace; font-size: 12px; border-radius: 3px; outline: none; }
    .control-group select:focus { border-color: #58a6ff; }
    
    .control-group input[type="range"] { -webkit-appearance: none; appearance: none; width: 100%; height: 6px; background: #161b22; border: 1px solid #21262d; border-radius: 3px; outline: none; }
    .control-group input[type="range"]::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 14px; height: 14px; border-radius: 50%; background: #58a6ff; cursor: pointer; }
    .control-group input[type="range"]::-webkit-slider-thumb:hover { background: #79c0ff; transform: scale(1.15); }

    .preset-btn-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; margin-bottom: 10px; }

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
      <h1>Landslide Telemetry & Seismic Command Gateway</h1>
      <div style="color: #8b949e; font-size: 11px; margin-top: 2px;">ESP-NOW Radio Telemetry & Multi-Sensor Fusion Engine</div>
    </div>
    <div style="display: flex; gap: 8px; align-items: center;">
      <button class="btn btn-alert" onclick="toggleSiren()">[Mute Siren]</button>
    </div>
  </header>

  <div class="main-container">
    <div>
      <!-- SYSTEM THREAT & DIAGNOSTICS -->
      <div class="terminal-box">
        <div class="title-bar">
          <span>System Threat & Diagnostics</span>
          <span id="risk-raw">[0 / 4 ABNORMAL NODES]</span>
        </div>
        <div class="content">
          <div class="status-panel" style="margin-bottom: 8px;">
            <div>
              <span class="lbl" style="font-size: 12px;">Threat Level: </span>
              <span id="status-text" class="status-txt status-normal">SYSTEM NORMAL</span>
            </div>
            <div>
              <button class="btn" id="audio-toggle-btn" onclick="toggleAudioAlert()">[AUDIO ALARM: ENABLED]</button>
            </div>
          </div>
          <div class="row" style="font-size: 11px; border-top: 1px solid #21262d; padding-top: 6px; margin-bottom: 0;">
            <div><span class="lbl">Poll Rate: </span><span class="val" id="diag-rate">100ms (10Hz)</span></div>
            <div><span class="lbl">Radio Status: </span><span class="val" style="color: #3fb950;">ESP-NOW 100% OK</span></div>
            <div><span class="lbl">Packets: </span><span class="val" id="diag-packets">0 PKTS</span></div>
            <div><span class="lbl">Peak Accel: </span><span class="val" id="diag-peak-acc" style="color: #d29922;">9.80 m/s²</span></div>
          </div>
        </div>
      </div>

      <!-- 3D MOUNTAIN TOPOLOGY & NODE SPATIAL MAP -->
      <div class="terminal-box" id="map-terminal-box">
        <div class="title-bar">
          <span>3D Mountain Topology & Node Spatial Map (Interactive Orbit)</span>
          <div style="display: flex; gap: 5px; align-items: center;">
            <button class="btn" onclick="setMapHeight(180)" style="padding: 2px 5px; font-size: 10px;">[S: 180px]</button>
            <button class="btn" onclick="setMapHeight(280)" style="padding: 2px 5px; font-size: 10px;">[M: 280px]</button>
            <button class="btn" onclick="setMapHeight(420)" style="padding: 2px 5px; font-size: 10px;">[L: 420px]</button>
            <span style="color: #30363d;">|</span>
            <button class="btn" onclick="zoomMap(-0.1)" style="padding: 2px 6px; font-size: 10px;">[Zoom Out -]</button>
            <button class="btn" onclick="zoomMap(0.1)" style="padding: 2px 6px; font-size: 10px;">[Zoom In +]</button>
            <button class="btn" onclick="resetMapView()" style="padding: 2px 6px; font-size: 10px;">[Reset]</button>
            <span id="zoom-val" style="font-size: 11px; color: #8b949e;">[ZOOM: 100%]</span>
          </div>
        </div>
        <div id="map-canvas-container" class="content" style="padding: 6px; resize: vertical; overflow: hidden; min-height: 160px; height: 280px; max-height: 800px; position: relative;">
          <canvas id="topo-canvas" style="background: #000; border: 1px solid #161b22; display: block; width: 100%; height: 100%; cursor: grab; border-radius: 2px;"></canvas>
        </div>
        <div id="map-drag-handle" style="background: #161b22; border-top: 1px solid #21262d; text-align: center; color: #8b949e; font-size: 10px; cursor: ns-resize; padding: 3px 0; font-weight: bold; user-select: none; letter-spacing: 2px;">
          ░░░ DRAG TO RESIZE MAP HEIGHT ░░░
        </div>
      </div>

      <!-- 4-NODE CONSOLE GRID (TELEMETRY + INTEGRATED WAVEFORM GRAPH CARDS) -->
      <div class="node-grid-container" id="node-grid"></div>
    </div>

    <!-- SIDEBAR CONTROL DECK -->
    <div class="sidebar">
      <!-- SCENARIO PRESETS & CONTROL INJECTION -->
      <div class="terminal-box">
        <div class="title-bar">
          <span>Simulation Controls</span>
        </div>
        <div class="content">
          <div style="font-size: 11px; color: #8b949e; margin-bottom: 6px; font-weight: bold;">Quick Scenario Presets:</div>
          <div class="preset-btn-grid">
            <button class="btn btn-success" onclick="applyPreset('normal')">[Normal All]</button>
            <button class="btn btn-warn" onclick="applyPreset('monsoon')">[Monsoon Rain]</button>
            <button class="btn btn-warn" onclick="applyPreset('creep')">[Slope Creep]</button>
            <button class="btn btn-alert" onclick="applyPreset('quake')">[Disaster Quake]</button>
          </div>

          <div style="border-top: 1px solid #21262d; padding-top: 8px; margin-top: 8px;">
            <div class="control-group">
              <label>Target Node Injection: <span id="target-node-badge" style="color: #58a6ff; font-weight: bold;">Node 1</span></label>
              <select id="node-select" onchange="loadNodeSimState()">
                <option value="1">Node 1 (Crest Sector)</option>
                <option value="2">Node 2 (Summit Sector)</option>
                <option value="3">Node 3 (Ridge Sector)</option>
                <option value="4">Node 4 (Base West Sector)</option>
              </select>
            </div>
            <div class="control-group">
              <label>Soil Saturation (+30 pts): <span id="lbl-soil-val" style="color: #58a6ff;">1200 / 4095</span></label>
              <input type="range" id="sim-soil" min="0" max="4095" value="1200" oninput="updateSimState()">
            </div>
            <div class="control-group">
              <label>Crack Potentiometer (+35 pts): <span id="lbl-pot-val" style="color: #58a6ff;">1000 / 4095</span></label>
              <input type="range" id="sim-pot" min="0" max="4095" value="1000" oninput="updateSimState()">
            </div>
            <div class="control-group">
              <label>MPU Accel (+25 pts): <span id="lbl-acc-val" style="color: #58a6ff;">9.80 m/s²</span></label>
              <input type="range" id="sim-acc" min="0" max="30" step="0.1" value="9.8" oninput="updateSimState()">
            </div>
            <div class="control-group">
              <label>SW-420 Shock (+20 pts):</label>
              <select id="sim-vib" onchange="updateSimState()">
                <option value="0">0: Normal (Stable)</option>
                <option value="1">1: Shock / Impact Triggered</option>
              </select>
            </div>
            <div style="background: #000; border: 1px solid #21262d; padding: 6px 8px; border-radius: 3px; font-size: 11px; display: flex; justify-content: space-between;">
              <span class="lbl">Fusion Risk Score:</span>
              <span id="fusion-score-val" style="font-weight: bold; color: #3fb950;">0 / 50 (NORMAL)</span>
            </div>
          </div>
        </div>
      </div>

      <!-- DYNAMIC FREQUENCY & WAVEFORM CONFIG -->
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
              <option value="1000">1000 ms (1.0 Hz - Energy Saver)</option>
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
        </div>
      </div>

      <!-- QR CODE SIGNUP DECK -->
      <div class="terminal-box">
        <div class="title-bar">
          <span>QR Code SMS Subscription</span>
        </div>
        <div class="content" style="text-align: center;">
          <div style="font-size: 11px; color: #8b949e; margin-bottom: 8px;">Scan to open SMS alert registration page:</div>
          <div style="display: inline-block; position: relative;">
            <img id="qr-img-card" src="" alt="Subscription QR Code" style="border: 4px solid #fff; border-radius: 6px; width: 140px; height: 140px; margin-bottom: 6px; background-color: #ffffff;">
          </div>
          <div id="qr-url-text" style="font-size: 10px; color: #38bdf8; font-family: monospace; word-break: break-all;">http://localhost:8080/subscribe</div>
        </div>
      </div>

      <!-- TRANSMISSION HISTORY LOGS -->
      <div class="terminal-box" style="flex-grow: 1;">
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
    let globalPeakAccel = 9.80;

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
      const filledLen = Math.round((val / max) * width);
      const filled = fillChar.repeat(Math.min(width, Math.max(0, filledLen)));
      const empty = emptyChar.repeat(Math.max(0, width - filled.length));
      return `[${filled}${empty}]`;
    }

    async function updateDashboard() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        latestNodesData = data.nodes;
        
        totalPacketsReceived += data.nodes.filter(n => n.online).length;
        const packetsElem = document.getElementById('diag-packets');
        if (packetsElem) packetsElem.innerText = `${totalPacketsReceived} PKTS`;

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
          const isAbnormal = n.abnormal === 1;
          let statusTag = isAbnormal ? '<span class="status-crit">[ABNORMAL]</span>' : '<span class="status-normal">[NORMAL]</span>';
          let headClass = isAbnormal ? 'color: #f85149;' : 'color: #3fb950;';

          if (n.acc > globalPeakAccel) {
            globalPeakAccel = n.acc;
            const peakElem = document.getElementById('diag-peak-acc');
            if (peakElem) peakElem.innerText = `${globalPeakAccel.toFixed(2)} m/s²`;
          }

          const potBar = renderAsciiBar(n.pot, 4095, 14);
          const soilBar = renderAsciiBar(n.soil, 4095, 14);
          const potOver = n.pot >= 2500;
          const soilOver = n.soil >= 2500;

          if (!window.nodeHistory) window.nodeHistory = {};
          if (!window.nodeHistory[n.id]) window.nodeHistory[n.id] = { pot: [], soil: [], acc: [], maxPot: 0, maxSoil: 0, maxAcc: 0 };

          const hist = window.nodeHistory[n.id];
          
          let samplePot = n.pot;
          let sampleSoil = n.soil;
          let sampleAcc = n.acc;

          const isElevated = (n.pot >= 2000 || n.soil >= 2000 || n.acc >= 11.0 || n.vibration === 1);
          if (isAbnormal || isElevated) {
            const potScale = isAbnormal ? 320 : (n.pot >= 2000 ? 180 : 50);
            const soilScale = isAbnormal ? 240 : (n.soil >= 2000 ? 140 : 40);
            const accScale = isAbnormal ? 5.5 : (n.acc >= 11.0 ? 2.5 : 0.8);

            samplePot += Math.floor((Math.random() - 0.5) * potScale);
            sampleSoil += Math.floor((Math.random() - 0.5) * soilScale);
            sampleAcc += (Math.random() - 0.5) * accScale;
          } else {
            samplePot += Math.floor((Math.random() - 0.5) * 16);
            sampleSoil += Math.floor((Math.random() - 0.5) * 12);
            sampleAcc += (Math.random() - 0.5) * 0.15;
          }

          samplePot = Math.max(0, Math.min(4095, samplePot));
          sampleSoil = Math.max(0, Math.min(4095, sampleSoil));
          sampleAcc = Math.max(0, Math.min(30.0, sampleAcc));

          hist.pot.push(samplePot); if (hist.pot.length > currentBufferLen) hist.pot.shift();
          hist.soil.push(sampleSoil); if (hist.soil.length > currentBufferLen) hist.soil.shift();
          hist.acc.push(sampleAcc); if (hist.acc.length > currentBufferLen) hist.acc.shift();

          if (samplePot > hist.maxPot) hist.maxPot = samplePot;
          if (sampleSoil > hist.maxSoil) hist.maxSoil = sampleSoil;
          if (sampleAcc > hist.maxAcc) hist.maxAcc = sampleAcc;

          let card = document.getElementById(`sim-card-${n.id}`);
          if (!card) {
            card = document.createElement('div');
            card.id = `sim-card-${n.id}`;
            grid.appendChild(card);
          }

          card.className = "terminal-box";
          if (isAbnormal) card.style.borderColor = "#f85149";
          else card.style.borderColor = "#21262d";

          const sectorLabel = sectors[n.id - 1] || "Sector Alpha";

          card.innerHTML = `
            <div class="title-bar">
              <span style="${headClass}">Node ${n.id} (${sectorLabel})</span>
              ${statusTag}
            </div>
            <div class="content" style="padding: 6px 8px;">
              <div class="row"><span class="lbl">Potentiometer (Crack):</span><span class="val" style="${potOver ? 'color: #f85149;' : ''}">${samplePot} / 4095 (Max: ${hist.maxPot})</span></div>
              <div class="meter-container">
                <div class="meter-bar ${potOver ? 'blink-alert' : ''}" style="color: ${potOver ? '#f85149' : '#58a6ff'};">${potBar}</div>
              </div>

              <div class="row"><span class="lbl">Soil Moisture:</span><span class="val" style="${soilOver ? 'color: #f85149;' : ''}">${sampleSoil} / 4095 (Max: ${hist.maxSoil})</span></div>
              <div class="meter-container">
                <div class="meter-bar ${soilOver ? 'blink-alert' : ''}" style="color: ${soilOver ? '#f85149' : '#38d430'};">${soilBar}</div>
              </div>

              <div class="row" style="font-size: 11.5px;"><span class="lbl">MPU Acceleration:</span><span class="val">${sampleAcc.toFixed(2)} m/s² (Peak: ${hist.maxAcc.toFixed(1)})</span></div>
              <div class="row" style="font-size: 11.5px;"><span class="lbl">Tilt (Pitch/Roll):</span><span class="val">${n.pitch.toFixed(1)}° / ${n.roll.toFixed(1)}°</span></div>
              <div class="row" style="font-size: 11.5px;"><span class="lbl">SW-420 Vibration:</span><span class="val" style="color: ${n.vibration ? '#f85149' : '#c9d1d9'};">${n.vibration ? 'SHOCK DETECTED' : 'STABLE'}</span></div>
              <div class="row" style="font-size: 11.5px; margin-bottom: 4px;"><span class="lbl">Node Battery Voltage:</span><span class="val" style="color: ${n.batteryMv < 3500 ? '#f85149' : '#3fb950'};">${n.batteryMv} mV</span></div>

              <div style="border-top: 1px solid #161b22; padding-top: 5px; margin-top: 5px;">
                <div class="row" style="font-size: 11px; margin-bottom: 3px;">
                  <span style="color: #8b949e; font-weight: bold;">Waveform Oscilloscope History</span>
                  <span style="color: ${isElevated ? '#f85149' : '#484f58'};">${isElevated ? '[TREMOR NOISE ACTIVE]' : '[STABLE]'}</span>
                </div>
                <canvas id="graph-sim-${n.id}" width="420" height="75" style="background: #000; border: 1px solid #161b22; display: block; width: 100%; height: 75px; border-radius: 2px;"></canvas>
              </div>
            </div>
          `;

          const canvas = document.getElementById(`graph-sim-${n.id}`);
          if (canvas) {
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
          let newHtml = '';
          if (!data.smsLogs || data.smsLogs.length === 0) {
            newHtml = '<p style="color: #484f58; font-size: 10px;">No SMS dispatches recorded.</p>';
          } else {
            newHtml = data.smsLogs.map(log => `
              <div class="sms-log-item">
                <strong>[SMS DISPATCH -> ${log.recipient}]</strong><br>
                ${log.text.replace(/\n/g, '<br>')}
              </div>
            `).join('');
          }
          if (logsContainer.innerHTML !== newHtml) {
            logsContainer.innerHTML = newHtml;
          }
        }
      } catch (err) { console.error(err); }
    }

    let mapZoom = 1.0;
    let rotX = 0.45; // Pitch angle (~25 deg)
    let rotY = -0.32; // Yaw angle (~-18 deg)
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
        rotY += dx * 0.008;
        rotX = Math.max(0.1, Math.min(1.2, rotX + dy * 0.008));
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

    // 3D Perspective Projection helper
    function project3D(x, y, z, cx, cy) {
      const cosY = Math.cos(rotY), sinY = Math.sin(rotY);
      const x1 = x * cosY - z * sinY;
      const z1 = x * sinY + z * cosY;

      const cosX = Math.cos(rotX), sinX = Math.sin(rotX);
      const y2 = y * cosX - z1 * sinX;
      const z2 = y * sinX + z1 * cosX;

      const distance = 480 * mapZoom;
      const scale = distance / (distance + z2 + 350);
      const px = cx + x1 * scale * 260;
      const py = cy - y2 * scale * 210;

      return { x: px, y: py, z: z2, scale: scale };
    }

    function draw3DSpatialMap(nodesList) {
      if (nodesList && nodesList.length > 0) latestNodesData = nodesList;
      const cvs = document.getElementById('topo-canvas');
      if (!cvs) return;
      const ctx = cvs.getContext('2d');
      const w = cvs.width, h = cvs.height;
      const cx = w / 2, cy = h / 2 + 35;
      ctx.clearRect(0, 0, w, h);

      // 1. Render Cardboard Base Plate (Physical Base)
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

      // Base Grid Lines
      ctx.strokeStyle = 'rgba(48, 54, 61, 0.4)';
      ctx.lineWidth = 1;
      for (let g = -0.8; g <= 0.8; g += 0.4) {
        let p1 = project3D(g, 0, -0.95, cx, cy), p2 = project3D(g, 0, 0.95, cx, cy);
        ctx.beginPath(); ctx.moveTo(p1.x, p1.y); ctx.lineTo(p2.x, p2.y); ctx.stroke();
      }

      // 2. Render Green Mat (Valley Floor under Central ESP)
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

      // 3. Render 3D Curved Mountain Surface Mesh (White Paper Model Curve)
      const resX = 14, resZ = 12;
      const getMountainY = (z) => {
        if (z < -0.15) return 0.05; // Valley floor
        if (z > 0.45) return 0.62;  // Upper ridge plateau
        const t = (z + 0.15) / 0.60;
        return 0.05 + 0.57 * (0.5 - 0.5 * Math.cos(Math.PI * t)); // Smooth S-curve slope
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

      // 4. Node Position Definitions strictly based on physical model photo:
      // Upper Plateau: Node 1 (left), Node 2 (right)
      // Lower Level: Node 3 (left), Central ESP32 (center), Node 4 (right)
      const physicalNodes = [
        { id: 1, label: "Node 1", x: -0.50, z: 0.60, yBase: 0.62, yTop: 0.88 },
        { id: 2, label: "Node 2", x: 0.50, z: 0.60, yBase: 0.62, yTop: 0.88 },
        { id: 3, label: "Node 3", x: -0.65, z: -0.48, yBase: 0.05, yTop: 0.28 },
        { id: 4, label: "Node 4", x: 0.65, z: -0.48, yBase: 0.05, yTop: 0.28 }
      ];
      const gatewayPos = { label: "Central ESP32", x: 0.00, z: -0.48, yBase: 0.06, yTop: 0.29 };

      const gwPBase = project3D(gatewayPos.x, gatewayPos.yBase, gatewayPos.z, cx, cy);
      const gwPTop  = project3D(gatewayPos.x, gatewayPos.yTop, gatewayPos.z, cx, cy);

      // 5. Draw ESP-NOW Radio Wave Wireless Mesh Beams
      const dashOffset = (Date.now() / 35) % 8;
      physicalNodes.forEach(pt => {
        const nData = (latestNodesData || []).find(n => n.id === pt.id) || { abnormal: 0, online: false };
        const isAbnormal = nData.abnormal === 1;
        const nPTop = project3D(pt.x, pt.yTop, pt.z, cx, cy);

        ctx.strokeStyle = isAbnormal ? 'rgba(248, 81, 73, 0.8)' : 'rgba(88, 166, 255, 0.45)';
        ctx.lineWidth = isAbnormal ? 2 : 1.5;
        ctx.setLineDash([4, 4]);
        ctx.lineDashOffset = -dashOffset;
        ctx.beginPath();
        ctx.moveTo(nPTop.x, nPTop.y);
        ctx.lineTo(gwPTop.x, gwPTop.y);
        ctx.stroke();
        ctx.setLineDash([]);
      });

      // Helper to draw 3D Wooden Dual-Tier Tower
      const drawWoodenTower = (x, z, yBase, yTop, labelText, colorHex, isGw, isAbnormal) => {
        const pBase = project3D(x, yBase, z, cx, cy);
        const pMid  = project3D(x, yBase + (yTop - yBase) * 0.5, z, cx, cy);
        const pTop  = project3D(x, yTop, z, cx, cy);

        const w = 0.08, d = 0.06; // Platform dimensions

        // 4 Legs
        const legOffsets = [
          { dx: -w, dz: -d }, { dx: w, dz: -d },
          { dx: w, dz: d }, { dx: -w, dz: d }
        ];

        ctx.strokeStyle = '#8b6b45';
        ctx.lineWidth = 1.8;
        legOffsets.forEach(off => {
          let legB = project3D(x + off.dx, yBase, z + off.dz, cx, cy);
          let legT = project3D(x + off.dx, yTop, z + off.dz, cx, cy);
          ctx.beginPath(); ctx.moveTo(legB.x, legB.y); ctx.lineTo(legT.x, legT.y); ctx.stroke();
        });

        // Lower & Upper Wooden Decks
        [yBase + (yTop - yBase) * 0.5, yTop].forEach((yLvl, idx) => {
          let deckCorners = [
            project3D(x - w, yLvl, z - d, cx, cy),
            project3D(x + w, yLvl, z - d, cx, cy),
            project3D(x + w, yLvl, z + d, cx, cy),
            project3D(x - w, yLvl, z + d, cx, cy)
          ];
          ctx.fillStyle = idx === 1 ? '#d29922' : '#8b6b45';
          ctx.strokeStyle = '#fae17d';
          ctx.lineWidth = 1;
          ctx.beginPath();
          ctx.moveTo(deckCorners[0].x, deckCorners[0].y);
          for (let c = 1; c < 4; c++) ctx.lineTo(deckCorners[c].x, deckCorners[c].y);
          ctx.closePath();
          ctx.fill();
          ctx.stroke();
        });

        // Top Beacon Light / Sensor Indicator
        const pulseR = 10 + Math.sin(Date.now() / 150) * 3;
        if (isAbnormal) {
          ctx.strokeStyle = `rgba(248, 81, 73, ${0.4 + Math.sin(Date.now() / 150) * 0.3})`;
          ctx.lineWidth = 2;
          ctx.beginPath(); ctx.arc(pTop.x, pTop.y - 6, pulseR, 0, 2 * Math.PI); ctx.stroke();
        }

        ctx.fillStyle = colorHex;
        ctx.beginPath(); ctx.arc(pTop.x, pTop.y - 6, isGw ? 7 : 6, 0, 2 * Math.PI); ctx.fill();

        // Node Title Tag (Strictly Node 1, Node 2, etc.)
        ctx.fillStyle = '#ffffff';
        ctx.font = 'bold 11px "JetBrains Mono", monospace';
        ctx.fillText(labelText, pTop.x - (isGw ? 42 : 22), pTop.y - 18);

        ctx.fillStyle = colorHex;
        ctx.font = 'bold 9px "JetBrains Mono", monospace';
        const subTxt = isGw ? '[GATEWAY ONLINE]' : (isAbnormal ? '[ALARM]' : '[ONLINE]');
        ctx.fillText(subTxt, pTop.x - 22, pTop.y + 16);
      };

      // 6. Draw Central ESP32 Gateway Tower
      drawWoodenTower(gatewayPos.x, gatewayPos.z, gatewayPos.yBase, gatewayPos.yTop, gatewayPos.label, '#58a6ff', true, false);

      // 7. Draw Sensor Node Towers
      physicalNodes.forEach(pt => {
        const nData = (latestNodesData || []).find(n => n.id === pt.id) || { abnormal: 0, vibration: 0, online: false };
        const isAbnormal = nData.abnormal === 1;
        const hasVib = nData.vibration === 1;
        const color = !nData.online ? '#484f58' : (isAbnormal ? '#f85149' : (hasVib ? '#f59e0b' : '#3fb950'));
        drawWoodenTower(pt.x, pt.z, pt.yBase, pt.yTop, pt.label, color, false, isAbnormal);

        // Draw Animated 3D Seismic Ground Shockwaves if SW-420 Vibration is active
        if (nData.online && hasVib) {
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

    function animateMapLoop() {
      draw3DSpatialMap(latestNodesData);
      requestAnimationFrame(animateMapLoop);
    }

    function updateSliderLabels() {
      const idElem = document.getElementById('node-select');
      if (!idElem) return;
      const id = idElem.value;
      const soil = document.getElementById('sim-soil').value;
      const pot = document.getElementById('sim-pot').value;
      const acc = document.getElementById('sim-acc').value;
      const vib = document.getElementById('sim-vib').value;

      const badgeElem = document.getElementById('target-node-badge');
      if (badgeElem) badgeElem.innerText = `Node ${id}`;
      
      const lblSoil = document.getElementById('lbl-soil-val');
      if (lblSoil) lblSoil.innerText = `${soil} / 4095`;
      
      const lblPot = document.getElementById('lbl-pot-val');
      if (lblPot) lblPot.innerText = `${pot} / 4095`;
      
      const lblAcc = document.getElementById('lbl-acc-val');
      if (lblAcc) lblAcc.innerText = `${parseFloat(acc).toFixed(1)} m/s²`;

      let score = 0;
      if (parseInt(soil) >= 2500) score += 30;
      if (parseInt(pot) >= 2500) score += 35;
      if (parseFloat(acc) >= 12.0) score += 25;
      if (parseInt(vib) === 1) score += 20;

      const fusionElem = document.getElementById('fusion-score-val');
      if (fusionElem) {
        if (score >= 50) {
          fusionElem.style.color = '#f85149';
          fusionElem.innerText = `${score} / 50 (ALARM TRIGGERED)`;
        } else {
          fusionElem.style.color = '#3fb950';
          fusionElem.innerText = `${score} / 50 (NORMAL)`;
        }
      }
    }

    function applyPreset(type) {
      const presets = {
        normal: { soil: 1200, pot: 1000, acc: 9.8, vibration: 0 },
        monsoon: { soil: 3200, pot: 1400, acc: 10.2, vibration: 0 },
        creep: { soil: 1800, pot: 3100, acc: 11.5, vibration: 0 },
        quake: { soil: 3400, pot: 3200, acc: 19.5, vibration: 1 }
      };

      const p = presets[type];
      if (!p) return;

      document.getElementById('sim-soil').value = p.soil;
      document.getElementById('sim-pot').value = p.pot;
      document.getElementById('sim-acc').value = p.acc;
      document.getElementById('sim-vib').value = p.vibration;

      // Apply to all nodes in local memory for instant zero-lag UI feedback
      (latestNodesData || []).forEach(n => {
        n.soil = p.soil;
        n.pot = p.pot;
        n.acc = p.acc;
        n.vibration = p.vibration;
        let riskScore = 0;
        if (p.soil >= 2500) riskScore += 30;
        if (p.pot >= 2500) riskScore += 35;
        if (p.acc >= 12.0) riskScore += 25;
        if (p.vibration === 1) riskScore += 20;
        n.abnormal = (riskScore >= 50) ? 1 : 0;
      });

      if (latestNodesData && latestNodesData.length > 0) {
        draw3DSpatialMap(latestNodesData);
      }

      const payload = { id: 'all', soil: p.soil, pot: p.pot, acc: p.acc, vibration: p.vibration };

      fetch('/api/update-sim', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      }).then(() => {
        updateSliderLabels();
        updateDashboard();
      });
    }

    function loadNodeSimState() {
      const id = parseInt(document.getElementById('node-select').value);
      fetch('/api/get-node?id=' + id)
        .then(res => res.json())
        .then(node => {
          document.getElementById('sim-soil').value = node.soil;
          document.getElementById('sim-pot').value = node.pot;
          document.getElementById('sim-acc').value = node.acc;
          document.getElementById('sim-vib').value = node.vibration;
          updateSliderLabels();
        });
    }

    let currentPollMs = 100;
    let currentDebounceMs = 100;
    let currentBufferLen = 30;
    let dashboardPollInterval = null;

    function applySettings() {
      currentPollMs = parseInt(document.getElementById('set-poll-rate').value);
      currentBufferLen = parseInt(document.getElementById('set-buffer-len').value);

      if (dashboardPollInterval) clearInterval(dashboardPollInterval);
      dashboardPollInterval = setInterval(updateDashboard, currentPollMs);

      const hz = (1000 / currentPollMs).toFixed(1);
      const rateElem = document.getElementById('diag-rate');
      if (rateElem) rateElem.innerText = `${hz} Hz (${currentPollMs}ms)`;
    }

    let simDebounceTimer = null;

    function updateSimState() {
      const id = parseInt(document.getElementById('node-select').value);
      const soil = parseInt(document.getElementById('sim-soil').value);
      const pot = parseInt(document.getElementById('sim-pot').value);
      const acc = parseFloat(document.getElementById('sim-acc').value);
      const vibration = parseInt(document.getElementById('sim-vib').value);

      updateSliderLabels();

      // Instant local DOM feedback for zero lag
      const targetNode = (latestNodesData || []).find(n => n.id === id);
      if (targetNode) {
        targetNode.soil = soil;
        targetNode.pot = pot;
        targetNode.acc = acc;
        targetNode.vibration = vibration;

        let riskScore = 0;
        if (soil >= 2500) riskScore += 30;
        if (pot >= 2500) riskScore += 35;
        if (acc >= 12.0) riskScore += 25;
        if (vibration === 1) riskScore += 20;
        targetNode.abnormal = (riskScore >= 50) ? 1 : 0;

        if (acc > 9.8) {
          targetNode.pitch = (Math.random() - 0.5) * 20.0;
          targetNode.roll = (Math.random() - 0.5) * 20.0;
        } else {
          targetNode.pitch = 0.0;
          targetNode.roll = 0.0;
        }
      }

      if (latestNodesData && latestNodesData.length > 0) {
        draw3DSpatialMap(latestNodesData);
      }

      if (simDebounceTimer) clearTimeout(simDebounceTimer);
      simDebounceTimer = setTimeout(() => {
        const payload = { id: id, soil: soil, pot: pot, acc: acc, vibration: vibration };
        fetch('/api/update-sim', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        }).then(() => updateDashboard());
      }, 30);
    }

    async function toggleSiren() {
      await fetch('/api/siren', { method: 'POST' });
    }

    function triggerAutoCycle() {
      fetch('/api/randomize', { method: 'POST' }).then(() => updateDashboard());
    }

    const SERVER_LAN_IP = "__SERVER_LAN_IP__";

    function updateQrCode() {
      const qrImg = document.getElementById('qr-img-card');
      const qrText = document.getElementById('qr-url-text');
      
      let host = window.location.hostname;
      let port = window.location.port ? (':' + window.location.port) : '';
      let protocol = window.location.protocol;

      if ((host === 'localhost' || host === '127.0.0.1') && SERVER_LAN_IP && SERVER_LAN_IP !== '127.0.0.1') {
        host = SERVER_LAN_IP;
        if (!window.location.port) port = ':8080';
      }

      const subUrl = `${protocol}//${host}${port}/subscribe`;
      if (qrText) qrText.innerText = subUrl;

      const encodedUrl = encodeURIComponent(subUrl);
      const primaryApi = `https://api.qrserver.com/v1/create-qr-code/?size=150x150&data=${encodedUrl}`;
      const fallbackApi = `https://quickchart.io/qr?size=150&text=${encodedUrl}`;

      if (qrImg) {
        qrImg.onerror = function() {
          if (qrImg.src !== fallbackApi) {
            qrImg.src = fallbackApi;
          }
        };
        qrImg.src = primaryApi;
      }
    }

    dashboardPollInterval = setInterval(updateDashboard, currentPollMs);
    initMapControls();
    loadNodeSimState();
    updateDashboard();
    updateQrCode();
    animateMapLoop();
  </script>
</body>
</html>
"""
            server_ip = get_local_ip()
            html = html.replace("__SERVER_LAN_IP__", server_ip)
            self.wfile.write(html.encode())
            return
            
    def do_POST(self):
        global nodes, siren_active, sms_logs
        
        # Update mock node parameters
        if self.path == '/api/update-sim':
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            params = json.loads(post_data.decode())
            
            target_indices = range(len(nodes)) if params.get('id') == 'all' else [params['id'] - 1]
            for idx in target_indices:
                nodes[idx]['soil'] = params['soil']
                nodes[idx]['pot'] = params['pot']
                nodes[idx]['acc'] = params['acc']
                nodes[idx]['vibration'] = params['vibration']
                
                # Calculate mock Pitch & Roll tilt angles based on acceleration deviation
                if params['acc'] > 9.8:
                    nodes[idx]['pitch'] = random.uniform(-15.0, 15.0)
                    nodes[idx]['roll'] = random.uniform(-15.0, 15.0)
                else:
                    nodes[idx]['pitch'] = 0.0
                    nodes[idx]['roll'] = 0.0
                    
                # Multi-Sensor Fusion Combination Algorithm (Requires >= 50 points to declare anomaly)
                risk_score = 0
                if nodes[idx]['soil'] >= 2500: risk_score += 30
                if nodes[idx]['pot'] >= 2500: risk_score += 35
                if nodes[idx]['acc'] >= 12.0: risk_score += 25
                if nodes[idx]['vibration'] == 1: risk_score += 20
                
                nodes[idx]['abnormal'] = 1 if risk_score >= 50 else 0
            
            # Check risk level for SMS sending simulation & Firebase dispatch
            risk = calculate_risk()
            if risk >= 2:
                target_str = "All Nodes" if params.get('id') == 'all' else f"Node {params['id']}"
                sms_text = f"[SIMULATED ALERT] Risk level escalated to: {risk}/4. Anomaly detected on {target_str}."
                sms_logs.append({
                    "recipient": "Firebase Subscribers",
                    "text": sms_text
                })
            
            # If all 4 nodes are abnormal (Earthquake/Landslide trigger), auto-dispatch SMS via Fast2SMS API to Firebase subscribers
            if risk == 4:
                try:
                    import urllib.request
                    import urllib.parse
                    # 1. Fetch phone numbers from Firebase REST endpoint
                    fb_req = urllib.request.urlopen("https://landslide-central-default-rtdb.asia-southeast1.firebasedatabase.app/subscribers.json")
                    fb_data = json.loads(fb_req.read().decode())
                    if fb_data and isinstance(fb_data, dict):
                        numbers_list = list(fb_data.keys())
                        numbers_str = ",".join(numbers_list)
                        api_key = "NT2deLKIMAi4ZFo0prcbVCUmx9fulqBH6DaO8nyJg7QWv3szwSLQj21RBymsAT0goFpnSUe7IVG6Z4t3"
                        msg = "EMERGENCY ALERT: Earthquake / Severe Landslide Activity Detected across all 4 sensors! Evacuate immediately."
                        encoded_msg = urllib.parse.quote(msg)
                        fast2sms_url = f"https://www.fast2sms.com/dev/bulkV2?authorization={api_key}&route=q&message={encoded_msg}&language=english&flash=0&numbers={numbers_str}"
                        urllib.request.urlopen(fast2sms_url)
                        print(f"[FIREBASE SMS DISPATCH] Triggered alert SMS to {len(numbers_list)} numbers: {numbers_str}")
                except Exception as e:
                    print(f"[FIREBASE SMS DISPATCH NOTE] Local simulation trigger logged. ({e})")

            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"OK")
            return
            
        elif self.path == '/api/subscribe':
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            params = json.loads(post_data.decode())
            phone = params.get('phone', '').strip()
            
            if len(phone) == 10 and phone.isdigit():
                # Store in Firebase Realtime DB
                try:
                    import urllib.request
                    url = f"https://landslide-central-default-rtdb.asia-southeast1.firebasedatabase.app/subscribers/{phone}.json"
                    req = urllib.request.Request(url, data=json.dumps({"timestamp": int(time.time())}).encode(), headers={'Content-Type': 'application/json'}, method='PUT')
                    urllib.request.urlopen(req)
                    print(f"[FIREBASE DB] Saved phone number to Firebase: {phone}")
                except Exception as e:
                    print(f"[FIREBASE DB NOTE] Saved subscriber locally: {phone}")
                
                sms_logs.append({"recipient": phone, "text": "SUBSCRIBED: Added to Emergency SMS Alert List."})
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps({"status": "success", "message": "Phone number successfully registered for SMS alerts!"}).encode())
            else:
                self.send_response(400)
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps({"status": "error", "message": "Please enter a valid 10-digit mobile number."}).encode())
            return

        elif self.path == '/api/siren':
            siren_active = not siren_active
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"OK")
            return
            
        elif self.path == '/api/randomize':
            # Simulate random seismic trigger on random node
            target = random.randint(0, 3)
            nodes[target]['soil'] = random.randint(2600, 3800)
            nodes[target]['pot'] = random.randint(2600, 3500)
            nodes[target]['acc'] = random.uniform(13.0, 22.0)
            nodes[target]['vibration'] = 1
            nodes[target]['abnormal'] = 1
            
            risk = calculate_risk()
            sms_text = f"[SIMULATED EMERGENCY] Earthquake/Landslide triggers on Sector Alpha. Affected: Node {target+1} has collapsed."
            sms_logs.append({
                "recipient": "Registered Firebase Subscribers",
                "text": sms_text
            })
            
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"OK")
            return

    # Add quick mock GET endpoint to sync slider defaults
    def log_message(self, format, *args):
        return # Silence server logger for clean console output

class GetHandler(SimulatorHandler):
    def do_GET(self):
        if self.path.startswith('/api/get-node'):
            query = self.path.split('?')[-1]
            node_id = int(query.split('=')[-1])
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(nodes[node_id - 1]).encode())
            return
        elif self.path == '/subscribe':
            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            sub_html = R"""<!DOCTYPE html>
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
"""
            self.wfile.write(sub_html.encode())
            return
        else:
            super().do_GET()

class ReuseServer(socketserver.TCPServer):
    allow_reuse_address = True
    def server_bind(self):
        import socket
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except AttributeError:
            pass
        super().server_bind()

# start server
try:
    local_ip = get_local_ip()
    with ReuseServer(("", PORT), GetHandler) as httpd:
        print(f"dashboard running at http://localhost:{PORT} (LAN: http://{local_ip}:{PORT})")
        print(f"signup page running at http://localhost:{PORT}/subscribe (LAN: http://{local_ip}:{PORT}/subscribe)")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("simulator stopped")
except OSError as e:
    if e.errno == 98:
        print(f"error: port {PORT} is occupied by another process.")
        print(f"run: fuser -k {PORT}/tcp to free port {PORT}.")
    else:
        raise e

