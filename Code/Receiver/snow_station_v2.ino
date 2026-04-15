/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║       SNOW STATION — LoRa Receiver + WiFi Dashboard         ║
 * ║       Board  : Heltec WiFi LoRa 32 V3  (SX1262 onboard)    ║
 * ║       Paired : E220-900T22D snow station transmitter        ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  Required libraries (Arduino Library Manager):
 *    • RadioLib          — Jan Gromeš            (LoRa SX1262)
 *
 *  Board package:
 *    • "Heltec ESP32 Series Dev-boards" by Heltec
 *      OR Espressif esp32 + select "Heltec WiFi LoRa 32(V3)"
 *
 *  Connect: WiFi  →  captwur neige  /  1234
 *  Open   : http://192.168.4.1
 *
 *  CSV payload from transmitter (E220 transparent mode):
 *    lat, lon, alt, speed, depth_cm, sats, hdop, date, time
 *
 *  LoRa params matched to E220-900T22D factory defaults:
 *    Frequency  : 850.125 MHz  (ch 0 for 900T22D)
 *                 ← Change to 868.125 if you have an 868T22D variant
 *    Bandwidth  : 125 kHz
 *    SF         : 9   (≈ 2.4 kbps air rate)
 *    CR         : 4/5
 *    Preamble   : 10 symbols
 *    Sync word  : 0x12  (private / non-LoRaWAN)
 */

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <WebServer.h>

// ═══════════════════════════════════════════════════════
//  HELTEC LORA 32 V3 — ONBOARD SX1262 PINS
// ═══════════════════════════════════════════════════════
#define LORA_SCK      9
#define LORA_MISO    11
#define LORA_MOSI    10
#define LORA_CS       8
#define LORA_RST     12
#define LORA_DIO1    14
#define LORA_BUSY    13

#define VEXT_PIN     36   // LOW = external VCC on  (powers antenna switch)
#define LED_PIN      35   // onboard LED

// ═══════════════════════════════════════════════════════
//  WIFI ACCESS POINT
// ═══════════════════════════════════════════════════════
#define AP_SSID  "captwur neige"
#define AP_PASS  "1234"

// ═══════════════════════════════════════════════════════
//  LORA PARAMETERS — MUST MATCH E220-900T22D DEFAULTS
// ═══════════════════════════════════════════════════════
#define LORA_FREQ       850.125   // MHz  (E220-900T22D ch.0)
#define LORA_BW         125.0     // kHz
#define LORA_SF         9
#define LORA_CR         5         // denominator  → 4/5
#define LORA_PREAMBLE   10
#define LORA_SYNC_WORD  0x12      // private LoRa (not LoRaWAN)
#define LORA_POWER_DBM  22        // rx power setting irrelevant but kept sane

// ═══════════════════════════════════════════════════════
//  OBJECTS
// ═══════════════════════════════════════════════════════
SPIClass  loraSPI(FSPI);
SX1262    radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);
WebServer server(80);

// ═══════════════════════════════════════════════════════
//  RECEIVED DATA STRUCTURE
// ═══════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════
//  FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════════
void checkLoRa();
void parsePayload(const String& payload);
void handleRoot();
void handleData();
void handleNotFound();

