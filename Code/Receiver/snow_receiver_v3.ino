/*
 * ╔══════════════════════════════════════════════════╗
 * ║       SNOW STATION — LoRa Receiver + Scanner    ║
 * ║       Board  : Heltec WiFi LoRa 32 V3           ║
 * ╚══════════════════════════════════════════════════╝
 */

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <WebServer.h>

// ═══════════════════════════════════════════════════
//  HELTEC LORA 32 V3 — ONBOARD SX1262 PINS
// ═══════════════════════════════════════════════════
#define LORA_SCK      9
#define LORA_MISO    11
#define LORA_MOSI    10
#define LORA_CS       8
#define LORA_RST     12
#define LORA_DIO1    14
#define LORA_BUSY    13

#define VEXT_PIN     36
#define LED_PIN      35

// ═══════════════════════════════════════════════════
//  WIFI ACCESS POINT
// ═══════════════════════════════════════════════════
#define AP_SSID  "captwur neige"
#define AP_PASS  "1234"

// ═══════════════════════════════════════════════════
//  LORA PARAMETERS
// ═══════════════════════════════════════════════════
#define LORA_FREQ       868.125   // MHz — will be updated by scanner
#define LORA_BW         125.0
#define LORA_SF         9
#define LORA_CR         5
#define LORA_PREAMBLE   8
#define LORA_SYNC_WORD  0x1429    // E220 default
#define LORA_POWER_DBM  22

// ═══════════════════════════════════════════════════
//  SCANNER CONFIGURATION
// ═══════════════════════════════════════════════════
#define SCAN_START_MHZ    850.0    // Start frequency
#define SCAN_END_MHZ      870.0    // End frequency
#define SCAN_STEP_MHZ       0.1    // Step size (100 kHz)
#define SCAN_DWELL_MS       100    // Time to listen per frequency
#define SCAN_RSSI_THRESH   -90     // RSSI threshold to detect signal

// ═══════════════════════════════════════════════════
//  OBJECTS
// ═══════════════════════════════════════════════════
SPIClass  loraSPI(FSPI);
SX1262    radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);
WebServer server(80);

// ═══════════════════════════════════════════════════
//  STATE
// ═══════════════════════════════════════════════════
bool       scannerMode    = false;
bool       scanning       = false;
float      currentFreq    = LORA_FREQ;
uint32_t   scanStartTime  = 0;
uint32_t   lastFreqChange = 0;

struct ScanResult {
  float   freq;
  int     rssi;
  float   snr;
  uint32_t count;
} scanResults[50];

uint8_t  scanResultCount  = 0;
uint8_t  currentScanIdx   = 0;

// ═══════════════════════════════════════════════════
//  RECEIVED DATA STRUCTURE
// ═══════════════════════════════════════════════════
struct SnowPacket {
  float    lat      = 0.0f;
  float    lon      = 0.0f;
  float    alt      = 0.0f;
  float    speed    = 0.0f;
  float    depth    = -1.0f;
  int      sats     = 0;
  float    hdop     = 0.0f;
  char     date[12] = "--";
  char     time_s[10]= "--";
  int      rssi     = 0;
  float    snr      = 0.0f;
  uint32_t lastRxMs = 0;
  uint32_t count    = 0;
  bool     valid    = false;
  char     raw[128] = "";
} pkt;

// ═══════════════════════════════════════════════════
//  FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════
void checkLoRa();
void parsePayload(const String& payload);
void handleRoot();
void handleData();
void handleScan();
void handleScanStart();
void handleScanStop();
void handleNotFound();
void startScanner();
void stopScanner();
void scannerLoop();

