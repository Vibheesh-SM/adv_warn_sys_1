import http.server
import socketserver
import json
import random
import threading
import time

PORT = 8080

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
  <title>Landslide & Earthquake Command Center (Simulator)</title>
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
    body { background: var(--bg); color: var(--text); min-height: 100vh; padding: 20px; display: flex; flex-direction: column; gap: 20px; }
    header { display: flex; justify-content: space-between; align-items: center; padding-bottom: 20px; border-bottom: 1px solid var(--card-border); }
    h1 { font-size: 1.6rem; font-weight: 800; background: linear-gradient(90deg, #60a5fa, #a78bfa); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
    
    .main-container { display: grid; grid-template-columns: 3fr 1.2fr; gap: 20px; }
    
    .status-banner { padding: 18px 24px; border-radius: 16px; display: flex; justify-content: space-between; align-items: center; backdrop-filter: blur(12px); border: 1px solid var(--card-border); transition: all 0.3s ease; }
    .status-title { font-size: 1.4rem; font-weight: 700; text-transform: uppercase; letter-spacing: 1px; }
    
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 20px; margin-top: 20px; }
    .card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 20px; padding: 20px; backdrop-filter: blur(16px); box-shadow: 0 10px 30px rgba(0,0,0,0.3); }
    .card-header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--card-border); padding-bottom: 12px; margin-bottom: 16px; }
    .node-title { font-weight: 700; font-size: 1.1rem; }
    .badge { padding: 4px 10px; border-radius: 20px; font-size: 0.75rem; font-weight: 700; text-transform: uppercase; }
    
    .metric { display: flex; justify-content: space-between; margin-bottom: 10px; font-size: 0.9rem; color: var(--text-muted); }
    .metric-val { font-weight: 600; color: var(--text); }
    .bar-bg { background: rgba(255,255,255,0.05); height: 6px; border-radius: 4px; overflow: hidden; margin-bottom: 14px; }
    .bar-fill { height: 100%; width: 0%; background: var(--accent); transition: width 0.4s ease; }
    
    .sidebar { display: flex; flex-direction: column; gap: 20px; }
    .control-panel { background: rgba(30, 41, 59, 0.5); border: 1px solid var(--card-border); border-radius: 20px; padding: 20px; }
    .control-title { font-weight: 700; font-size: 1.1rem; margin-bottom: 15px; border-bottom: 1px solid var(--card-border); padding-bottom: 10px; }
    .control-panel label { display: block; font-size: 0.85rem; color: var(--text-muted); margin-bottom: 5px; }
    .control-group { margin-bottom: 15px; }
    .control-group label { display: block; font-size: 0.85rem; color: var(--text-muted); margin-bottom: 5px; }
    .control-group select, .control-group input[type="range"] { width: 100%; }
    
    .sms-log-panel { background: rgba(15, 23, 42, 0.8); border: 1px solid var(--card-border); border-radius: 20px; padding: 20px; flex-grow: 1; }
    .sms-log-item { font-size: 0.8rem; background: rgba(255,255,255,0.02); border-left: 3px solid var(--accent); padding: 8px; margin-bottom: 10px; border-radius: 0 8px 8px 0; font-family: monospace; color: #a7f3d0; }
    
    .btn { background: var(--accent); color: white; border: none; padding: 10px 20px; border-radius: 10px; font-weight: 700; cursor: pointer; transition: 0.2s; }
    .btn-danger { background: #ef4444; }
    .btn:hover { transform: scale(1.03); }
  </style>
</head>
<body>
  <header>
    <div>
      <h1>EARTHQUAKE & LANDSLIDE COMMAND CENTER <span style="font-size: 0.8rem; color: var(--accent); vertical-align: middle;">[SIMULATOR]</span></h1>
      <p style="color: var(--text-muted); font-size: 0.85rem; margin-top: 4px;">ESP32 Mock Interface & GSM SMS Broadcast Dispatcher</p>
    </div>
    <button class="btn btn-danger" onclick="toggleSiren()">MUTE ALARM SIREN</button>
  </header>

  <div class="main-container">
    <div>
      <div id="banner" class="status-banner" style="background: rgba(16, 185, 129, 0.15); border-color: var(--normal);">
        <div>
          <div style="font-size: 0.8rem; color: var(--text-muted); text-transform: uppercase; letter-spacing: 1px;">Overall System Threat Level</div>
          <div id="status-text" class="status-title" style="color: var(--normal);">SYSTEM NORMAL</div>
        </div>
        <div id="abnormal-count-badge" style="font-size: 2.2rem; font-weight: 800; color: var(--normal);">0/4</div>
      </div>

      <div class="grid" id="node-grid"></div>
    </div>

    <div class="sidebar">
      <div class="control-panel">
        <div class="control-title">SIMULATOR CONTROLS</div>
        <div class="control-group">
          <label for="node-select">Select Node to Modify:</label>
          <select id="node-select" onchange="loadNodeSimState()">
            <option value="1">Node 1</option>
            <option value="2">Node 2</option>
            <option value="3">Node 3</option>
            <option value="4">Node 4</option>
          </select>
        </div>
        <div class="control-group">
          <label>Soil Moisture:</label>
          <input type="range" id="sim-soil" min="0" max="4095" value="1200" oninput="updateSimState()">
        </div>
        <div class="control-group">
          <label>Slope Crack / Displacement (Pot):</label>
          <input type="range" id="sim-pot" min="0" max="4095" value="1000" oninput="updateSimState()">
        </div>
        <div class="control-group">
          <label>Dynamic Acceleration (MPU):</label>
          <input type="range" id="sim-acc" min="0" max="30" step="0.1" value="9.8" oninput="updateSimState()">
        </div>
        <div class="control-group">
          <label>Vibration Shock Sensor (SW-420):</label>
          <select id="sim-vib" onchange="updateSimState()">
            <option value="0">Normal (LOW)</option>
            <option value="1">Shock Detected (HIGH)</option>
          </select>
        </div>
        <button class="btn" style="width: 100%; margin-top: 10px;" onclick="triggerAutoCycle()">Trigger Random Movement</button>
      </div>

      <div class="sms-log-panel">
        <div class="control-title" style="font-size: 0.95rem;">GSM SMS TRANSMISSION LOGS</div>
        <div id="sms-logs-container">
          <p style="color: var(--text-muted); font-size: 0.85rem;">No SMS messages sent yet.</p>
        </div>
      </div>
    </div>
  </div>

  <script>
    async function updateDashboard() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        
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

        // Nodes
        const grid = document.getElementById('node-grid');
        grid.innerHTML = '';
        data.nodes.forEach(n => {
          const isAbnormal = n.abnormal === 1;
          const badgeColor = isAbnormal ? '#ef4444' : '#10b981';
          grid.innerHTML += `
            <div class="card" style="${isAbnormal ? 'border-color: #ef4444;' : ''}">
              <div class="card-header">
                <span class="node-title">NODE ${n.id}</span>
                <span class="badge" style="background: ${badgeColor}22; color: ${badgeColor}; border: 1px solid ${badgeColor};">${isAbnormal ? 'ABNORMAL' : 'NORMAL'}</span>
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

        // Logs
        const logsContainer = document.getElementById('sms-logs-container');
        if (data.smsLogs.length === 0) {
          logsContainer.innerHTML = '<p style="color: var(--text-muted); font-size: 0.85rem;">No SMS messages sent yet.</p>';
        } else {
          logsContainer.innerHTML = data.smsLogs.map(log => `
            <div class="sms-log-item">
              <strong>[SMS Sent to ${log.recipient}]</strong><br>
              ${log.text.replace(/\n/g, '<br>')}
            </div>
          `).join('');
        }
      } catch (err) { console.error(err); }
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
        });
    }

    function updateSimState() {
      const id = parseInt(document.getElementById('node-select').value);
      const payload = {
        id: id,
        soil: parseInt(document.getElementById('sim-soil').value),
        pot: parseInt(document.getElementById('sim-pot').value),
        acc: parseFloat(document.getElementById('sim-acc').value),
        vibration: parseInt(document.getElementById('sim-vib').value)
      };

      fetch('/api/update-sim', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      }).then(() => updateDashboard());
    }

    async function toggleSiren() {
      await fetch('/api/siren', { method: 'POST' });
    }

    function triggerAutoCycle() {
      fetch('/api/randomize', { method: 'POST' }).then(() => updateDashboard());
    }

    setInterval(updateDashboard, 1000);
    loadNodeSimState();
    updateDashboard();
  </script>
</body>
</html>
"""
            self.wfile.write(html.encode())
            return
            
    def do_POST(self):
        global nodes, siren_active, sms_logs
        
        # Update mock node parameters
        if self.path == '/api/update-sim':
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            params = json.loads(post_data.decode())
            
            idx = params['id'] - 1
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
                
            # Abnormality calculation rule (Soil > 2500, Pot > 2500, Acc > 12, or Shock == 1)
            signals = 0
            if nodes[idx]['soil'] > 2500: signals += 1
            if nodes[idx]['pot'] > 2500: signals += 1
            if nodes[idx]['acc'] > 12.0: signals += 1
            if nodes[idx]['vibration'] == 1: signals += 1
            
            nodes[idx]['abnormal'] = 1 if signals >= 2 else 0
            
            # Check risk level for SMS sending simulation
            risk = calculate_risk()
            if risk >= 2:
                # Add mock SMS log
                sms_text = f"[SIMULATED ALERT] Risk level escalated to: {risk}/4. Abnormal Node details: Node {params['id']} triggered."
                sms_logs.append({
                    "recipient": "+1234567890",
                    "text": sms_text
                })
            
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"OK")
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
                "recipient": "+919876543210",
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
        else:
            super().do_GET()

# Start HTTP Server
with socketserver.TCPServer(("", PORT), GetHandler) as httpd:
    print("\n=======================================================")
    print(f"      EARTHQUAKE & LANDSLIDE SYSTEM WEB SIMULATOR     ")
    print(f"      Dashboard running at: http://localhost:{PORT}   ")
    print("=======================================================")
    print(" >>> Open your browser and navigate to the link above!")
    print(" >>> Adjust sensor values in the side control panel.")
    print(" >>> Trigger triggers to see the warning matrix and SMS alerts.")
    print("Press Ctrl+C to terminate the simulator.\n")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nSimulator Stopped.")
