/*
 * ╔══════════════════════════════════════════════════════════════════╗
 * ║   SNOW STATION — LoRa Transmitter v3  (WiFi Dashboard)         ║
 * ║   Board  : ESP32                                                ║
 * ║   WiFi   : AP "snow sensor transmitter" / 1234567890           ║
 * ║   URL    : http://192.168.4.1                                   ║
 * ╚══════════════════════════════════════════════════════════════════╝
 *
 *  Removed  : LCD screen (LiquidCrystal_I2C) + physical button
 *  Added    : WiFi Access Point + 5-panel web dashboard
 *
 *  Dashboard panels
 *    1. Overview   — snow depth hero, GPS, LoRa TX, SD status
 *    2. GPS Map    — Leaflet / OSM live marker
 *    3. LoRa TX    — transmission stats, last payload
 *    4. SD Logger  — file status, row count, CSV download
 *    5. System     — RAM, Flash, CPU, uptime, WiFi AP info
 *
 *  Libraries required (same as before, minus LiquidCrystal_I2C):
 *    TinyGPSPlus, LoRa_E220, SoftwareSerial (core), WiFi (core), WebServer (core)
 */

#include <HardwareSerial.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <SD.h>
#include <LoRa_E220.h>
#include <WiFi.h>
#include <WebServer.h>

// ═══════════════════════════════════════════════════
//  PIN DEFINITIONS
// ═══════════════════════════════════════════════════

#define GPS_RX_PIN      16
#define GPS_TX_PIN      17

#define US_RX_PIN        4

#define LORA_RX_PIN     13
#define LORA_TX_PIN     14
#define LORA_M0_PIN     26
#define LORA_M1_PIN     27
#define LORA_AUX_PIN    32

#define SD_MOSI_PIN     23
#define SD_MISO_PIN     19
#define SD_SCK_PIN      18
#define SD_CS_PIN        5

// ═══════════════════════════════════════════════════
//  CONFIG
// ═══════════════════════════════════════════════════

#define LOG_FILE          "/snowlog.csv"
#define LOG_INTERVAL_MS    2000
#define TIMEZONE_OFFSET_H  2
#define WIFI_AP_SSID       "snow sensor transmitter"
#define WIFI_AP_PASS       "1234567890"
#define MAX_HIST           60

// ═══════════════════════════════════════════════════
//  OBJECTS
// ═══════════════════════════════════════════════════

TinyGPSPlus    gps;
HardwareSerial gpsSerial(2);
SoftwareSerial usSerial(US_RX_PIN, -1);
HardwareSerial loraSerial(1);
LoRa_E220      lora(&loraSerial, LORA_AUX_PIN, LORA_M0_PIN, LORA_M1_PIN);
WebServer      server(80);

// ═══════════════════════════════════════════════════
//  STATE
// ═══════════════════════════════════════════════════

float    distCm       = -1;
bool     sdReady      = false;
bool     loraReady    = false;
uint8_t  usBuf[4];
uint8_t  usIdx        = 0;
uint32_t loraLastTxMs = 0;
uint32_t loraCount    = 0;
uint32_t sdRowCount   = 0;
uint32_t lastReadMs   = 0;        // time of last valid ultrasonic reading
char     lastPayload[140] = "";   // last LoRa payload string

// in-memory circular history (60 entries, mirrors receiver pattern)
struct HistEntry {
  float  depth, lat, lon, alt, speed;
  int    sats;
  char   date[12];
  char   time_s[10];
};
HistEntry g_hist[MAX_HIST];
int g_histHead  = 0;
int g_histCount = 0;

// ═══════════════════════════════════════════════════
//  TIME HELPER  (unchanged from original)
// ═══════════════════════════════════════════════════

void localTime(int &h, int &m, int &s, int &d, int &mo, int &y) {
  h  = gps.time.isValid() ? (int)gps.time.hour()   : 0;
  m  = gps.time.isValid() ? (int)gps.time.minute() : 0;
  s  = gps.time.isValid() ? (int)gps.time.second() : 0;
  d  = gps.date.isValid() ? (int)gps.date.day()    : 1;
  mo = gps.date.isValid() ? (int)gps.date.month()  : 1;
  y  = gps.date.isValid() ? (int)gps.date.year()   : 2000;

  if (!gps.time.isValid() || !gps.date.isValid()) return;

  h += TIMEZONE_OFFSET_H;

  if (h >= 24) {
    h -= 24; d += 1;
    int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if ((y%4==0&&y%100!=0)||(y%400==0)) dim[1]=29;
    if (d > dim[mo-1]) { d=1; mo++; if (mo>12){mo=1;y++;} }
  }
  if (h < 0) {
    h += 24; d--;
    if (d < 1) {
      mo--;
      if (mo<1){mo=12;y--;}
      int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
      if ((y%4==0&&y%100!=0)||(y%400==0)) dim[1]=29;
      d = dim[mo-1];
    }
  }
}

// ═══════════════════════════════════════════════════
//  EMBEDDED WEB APPLICATION  (flash / PROGMEM)
// ═══════════════════════════════════════════════════

static const char INDEX_HTML[] PROGMEM = R"HTMLEND(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Snow Sensor TX ❄</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<style>
@import url('https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;600;700&display=swap');

:root {
  --bg:          #08080a;
  --bg2:         #0d0d10;
  --surface:     rgba(255,255,255,0.042);
  --surface2:    rgba(255,255,255,0.07);
  --border:      rgba(255,255,255,0.08);
  --border2:     rgba(255,255,255,0.14);
  --text:        #f0f0f5;
  --text-dim:    rgba(240,240,245,0.40);
  --text-mid:    rgba(240,240,245,0.68);
  --blue:        #0a84ff;
  --blue-soft:   rgba(10,132,255,0.15);
  --cyan:        #64d2ff;
  --green:       #30d158;
  --green-soft:  rgba(48,209,88,0.14);
  --yellow:      #ffd60a;
  --yellow-soft: rgba(255,214,10,0.13);
  --red:         #ff453a;
  --red-soft:    rgba(255,69,58,0.13);
  --orange:      #ff9f0a;
  --orange-soft: rgba(255,159,10,0.13);
  --sidebar-w:   220px;
  --r:           18px;
}

*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%;background:var(--bg);color:var(--text);
  font-family:'Outfit',-apple-system,BlinkMacSystemFont,'Helvetica Neue',sans-serif;
  font-size:15px;-webkit-font-smoothing:antialiased;overflow:hidden}

body::before{content:'';position:fixed;inset:0;
  background-image:url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.75' numOctaves='4' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.025'/%3E%3C/svg%3E");
  background-size:200px;pointer-events:none;z-index:0}

.layout{display:flex;height:100vh;position:relative;z-index:1}

.sidebar{
  width:var(--sidebar-w);flex-shrink:0;
  background:rgba(10,10,13,0.95);border-right:1px solid var(--border);
  display:flex;flex-direction:column;
  backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);z-index:10}

.sidebar-logo{
  padding:22px 20px 18px;border-bottom:1px solid var(--border);
  display:flex;align-items:center;gap:12px}
.logo-icon{font-size:22px;line-height:1;
  filter:drop-shadow(0 0 8px rgba(100,210,255,0.55));
  animation:spinSlow 14s linear infinite}