// ═══════════════════════════════════════════════════
//  HTML DASHBOARD WITH SCANNER
// ═══════════════════════════════════════════════════
static const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Station Neige ❄ + Scanner</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;600;700&display=swap');

  :root {
    --bg:          #08080a;
    --surface:     rgba(255,255,255,0.042);
    --border:      rgba(255,255,255,0.08);
    --border2:     rgba(255,255,255,0.14);
    --text:        #f0f0f5;
    --text-dim:    rgba(240,240,245,0.45);
    --text-mid:    rgba(240,240,245,0.7);
    --blue:        #0a84ff;
    --cyan:        #64d2ff;
    --green:       #30d158;
    --yellow:      #ffd60a;
    --red:         #ff453a;
    --purple:      #bf5af2;
    --r:           20px;
  }

  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  html, body {
    height: 100%;
    background: var(--bg);
    color: var(--text);
    font-family: 'Outfit', -apple-system, BlinkMacSystemFont, sans-serif;
    font-size: 15px;
    -webkit-font-smoothing: antialiased;
  }

  body::before {
    content: '';
    position: fixed;
    inset: 0;
    background-image: url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.75' numOctaves='4' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.03'/%3E%3C/svg%3E");
    background-size: 200px;
    pointer-events: none;
    z-index: 0;
  }

  .app {
    position: relative;
    z-index: 1;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
  }

  /* TITLE BAR */
  .titlebar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 18px 28px;
    border-bottom: 1px solid var(--border);
    background: rgba(8,8,10,0.85);
    backdrop-filter: blur(24px);
    position: sticky;
    top: 0;
    z-index: 100;
  }

  .titlebar-left {
    display: flex;
    align-items: center;
    gap: 14px;
  }

  .snowflake-icon {
    font-size: 24px;
    filter: drop-shadow(0 0 8px rgba(100,210,255,0.6));
    animation: spin 12s linear infinite;
  }

  @keyframes spin { to { transform: rotate(360deg); } }

  .titlebar h1 {
    font-size: 17px;
    font-weight: 600;
    letter-spacing: -0.3px;
  }

  .titlebar-sub {
    font-size: 12px;
    color: var(--text-dim);
    letter-spacing: 0.5px;
    text-transform: uppercase;
  }

  .status-badge {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 6px 14px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 100px;
    font-size: 12px;
    font-weight: 500;
    color: var(--text-mid);
  }

  .status-badge.live {
    border-color: rgba(48,209,88,0.4);
    color: var(--green);
    background: rgba(48,209,88,0.15);
  }

  .status-badge.scanning {
    border-color: rgba(191,90,242,0.4);
    color: var(--purple);
    background: rgba(191,90,242,0.15);
  }

  .status-dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--text-dim);
  }

  .status-badge.live .status-dot {
    background: var(--green);
    box-shadow: 0 0 6px var(--green);
    animation: pulse 2s infinite;
  }

  .status-badge.scanning .status-dot {
    background: var(--purple);
    box-shadow: 0 0 6px var(--purple);
    animation: pulse 0.5s infinite;
  }

  @keyframes pulse {
    0%,100% { opacity: 1; }
    50% { opacity: 0.4; }
  }

  /* MODE SWITCHER */
  .mode-switcher {
    display: flex;
    gap: 8px;
    margin-bottom: 24px;
  }

  .mode-btn {
    flex: 1;
    padding: 14px 20px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    color: var(--text-mid);
    font-size: 14px;
    font-weight: 500;
    cursor: pointer;
    transition: all 0.3s;
    text-align: center;
  }

  .mode-btn:hover {
    border-color: var(--border2);
    color: var(--text);
  }

  .mode-btn.active {
    background: rgba(10,132,255,0.2);
    border-color: var(--blue);
    color: var(--blue);
  }

  /* MAIN CONTENT */
  .main {
    flex: 1;
    padding: 28px;
    max-width: 1100px;
    width: 100%;
    margin: 0 auto;
  }

  /* SCANNER SECTION */
  .scanner-panel {
    background: linear-gradient(135deg,
      rgba(191,90,242,0.1) 0%,
      rgba(10,132,255,0.05) 100%);
    border: 1px solid rgba(191,90,242,0.3);
    border-radius: var(--r);
    padding: 28px;
    margin-bottom: 24px;
  }

  .scanner-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 20px;
  }

  .scanner-title {
    font-size: 16px;
    font-weight: 600;
    color: var(--purple);
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .scanner-controls {
    display: flex;
    gap: 10px;
  }

  .btn {
    padding: 8px 16px;
    border: none;
    border-radius: 8px;
    font-size: 13px;
    font-weight: 500;
    cursor: pointer;
    transition: all 0.3s;
  }

  .btn-primary {
    background: var(--purple);
    color: white;
  }

  .btn-primary:hover {
    background: #a54ae0;
    transform: translateY(-1px);
  }

  .btn-secondary {
    background: var(--surface);
    border: 1px solid var(--border);
    color: var(--text-mid);
  }

  .btn-secondary:hover {
    border-color: var(--border2);
    color: var(--text);
  }

  .btn-danger {
    background: var(--red);
    color: white;
  }

  .btn-danger:hover {
    background: #e63e34;
  }

  /* FREQUENCY DISPLAY */
  .freq-display {
    background: rgba(0,0,0,0.4);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 20px;
    text-align: center;
    margin-bottom: 20px;
  }

  .freq-label {
    font-size: 11px;
    color: var(--text-dim);
    text-transform: uppercase;
    letter-spacing: 1px;
    margin-bottom: 8px;
  }

  .freq-value {
    font-size: 48px;
    font-weight: 300;
    color: var(--cyan);
    font-variant-numeric: tabular-nums;
    letter-spacing: -2px;
  }

  .freq-value span {
    font-size: 18px;
    color: var(--text-dim);
    margin-left: 4px;
  }

  /* SCAN PROGRESS */
  .scan-progress {
    margin-bottom: 20px;
  }

  .progress-bar {
    height: 6px;
    background: var(--surface);
    border-radius: 3px;
    overflow: hidden;
    margin-bottom: 10px;
  }

  .progress-fill {
    height: 100%;
    background: linear-gradient(90deg, var(--purple), var(--blue));
    border-radius: 3px;
    transition: width 0.3s;
  }

  .progress-text {
    font-size: 12px;
    color: var(--text-dim);
    text-align: center;
  }

  /* SCAN RESULTS TABLE */
  .scan-results {
    background: rgba(0,0,0,0.3);
    border: 1px solid var(--border);
    border-radius: 12px;
    overflow: hidden;
  }

  .results-header {
    padding: 14px 18px;
    background: var(--surface);
    border-bottom: 1px solid var(--border);
    font-size: 12px;
    font-weight: 600;
    color: var(--text-dim);
    text-transform: uppercase;
    letter-spacing: 0.8px;
    display: grid;
    grid-template-columns: 2fr 1fr 1fr 1fr 100px;
    gap: 10px;
  }

  .result-row {
    padding: 12px 18px;
    border-bottom: 1px solid var(--border);
    font-size: 13px;
    display: grid;
    grid-template-columns: 2fr 1fr 1fr 1fr 100px;
    gap: 10px;
    align-items: center;
    transition: background 0.2s;
  }

  .result-row:hover {
    background: rgba(255,255,255,0.02);
  }

  .result-row.strong {
    background: rgba(48,209,88,0.08);
    border-left: 3px solid var(--green);
  }

  .result-row.medium {
    background: rgba(255,214,10,0.08);
    border-left: 3px solid var(--yellow);
  }

  .result-row.weak {
    border-left: 3px solid var(--red);
  }

  .freq-col {
    font-weight: 500;
    color: var(--cyan);
    font-variant-numeric: tabular-nums;
  }

  .rssi-col {
    color: var(--text-mid);
    font-variant-numeric: tabular-nums;
  }

  .snr-col {
    color: var(--text-mid);
    font-variant-numeric: tabular-nums;
  }

  .count-col {
    color: var(--text-dim);
    font-variant-numeric: tabular-nums;
  }

  .use-btn {
    padding: 6px 12px;
    font-size: 11px;
  }

  /* GRID */
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
    gap: 16px;
  }

  /* CARD */
  .card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--r);
    padding: 22px 24px;
    transition: border-color 0.3s, transform 0.2s;
    position: relative;
  }

  .card:hover {
    border-color: var(--border2);
    transform: translateY(-1px);
  }

  .card-label {
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 1.4px;
    text-transform: uppercase;
    color: var(--text-dim);
    margin-bottom: 16px;
    display: flex;
    align-items: center;
    gap: 7px;
  }

  .card-value {
    font-size: 32px;
    font-weight: 300;
    letter-spacing: -1.5px;
    color: var(--text);
    line-height: 1;
    margin-bottom: 4px;
  }

  .card-value .unit {
    font-size: 16px;
    font-weight: 400;
    color: var(--text-dim);
    margin-left: 3px;
  }

  .card-sub {
    font-size: 12px;
    color: var(--text-dim);
    margin-top: 10px;
  }

  .card::before {
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0;
    height: 2px;
    border-radius: 2px 2px 0 0;
    background: transparent;
  }

  .card.accent-blue::before   { background: linear-gradient(90deg, var(--blue), transparent); }
  .card.accent-green::before  { background: linear-gradient(90deg, var(--green), transparent); }
  .card.accent-cyan::before   { background: linear-gradient(90deg, var(--cyan), transparent); }

  /* NO DATA */
  .no-data {
    text-align: center;
    padding: 80px 20px;
    color: var(--text-dim);
  }

  .no-data-icon {
    font-size: 56px;
    margin-bottom: 16px;
    opacity: 0.3;
  }

  .no-data h2 {
    font-size: 18px;
    font-weight: 500;
    color: var(--text-mid);
    margin-bottom: 8px;
  }

  /* FOOTER */
  .footer {
    text-align: center;
    padding: 20px;
    font-size: 11px;
    color: var(--text-dim);
    border-top: 1px solid var(--border);
  }

  .hidden { display: none !important; }

  @media (max-width: 640px) {
    .main { padding: 16px; }
    .scanner-header { flex-direction: column; gap: 14px; align-items: flex-start; }
    .result-row, .results-header { grid-template-columns: 1fr 1fr; }
    .freq-value { font-size: 36px; }
  }