// ═══════════════════════════════════════════════════════
//  HTML DASHBOARD  (stored in flash)
// ═══════════════════════════════════════════════════════
static const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Station Neige ❄</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;600;700&display=swap');

  :root {
    --bg:          #08080a;
    --bg2:         #111114;
    --surface:     rgba(255,255,255,0.042);
    --surface2:    rgba(255,255,255,0.07);
    --border:      rgba(255,255,255,0.08);
    --border2:     rgba(255,255,255,0.14);
    --text:        #f0f0f5;
    --text-dim:    rgba(240,240,245,0.45);
    --text-mid:    rgba(240,240,245,0.7);
    --blue:        #0a84ff;
    --blue-soft:   rgba(10,132,255,0.18);
    --cyan:        #64d2ff;
    --green:       #30d158;
    --green-soft:  rgba(48,209,88,0.15);
    --yellow:      #ffd60a;
    --yellow-soft: rgba(255,214,10,0.15);
    --red:         #ff453a;
    --red-soft:    rgba(255,69,58,0.15);
    --snow:        #c8e6fa;
    --r:           20px;
  }

  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  html, body {
    height: 100%;
    background: var(--bg);
    color: var(--text);
    font-family: 'Outfit', -apple-system, BlinkMacSystemFont, 'Helvetica Neue', sans-serif;
    font-size: 15px;
    -webkit-font-smoothing: antialiased;
    overflow-x: hidden;
  }

  /* Subtle noise grain texture */
  body::before {
    content: '';
    position: fixed;
    inset: 0;
    background-image: url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.75' numOctaves='4' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.03'/%3E%3C/svg%3E");
    background-size: 200px;
    pointer-events: none;
    z-index: 0;
  }

  /* Ambient glow */
  body::after {
    content: '';
    position: fixed;
    top: -30vh;
    left: 50%;
    transform: translateX(-50%);
    width: 70vw;
    height: 60vh;
    background: radial-gradient(ellipse, rgba(10,132,255,0.07) 0%, transparent 70%);
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

  /* ── TITLE BAR ───────────────────────────────────── */
  .titlebar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 18px 28px;
    border-bottom: 1px solid var(--border);
    background: rgba(8,8,10,0.85);
    backdrop-filter: blur(24px);
    -webkit-backdrop-filter: blur(24px);
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
    line-height: 1;
    filter: drop-shadow(0 0 8px rgba(100,210,255,0.6));
    animation: spin 12s linear infinite;
  }

  @keyframes spin { to { transform: rotate(360deg); } }

  .titlebar h1 {
    font-size: 17px;
    font-weight: 600;
    letter-spacing: -0.3px;
    color: var(--text);
  }

  .titlebar-sub {
    font-size: 12px;
    color: var(--text-dim);
    font-weight: 400;
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
    transition: all 0.4s ease;
  }

  .status-badge.live {
    border-color: rgba(48,209,88,0.4);
    color: var(--green);
    background: var(--green-soft);
  }

  .status-dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--text-dim);
    transition: background 0.4s;
  }

  .status-badge.live .status-dot {
    background: var(--green);
    box-shadow: 0 0 6px var(--green);
    animation: pulse 2s infinite;
  }

  @keyframes pulse {
    0%,100% { opacity: 1; }
    50% { opacity: 0.4; }
  }

  /* ── MAIN CONTENT ────────────────────────────────── */
  .main {
    flex: 1;
    padding: 28px;
    max-width: 1100px;
    width: 100%;
    margin: 0 auto;
  }

  /* ── HERO — SNOW DEPTH ───────────────────────────── */
  .hero {
    background: linear-gradient(135deg,
      rgba(10,132,255,0.12) 0%,
      rgba(100,210,255,0.06) 50%,
      rgba(8,8,10,0) 100%);
    border: 1px solid rgba(10,132,255,0.25);
    border-radius: var(--r);
    padding: 36px 40px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 24px;
    margin-bottom: 24px;
    position: relative;
    overflow: hidden;
  }

  .hero::after {
    content: '❄';
    position: absolute;
    right: -20px;
    bottom: -30px;
    font-size: 160px;
    opacity: 0.04;
    pointer-events: none;
  }

  .hero-label {
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 1.5px;
    text-transform: uppercase;
    color: var(--blue);
    margin-bottom: 10px;
  }

  .hero-depth {
    font-size: 72px;
    font-weight: 300;
    letter-spacing: -4px;
    line-height: 1;
    color: var(--text);
  }

  .hero-depth span {
    font-size: 28px;
    font-weight: 400;
    letter-spacing: -1px;
    color: var(--text-mid);
    margin-left: 4px;
  }

  .hero-depth-m {
    font-size: 20px;
    color: var(--text-dim);
    margin-top: 8px;
    font-weight: 400;
  }

  .hero-meta {
    text-align: right;
  }

  .hero-meta-row {
    font-size: 13px;
    color: var(--text-dim);
    margin-bottom: 6px;
  }

  .hero-meta-row strong {
    color: var(--text-mid);
    font-weight: 500;
  }

  .hero-count {
    font-size: 11px;
    color: var(--blue);
    font-weight: 600;
    letter-spacing: 0.5px;
  }

  /* ── GRID ────────────────────────────────────────── */
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
    gap: 16px;
  }

  /* ── CARD ────────────────────────────────────────── */
  .card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--r);
    padding: 22px 24px;
    transition: border-color 0.3s, transform 0.2s;
    position: relative;
    overflow: hidden;
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

  .card-icon {
    font-size: 14px;
    opacity: 0.8;
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
    letter-spacing: 0px;
    margin-left: 3px;
  }

  .card-sub {
    font-size: 12px;
    color: var(--text-dim);
    margin-top: 10px;
  }

  /* card accent stripe */
  .card::before {
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0;
    height: 2px;
    border-radius: 2px 2px 0 0;
    background: transparent;
    transition: background 0.3s;
  }

  .card.accent-blue::before   { background: linear-gradient(90deg, var(--blue), transparent); }
  .card.accent-green::before  { background: linear-gradient(90deg, var(--green), transparent); }
  .card.accent-cyan::before   { background: linear-gradient(90deg, var(--cyan), transparent); }
  .card.accent-yellow::before { background: linear-gradient(90deg, var(--yellow), transparent); }

  /* ── COORDINATES CARD  (wide) ────────────────────── */
  .card-wide {
    grid-column: span 2;
  }

  .coords-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 20px;
    margin-top: 4px;
  }

  .coord-block {}
  .coord-block .coord-label {
    font-size: 11px;
    color: var(--text-dim);
    text-transform: uppercase;
    letter-spacing: 1px;
    margin-bottom: 6px;
  }
  .coord-block .coord-val {
    font-size: 24px;
    font-weight: 400;
    letter-spacing: -0.5px;
    font-variant-numeric: tabular-nums;
  }

  /* ── SIGNAL CARD ─────────────────────────────────── */
  .signal-bar-wrap {
    margin-top: 14px;
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .signal-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
  }

  .signal-row-label {
    font-size: 11px;
    color: var(--text-dim);
    width: 50px;
    text-transform: uppercase;
    letter-spacing: 0.8px;
  }

  .signal-bar-track {
    flex: 1;
    height: 5px;
    background: var(--surface2);
    border-radius: 3px;
    overflow: hidden;
  }

  .signal-bar-fill {
    height: 100%;
    border-radius: 3px;
    transition: width 0.6s cubic-bezier(0.4,0,0.2,1);
  }

  .signal-row-val {
    font-size: 12px;
    color: var(--text-mid);
    font-variant-numeric: tabular-nums;
    width: 60px;
    text-align: right;
  }

  /* ── TIMESTAMP / LAST RX ─────────────────────────── */
  .last-rx-bar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 12px 20px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    margin-bottom: 24px;
    font-size: 13px;
    color: var(--text-dim);
  }

  .last-rx-bar strong {
    color: var(--text-mid);
    font-weight: 500;
  }

  .rx-age {
    font-size: 12px;
    color: var(--text-dim);
  }

  .rx-age.fresh { color: var(--green); }
  .rx-age.stale { color: var(--yellow); }
  .rx-age.old   { color: var(--red); }

  /* ── NO DATA OVERLAY ─────────────────────────────── */
  .no-data {
    text-align: center;
    padding: 80px 20px;
    color: var(--text-dim);
  }

  .no-data-icon {
    font-size: 56px;
    margin-bottom: 16px;
    opacity: 0.3;
    animation: float 3s ease-in-out infinite;
  }

  @keyframes float {
    0%,100% { transform: translateY(0); }
    50% { transform: translateY(-8px); }
  }

  .no-data h2 {
    font-size: 18px;
    font-weight: 500;
    color: var(--text-mid);
    margin-bottom: 8px;
  }

  .no-data p {
    font-size: 14px;
    color: var(--text-dim);
  }

  /* ── RAW PACKET ──────────────────────────────────── */
  .raw-section {
    margin-top: 24px;
  }

  .raw-label {
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 1.4px;
    text-transform: uppercase;
    color: var(--text-dim);
    margin-bottom: 10px;
  }

  .raw-box {
    background: rgba(0,0,0,0.4);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 14px 18px;
    font-family: 'SF Mono', 'Fira Code', 'Cascadia Code', monospace;
    font-size: 12px;
    color: var(--cyan);
    word-break: break-all;
    letter-spacing: 0.3px;
  }

  /* ── FOOTER ──────────────────────────────────────── */
  .footer {
    text-align: center;
    padding: 20px;
    font-size: 11px;
    color: var(--text-dim);
    border-top: 1px solid var(--border);
    letter-spacing: 0.3px;
  }

  /* ── RESPONSIVE ──────────────────────────────────── */
  @media (max-width: 640px) {
    .main { padding: 16px; }
    .hero { flex-direction: column; align-items: flex-start; }
    .hero-depth { font-size: 56px; }
    .hero-meta { text-align: left; }
    .card-wide { grid-column: span 1; }
    .coords-grid { grid-template-columns: 1fr; }
    .titlebar { padding: 14px 16px; }
    .titlebar h1 { font-size: 15px; }
  }