@keyframes spinSlow{to{transform:rotate(360deg)}}
.logo-title{font-size:15px;font-weight:600;letter-spacing:-0.2px}
.logo-sub{font-size:10px;color:var(--text-dim);letter-spacing:0.8px;text-transform:uppercase;margin-top:1px}

.nav{flex:1;padding:12px 10px;display:flex;flex-direction:column;gap:4px}
.nav-item{
  display:flex;align-items:center;gap:12px;padding:10px 12px;border-radius:12px;
  cursor:pointer;transition:all 0.18s ease;color:var(--text-dim);
  font-size:14px;font-weight:500;border:1px solid transparent;user-select:none}
.nav-item:hover{background:var(--surface2);color:var(--text-mid)}
.nav-item.active{background:var(--blue-soft);border-color:rgba(10,132,255,0.25);color:var(--blue)}
.nav-icon{font-size:17px;width:22px;text-align:center;flex-shrink:0}
.nav-label{flex:1}

.sidebar-footer{
  padding:16px 14px;border-top:1px solid var(--border);
  font-size:11px;color:var(--text-dim)}
.sidebar-footer .sf-row{display:flex;justify-content:space-between;margin-bottom:4px}
.sf-val{color:var(--text-mid);font-weight:500}

.content{flex:1;display:flex;flex-direction:column;overflow:hidden}
.topbar{
  display:flex;align-items:center;justify-content:space-between;
  padding:16px 28px;border-bottom:1px solid var(--border);
  background:rgba(8,8,10,0.88);backdrop-filter:blur(20px);
  -webkit-backdrop-filter:blur(20px);flex-shrink:0}
.topbar-title{font-size:16px;font-weight:600;letter-spacing:-0.2px}
.topbar-sub{font-size:11px;color:var(--text-dim);margin-top:2px;text-transform:uppercase;letter-spacing:0.7px}

.live-badge{
  display:flex;align-items:center;gap:8px;padding:6px 14px;
  background:var(--surface);border:1px solid var(--border);
  border-radius:100px;font-size:12px;font-weight:500;color:var(--text-mid);transition:all 0.4s}
.live-badge.on{background:var(--green-soft);border-color:rgba(48,209,88,0.35);color:var(--green)}
.live-dot{width:7px;height:7px;border-radius:50%;background:var(--text-dim);transition:all 0.4s}
.live-badge.on .live-dot{background:var(--green);box-shadow:0 0 6px var(--green);animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.35}}

.panels{flex:1;overflow-y:auto;overflow-x:hidden;scroll-behavior:smooth}
.panels::-webkit-scrollbar{width:5px}
.panels::-webkit-scrollbar-track{background:transparent}
.panels::-webkit-scrollbar-thumb{background:var(--border2);border-radius:3px}

.panel{display:none;padding:26px 28px;animation:fadeIn 0.25s ease}
.panel.active{display:block}
@keyframes fadeIn{from{opacity:0;transform:translateY(6px)}to{opacity:1;transform:translateY(0)}}

.section-title{
  font-size:11px;font-weight:600;letter-spacing:1.5px;text-transform:uppercase;
  color:var(--text-dim);margin-bottom:18px;display:flex;align-items:center;gap:8px}
.section-title::after{content:'';flex:1;height:1px;background:var(--border)}

.card{
  background:var(--surface);border:1px solid var(--border);border-radius:var(--r);
  padding:20px 22px;transition:border-color 0.25s,transform 0.18s;
  position:relative;overflow:hidden}
.card:hover{border-color:var(--border2);transform:translateY(-1px)}
.card::before{content:'';position:absolute;top:0;left:0;right:0;height:2px;
  border-radius:2px 2px 0 0;background:transparent;transition:background 0.3s}
.card.c-blue::before  {background:linear-gradient(90deg,var(--blue),transparent)}
.card.c-green::before {background:linear-gradient(90deg,var(--green),transparent)}
.card.c-cyan::before  {background:linear-gradient(90deg,var(--cyan),transparent)}
.card.c-yellow::before{background:linear-gradient(90deg,var(--yellow),transparent)}
.card.c-orange::before{background:linear-gradient(90deg,var(--orange),transparent)}
.card.c-red::before   {background:linear-gradient(90deg,var(--red),transparent)}

.card-label{font-size:11px;font-weight:600;letter-spacing:1.3px;text-transform:uppercase;
  color:var(--text-dim);margin-bottom:14px;display:flex;align-items:center;gap:6px}
.card-icon{font-size:14px}
.card-value{font-size:30px;font-weight:300;letter-spacing:-1.2px;line-height:1;margin-bottom:4px}
.card-value .unit{font-size:15px;font-weight:400;color:var(--text-dim);margin-left:3px}
.card-sub{font-size:12px;color:var(--text-dim);margin-top:8px}

.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:14px}
.col-2{grid-column:span 2}

.hero{
  background:linear-gradient(135deg,rgba(10,132,255,0.11) 0%,rgba(100,210,255,0.05) 50%,transparent 100%);
  border:1px solid rgba(10,132,255,0.22);border-radius:var(--r);
  padding:32px 36px;display:flex;align-items:center;justify-content:space-between;
  gap:20px;margin-bottom:22px;position:relative;overflow:hidden}
.hero::after{content:'❄';position:absolute;right:-15px;bottom:-25px;
  font-size:150px;opacity:0.04;pointer-events:none}
.hero-lbl{font-size:11px;font-weight:600;letter-spacing:1.5px;text-transform:uppercase;
  color:var(--blue);margin-bottom:8px}
.hero-val{font-size:68px;font-weight:300;letter-spacing:-4px;line-height:1}
.hero-val span{font-size:26px;font-weight:400;color:var(--text-mid);letter-spacing:0;margin-left:3px}
.hero-sub{font-size:18px;color:var(--text-dim);margin-top:6px;font-weight:400}
.hero-meta{text-align:right}
.hero-meta-row{font-size:13px;color:var(--text-dim);margin-bottom:5px}
.hero-meta-row strong{color:var(--text-mid);font-weight:500}

.last-rx{
  display:flex;align-items:center;justify-content:space-between;
  padding:10px 18px;background:var(--surface);border:1px solid var(--border);
  border-radius:10px;margin-bottom:20px;font-size:13px;color:var(--text-dim)}
.last-rx strong{color:var(--text-mid);font-weight:500}
.rx-age{font-size:12px}
.rx-age.fresh{color:var(--green)}.rx-age.stale{color:var(--yellow)}.rx-age.old{color:var(--red)}

.coords-grid{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:4px}
.coord-lbl{font-size:10px;color:var(--text-dim);text-transform:uppercase;letter-spacing:1px;margin-bottom:5px}
.coord-val{font-size:22px;font-weight:400;letter-spacing:-.3px;font-variant-numeric:tabular-nums}

.raw-wrap{margin-top:22px}
.raw-lbl{font-size:10px;font-weight:600;letter-spacing:1.4px;text-transform:uppercase;
  color:var(--text-dim);margin-bottom:8px}