</style>
</head>
<body>
<div class="app">

  <header class="titlebar">
    <div class="titlebar-left">
      <div class="snowflake-icon">❄</div>
      <div>
        <div class="titlebar h1">Station Neige</div>
        <div class="titlebar-sub">LoRa Scanner + Receiver</div>
      </div>
    </div>
    <div class="status-badge" id="statusBadge">
      <div class="status-dot" id="statusDot"></div>
      <span id="statusText">En attente…</span>
    </div>
  </header>

  <div class="main">

    <!-- MODE SWITCHER -->
    <div class="mode-switcher">
      <div class="mode-btn active" id="modeReceiver" onclick="setMode('receiver')">
        📡 Mode Réception
      </div>
      <div class="mode-btn" id="modeScanner" onclick="setMode('scanner')">
        🔍 Mode Scanner
      </div>
    </div>

    <!-- SCANNER PANEL -->
    <div id="scannerPanel" class="scanner-panel hidden">
      <div class="scanner-header">
        <div class="scanner-title">
          <span>📡</span>
          <span>Scanner de Fréquences LoRa</span>
        </div>
        <div class="scanner-controls">
          <button class="btn btn-primary" id="startScanBtn" onclick="startScan()">
            ▶ Démarrer le scan
          </button>
          <button class="btn btn-danger hidden" id="stopScanBtn" onclick="stopScan()">
            ⏹ Arrêter
          </button>
        </div>
      </div>

      <div class="freq-display">
        <div class="freq-label">Fréquence actuelle</div>
        <div class="freq-value" id="currentFreq">868.125<span>MHz</span></div>
      </div>

      <div class="scan-progress hidden" id="scanProgress">
        <div class="progress-bar">
          <div class="progress-fill" id="progressFill" style="width:0%"></div>
        </div>
        <div class="progress-text" id="progressText">Scan en cours... 0%</div>
      </div>

      <div class="scan-results">
        <div class="results-header">
          <div>Fréquence</div>
          <div>RSSI</div>
          <div>SNR</div>
          <div>Paquets</div>
          <div>Action</div>
        </div>
        <div id="resultsList">
          <div class="result-row" style="text-align:center;color:var(--text-dim);padding:30px;">
            Aucun résultat — démarrez un scan
          </div>
        </div>
      </div>
    </div>

    <!-- RECEIVER PANEL -->
    <div id="receiverPanel">
      <div class="last-rx-bar" id="lastRxBar" style="display:none;align-items:center;justify-content:space-between;padding:12px 20px;background:var(--surface);border:1px solid var(--border);border-radius:12px;margin-bottom:24px;font-size:13px;color:var(--text-dim);">
        <span>Dernière réception : <strong id="lastRxTime">--</strong></span>
        <span id="rxAge" style="font-size:12px;">--</span>
      </div>

      <div class="hero" id="heroCard" style="background:linear-gradient(135deg,rgba(10,132,255,0.12) 0%,rgba(100,210,255,0.06) 50%,rgba(8,8,10,0) 100%);border:1px solid rgba(10,132,255,0.25);border-radius:var(--r);padding:36px 40px;display:flex;align-items:center;justify-content:space-between;gap:24px;margin-bottom:24px;position:relative;overflow:hidden;">
        <div>
          <div style="font-size:12px;font-weight:600;letter-spacing:1.5px;text-transform:uppercase;color:var(--blue);margin-bottom:10px;">Hauteur de neige</div>
          <div style="font-size:72px;font-weight:300;letter-spacing:-4px;line-height:1;color:var(--text);" id="depthValue">---</div>
          <div style="font-size:20px;color:var(--text-dim);margin-top:8px;" id="depthMeters">-- m</div>
        </div>
        <div style="text-align:right;">
          <div style="font-size:13px;color:var(--text-dim);margin-bottom:6px;">📦 Paquets reçus</div>
          <div style="font-size:11px;color:var(--blue);font-weight:600;" id="packetCount"># 0</div>
          <br>
          <div style="font-size:13px;color:var(--text-dim);margin-bottom:6px;">📡 RSSI <strong id="rssiValue">-- dBm</strong></div>
          <div style="font-size:13px;color:var(--text-dim);">〰 SNR <strong id="snrValue">-- dB</strong></div>
        </div>
      </div>

      <div class="grid" id="dataGrid">
        <div class="no-data" id="noData">
          <div class="no-data-icon">📡</div>
          <h2>En attente de données LoRa</h2>
          <p>Démarrez la station de neige et attendez la première transmission.</p>
        </div>
      </div>
    </div>

  </div>

  <footer class="footer">
    Heltec LoRa 32 V3 · SX1262 · <span id="freqFooter">868.125 MHz</span> · SF9 BW125
  </footer>