</style>
</head>
<body>
<div class="app">

  <!-- TITLE BAR -->
  <header class="titlebar">
    <div class="titlebar-left">
      <div class="snowflake-icon">❄</div>
      <div>
        <div class="titlebar h1" style="font-size:17px;font-weight:600;">Station Neige</div>
        <div class="titlebar-sub">LoRa · GPS · Ultrason</div>
      </div>
    </div>
    <div class="status-badge" id="statusBadge">
      <div class="status-dot" id="statusDot"></div>
      <span id="statusText">En attente…</span>
    </div>
  </header>

  <div class="main" id="mainContent">
    <!-- injected by JS -->
    <div class="no-data" id="noData">
      <div class="no-data-icon">📡</div>
      <h2>En attente de données LoRa</h2>
      <p>Démarrez la station de neige et attendez la première transmission.</p>
    </div>
  </div>

  <footer class="footer">
    Heltec LoRa 32 V3 · SX1262 · 850.125 MHz · SF9 BW125 · Refresh 2 s
  </footer>

</div>

<script>
const fmt1 = v => isNaN(v) ? '---' : Number(v).toFixed(1);
const fmt2 = v => isNaN(v) ? '---' : Number(v).toFixed(2);
const fmt5 = v => isNaN(v) ? '---' : Number(v).toFixed(5);
let firstData = false;