.raw-box{background:rgba(0,0,0,0.45);border:1px solid var(--border);border-radius:10px;
  padding:12px 16px;font-family:'SF Mono','Fira Code','Cascadia Code',monospace;
  font-size:11px;color:var(--cyan);word-break:break-all;letter-spacing:.3px}

.no-data{text-align:center;padding:70px 20px;color:var(--text-dim)}
.no-data-icon{font-size:52px;margin-bottom:14px;opacity:.28;animation:float 3s ease-in-out infinite}
@keyframes float{0%,100%{transform:translateY(0)}50%{transform:translateY(-8px)}}
.no-data h2{font-size:17px;font-weight:500;color:var(--text-mid);margin-bottom:7px}
.no-data p{font-size:13px}

.map-wrap{border-radius:var(--r);overflow:hidden;border:1px solid var(--border);margin-bottom:16px}
#map{height:420px;background:#111114}
.map-info{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}
.map-stat{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:14px 16px}
.map-stat-lbl{font-size:10px;color:var(--text-dim);text-transform:uppercase;letter-spacing:1px;margin-bottom:6px}
.map-stat-val{font-size:18px;font-weight:500;letter-spacing:-.5px}

.stat-display{
  background:linear-gradient(135deg,rgba(10,132,255,0.1),transparent);
  border:1px solid rgba(10,132,255,0.2);border-radius:var(--r);
  padding:28px 32px;margin-bottom:24px;text-align:center}
.stat-lbl{font-size:11px;font-weight:600;letter-spacing:1.5px;text-transform:uppercase;
  color:var(--blue);margin-bottom:8px}
.stat-val{font-size:58px;font-weight:300;letter-spacing:-3px;line-height:1}
.stat-val span{font-size:22px;font-weight:400;color:var(--text-dim);margin-left:3px}

.info-row{display:flex;justify-content:space-between;align-items:center;
  padding:10px 0;border-bottom:1px solid var(--border);font-size:13px}
.info-row:last-child{border-bottom:none;padding-bottom:0}
.info-key{color:var(--text-dim)}
.info-val{color:var(--text-mid);font-weight:500;font-variant-numeric:tabular-nums}
.info-val.good{color:var(--green)}.info-val.bad{color:var(--red)}

.btn{display:inline-flex;align-items:center;gap:8px;padding:10px 22px;border-radius:100px;
  border:none;cursor:pointer;font-family:inherit;font-size:13px;font-weight:600;
  letter-spacing:.3px;transition:all .2s;user-select:none}