</div>

<script>
let scannerMode = false;
let scanInterval = null;

function setMode(mode) {
  scannerMode = (mode === 'scanner');
  document.getElementById('modeReceiver').classList.toggle('active', !scannerMode);
  document.getElementById('modeScanner').classList.toggle('active', scannerMode);
  document.getElementById('scannerPanel').classList.toggle('hidden', !scannerMode);
  document.getElementById('receiverPanel').classList.toggle('hidden', scannerMode);
  
  const badge = document.getElementById('statusBadge');
  const dot = document.getElementById('statusDot');
  const text = document.getElementById('statusText');
  
  if (scannerMode) {
    badge.className = 'status-badge scanning';
    text.textContent = 'Mode Scanner';
  } else {
    badge.className = 'status-badge';
    dot.style.background = 'var(--text-dim)';
    text.textContent = 'Mode Réception';
  }
  
  fetchData();
}

async function startScan() {
  try {
    const r = await fetch('/scan/start');
    const d = await r.json();
    document.getElementById('startScanBtn').classList.add('hidden');
    document.getElementById('stopScanBtn').classList.remove('hidden');
    document.getElementById('scanProgress').classList.remove('hidden');
    document.getElementById('resultsList').innerHTML = '<div class="result-row" style="text-align:center;color:var(--text-dim);">Scan en cours...</div>';
    
    scanInterval = setInterval(fetchScanResults, 500);
  } catch(e) {
    alert('Erreur: ' + e);
  }
}