function rssiColor(rssi) {
  if (rssi > -80)  return 'var(--green)';
  if (rssi > -100) return 'var(--yellow)';
  return 'var(--red)';
}

function rssiPct(rssi) {
  // map -130..-40 → 0..100
  const pct = Math.max(0, Math.min(100, ((rssi + 130) / 90) * 100));
  return pct.toFixed(0);
}

function snrPct(snr) {
  // SX1262 SNR range roughly -20..+10 dB → 0..100
  const pct = Math.max(0, Math.min(100, ((snr + 20) / 30) * 100));
  return pct.toFixed(0);
}

function ageClass(ms) {
  if (ms < 10000) return 'fresh';
  if (ms < 30000) return 'stale';
  return 'old';
}

function ageStr(ms) {
  if (ms < 5000)  return 'À l\'instant';
  if (ms < 60000) return `Il y a ${Math.floor(ms/1000)} s`;
  return `Il y a ${Math.floor(ms/60000)} min`;
}

function render(d) {
  if (!d.valid) return;

  const age = d.last_rx_ms > 0 ? (Date.now() - window._lastUpdate + d.age_ms) : 9999999;
  const depthCm = parseFloat(d.depth);
  const depthM  = depthCm / 100;
  const noDepth = depthCm < 0;

  document.getElementById('noData').style.display = 'none';

  // Update status badge
  const badge = document.getElementById('statusBadge');
  badge.className = 'status-badge live';
  document.getElementById('statusText').textContent = 'En direct';

  const html = `
    <!-- LAST RX BAR -->
    <div class="last-rx-bar">
      <span>Dernière réception : <strong>${d.date} · ${d.time_s}</strong></span>
      <span class="rx-age ${ageClass(d.age_ms)}">${ageStr(d.age_ms)}</span>
    </div>

    <!-- HERO CARD -->
    <div class="hero">
      <div>
        <div class="hero-label">Hauteur de neige</div>
        <div class="hero-depth">
          ${noDepth ? '---' : fmt1(depthCm)}<span>${noDepth ? '' : 'cm'}</span>
        </div>
        ${noDepth ? '' : `<div class="hero-depth-m">${fmt2(depthM)} m</div>`}
      </div>
      <div class="hero-meta">
        <div class="hero-meta-row">📦 Paquets reçus</div>
        <div class="hero-count"># ${d.count}</div>
        <br>
        <div class="hero-meta-row">📡 RSSI <strong>${d.rssi} dBm</strong></div>
        <div class="hero-meta-row">〰 SNR  <strong>${fmt1(d.snr)} dB</strong></div>
      </div>
    </div>

    <!-- GRID -->
    <div class="grid">

      <!-- COORDINATES -->
      <div class="card card-wide accent-blue">
        <div class="card-label"><span class="card-icon">🌐</span> Coordonnées GPS</div>
        <div class="coords-grid">
          <div class="coord-block">
            <div class="coord-label">Latitude</div>
            <div class="coord-val">${fmt5(d.lat)} °N</div>
          </div>
          <div class="coord-block">
            <div class="coord-label">Longitude</div>
            <div class="coord-val">${fmt5(d.lon)} °E</div>
          </div>
        </div>
        <div class="card-sub" style="margin-top:14px;">
          <a href="https://maps.google.com/?q=${d.lat},${d.lon}" target="_blank"
             style="color:var(--blue);text-decoration:none;font-size:12px;">
            Ouvrir dans Maps →
          </a>
        </div>
      </div>

      <!-- ALTITUDE -->
      <div class="card accent-cyan">
        <div class="card-label"><span class="card-icon">⛰</span> Altitude</div>
        <div class="card-value">${fmt1(d.alt)}<span class="unit">m</span></div>
        <div class="card-sub">Au-dessus du niveau de la mer</div>
      </div>

      <!-- SPEED -->
      <div class="card accent-yellow">
        <div class="card-label"><span class="card-icon">💨</span> Vitesse</div>
        <div class="card-value">${fmt1(d.speed)}<span class="unit">km/h</span></div>
        <div class="card-sub">Déplacement de la station</div>
      </div>

      <!-- SATELLITES -->
      <div class="card accent-green">
        <div class="card-label"><span class="card-icon">🛰</span> Satellites GPS</div>
        <div class="card-value">${d.sats}<span class="unit">sat</span></div>
        <div class="card-sub">HDOP : ${fmt2(d.hdop)}</div>
      </div>

      <!-- SIGNAL QUALITY -->
      <div class="card">
        <div class="card-label"><span class="card-icon">📶</span> Qualité du signal</div>
        <div class="signal-bar-wrap">
          <div class="signal-row">
            <span class="signal-row-label">RSSI</span>
            <div class="signal-bar-track">
              <div class="signal-bar-fill" id="rssiBar"
                style="width:${rssiPct(d.rssi)}%;background:${rssiColor(d.rssi)};"></div>
            </div>
            <span class="signal-row-val">${d.rssi} dBm</span>
          </div>
          <div class="signal-row">
            <span class="signal-row-label">SNR</span>
            <div class="signal-bar-track">
              <div class="signal-bar-fill"
                style="width:${snrPct(d.snr)}%;background:var(--blue);"></div>
            </div>
            <span class="signal-row-val">${fmt1(d.snr)} dB</span>
          </div>
        </div>
      </div>

      <!-- DATE / TIME -->
      <div class="card">
        <div class="card-label"><span class="card-icon">🕐</span> Horodatage</div>
        <div class="card-value" style="font-size:22px;letter-spacing:-0.5px;">${d.time_s}</div>
        <div class="card-sub">${d.date} — UTC+2</div>
      </div>

    </div>

    <!-- RAW PACKET -->
    <div class="raw-section">
      <div class="raw-label">Trame brute LoRa</div>
      <div class="raw-box">${d.raw}</div>
    </div>
  `;

  document.getElementById('mainContent').innerHTML = html +
    `<div id="noData" style="display:none"></div>`;
}