.btn-primary{background:var(--blue);color:#fff}
.btn-primary:hover{background:#1a8fff;transform:translateY(-1px);box-shadow:0 4px 16px rgba(10,132,255,.35)}
.btn-primary:active{transform:translateY(0)}
.btn-ghost{background:var(--surface2);color:var(--text-mid);border:1px solid var(--border)}
.btn-ghost:hover{border-color:var(--border2);color:var(--text)}

.progress-wrap{margin-bottom:18px}
.progress-label{display:flex;justify-content:space-between;font-size:11px;
  color:var(--text-dim);margin-bottom:6px}
.progress-label strong{color:var(--text-mid)}
.progress-track{height:8px;background:var(--surface2);border-radius:4px;overflow:hidden}
.progress-fill{height:100%;border-radius:4px;transition:width .6s cubic-bezier(.4,0,.2,1)}

.data-top{display:flex;align-items:center;justify-content:space-between;
  flex-wrap:wrap;gap:10px;margin-bottom:18px}
.data-count{font-size:13px;color:var(--text-dim)}
.data-count strong{color:var(--text-mid)}
.table-wrap{overflow-x:auto;border-radius:var(--r);border:1px solid var(--border)}
table{width:100%;border-collapse:collapse;font-size:12px}
thead tr{border-bottom:1px solid var(--border)}
thead th{text-align:left;padding:12px 14px;font-size:10px;font-weight:600;
  letter-spacing:1.2px;text-transform:uppercase;color:var(--text-dim);
  background:rgba(0,0,0,.25);white-space:nowrap}
tbody tr{border-bottom:1px solid rgba(255,255,255,0.04);transition:background .15s}
tbody tr:last-child{border-bottom:none}
tbody tr:hover{background:var(--surface2)}
tbody td{padding:11px 14px;color:var(--text-mid);white-space:nowrap;font-variant-numeric:tabular-nums}
tbody td:first-child{color:var(--text-dim)}
.td-good{color:var(--green)!important}
.td-warn{color:var(--yellow)!important}
.td-bad{color:var(--red)!important}

@media(max-width:700px){
  .sidebar{width:60px}
  .logo-title,.logo-sub,.nav-label,.sidebar-footer{display:none}
  .sidebar-logo{justify-content:center;padding:16px 0}
  .nav{padding:8px 6px}
  .nav-item{justify-content:center;padding:12px 8px}
  .panel{padding:16px}
  .topbar{padding:14px 16px}
  .col-2{grid-column:span 1}
  .hero{flex-direction:column;align-items:flex-start;padding:22px}
  .hero-val{font-size:52px}
  .hero-meta{text-align:left}
  .coords-grid{grid-template-columns:1fr}
  .map-info{grid-template-columns:1fr}
  .data-top{flex-direction:column;align-items:flex-start}
}
</style>
</head>
<body>
<div class="layout">

  <!-- ══ SIDEBAR ════════════════════════════════════════ -->
  <nav class="sidebar">
    <div class="sidebar-logo">
      <div class="logo-icon">❄</div>
      <div>
        <div class="logo-title">Snow TX</div>
        <div class="logo-sub">TX · GPS · SD</div>
      </div>
    </div>

    <div class="nav">
      <div class="nav-item active" onclick="showPanel('dash')" id="nav-dash">
        <span class="nav-icon">📊</span>
        <span class="nav-label">Dashboard</span>
      </div>
      <div class="nav-item" onclick="showPanel('map')" id="nav-map">
        <span class="nav-icon">🗺</span>
        <span class="nav-label">GPS Map</span>
      </div>
      <div class="nav-item" onclick="showPanel('lora')" id="nav-lora">
        <span class="nav-icon">📡</span>
        <span class="nav-label">LoRa TX</span>
      </div>
      <div class="nav-item" onclick="showPanel('sd')" id="nav-sd">
        <span class="nav-icon">💾</span>
        <span class="nav-label">SD Logger</span>
      </div>
      <div class="nav-item" onclick="showPanel('sys')" id="nav-sys">
        <span class="nav-icon">⚙️</span>
        <span class="nav-label">System</span>
      </div>
    </div>

    <div class="sidebar-footer">
      <div class="sf-row"><span>Depth</span><span class="sf-val" id="sf-depth">---</span></div>
      <div class="sf-row"><span>TX Count</span><span class="sf-val" id="sf-txcount">0</span></div>
      <div class="sf-row"><span>SD Rows</span><span class="sf-val" id="sf-sdrows">---</span></div>
    </div>
  </nav>

  <!-- ══ MAIN CONTENT ══════════════════════════════════ -->
  <div class="content">
    <div class="topbar">
      <div>
        <div class="topbar-title" id="page-title">Dashboard</div>
        <div class="topbar-sub" id="page-sub">Live sensor overview</div>
      </div>
      <div class="live-badge" id="liveBadge">
        <div class="live-dot"></div>
        <span id="liveText">Connecting…</span>
      </div>
    </div>

    <div class="panels">

      <!-- ══ PANEL: DASHBOARD ══════════════════════════ -->
      <div class="panel active" id="panel-dash">
        <div id="dash-inner">
          <div class="no-data">
            <div class="no-data-icon">🌨</div>
            <h2>Waiting for sensor data…</h2>
            <p>Ultrasonic sensor initialising. Please wait a moment.</p>
          </div>
        </div>
      </div>

      <!-- ══ PANEL: GPS MAP ═════════════════════════════ -->
      <div class="panel" id="panel-map">
        <div class="section-title">Transmitter GPS Location</div>
        <div class="map-wrap">
          <div id="map"></div>
        </div>
        <div class="map-info" id="mapInfo">
          <div class="map-stat">
            <div class="map-stat-lbl">Latitude</div>
            <div class="map-stat-val" id="map-lat">---</div>
          </div>
          <div class="map-stat">
            <div class="map-stat-lbl">Longitude</div>
            <div class="map-stat-val" id="map-lon">---</div>
          </div>
          <div class="map-stat">
            <div class="map-stat-lbl">Altitude</div>
            <div class="map-stat-val" id="map-alt">--- m</div>
          </div>
        </div>
        <div style="margin-top:12px;font-size:12px;color:var(--text-dim);
          padding:10px 14px;background:var(--surface);border-radius:10px;border:1px solid var(--border)">
          🌐 Map tiles require an internet connection on your device.
          Position updates with every valid GPS fix.
        </div>
      </div>

      <!-- ══ PANEL: LORA TX ═════════════════════════════ -->
      <div class="panel" id="panel-lora">
        <div class="section-title">LoRa Transmission Status</div>
        <div class="stat-display">
          <div class="stat-lbl">Total Packets Transmitted</div>
          <div class="stat-val" id="lora-count-big">---<span>pkts</span></div>
        </div>
        <div class="grid">
          <div class="card c-orange">
            <div class="card-label"><span class="card-icon">📡</span> Module Status</div>
            <div class="card-value" id="lora-module-val" style="font-size:20px;letter-spacing:-.3px">---</div>
            <div class="card-sub">E220-900T22D</div>
          </div>
          <div class="card c-blue">
            <div class="card-label"><span class="card-icon">🕐</span> Last Transmission</div>
            <div class="card-value" id="lora-last-tx" style="font-size:20px;letter-spacing:-.3px">---</div>
            <div class="card-sub">TX interval: 2 s</div>
          </div>
          <div class="card col-2">
            <div class="card-label"><span class="card-icon">📦</span> Last Payload Sent</div>
            <div class="raw-box" id="lora-payload" style="margin-top:8px">
              Waiting for first transmission…
            </div>
            <div class="card-sub" style="margin-top:10px">
              Format: lat, lon, alt, speed, depth, sats, hdop, date, time
            </div>
          </div>
        </div>
        <div style="margin-top:14px;font-size:12px;color:var(--text-dim);
          padding:12px 16px;background:var(--surface);border-radius:10px;border:1px solid var(--border)">
          ℹ️ Transmitting via E220-900T22D on default channel.
          Ensure the receiver is tuned to the same frequency.
        </div>
      </div>

      <!-- ══ PANEL: SD LOGGER ═══════════════════════════ -->
      <div class="panel" id="panel-sd">
        <div class="section-title">SD Card Logger</div>
        <div class="stat-display">
          <div class="stat-lbl">Total Rows Logged</div>
          <div class="stat-val" id="sd-rows-big">---<span>rows</span></div>
        </div>
        <div class="grid">
          <div class="card c-green">
            <div class="card-label"><span class="card-icon">💾</span> SD Card</div>
            <div class="card-value" id="sd-status-val" style="font-size:20px;letter-spacing:-.3px">---</div>
            <div class="card-sub">FAT32 · SPI bus</div>
          </div>
          <div class="card c-blue">
            <div class="card-label"><span class="card-icon">📄</span> Log File</div>
            <div class="card-value" style="font-size:16px;letter-spacing:-.2px">/snowlog.csv</div>
            <div class="card-sub">Logged every 2 seconds</div>
          </div>
        </div>

        <div class="data-top" style="margin-top:26px">
          <div class="section-title" style="margin-bottom:0;flex:1">Session History</div>
          <div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap">
            <span class="data-count"><strong id="histCountLabel">0</strong> / 60 entries</span>
            <button class="btn btn-ghost" onclick="refreshHistory()">↻ Refresh</button>
            <a href="/download" download="snowlog.csv">
              <button class="btn btn-primary">⬇ Download CSV</button>
            </a>
          </div>
        </div>

        <div style="margin-top:14px" class="table-wrap">
          <table>
            <thead>
              <tr>
                <th>#</th><th>Time</th><th>Date</th>
                <th>Depth (cm)</th><th>Latitude</th><th>Longitude</th>
                <th>Alt (m)</th><th>Speed</th><th>Sats</th>
              </tr>
            </thead>
            <tbody id="histBody">
              <tr><td colspan="9" style="text-align:center;padding:30px;color:var(--text-dim)">
                No data yet — waiting for sensor readings
              </td></tr>
            </tbody>
          </table>
        </div>
      </div>

      <!-- ══ PANEL: SYSTEM ══════════════════════════════ -->
      <div class="panel" id="panel-sys">
        <div class="section-title">ESP32 System Resources</div>
        <div class="grid">
          <div class="card col-2 c-blue">
            <div class="card-label"><span class="card-icon">🧠</span> RAM Usage</div>
            <div class="progress-wrap" style="margin-top:10px">
              <div class="progress-label">
                <span id="ram-label">Free / Total</span>
                <strong id="ram-pct">--%</strong>
              </div>
              <div class="progress-track">
                <div class="progress-fill" id="ram-fill"
                  style="width:0%;background:var(--blue)"></div>
              </div>
            </div>
          </div>

          <div class="card col-2 c-cyan">
            <div class="card-label"><span class="card-icon">💽</span> Flash Usage</div>
            <div class="progress-wrap" style="margin-top:10px">
              <div class="progress-label">
                <span id="flash-label">Sketch / Total</span>
                <strong id="flash-pct">--%</strong>
              </div>
              <div class="progress-track">
                <div class="progress-fill" id="flash-fill"
                  style="width:0%;background:var(--cyan)"></div>
              </div>
            </div>
          </div>

          <div class="card c-orange">
            <div class="card-label"><span class="card-icon">⚡</span> CPU</div>
            <div class="card-value" id="cpu-val">---<span class="unit">MHz</span></div>
            <div class="card-sub">Xtensa dual-core LX6</div>
          </div>

          <div class="card c-green">
            <div class="card-label"><span class="card-icon">⏱</span> Uptime</div>
            <div class="card-value" id="uptime-val" style="font-size:20px;letter-spacing:-.3px">---</div>
            <div class="card-sub">Since last boot</div>
          </div>

          <div class="card col-2">
            <div class="card-label"><span class="card-icon">📶</span> WiFi Access Point</div>
            <div style="margin-top:4px">
              <div class="info-row"><span class="info-key">SSID</span>
                <span class="info-val">snow sensor transmitter</span></div>
              <div class="info-row"><span class="info-key">Password</span>
                <span class="info-val">1234567890</span></div>
              <div class="info-row"><span class="info-key">IP Address</span>
                <span class="info-val">192.168.4.1</span></div>
              <div class="info-row"><span class="info-key">Connected Clients</span>
                <span class="info-val" id="wifi-clients">---</span></div>
            </div>
          </div>
        </div>
      </div>

    </div><!-- end .panels -->
  </div><!-- end .content -->
</div><!-- end .layout -->

<script>
// ══ PANEL NAVIGATION ════════════════════════════════
const panels = {
  dash: {el:'panel-dash', nav:'nav-dash', title:'Dashboard',        sub:'Live sensor overview'},
  map:  {el:'panel-map',  nav:'nav-map',  title:'GPS Map',          sub:'Real-time transmitter position'},
  lora: {el:'panel-lora', nav:'nav-lora', title:'LoRa TX',          sub:'Transmission stats and last payload'},
  sd:   {el:'panel-sd',   nav:'nav-sd',   title:'SD Logger',        sub:'Log file status and session history'},
  sys:  {el:'panel-sys',  nav:'nav-sys',  title:'System',           sub:'ESP32 resources and WiFi AP info'},
};
let activePanel = 'dash';
let mapInitialized = false;
let leafletMap = null;
let mapMarker = null;
let lastData = null;

function showPanel(id) {
  if (!panels[id]) return;
  document.getElementById(panels[activePanel].el).classList.remove('active');
  document.getElementById(panels[activePanel].nav).classList.remove('active');
  document.getElementById(panels[id].el).classList.add('active');
  document.getElementById(panels[id].nav).classList.add('active');
  document.getElementById('page-title').textContent = panels[id].title;
  document.getElementById('page-sub').textContent   = panels[id].sub;
  activePanel = id;
  if (id === 'map')  initOrUpdateMap();
  if (id === 'sd')   refreshHistory();
  if (id === 'lora' && lastData) renderLora(lastData);
  if (id === 'sys'  && lastData) renderSys(lastData);
}

// ══ FORMATTERS ══════════════════════════════════════
const fmt1 = v => isNaN(+v) ? '---' : (+v).toFixed(1);
const fmt2 = v => isNaN(+v) ? '---' : (+v).toFixed(2);
const fmt5 = v => isNaN(+v) ? '---' : (+v).toFixed(5);

function ageClass(ms) { return ms < 10000 ? 'fresh' : ms < 30000 ? 'stale' : 'old'; }
function ageStr(ms) {
  if (ms < 4000)  return 'Just now';
  if (ms < 60000) return `${Math.floor(ms / 1000)} s ago`;
  return `${Math.floor(ms / 60000)} min ago`;
}
function txAgeStr(ms) {
  if (ms <= 0) return 'Pending…';
  return ageStr(ms);
}
function uptimeStr(ms) {
  const s = Math.floor(ms / 1000);
  const m = Math.floor(s / 60);
  const h = Math.floor(m / 60);
  const d = Math.floor(h / 24);
  if (d > 0)  return `${d}d ${h % 24}h ${m % 60}m`;
  if (h > 0)  return `${h}h ${m % 60}m ${s % 60}s`;
  if (m > 0)  return `${m}m ${s % 60}s`;
  return `${s}s`;
}

// ══ DASHBOARD ════════════════════════════════════════
function renderDash(d) {
  const noDepth = d.depth < 0;
  const depthCm = noDepth ? '---' : fmt1(d.depth);
  const depthM  = noDepth ? '' : fmt2(d.depth / 100);
  const age     = d.read_age_ms || 0;
  const hasTime = d.date !== '?' && d.time_s !== '?';

  // Sidebar footer
  document.getElementById('sf-depth').textContent   = noDepth ? '---' : depthCm + ' cm';
  document.getElementById('sf-txcount').textContent = d.lora_count;
  document.getElementById('sf-sdrows').textContent  = d.sd_ready ? d.sd_rows : 'No SD';

  // Live badge
  document.getElementById('liveBadge').className = 'live-badge on';
  document.getElementById('liveText').textContent = 'Live';

  document.getElementById('dash-inner').innerHTML = `
    <div class="last-rx">
      <span>Last reading : <strong>${hasTime ? d.date + ' · ' + d.time_s : 'Waiting for GPS time…'}</strong></span>
      <span class="rx-age ${ageClass(age)}">${ageStr(age)}</span>
    </div>

    <div class="hero">
      <div>
        <div class="hero-lbl">Snow Depth</div>
        <div class="hero-val">${depthCm}<span>${noDepth ? '' : 'cm'}</span></div>
        ${noDepth ? '' : `<div class="hero-sub">${depthM} m</div>`}
      </div>
      <div class="hero-meta">
        <div class="hero-meta-row">📡 TX Packets</div>
        <div style="font-size:22px;font-weight:600;color:var(--blue);margin-bottom:10px">${d.lora_count}</div>
        <div class="hero-meta-row">💾 SD Rows <strong>${d.sd_ready ? d.sd_rows : 'No SD'}</strong></div>
        <div class="hero-meta-row">🛰 Satellites <strong>${d.gps_valid ? d.sats : '---'}</strong></div>
      </div>
    </div>

    <div class="grid">
      <div class="card col-2 c-blue">
        <div class="card-label"><span class="card-icon">🌐</span> GPS Coordinates</div>
        ${d.gps_valid ? `
          <div class="coords-grid">
            <div>
              <div class="coord-lbl">Latitude</div>
              <div class="coord-val">${fmt5(d.lat)} °N</div>
            </div>
            <div>
              <div class="coord-lbl">Longitude</div>
              <div class="coord-val">${fmt5(d.lon)} °E</div>
            </div>
          </div>
          <div class="card-sub" style="margin-top:12px">
            <a href="https://maps.google.com/?q=${d.lat},${d.lon}" target="_blank"
               style="color:var(--blue);text-decoration:none;font-size:12px">
              Open in Google Maps →
            </a>
          </div>
        ` : `<div class="card-sub" style="margin-top:10px;font-size:13px">Waiting for GPS fix…</div>`}
      </div>

      <div class="card c-cyan">
        <div class="card-label"><span class="card-icon">⛰</span> Altitude</div>
        <div class="card-value">${d.gps_valid ? fmt1(d.alt) : '---'}<span class="unit">m</span></div>
        <div class="card-sub">Above sea level</div>
      </div>

      <div class="card c-yellow">
        <div class="card-label"><span class="card-icon">💨</span> GPS Speed</div>
        <div class="card-value">${d.gps_valid ? fmt1(d.speed) : '---'}<span class="unit">km/h</span></div>
        <div class="card-sub">Station movement</div>
      </div>

      <div class="card c-green">
        <div class="card-label"><span class="card-icon">🛰</span> Satellites</div>
        <div class="card-value">${d.gps_valid ? d.sats : '---'}<span class="unit">sat</span></div>
        <div class="card-sub">HDOP : ${d.gps_valid ? fmt2(d.hdop) : '---'}</div>
      </div>

      <div class="card c-orange">
        <div class="card-label"><span class="card-icon">📡</span> LoRa TX</div>
        <div class="card-value" style="font-size:20px;letter-spacing:-.3px">${txAgeStr(d.lora_last_tx_ms)}</div>
        <div class="card-sub">${d.lora_ready
          ? '✓ Module ready · ' + d.lora_count + ' packets sent'
          : '✗ Module not ready — check GPIO13/14/26/27/32'}</div>
      </div>

      <div class="card">
        <div class="card-label"><span class="card-icon">🕐</span> Timestamp</div>
        <div class="card-value" style="font-size:20px;letter-spacing:-.3px">${hasTime ? d.time_s : '---'}</div>
        <div class="card-sub">${hasTime ? d.date + ' — local time (UTC+2)' : 'Waiting for GPS…'}</div>
      </div>
    </div>

    <div class="raw-wrap">
      <div class="raw-lbl">LoRa Payload Being Transmitted</div>
      <div class="raw-box">${d.last_payload || 'Waiting for first transmission…'}</div>
    </div>
  `;
}

// ══ LORA PANEL ══════════════════════════════════════
function renderLora(d) {
  const el = id => document.getElementById(id);
  if (el('lora-count-big')) el('lora-count-big').innerHTML = `${d.lora_count}<span>pkts</span>`;
  if (el('lora-module-val')) {
    el('lora-module-val').textContent = d.lora_ready ? 'READY' : 'NOT READY';
    el('lora-module-val').style.color = d.lora_ready ? 'var(--green)' : 'var(--red)';
  }
  if (el('lora-last-tx'))  el('lora-last-tx').textContent  = txAgeStr(d.lora_last_tx_ms);
  if (el('lora-payload'))  el('lora-payload').textContent   = d.last_payload || 'Waiting for first transmission…';
}

// ══ SD PANEL ════════════════════════════════════════
function renderSD(d) {
  const el = id => document.getElementById(id);
  if (el('sd-rows-big'))   el('sd-rows-big').innerHTML   = `${d.sd_ready ? d.sd_rows : '---'}<span>rows</span>`;
  if (el('sd-status-val')) {
    el('sd-status-val').textContent = d.sd_ready ? 'READY' : 'NOT FOUND';
    el('sd-status-val').style.color = d.sd_ready ? 'var(--green)' : 'var(--red)';
  }
}

// ══ SYSTEM PANEL ════════════════════════════════════
function renderSys(d) {
  const el = id => document.getElementById(id);

  const freeKB  = Math.round(d.free_heap  / 1024);
  const totalKB = Math.round(d.total_heap / 1024);
  const usedKB  = totalKB - freeKB;
  const ramPct  = Math.round((usedKB / totalKB) * 100);
  if (el('ram-fill'))  el('ram-fill').style.width  = ramPct + '%';
  if (el('ram-label')) el('ram-label').textContent = `${freeKB} KB free / ${totalKB} KB total`;
  if (el('ram-pct'))   el('ram-pct').textContent   = ramPct + '% used';

  const sketchKB = Math.round(d.sketch_size / 1024);
  const flashKB  = Math.round(d.total_flash / 1024);
  const flashPct = Math.round((sketchKB / flashKB) * 100);
  if (el('flash-fill'))  el('flash-fill').style.width  = flashPct + '%';
  if (el('flash-label')) el('flash-label').textContent = `${sketchKB} KB used / ${flashKB} KB total`;
  if (el('flash-pct'))   el('flash-pct').textContent   = flashPct + '% used';

  if (el('cpu-val'))      el('cpu-val').innerHTML      = `${d.cpu_mhz}<span class="unit">MHz</span>`;
  if (el('uptime-val'))   el('uptime-val').textContent = uptimeStr(d.uptime_ms);
  if (el('wifi-clients')) el('wifi-clients').textContent = d.wifi_clients;
}

// ══ MAIN FETCH LOOP ══════════════════════════════════
async function fetchDash() {
  try {
    const r = await fetch('/data');
    const d = await r.json();
    lastData = d;
    // Always update sidebar footer
    document.getElementById('sf-depth').textContent   = d.depth >= 0 ? d.depth.toFixed(1) + ' cm' : '---';
    document.getElementById('sf-txcount').textContent = d.lora_count;
    document.getElementById('sf-sdrows').textContent  = d.sd_ready ? d.sd_rows : 'No SD';
    // Render active panel
    if (activePanel === 'dash') renderDash(d);
    if (activePanel === 'lora') renderLora(d);
    if (activePanel === 'sd')   renderSD(d);
    if (activePanel === 'sys')  renderSys(d);
  } catch(e) {
    document.getElementById('liveBadge').className = 'live-badge';
    document.getElementById('liveText').textContent = 'Offline';
  }
}
setInterval(fetchDash, 2000);
fetchDash();

// ══ MAP ══════════════════════════════════════════════
async function initOrUpdateMap() {
  const d = lastData || await fetch('/data').then(r => r.json()).catch(() => null);
  if (!d || !d.gps_valid || +d.lat === 0) {
    document.getElementById('map').innerHTML =
      '<div style="display:flex;align-items:center;justify-content:center;height:100%;' +
      'color:var(--text-dim);font-size:14px">No GPS fix available yet</div>';
    return;
  }
  const lat = +d.lat, lon = +d.lon;
  document.getElementById('map-lat').textContent = lat.toFixed(5) + ' °N';
  document.getElementById('map-lon').textContent = lon.toFixed(5) + ' °E';
  document.getElementById('map-alt').textContent = (+d.alt).toFixed(1) + ' m';

  if (!mapInitialized) {
    leafletMap = L.map('map').setView([lat, lon], 14);
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
      attribution: '© OSM contributors', maxZoom: 19
    }).addTo(leafletMap);
    const snowIcon = L.divIcon({
      html: '<div style="font-size:28px;filter:drop-shadow(0 2px 6px rgba(0,0,0,.8))">❄</div>',
      iconSize: [30, 30], iconAnchor: [15, 15], className: ''
    });
    mapMarker = L.marker([lat, lon], { icon: snowIcon }).addTo(leafletMap);
    mapMarker.bindPopup(
      `<b>Snow Transmitter</b><br>` +
      `Depth : ${d.depth >= 0 ? d.depth.toFixed(1) + ' cm' : 'N/A'}<br>` +
      `Alt   : ${(+d.alt).toFixed(0)} m<br>` +
      `${d.date} ${d.time_s}`
    ).openPopup();
    mapInitialized = true;
  } else {
    mapMarker.setLatLng([lat, lon]);
    leafletMap.setView([lat, lon]);
    leafletMap.invalidateSize();
  }
}