async function stopScan() {
  try {
    await fetch('/scan/stop');
    document.getElementById('startScanBtn').classList.remove('hidden');
    document.getElementById('stopScanBtn').classList.add('hidden');
    document.getElementById('scanProgress').classList.add('hidden');
    if (scanInterval) clearInterval(scanInterval);
  } catch(e) {
    alert('Erreur: ' + e);
  }
}

async function fetchScanResults() {
  try {
    const r = await fetch('/scan/data');
    const d = await r.json();
    
    if (d.scanning) {
      const pct = Math.round((d.currentIdx / d.totalSteps) * 100);
      document.getElementById('progressFill').style.width = pct + '%';
      document.getElementById('progressText').textContent = 
        `Scan en cours... ${pct}% (${d.currentFreq.toFixed(3)} MHz)`;
      document.getElementById('currentFreq').innerHTML = 
        d.currentFreq.toFixed(3) + '<span>MHz</span>';
      
      if (d.results && d.results.length > 0) {
        renderScanResults(d.results);
      }
    } else {
      if (scanInterval) clearInterval(scanInterval);
      document.getElementById('startScanBtn').classList.remove('hidden');
      document.getElementById('stopScanBtn').classList.add('hidden');
      document.getElementById('scanProgress').classList.add('hidden');
      document.getElementById('statusBadge').className = 'status-badge scanning';
      document.getElementById('statusText').textContent = 'Scan terminé';
    }
  } catch(e) {
    console.error(e);
  }
}

function renderScanResults(results) {
  const list = document.getElementById('resultsList');
  if (!results || results.length === 0) {
    list.innerHTML = '<div class="result-row" style="text-align:center;color:var(--text-dim);">Aucun signal détecté</div>';
    return;
  }
  
  let html = '';
  results.sort((a,b) => b.rssi - a.rssi);
  
  results.forEach(r => {
    let cls = 'weak';
    if (r.rssi > -80) cls = 'strong';
    else if (r.rssi > -100) cls = 'medium';
    
    html += `
      <div class="result-row ${cls}">
        <div class="freq-col">${r.freq.toFixed(3)} MHz</div>
        <div class="rssi-col">${r.rssi} dBm</div>
        <div class="snr-col">${r.snr.toFixed(1)} dB</div>
        <div class="count-col">${r.count}</div>
        <div><button class="btn btn-secondary use-btn" onclick="useFrequency(${r.freq})">Utiliser</button></div>
      </div>
    `;
  });
  
  list.innerHTML = html;
}

async function useFrequency(freq) {
  try {
    await fetch('/freq?val=' + freq);
    alert('Fréquence changée: ' + freq.toFixed(3) + ' MHz');
    setMode('receiver');
  } catch(e) {
    alert('Erreur: ' + e);
  }
}

function fmt1(v) { return isNaN(v) ? '---' : Number(v).toFixed(1); }
function fmt2(v) { return isNaN(v) ? '---' : Number(v).toFixed(2); }
function fmt5(v) { return isNaN(v) ? '---' : Number(v).toFixed(5); }

function ageClass(ms) {
  if (ms < 10000) return 'fresh';
  if (ms < 30000) return 'stale';
  return 'old';
}

function ageStr(ms) {
  if (ms < 5000) return 'À l\'instant';
  if (ms < 60000) return `Il y a ${Math.floor(ms/1000)} s`;
  return `Il y a ${Math.floor(ms/60000)} min`;
}