async function fetchData() {
  try {
    const r = await fetch('/data');
    const d = await r.json();
    window._lastUpdate = Date.now();
    render(d);
  } catch(e) {
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

// ═══════════════════════════════════════════════════════
//  PARSE CSV PAYLOAD
//  Format: lat,lon,alt,speed,depth,sats,hdop,date,time
// ═══════════════════════════════════════════════════════
void parsePayload(const String& payload) {
  // strtok-safe copy
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
  pkt.valid   = (field >= 7);
  pkt.lastRxMs = millis();
  pkt.count++;
}

// ═══════════════════════════════════════════════════════
//  WEB HANDLERS
// ═══════════════════════════════════════════════════════
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

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ═══════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);

  // Power on the antenna / Vext rail
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);   // LOW = power ON for Heltec V3 Vext
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // ── SPI + SX1262 ──────────────────────────────────────
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
    Serial.printf("[ LoRa ] INIT FAILED — code %d  (check SPI wiring)\n", state);
    // Blink fast to signal error but don't halt — WiFi still starts
    for (int i = 0; i < 10; i++) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(120);
    }
  }

  // Put radio into continuous receive mode
  state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE)
    Serial.printf("[ LoRa ] startReceive failed — code %d\n", state);
  else
    Serial.println("[ LoRa ] Listening...");

  // ── WiFi AP ───────────────────────────────────────────
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[ WiFi ] AP \"%s\"  IP %s\n", AP_SSID, ip.toString().c_str());

  // ── Web server routes ─────────────────────────────────
  server.on("/",     HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[ HTTP ] Server started  →  http://192.168.4.1");

  Serial.println();
  Serial.println("══════════════════════════════════════════════");
  Serial.println("  Snow Receiver — connect to WiFi:");
  Serial.printf ("  SSID : %s\n", AP_SSID);
  Serial.println("  PASS : 1234");
  Serial.println("  URL  : http://192.168.4.1");
  Serial.println("══════════════════════════════════════════════");
  Serial.println();
}

// ═══════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════
void loop() {
  server.handleClient();
  checkLoRa();
}

// ═══════════════════════════════════════════════════════
//  LORA RX POLL  (no IRQ — polling for simplicity)
// ═══════════════════════════════════════════════════════
void checkLoRa() {
  // RadioLib: check if a packet has arrived
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

      // Blink LED to confirm
      digitalWrite(LED_PIN, HIGH);
      delay(40);
      digitalWrite(LED_PIN, LOW);

      // Re-arm receiver
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