// ══ HISTORY ══════════════════════════════════════════
async function refreshHistory() {
  try {
    const r = await fetch('/history');
    const d = await r.json();
    renderHistory(d);
    if (lastData) renderSD(lastData);
  } catch(e) {}
}

function renderHistory(data) {
  document.getElementById('histCountLabel').textContent = data.count || 0;
  if (!data.entries || !data.entries.length) {
    document.getElementById('histBody').innerHTML =
      '<tr><td colspan="9" style="text-align:center;padding:30px;color:var(--text-dim)">No readings yet</td></tr>';
    return;
  }
  const rows = data.entries.map((e, i) => `
    <tr>
      <td>${i + 1}</td>
      <td>${e.time_s}</td>
      <td>${e.date}</td>
      <td class="${+e.depth >= 0 ? 'td-good' : ''}">${+e.depth >= 0 ? (+e.depth).toFixed(1) + ' cm' : '---'}</td>
      <td>${(+e.lat).toFixed(5)}</td>
      <td>${(+e.lon).toFixed(5)}</td>
      <td>${(+e.alt).toFixed(1)}</td>
      <td>${(+e.speed).toFixed(1)} km/h</td>
      <td>${e.sats}</td>
    </tr>`).join('');
  document.getElementById('histBody').innerHTML = rows;
}
</script>
</body>
</html>
)HTMLEND";