async function fetchData() {
  try {
    const endpoint = scannerMode ? '/scan/data' : '/data';
    const r = await fetch(endpoint);
    const d = await r.json();
    
    if (scannerMode) {
      if (d.results) renderScanResults(d.results);
      return;
    }
    
    // Receiver mode
    if (!d.valid) {
      document.getElementById('noData').style.display = 'block';
      document.getElementById('dataGrid').innerHTML = 
        '<div class="no-data" id="noData"><div class="no-data-icon">📡</div><h2>En attente de données LoRa</h2><p>Démarrez la station de neige.</p></div>';
      document.getElementById('lastRxBar').style.display = 'none';
      document.getElementById('statusBadge').className = 'status-badge';
      document.getElementById('statusText').textContent = 'En attente…';
      return;
    }
    
    document.getElementById('noData').style.display = 'none';
    document.getElementById('lastRxBar').style.display = 'flex';
    document.getElementById('statusBadge').className = 'status-badge live';
    document.getElementById('statusText').textContent = 'En direct';
    
    const depthCm = parseFloat(d.depth);
    const depthM = depthCm / 100;
    const noDepth = depthCm < 0;
    
    document.getElementById('depthValue').textContent = noDepth ? '---' : fmt1(depthCm);
    document.getElementById('depthMeters').textContent = noDepth ? '-- m' : fmt2(depthM) + ' m';
    document.getElementById('packetCount').textContent = '# ' + d.count;
    document.getElementById('rssiValue').textContent = d.rssi + ' dBm';
    document.getElementById('snrValue').textContent = fmt1(d.snr) + ' dB';
    document.getElementById('lastRxTime').textContent = d.date + ' · ' + d.time_s;
    document.getElementById('rxAge').textContent = ageStr(d.age_ms);
    document.getElementById('rxAge').className = 'rx-age ' + ageClass(d.age_ms);
    
    document.getElementById('dataGrid').innerHTML = `
      <div class="card" style="grid-column:span 2;background:var(--surface);border:1px solid var(--border);border-radius:var(--r);padding:22px 24px;">
        <div style="font-size:11px;font-weight:600;letter-spacing:1.4px;text-transform:uppercase;color:var(--text-dim);margin-bottom:16px;">🌐 Coordonnées GPS</div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:20px;">
          <div><div style="font-size:11px;color:var(--text-dim);margin-bottom:6px;">Latitude</div><div style="font-size:24px;">${fmt5(d.lat)} °N</div></div>
          <div><div style="font-size:11px;color:var(--text-dim);margin-bottom:6px;">Longitude</div><div style="font-size:24px;">${fmt5(d.lon)} °E</div></div>
        </div>
      </div>
      <div class="card" style="background:var(--surface);border:1px solid var(--border);border-radius:var(--r);padding:22px 24px;">
        <div style="font-size:11px;font-weight:600;letter-spacing:1.4px;text-transform:uppercase;color:var(--text-dim);margin-bottom:16px;">⛰ Altitude</div>
        <div style="font-size:32px;font-weight:300;">${fmt1(d.alt)}<span style="font-size:16px;color:var(--text-dim);margin-left:3px;">m</span></div>
      </div>
      <div class="card" style="background:var(--surface);border:1px solid var(--border);border-radius:var(--r);padding:22px 24px;">
        <div style="font-size:11px;font-weight:600;letter-spacing:1.4px;text-transform:uppercase;color:var(--text-dim);margin-bottom:16px;">💨 Vitesse</div>
        <div style="font-size:32px;font-weight:300;">${fmt1(d.speed)}<span style="font-size:16px;color:var(--text-dim);margin-left:3px;">km/h</span></div>
      </div>
      <div class="card" style="background:var(--surface);border:1px solid var(--border);border-radius:var(--r);padding:22px 24px;">
        <div style="font-size:11px;font-weight:600;letter-spacing:1.4px;text-transform:uppercase;color:var(--text-dim);margin-bottom:16px;">🛰 Satellites</div>
        <div style="font-size:32px;font-weight:300;">${d.sats}<span style="font-size:16px;color:var(--text-dim);margin-left:3px;">sat</span></div>
        <div style="font-size:12px;color:var(--text-dim);margin-top:10px;">HDOP: ${fmt2(d.hdop)}</div>
      </div>
      <div class="card" style="background:var(--surface);border:1px solid var(--border);border-radius:var(--r);padding:22px 24px;">
        <div style="font-size:11px;font-weight:600;letter-spacing:1.4px;text-transform:uppercase;color:var(--text-dim);margin-bottom:16px;">🕐 Horodatage</div>
        <div style="font-size:22px;letter-spacing:-0.5px;">${d.time_s}</div>
        <div style="font-size:12px;color:var(--text-dim);margin-top:6px;">${d.date}</div>
      </div>
    `;
    
  } catch(e) {
    console.error(e);
    document.getElementById('statusBadge').className = 'status-badge';
    document.getElementById('statusText').textContent = 'Hors ligne';
  }
}