// ═══════════════════════════════════════════════════
//  WEB HANDLERS
// ═══════════════════════════════════════════════════

void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  int h, m, s, d, mo, y;
  localTime(h, m, s, d, mo, y);

  char dateBuf[12] = "?";
  char timeBuf[10] = "?";
  if (gps.date.isValid()) snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", y, mo, d);
  if (gps.time.isValid()) snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", h, m, s);

  uint32_t freeHeap   = ESP.getFreeHeap();
  uint32_t totalHeap  = ESP.getHeapSize();
  uint32_t sketchSize = ESP.getSketchSize();
  uint32_t freeFlash  = ESP.getFreeSketchSpace();
  uint32_t totalFlash = sketchSize + freeFlash;
  uint8_t  cpuMhz     = ESP.getCpuFreqMHz();
  uint32_t uptimeMs   = millis();
  uint32_t loraTxAge  = loraLastTxMs > 0 ? (millis() - loraLastTxMs) : 0;
  uint32_t readAge    = lastReadMs   > 0 ? (millis() - lastReadMs)   : 9999999u;
  uint8_t  wifiCli    = WiFi.softAPgetStationNum();

  // Safely escape lastPayload (no quotes inside)
  char safePayload[145];
  strncpy(safePayload, lastPayload, sizeof(safePayload) - 1);
  safePayload[sizeof(safePayload) - 1] = '\0';

  char json[700];
  snprintf(json, sizeof(json),
    "{"
    "\"depth\":%.1f,"
    "\"lat\":%.5f,\"lon\":%.5f,"
    "\"alt\":%.1f,\"speed\":%.1f,"
    "\"sats\":%d,\"hdop\":%.2f,"
    "\"gps_valid\":%s,"
    "\"date\":\"%s\",\"time_s\":\"%s\","
    "\"lora_ready\":%s,"
    "\"lora_count\":%lu,"
    "\"lora_last_tx_ms\":%lu,"
    "\"sd_ready\":%s,"
    "\"sd_rows\":%lu,"
    "\"free_heap\":%lu,\"total_heap\":%lu,"
    "\"sketch_size\":%lu,\"total_flash\":%lu,"
    "\"cpu_mhz\":%u,"
    "\"uptime_ms\":%lu,"
    "\"wifi_clients\":%u,"
    "\"read_age_ms\":%lu,"
    "\"last_payload\":\"%s\""
    "}",
    distCm,
    gps.location.isValid()   ? gps.location.lat()          : 0.0,
    gps.location.isValid()   ? gps.location.lng()          : 0.0,
    gps.altitude.isValid()   ? gps.altitude.meters()       : 0.0,
    gps.speed.isValid()      ? gps.speed.kmph()            : 0.0,
    gps.satellites.isValid() ? (int)gps.satellites.value() : 0,
    gps.hdop.isValid()       ? gps.hdop.hdop()             : 0.0,
    gps.location.isValid()   ? "true"  : "false",
    dateBuf, timeBuf,
    loraReady                ? "true"  : "false",
    loraCount,
    loraTxAge,
    sdReady                  ? "true"  : "false",
    sdRowCount,
    freeHeap, totalHeap,
    sketchSize, totalFlash,
    cpuMhz,
    uptimeMs,
    (uint32_t)wifiCli,
    readAge,
    safePayload
  );

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", json);
}

void handleHistory() {
  String json = "{\"count\":";
  json += g_histCount;
  json += ",\"entries\":[";

  int start = (g_histCount < MAX_HIST) ? 0 : g_histHead;
  for (int i = 0; i < g_histCount; i++) {
    int idx = (start + i) % MAX_HIST;
    HistEntry& e = g_hist[idx];
    if (i) json += ',';
    char entry[200];
    snprintf(entry, sizeof(entry),
      "{\"lat\":%.5f,\"lon\":%.5f,\"alt\":%.1f,\"speed\":%.1f,"
      "\"depth\":%.1f,\"sats\":%d,"
      "\"date\":\"%s\",\"time_s\":\"%s\"}",
      e.lat, e.lon, e.alt, e.speed,
      e.depth, e.sats,
      e.date, e.time_s);
    json += entry;
  }
  json += "]}";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", json);
}

void handleDownload() {
  if (!sdReady) {
    server.send(503, "text/plain", "SD card not available");
    return;
  }
  File f = SD.open(LOG_FILE, FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "Log file not found on SD card");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=snowlog.csv");
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("Content-Length", String(f.size()));
  server.send(200, "text/csv", "");

  WiFiClient client = server.client();
  uint8_t buf[512];
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    if (n > 0) client.write(buf, n);
  }
  f.close();
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ═══════════════════════════════════════════════════
//  SD HEADER  (unchanged from original)
// ═══════════════════════════════════════════════════

void writeSDHeader() {
  if (SD.exists(LOG_FILE)) return;
  File f = SD.open(LOG_FILE, FILE_WRITE);
  if (!f) return;
  f.println("date,time,lat,lon,alt_m,speed_kmh,depth_cm");
  f.close();
  Serial.println("[ SD   ] Header written to " LOG_FILE);
}

// ═══════════════════════════════════════════════════
//  GPS  (unchanged)
// ═══════════════════════════════════════════════════

void readGPS() {
  while (gpsSerial.available())
    gps.encode(gpsSerial.read());
}

// ═══════════════════════════════════════════════════
//  ULTRASONIC  (unchanged, added lastReadMs update)
// ═══════════════════════════════════════════════════

void readUltrasonic() {
  while (usSerial.available()) {
    uint8_t b = usSerial.read();
    if (usIdx == 0 && b != 0xFF) continue;
    usBuf[usIdx++] = b;
    if (usIdx == 4) {
      usIdx = 0;
      uint8_t ck = (usBuf[0] + usBuf[1] + usBuf[2]) & 0xFF;
      if (ck == usBuf[3]) {
        float dist = ((usBuf[1] << 8) | usBuf[2]) / 10.0;
        if (dist >= 28.0 && dist <= 750.0) {
          distCm    = dist;
          lastReadMs = millis();
        }
      }
    }
  }
}

// ═══════════════════════════════════════════════════
//  SERIAL REPORT  (unchanged)
// ═══════════════════════════════════════════════════