fetchData();
setInterval(fetchData, 2000);
</script>
</body>
</html>
)rawhtml";

// ═══════════════════════════════════════════════════
//  PARSE CSV PAYLOAD
// ═══════════════════════════════════════════════════
void parsePayload(const String& payload) {
  char buf[128];
  payload.toCharArray(buf, sizeof(buf));

  char* tok = strtok(buf, ",");
  int field = 0;
  while (tok && field < 9) {
    switch (field) {
      case 0: pkt.lat   = atof(tok); break;
      case 1: pkt.lon   = atof(tok); break;
      case 2: pkt.alt   = atof(tok); break;
      case 3: pkt.speed = atof(tok); break;
      case 4: pkt.depth = atof(tok); break;
      case 5: pkt.sats  = atoi(tok); break;
      case 6: pkt.hdop  = atof(tok); break;
      case 7: strncpy(pkt.date,   tok, sizeof(pkt.date)   - 1); break;
      case 8: strncpy(pkt.time_s, tok, sizeof(pkt.time_s) - 1); break;
    }
    tok = strtok(nullptr, ",");
    field++;
  }

  payload.toCharArray(pkt.raw, sizeof(pkt.raw));
  pkt.valid    = (field >= 7);
  pkt.lastRxMs = millis();
  pkt.count++;
}

// ═══════════════════════════════════════════════════
//  SCANNER FUNCTIONS
// ═══════════════════════════════════════════════════

void startScanner() {
  if (scanning) return;
  
  Serial.println("[SCAN] Starting frequency scan...");
  scanning = true;
  scanResultCount = 0;
  currentScanIdx = 0;
  scanStartTime = millis();
  
  // Reset radio for scanning
  radio.setFrequency(SCAN_START_MHZ);
  currentFreq = SCAN_START_MHZ;
  radio.startReceive();
  
  Serial.printf("[SCAN] Range: %.3f - %.3f MHz, Step: %.3f MHz\n",
    SCAN_START_MHZ, SCAN_END_MHZ, SCAN_STEP_MHZ);
}

void stopScanner() {
  if (!scanning) return;
  
  Serial.println("[SCAN] Stopping scan");
  scanning = false;
  
  // Return to normal frequency
  radio.setFrequency(currentFreq);
  radio.startReceive();
}

void scannerLoop() {
  if (!scanning) return;
  
  uint32_t now = millis();
  
  // Check if dwell time has passed
  if (now - lastFreqChange < SCAN_DWELL_MS) return;
  lastFreqChange = now;
  
  // Read RSSI at current frequency
  int rssi = (int)radio.getRSSI();
  float snr = radio.getSNR();
  
  // Check for packets
  if (radio.available()) {
    String payload;
    int state = radio.readData(payload);
    
    if (state == RADIOLIB_ERR_NONE && rssi > SCAN_RSSI_THRESH) {
      // Store result
      if (scanResultCount < 50) {
        scanResults[scanResultCount].freq = currentFreq;
        scanResults[scanResultCount].rssi = rssi;
        scanResults[scanResultCount].snr = snr;
        scanResults[scanResultCount].count = 1;
        scanResultCount++;
        
        Serial.printf("[SCAN] Signal found! %.3f MHz  RSSI=%d dBm  SNR=%.1f dB\n",
          currentFreq, rssi, snr);
      }
    }
  }
  
  // Move to next frequency
  currentFreq += SCAN_STEP_MHZ;
  currentScanIdx++;
  
  if (currentFreq > SCAN_END_MHZ) {
    // Scan complete
    scanning = false;
    Serial.printf("[SCAN] Complete. Found %d signals\n", scanResultCount);
  } else {
    // Set new frequency
    radio.setFrequency(currentFreq);
    radio.startReceive();
  }
}

// ═══════════════════════════════════════════════════
//  WEB HANDLERS
// ═══════════════════════════════════════════════════

void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  uint32_t age = pkt.lastRxMs > 0 ? (millis() - pkt.lastRxMs) : 9999999u;

  char json[512];
  snprintf(json, sizeof(json),
    "{"
    "\"valid\":%s,"
    "\"lat\":%.5f,\"lon\":%.5f,"
    "\"alt\":%.1f,\"speed\":%.1f,"
    "\"depth\":%.1f,"
    "\"sats\":%d,\"hdop\":%.2f,"
    "\"date\":\"%s\",\"time_s\":\"%s\","
    "\"rssi\":%d,\"snr\":%.1f,"
    "\"count\":%lu,\"age_ms\":%lu,"
    "\"raw\":\"%s\""
    "}",
    pkt.valid ? "true" : "false",
    pkt.lat, pkt.lon,
    pkt.alt, pkt.speed,
    pkt.depth,
    pkt.sats, pkt.hdop,
    pkt.date, pkt.time_s,
    pkt.rssi, pkt.snr,
    pkt.count, age,
    pkt.raw
  );

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", json);
}