void printReport() {
  int h, m, s, d, mo, y;
  localTime(h, m, s, d, mo, y);

  Serial.println("─────────────────────────────────────────────");

  Serial.print("[ US    ] Snow depth  : ");
  if (distCm >= 0)
    Serial.printf("%.1f cm  (%.3f m)\n", distCm, distCm / 100.0);
  else
    Serial.println("no data — check GPIO4 wiring");

  Serial.print("[ GPS   ] Location    : ");
  if (gps.location.isValid())
    Serial.printf("%.6f N   %.6f E\n", gps.location.lat(), gps.location.lng());
  else
    Serial.println("no fix yet");

  Serial.print("[ GPS   ] Altitude    : ");
  if (gps.altitude.isValid())
    Serial.printf("%.1f m\n", gps.altitude.meters());
  else
    Serial.println("---");

  Serial.print("[ GPS   ] Speed       : ");
  if (gps.speed.isValid())
    Serial.printf("%.1f km/h\n", gps.speed.kmph());
  else
    Serial.println("---");

  Serial.print("[ GPS   ] Satellites  : ");
  if (gps.satellites.isValid())
    Serial.printf("%d   HDOP %.1f\n", gps.satellites.value(), gps.hdop.isValid() ? gps.hdop.hdop() : 0.0f);
  else
    Serial.println("---");

  Serial.print("[ GPS   ] Date / Time : ");
  if (gps.date.isValid() && gps.time.isValid())
    Serial.printf("%04d-%02d-%02d  %02d:%02d:%02d  (UTC+%d)\n", y, mo, d, h, m, s, TIMEZONE_OFFSET_H);
  else
    Serial.println("waiting for fix…");

  Serial.print("[ LoRa  ] TX count    : ");
  Serial.println(loraCount);

  Serial.print("[ SD    ] Rows logged : ");
  if (sdReady) Serial.println(sdRowCount);
  else         Serial.println("SD not ready");

  Serial.println();
}

// ═══════════════════════════════════════════════════
//  SD LOG  (unchanged from original)
// ═══════════════════════════════════════════════════

void logToSD() {
  if (!sdReady) return;
  File f = SD.open(LOG_FILE, FILE_APPEND);
  if (!f) return;

  int h, m, s, d, mo, y;
  localTime(h, m, s, d, mo, y);

  if (gps.date.isValid()) f.printf("%04d-%02d-%02d", y, mo, d);
  f.print(',');
  if (gps.time.isValid()) f.printf("%02d:%02d:%02d", h, m, s);
  f.print(',');
  if (gps.location.isValid()) {
    f.printf("%.6f,%.6f", gps.location.lat(), gps.location.lng());
  } else {
    f.print(",");
  }
  f.print(',');
  if (gps.altitude.isValid()) f.print(gps.altitude.meters(), 1); f.print(',');
  if (gps.speed.isValid())    f.print(gps.speed.kmph(),      1); f.print(',');
  if (distCm >= 0)            f.print(distCm, 1);
  f.println();

  sdRowCount++;
  f.close();
}

// ═══════════════════════════════════════════════════
//  LoRa TX  (adds history entry + stores payload)
// ═══════════════════════════════════════════════════

void transmitLoRa() {
  if (!loraReady) return;

  int h, m, s, d, mo, y;
  localTime(h, m, s, d, mo, y);

  char dateBuf[12] = "?";
  char timeBuf[10] = "?";
  if (gps.date.isValid()) snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", y, mo, d);
  if (gps.time.isValid()) snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", h, m, s);

  snprintf(lastPayload, sizeof(lastPayload),
    "%.5f,%.5f,%.1f,%.1f,%.1f,%d,%.2f,%s,%s",
    gps.location.isValid()   ? gps.location.lat()          : 0.0,
    gps.location.isValid()   ? gps.location.lng()          : 0.0,
    gps.altitude.isValid()   ? gps.altitude.meters()       : 0.0,
    gps.speed.isValid()      ? gps.speed.kmph()            : 0.0,
    distCm >= 0              ? distCm                      : 0.0,
    gps.satellites.isValid() ? (int)gps.satellites.value() : 0,
    gps.hdop.isValid()       ? gps.hdop.hdop()             : 0.0,
    dateBuf,
    timeBuf
  );

  lora.sendMessage(lastPayload);
  loraLastTxMs = millis();
  loraCount++;

  // Store in in-memory history ring buffer
  HistEntry& he = g_hist[g_histHead];
  he.depth = distCm;
  he.lat   = gps.location.isValid() ? gps.location.lat() : 0.0;
  he.lon   = gps.location.isValid() ? gps.location.lng() : 0.0;
  he.alt   = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
  he.speed = gps.speed.isValid()    ? gps.speed.kmph()      : 0.0;
  he.sats  = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
  strncpy(he.date,   dateBuf, sizeof(he.date)   - 1);
  strncpy(he.time_s, timeBuf, sizeof(he.time_s) - 1);
  g_histHead = (g_histHead + 1) % MAX_HIST;
  if (g_histCount < MAX_HIST) g_histCount++;
}

// ═══════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);

  // ── GPS ──────────────────────────────────────────
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("[ GPS  ] UART2 started on GPIO16/17");

  // ── Ultrasonic ───────────────────────────────────
  usSerial.begin(9600);
  Serial.println("[ US   ] SoftwareSerial started on GPIO4");

  // ── LoRa ─────────────────────────────────────────
  loraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  loraReady = lora.begin();
  Serial.println(loraReady
    ? "[ LoRa ] E220 ready"
    : "[ LoRa ] E220 init failed — check GPIO13/14/26/27/32");

  // ── SD Card ──────────────────────────────────────
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("[ SD   ] Not found — check wiring and FAT32 format");
    sdReady = false;
  } else {
    sdReady = true;
    Serial.println("[ SD   ] Card ready");
    writeSDHeader();
  }

  // ── WiFi Access Point ────────────────────────────
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  Serial.printf("[ WiFi ] AP started — SSID \"%s\"  pass \"%s\"\n", WIFI_AP_SSID, WIFI_AP_PASS);
  Serial.printf("[ HTTP ] Open  http://%s\n", WiFi.softAPIP().toString().c_str());

  // ── Web routes ───────────────────────────────────
  server.on("/",         HTTP_GET, handleRoot);
  server.on("/data",     HTTP_GET, handleData);
  server.on("/history",  HTTP_GET, handleHistory);
  server.on("/download", HTTP_GET, handleDownload);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[ HTTP ] Server started on port 80");

  Serial.println();
  Serial.println("═══════════════════════════════════════════════");
  Serial.println("  Snow Transmitter v3 — ready");
  Serial.printf("  WiFi : %s / %s\n", WIFI_AP_SSID, WIFI_AP_PASS);
  Serial.printf("  URL  : http://%s\n", WiFi.softAPIP().toString().c_str());
  Serial.println("═══════════════════════════════════════════════");
  Serial.println();
}

// ═══════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════

void loop() {
  server.handleClient();   // serve WiFi requests first

  readGPS();
  readUltrasonic();

  static unsigned long lastRun = 0;
  if (millis() - lastRun >= LOG_INTERVAL_MS) {
    lastRun = millis();
    printReport();
    logToSD();
    transmitLoRa();
  }
}