void handleScan() {
  char json[1024];
  String resultsJson = "[";
  
  for (uint8_t i = 0; i < scanResultCount; i++) {
    if (i > 0) resultsJson += ",";
    resultsJson += "{";
    resultsJson += "\"freq\":" + String(scanResults[i].freq) + ",";
    resultsJson += "\"rssi\":" + String(scanResults[i].rssi) + ",";
    resultsJson += "\"snr\":" + String(scanResults[i].snr, 1) + ",";
    resultsJson += "\"count\":" + String(scanResults[i].count);
    resultsJson += "}";
  }
  resultsJson += "]";
  
  snprintf(json, sizeof(json),
    "{"
    "\"scanning\":%s,"
    "\"currentFreq\":%.3f,"
    "\"currentIdx\":%d,"
    "\"totalSteps\":%d,"
    "\"results\":%s"
    "}",
    scanning ? "true" : "false",
    currentFreq,
    currentScanIdx,
    (int)((SCAN_END_MHZ - SCAN_START_MHZ) / SCAN_STEP_MHZ),
    resultsJson.c_str()
  );
  
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", json);
}

void handleScanStart() {
  startScanner();
  server.send(200, "application/json", "{\"status\":\"started\"}");
}

void handleScanStop() {
  stopScanner();
  server.send(200, "application/json", "{\"status\":\"stopped\"}");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ═══════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

  Serial.println("[ LoRa ] Initialising SX1262...");
  int state = radio.begin(
    LORA_FREQ,
    LORA_BW,
    LORA_SF,
    LORA_CR,
    LORA_SYNC_WORD,
    LORA_POWER_DBM,
    LORA_PREAMBLE
  );

  if (state == RADIOLIB_ERR_NONE) {
    Serial.printf("[ LoRa ] SX1262 ready  %.3f MHz  SF%d  BW%.0f  CR4/%d\n",
      LORA_FREQ, LORA_SF, LORA_BW, LORA_CR);
  } else {
    Serial.printf("[ LoRa ] INIT FAILED — code %d\n", state);
    for (int i = 0; i < 10; i++) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(120);
    }
  }

  radio.startReceive();
  Serial.println("[ LoRa ] Listening...");

  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[ WiFi ] AP \"%s\"  IP %s\n", AP_SSID, ip.toString().c_str());

  server.on("/",         HTTP_GET, handleRoot);
  server.on("/data",     HTTP_GET, handleData);
  server.on("/scan",     HTTP_GET, handleScan);
  server.on("/scan/start", HTTP_GET, handleScanStart);
  server.on("/scan/stop",  HTTP_GET, handleScanStop);
  server.on("/scan/data",  HTTP_GET, handleScan);
  server.onNotFound(handleNotFound);
  server.begin();
  
  Serial.println("[ HTTP ] Server started  →  http://192.168.4.1");
  Serial.println();
  Serial.println("══════════════════════════════════════════════");
  Serial.println("  Snow Receiver + Scanner");
  Serial.println("  WiFi: captwur neige / 1234");
  Serial.println("  URL : http://192.168.4.1");
  Serial.println("══════════════════════════════════════════════");
}

// ═══════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════

void loop() {
  server.handleClient();
  
  if (scanning) {
    scannerLoop();
  } else {
    checkLoRa();
  }
}

// ═══════════════════════════════════════════════════
//  LORA RX POLL
// ═══════════════════════════════════════════════════

void checkLoRa() {
  if (radio.available()) {
    String payload;
    int state = radio.readData(payload);

    if (state == RADIOLIB_ERR_NONE) {
      pkt.rssi = (int)radio.getRSSI();
      pkt.snr  = radio.getSNR();

      Serial.printf("[ LoRa ] RX  RSSI=%d dBm  SNR=%.1f dB  len=%d\n",
        pkt.rssi, pkt.snr, payload.length());
      Serial.printf("         %s\n", payload.c_str());

      parsePayload(payload);

      digitalWrite(LED_PIN, HIGH);
      delay(40);
      digitalWrite(LED_PIN, LOW);

      radio.startReceive();

    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      Serial.println("[ LoRa ] CRC error — packet dropped");
      radio.startReceive();
    } else {
      Serial.printf("[ LoRa ] Read error — code %d\n", state);
      radio.startReceive();
    }
  }
}
