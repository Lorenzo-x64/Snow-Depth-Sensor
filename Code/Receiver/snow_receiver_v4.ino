/*
 * ╔══════════════════════════════════════════════════════════════════╗
 * ║   SNOW STATION — LoRa Receiver v4                              ║
 * ║   Board  : Heltec WiFi LoRa 32 V3  (SX1262 onboard)           ║
 * ║   WiFi   : STA → ESP_84CA80  (open / no password)             ║
 * ║   Fallback AP : Station-Neige / 1234                          ║
 * ║   URL    : http://<ip>  or  http://stationneige.local         ║
 * ╚══════════════════════════════════════════════════════════════════╝
 *
 *  Required library (Arduino Library Manager):
 *    • RadioLib  by Jan Gromeš
 *
 *  Board package:
 *    • "Heltec ESP32 Series Dev-boards" → Heltec WiFi LoRa 32(V3)
 *
 *  Features:
 *    1. Live dashboard  — snow depth, GPS, signal quality
 *    2. LoRa scanner    — sweep 850–870 MHz, plot RSSI
 *    3. Frequency tuner — slide to 868 MHz EU band + apply
 *    4. GPS map         — Leaflet / OSM live marker
 *    5. Data log        — table of last 60 packets + CSV download
 */

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ═══════════════════════════════════════════════════════
//  HELTEC V3 — ONBOARD SX1262 PIN MAP
// ═══════════════════════════════════════════════════════
#define LORA_SCK      9
#define LORA_MISO    11
#define LORA_MOSI    10
#define LORA_CS       8
#define LORA_RST     12
#define LORA_DIO1    14
#define LORA_BUSY    13
#define VEXT_PIN     36   // LOW = Vext rail ON
#define LED_PIN      35

// ═══════════════════════════════════════════════════════
//  WIFI
// ═══════════════════════════════════════════════════════
#define WIFI_STA_SSID   "ESP_84CA80"
#define WIFI_STA_PASS   ""            // open network
#define WIFI_AP_SSID    "Station-Neige"
#define WIFI_AP_PASS    "1234"
#define WIFI_TIMEOUT_MS 12000

bool wifiIsAP = false;   // true when running as fallback AP

// ═══════════════════════════════════════════════════════
//  LORA DEFAULTS
// ═══════════════════════════════════════════════════════
float   g_freq      = 868.125f;  // EU 868 default; set to 850.125 for E220 ch.0
#define LORA_BW      125.0
#define LORA_SF      9
#define LORA_CR      5
#define LORA_PREAMBLE 10
#define LORA_SYNC    0x12   // private LoRa (non-LoRaWAN)
#define LORA_POWER   22

// ═══════════════════════════════════════════════════════
//  CURRENT PACKET
// ═══════════════════════════════════════════════════════
struct SnowPacket {
  float    lat = 0, lon = 0, alt = 0, speed = 0, depth = -1;
  int      sats = 0;
  float    hdop = 0, snr = 0;
  int      rssi = 0;
  char     date[12]   = "--";
  char     time_s[10] = "--";
  char     raw[128]   = "";
  uint32_t lastMs     = 0;
  uint32_t count      = 0;
  bool     valid      = false;
} g_pkt;

// ═══════════════════════════════════════════════════════
//  PACKET HISTORY  (circular buffer, 60 entries)
// ═══════════════════════════════════════════════════════
#define MAX_HIST 60
struct HistEntry {
  float lat, lon, alt, speed, depth;
  int   sats, rssi;
  float snr;
  char  date[12];
  char  time_s[10];
};
HistEntry g_hist[MAX_HIST];
int  g_histHead  = 0;
int  g_histCount = 0;

// ═══════════════════════════════════════════════════════
//  SCANNER
//  Sweep 850–870 MHz, 1 MHz steps → 21 points
// ═══════════════════════════════════════════════════════
#define SCAN_N    21
#define SCAN_FROM 850.0f
#define SCAN_STEP   1.0f
#define SCAN_DWELL_MS 80   // ms per frequency

struct ScanPt { float freq; float rssi; };
ScanPt g_scan[SCAN_N];
bool   g_scanDone   = false;
bool   g_scanning   = false;

// ═══════════════════════════════════════════════════════
//  OBJECTS
// ═══════════════════════════════════════════════════════
SPIClass  loraSPI(FSPI);
SX1262    radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);
WebServer server(80);

// ═══════════════════════════════════════════════════════
//  FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════════
void checkLoRa();
void parsePayload(const String& s);
bool radioInit(float freq);
void radioRxRestart();
void doScan();

void handleRoot();
void handleData();
void handleScan();
void handleSetFreq();
void handleHistory();
void handleDownload();
void handleNotFound();

// ═══════════════════════════════════════════════════════
//  EMBEDDED WEB APPLICATION  (stored in flash)
// ═══════════════════════════════════════════════════════
static const char INDEX_HTML[] PROGMEM = R"HTMLEND(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Station Neige ❄</title>
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
  --sidebar-w:   220px;
  --r:           18px;
}

*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%;background:var(--bg);color:var(--text);
  font-family:'Outfit',-apple-system,BlinkMacSystemFont,'Helvetica Neue',sans-serif;
  font-size:15px;-webkit-font-smoothing:antialiased;overflow:hidden}

/* grain */
body::before{content:'';position:fixed;inset:0;
  background-image:url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.75' numOctaves='4' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.025'/%3E%3C/svg%3E");
  background-size:200px;pointer-events:none;z-index:0}

/* ── LAYOUT ──────────────────────────────────── */
.layout{display:flex;height:100vh;position:relative;z-index:1}

/* ── SIDEBAR ──────────────────────────────────── */
.sidebar{
  width:var(--sidebar-w);
  flex-shrink:0;
  background:rgba(10,10,13,0.95);
  border-right:1px solid var(--border);
  display:flex;flex-direction:column;
  backdrop-filter:blur(20px);
  -webkit-backdrop-filter:blur(20px);
  z-index:10;
}

.sidebar-logo{
  padding:22px 20px 18px;
  border-bottom:1px solid var(--border);
  display:flex;align-items:center;gap:12px;
}
.logo-icon{font-size:22px;line-height:1;
  filter:drop-shadow(0 0 8px rgba(100,210,255,0.55));
  animation:spinSlow 14s linear infinite}
@keyframes spinSlow{to{transform:rotate(360deg)}}
.logo-title{font-size:15px;font-weight:600;letter-spacing:-0.2px}
.logo-sub{font-size:10px;color:var(--text-dim);letter-spacing:0.8px;text-transform:uppercase;margin-top:1px}

.nav{flex:1;padding:12px 10px;display:flex;flex-direction:column;gap:4px}

.nav-item{
  display:flex;align-items:center;gap:12px;
  padding:10px 12px;border-radius:12px;
  cursor:pointer;transition:all 0.18s ease;
  color:var(--text-dim);font-size:14px;font-weight:500;
  border:1px solid transparent;user-select:none;
}
.nav-item:hover{background:var(--surface2);color:var(--text-mid)}
.nav-item.active{
  background:var(--blue-soft);
  border-color:rgba(10,132,255,0.25);
  color:var(--blue);
}
.nav-icon{font-size:17px;width:22px;text-align:center;flex-shrink:0}
.nav-label{flex:1}
.nav-badge{
  font-size:10px;padding:2px 7px;border-radius:20px;
  background:var(--green-soft);color:var(--green);
  font-weight:600;display:none
}
.nav-badge.show{display:block}

.sidebar-footer{
  padding:16px 14px;border-top:1px solid var(--border);
  font-size:11px;color:var(--text-dim)
}
.sidebar-footer .sf-row{display:flex;justify-content:space-between;margin-bottom:4px}
.sf-val{color:var(--text-mid);font-weight:500}

/* ── CONTENT ──────────────────────────────────── */
.content{flex:1;display:flex;flex-direction:column;overflow:hidden}

.topbar{
  display:flex;align-items:center;justify-content:space-between;
  padding:16px 28px;border-bottom:1px solid var(--border);
  background:rgba(8,8,10,0.88);backdrop-filter:blur(20px);
  -webkit-backdrop-filter:blur(20px);flex-shrink:0;
}
.topbar-title{font-size:16px;font-weight:600;letter-spacing:-0.2px}
.topbar-sub{font-size:11px;color:var(--text-dim);margin-top:2px;
  text-transform:uppercase;letter-spacing:0.7px}

.live-badge{
  display:flex;align-items:center;gap:8px;padding:6px 14px;
  background:var(--surface);border:1px solid var(--border);
  border-radius:100px;font-size:12px;font-weight:500;color:var(--text-mid);
  transition:all 0.4s
}
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

/* ── SHARED CARD STYLES ───────────────────────── */
.section-title{
  font-size:11px;font-weight:600;letter-spacing:1.5px;
  text-transform:uppercase;color:var(--text-dim);margin-bottom:18px;
  display:flex;align-items:center;gap:8px
}
.section-title::after{content:'';flex:1;height:1px;background:var(--border)}

.card{
  background:var(--surface);border:1px solid var(--border);
  border-radius:var(--r);padding:20px 22px;
  transition:border-color 0.25s,transform 0.18s;position:relative;overflow:hidden
}
.card:hover{border-color:var(--border2);transform:translateY(-1px)}
.card::before{content:'';position:absolute;top:0;left:0;right:0;height:2px;
  border-radius:2px 2px 0 0;background:transparent;transition:background 0.3s}
.card.c-blue::before  {background:linear-gradient(90deg,var(--blue),transparent)}
.card.c-green::before {background:linear-gradient(90deg,var(--green),transparent)}
.card.c-cyan::before  {background:linear-gradient(90deg,var(--cyan),transparent)}
.card.c-yellow::before{background:linear-gradient(90deg,var(--yellow),transparent)}
.card.c-orange::before{background:linear-gradient(90deg,var(--orange),transparent)}

.card-label{font-size:11px;font-weight:600;letter-spacing:1.3px;
  text-transform:uppercase;color:var(--text-dim);margin-bottom:14px;
  display:flex;align-items:center;gap:6px}
.card-icon{font-size:14px}
.card-value{font-size:30px;font-weight:300;letter-spacing:-1.2px;line-height:1;margin-bottom:4px}
.card-value .unit{font-size:15px;font-weight:400;color:var(--text-dim);margin-left:3px}
.card-sub{font-size:12px;color:var(--text-dim);margin-top:8px}

.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:14px}
.col-2{grid-column:span 2}

/* ── DASHBOARD HERO ───────────────────────────── */
.hero{
  background:linear-gradient(135deg,rgba(10,132,255,0.11) 0%,rgba(100,210,255,0.05) 50%,transparent 100%);
  border:1px solid rgba(10,132,255,0.22);border-radius:var(--r);
  padding:32px 36px;display:flex;align-items:center;justify-content:space-between;
  gap:20px;margin-bottom:22px;position:relative;overflow:hidden
}
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
  border-radius:10px;margin-bottom:20px;font-size:13px;color:var(--text-dim)
}
.last-rx strong{color:var(--text-mid);font-weight:500}
.rx-age{font-size:12px}
.rx-age.fresh{color:var(--green)}.rx-age.stale{color:var(--yellow)}.rx-age.old{color:var(--red)}

/* Signal bars */
.sig-wrap{margin-top:12px;display:flex;flex-direction:column;gap:9px}
.sig-row{display:flex;align-items:center;gap:10px}
.sig-lbl{font-size:10px;color:var(--text-dim);text-transform:uppercase;letter-spacing:.8px;width:42px}
.sig-track{flex:1;height:5px;background:var(--surface2);border-radius:3px;overflow:hidden}
.sig-fill{height:100%;border-radius:3px;transition:width .6s cubic-bezier(.4,0,.2,1)}
.sig-val{font-size:11px;color:var(--text-mid);width:62px;text-align:right;font-variant-numeric:tabular-nums}

/* Coords card */
.coords-grid{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:4px}
.coord-lbl{font-size:10px;color:var(--text-dim);text-transform:uppercase;letter-spacing:1px;margin-bottom:5px}
.coord-val{font-size:22px;font-weight:400;letter-spacing:-.3px;font-variant-numeric:tabular-nums}

/* Raw packet */
.raw-wrap{margin-top:22px}
.raw-lbl{font-size:10px;font-weight:600;letter-spacing:1.4px;text-transform:uppercase;color:var(--text-dim);margin-bottom:8px}
.raw-box{background:rgba(0,0,0,0.45);border:1px solid var(--border);border-radius:10px;
  padding:12px 16px;font-family:'SF Mono','Fira Code','Cascadia Code',monospace;
  font-size:11px;color:var(--cyan);word-break:break-all;letter-spacing:.3px}

/* No data */
.no-data{text-align:center;padding:70px 20px;color:var(--text-dim)}
.no-data-icon{font-size:52px;margin-bottom:14px;opacity:.28;animation:float 3s ease-in-out infinite}
@keyframes float{0%,100%{transform:translateY(0)}50%{transform:translateY(-8px)}}
.no-data h2{font-size:17px;font-weight:500;color:var(--text-mid);margin-bottom:7px}
.no-data p{font-size:13px}

/* ── SCANNER ──────────────────────────────────── */
.scan-controls{display:flex;align-items:center;gap:14px;margin-bottom:24px;flex-wrap:wrap}
.btn{
  display:inline-flex;align-items:center;gap:8px;
  padding:10px 22px;border-radius:100px;border:none;cursor:pointer;
  font-family:inherit;font-size:13px;font-weight:600;letter-spacing:.3px;
  transition:all .2s;user-select:none
}
.btn-primary{background:var(--blue);color:#fff}
.btn-primary:hover{background:#1a8fff;transform:translateY(-1px);box-shadow:0 4px 16px rgba(10,132,255,.35)}
.btn-primary:active{transform:translateY(0)}
.btn-primary:disabled{opacity:.4;cursor:not-allowed;transform:none}
.btn-ghost{background:var(--surface2);color:var(--text-mid);border:1px solid var(--border)}
.btn-ghost:hover{border-color:var(--border2);color:var(--text)}
.btn-danger{background:var(--red-soft);color:var(--red);border:1px solid rgba(255,69,58,.25)}
.btn-danger:hover{background:rgba(255,69,58,.22)}

.scan-status{font-size:12px;color:var(--text-dim);margin-left:auto}
.scan-status.scanning{color:var(--yellow);animation:blink .8s infinite}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.4}}

.scan-chart-wrap{
  background:var(--surface);border:1px solid var(--border);border-radius:var(--r);
  padding:24px;overflow-x:auto
}
.scan-chart{display:flex;align-items:flex-end;gap:6px;height:200px;min-width:500px}
.scan-bar-group{display:flex;flex-direction:column;align-items:center;flex:1;height:100%;justify-content:flex-end}
.scan-bar{
  width:100%;border-radius:4px 4px 0 0;min-height:3px;
  transition:height .5s cubic-bezier(.4,0,.2,1),background .5s;
  position:relative;cursor:default
}
.scan-bar:hover::after{
  content:attr(data-tip);position:absolute;bottom:calc(100% + 6px);left:50%;
  transform:translateX(-50%);background:rgba(0,0,0,0.85);
  color:var(--text);font-size:11px;padding:4px 8px;border-radius:6px;
  white-space:nowrap;z-index:20;pointer-events:none
}
.scan-freq{font-size:9px;color:var(--text-dim);margin-top:6px;text-align:center;
  letter-spacing:.3px;writing-mode:vertical-lr;transform:rotate(180deg);height:42px;
  display:flex;align-items:center;justify-content:center}
.scan-axis{display:flex;justify-content:space-between;margin-top:8px;
  font-size:10px;color:var(--text-dim)}

.scan-legend{display:flex;gap:20px;margin-top:16px;font-size:11px;color:var(--text-dim)}
.scan-legend-item{display:flex;align-items:center;gap:6px}
.scan-legend-dot{width:10px;height:10px;border-radius:2px}

/* ── FREQUENCY TUNER ──────────────────────────── */
.freq-display{
  background:linear-gradient(135deg,rgba(10,132,255,0.1),transparent);
  border:1px solid rgba(10,132,255,0.2);border-radius:var(--r);
  padding:28px 32px;margin-bottom:24px;text-align:center
}
.freq-current-lbl{font-size:11px;font-weight:600;letter-spacing:1.5px;
  text-transform:uppercase;color:var(--blue);margin-bottom:8px}
.freq-current-val{font-size:58px;font-weight:300;letter-spacing:-3px;line-height:1}
.freq-current-val span{font-size:22px;font-weight:400;color:var(--text-dim);margin-left:3px}

.freq-card{background:var(--surface);border:1px solid var(--border);border-radius:var(--r);padding:24px}
.freq-slider-wrap{margin:20px 0}
.freq-slider{width:100%;-webkit-appearance:none;height:6px;border-radius:3px;
  background:linear-gradient(90deg,var(--blue-soft),var(--surface2));
  outline:none;cursor:pointer}
.freq-slider::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;
  border-radius:50%;background:var(--blue);cursor:pointer;
  box-shadow:0 2px 8px rgba(10,132,255,.5);transition:transform .15s}
.freq-slider::-webkit-slider-thumb:hover{transform:scale(1.15)}
.freq-row{display:flex;align-items:center;gap:12px;margin-bottom:16px}
.freq-input{
  background:var(--surface2);border:1px solid var(--border);border-radius:10px;
  color:var(--text);font-family:inherit;font-size:16px;font-weight:500;
  padding:10px 14px;width:140px;text-align:center;transition:border-color .2s
}
.freq-input:focus{outline:none;border-color:var(--blue)}
.freq-presets{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:20px}
.freq-preset{
  padding:6px 14px;border-radius:8px;border:1px solid var(--border);
  background:var(--surface2);color:var(--text-dim);font-size:12px;font-weight:500;
  cursor:pointer;transition:all .2s
}
.freq-preset:hover{border-color:var(--border2);color:var(--text-mid)}
.freq-preset.active-preset{border-color:rgba(10,132,255,.35);background:var(--blue-soft);color:var(--blue)}

.freq-note{font-size:12px;color:var(--text-dim);margin-top:14px;
  padding:10px 14px;background:var(--surface2);border-radius:8px;line-height:1.6}

/* ── MAP ──────────────────────────────────────── */
.map-wrap{border-radius:var(--r);overflow:hidden;border:1px solid var(--border);margin-bottom:16px}
#map{height:420px;background:#111114}
.map-info{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}
.map-stat{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:14px 16px}
.map-stat-lbl{font-size:10px;color:var(--text-dim);text-transform:uppercase;letter-spacing:1px;margin-bottom:6px}
.map-stat-val{font-size:18px;font-weight:500;letter-spacing:-.5px}

/* ── DATA TABLE ───────────────────────────────── */
.data-top{display:flex;align-items:center;justify-content:space-between;margin-bottom:18px}
.data-count{font-size:13px;color:var(--text-dim)}
.data-count strong{color:var(--text-mid)}
.table-wrap{overflow-x:auto;border-radius:var(--r);border:1px solid var(--border)}
table{width:100%;border-collapse:collapse;font-size:12px}
thead tr{border-bottom:1px solid var(--border)}
thead th{
  text-align:left;padding:12px 14px;font-size:10px;font-weight:600;
  letter-spacing:1.2px;text-transform:uppercase;color:var(--text-dim);
  background:rgba(0,0,0,.25);white-space:nowrap
}
tbody tr{border-bottom:1px solid rgba(255,255,255,0.04);transition:background .15s}
tbody tr:last-child{border-bottom:none}
tbody tr:hover{background:var(--surface2)}
tbody td{padding:11px 14px;color:var(--text-mid);white-space:nowrap;font-variant-numeric:tabular-nums}
tbody td:first-child{color:var(--text-dim)}
.td-good{color:var(--green)!important}
.td-warn{color:var(--yellow)!important}
.td-bad{color:var(--red)!important}

/* ── RESPONSIVE ───────────────────────────────── */
@media(max-width:700px){
  .sidebar{width:60px}
  .logo-title,.logo-sub,.nav-label,.nav-badge,.sidebar-footer{display:none}
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
}
</style>
</head>
<body>
<div class="layout">

  <!-- ══ SIDEBAR ══════════════════════════════════════ -->
  <nav class="sidebar">
    <div class="sidebar-logo">
      <div class="logo-icon">❄</div>
      <div>
        <div class="logo-title">Station Neige</div>
        <div class="logo-sub">LoRa · GPS</div>
      </div>
    </div>

    <div class="nav">
      <div class="nav-item active" onclick="showPanel('dash')" id="nav-dash">
        <span class="nav-icon">📊</span>
        <span class="nav-label">Tableau de bord</span>
        <span class="nav-badge" id="badge-dash"></span>
      </div>
      <div class="nav-item" onclick="showPanel('scan')" id="nav-scan">
        <span class="nav-icon">📡</span>
        <span class="nav-label">Scanner LoRa</span>
      </div>
      <div class="nav-item" onclick="showPanel('freq')" id="nav-freq">
        <span class="nav-icon">🎛</span>
        <span class="nav-label">Fréquence</span>
      </div>
      <div class="nav-item" onclick="showPanel('map')" id="nav-map">
        <span class="nav-icon">🗺</span>
        <span class="nav-label">Carte GPS</span>
      </div>
      <div class="nav-item" onclick="showPanel('data')" id="nav-data">
        <span class="nav-icon">💾</span>
        <span class="nav-label">Données</span>
      </div>
    </div>

    <div class="sidebar-footer">
      <div class="sf-row"><span>Freq</span><span class="sf-val" id="sf-freq">---</span></div>
      <div class="sf-row"><span>Paquets</span><span class="sf-val" id="sf-count">0</span></div>
      <div class="sf-row"><span>RSSI</span><span class="sf-val" id="sf-rssi">---</span></div>
    </div>
  </nav>

  <!-- ══ MAIN CONTENT ══════════════════════════════════ -->
  <div class="content">
    <div class="topbar">
      <div>
        <div class="topbar-title" id="page-title">Tableau de bord</div>
        <div class="topbar-sub" id="page-sub">Vue générale en temps réel</div>
      </div>
      <div class="live-badge" id="liveBadge">
        <div class="live-dot" id="liveDot"></div>
        <span id="liveText">En attente…</span>
      </div>
    </div>

    <div class="panels">

      <!-- ══ PANEL: DASHBOARD ══════════════════════ -->
      <div class="panel active" id="panel-dash">
        <div id="dash-inner">
          <div class="no-data">
            <div class="no-data-icon">📡</div>
            <h2>En attente de données LoRa</h2>
            <p>Démarrez la station et patientez quelques secondes.</p>
          </div>
        </div>
      </div>

      <!-- ══ PANEL: SCANNER ════════════════════════ -->
      <div class="panel" id="panel-scan">
        <div class="section-title">Scanner de fréquences LoRa</div>
        <div class="scan-controls">
          <button class="btn btn-primary" id="btnScan" onclick="startScan()">
            🔍 Lancer le scan
          </button>
          <span class="scan-status" id="scanStatus">Prêt — plage 850–870 MHz</span>
        </div>
        <div class="scan-chart-wrap">
          <div class="scan-chart" id="scanChart">
            <div style="color:var(--text-dim);font-size:13px;width:100%;text-align:center;
              padding-top:80px">Appuyer sur "Lancer le scan" pour démarrer</div>
          </div>
          <div class="scan-axis">
            <span>850 MHz</span><span>855 MHz</span><span>860 MHz</span>
            <span>865 MHz</span><span>870 MHz</span>
          </div>
          <div class="scan-legend">
            <div class="scan-legend-item">
              <div class="scan-legend-dot" style="background:var(--green)"></div>Fort signal
            </div>
            <div class="scan-legend-item">
              <div class="scan-legend-dot" style="background:var(--yellow)"></div>Signal moyen
            </div>
            <div class="scan-legend-item">
              <div class="scan-legend-dot" style="background:var(--text-dim)"></div>Bruit de fond
            </div>
          </div>
        </div>
        <div style="margin-top:14px;font-size:12px;color:var(--text-dim);
          padding:12px 16px;background:var(--surface);border-radius:10px;border:1px solid var(--border)">
          ℹ️ Le scan suspend la réception LoRa pendant ~3 s. La barre la plus haute indique la fréquence
          avec le signal le plus fort — idéale pour localiser votre émetteur.
        </div>
      </div>

      <!-- ══ PANEL: FREQUENCY ══════════════════════ -->
      <div class="panel" id="panel-freq">
        <div class="section-title">Syntonisation de fréquence</div>
        <div class="freq-display">
          <div class="freq-current-lbl">Fréquence active</div>
          <div class="freq-current-val" id="freqDisplay">---<span>MHz</span></div>
        </div>
        <div class="freq-card">
          <div class="card-label"><span class="card-icon">🎚</span> Ajustement manuel</div>
          <div style="font-size:12px;color:var(--text-dim);margin-bottom:16px">
            Plage EU868 : 863–870 MHz · Plage E220 ch.0 : 850.125 MHz
          </div>
          <div class="freq-presets" id="freqPresets">
            <div class="freq-preset" onclick="setFreqPreset(850.125)">850.125<br><span style="font-size:10px;opacity:.6">E220 ch.0</span></div>
            <div class="freq-preset" onclick="setFreqPreset(863.125)">863.125<br><span style="font-size:10px;opacity:.6">EU ch.0</span></div>
            <div class="freq-preset" onclick="setFreqPreset(865.062)">865.062<br><span style="font-size:10px;opacity:.6">EU ch.2</span></div>
            <div class="freq-preset" onclick="setFreqPreset(867.0)">867.000<br><span style="font-size:10px;opacity:.6">EU ch.4</span></div>
            <div class="freq-preset" onclick="setFreqPreset(868.0)">868.000<br><span style="font-size:10px;opacity:.6">EU 868</span></div>
            <div class="freq-preset" onclick="setFreqPreset(868.125)">868.125<br><span style="font-size:10px;opacity:.6">EU ch.0</span></div>
          </div>
          <div class="freq-slider-wrap">
            <input type="range" class="freq-slider" id="freqSlider"
              min="848" max="870" step="0.125" value="868.125"
              oninput="onSlider(this.value)" style="width:100%">
            <div style="display:flex;justify-content:space-between;font-size:10px;
              color:var(--text-dim);margin-top:6px"><span>848 MHz</span><span>870 MHz</span></div>
          </div>
          <div class="freq-row">
            <input type="number" class="freq-input" id="freqInput"
              min="848" max="870" step="0.125" value="868.125"
              oninput="onFreqInput(this.value)" placeholder="868.125">
            <span style="font-size:14px;color:var(--text-dim)">MHz</span>
            <button class="btn btn-primary" onclick="applyFreq()" id="btnApply">
              ✓ Appliquer
            </button>
          </div>
          <div class="freq-note">
            ⚠️ Assurez-vous que l'émetteur (E220-900T22D) est configuré sur la même fréquence.
            Par défaut l'E220 utilise <strong>850.125 MHz</strong> (canal 0).
            Pour le band EU868, la fréquence standard est <strong>868.125 MHz</strong>.
          </div>
        </div>
      </div>

      <!-- ══ PANEL: MAP ════════════════════════════ -->
      <div class="panel" id="panel-map">
        <div class="section-title">Localisation de l'émetteur</div>
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
          🌐 Les tuiles de carte nécessitent une connexion Internet.
          La position est mise à jour dès réception d'un nouveau paquet GPS valide.
        </div>
      </div>

      <!-- ══ PANEL: DATA ═══════════════════════════ -->
      <div class="panel" id="panel-data">
        <div class="data-top">
          <div class="section-title" style="margin-bottom:0;flex:1">Historique des paquets reçus</div>
          <div style="display:flex;gap:10px;align-items:center">
            <span class="data-count"><strong id="histCountLabel">0</strong> / 60 entrées</span>
            <button class="btn btn-ghost" onclick="refreshHistory()">↻ Rafraîchir</button>
            <a href="/download" download="station_neige.csv">
              <button class="btn btn-primary">⬇ Télécharger CSV</button>
            </a>
          </div>
        </div>
        <div style="margin-top:18px" class="table-wrap">
          <table>
            <thead>
              <tr>
                <th>#</th><th>Heure</th><th>Date</th>
                <th>Latitude</th><th>Longitude</th><th>Alt (m)</th>
                <th>Vitesse</th><th>Neige (cm)</th><th>Sats</th>
                <th>RSSI</th><th>SNR</th>
              </tr>
            </thead>
            <tbody id="histBody">
              <tr><td colspan="11" style="text-align:center;padding:30px;color:var(--text-dim)">
                Aucune donnée — en attente de paquets LoRa
              </td></tr>
            </tbody>
          </table>
        </div>
      </div>

    </div><!-- end .panels -->
  </div><!-- end .content -->
</div><!-- end .layout -->

<script>
// ══ PANEL NAVIGATION ════════════════════════════
const panels = {
  dash:  {el:'panel-dash',nav:'nav-dash',title:'Tableau de bord',    sub:'Vue générale en temps réel'},
  scan:  {el:'panel-scan',nav:'nav-scan',title:'Scanner LoRa',       sub:'Analyse du spectre 850–870 MHz'},
  freq:  {el:'panel-freq',nav:'nav-freq',title:'Syntonisation',      sub:'Réglage de la fréquence de réception'},
  map:   {el:'panel-map', nav:'nav-map', title:'Carte GPS',          sub:'Position en temps réel de l\'émetteur'},
  data:  {el:'panel-data',nav:'nav-data',title:'Journal de données', sub:'Historique et export CSV'},
};
let activePanel = 'dash';
let mapInitialized = false;
let leafletMap = null;
let mapMarker = null;

function showPanel(id) {
  if (!panels[id]) return;
  const prev = panels[activePanel];
  const next = panels[id];
  document.getElementById(prev.el).classList.remove('active');
  document.getElementById(prev.nav).classList.remove('active');
  document.getElementById(next.el).classList.add('active');
  document.getElementById(next.nav).classList.add('active');
  document.getElementById('page-title').textContent = next.title;
  document.getElementById('page-sub').textContent   = next.sub;
  activePanel = id;

  if (id === 'map')  initOrUpdateMap();
  if (id === 'data') refreshHistory();
  if (id === 'freq') fetchCurrentFreq();
}

// ══ DASHBOARD ═══════════════════════════════════
let lastData = null;

const fmt1 = v => isNaN(+v) ? '---' : (+v).toFixed(1);
const fmt2 = v => isNaN(+v) ? '---' : (+v).toFixed(2);
const fmt5 = v => isNaN(+v) ? '---' : (+v).toFixed(5);

function rssiColor(r) {
  if (r > -80)  return 'var(--green)';
  if (r > -100) return 'var(--yellow)';
  return 'var(--red)';
}
function rssiPct(r)  { return Math.max(0,Math.min(100,((+r+130)/90)*100)).toFixed(0); }
function snrPct(s)   { return Math.max(0,Math.min(100,((+s+20)/30)*100)).toFixed(0);  }
function ageClass(ms){ return ms<10000?'fresh':ms<30000?'stale':'old'; }
function ageStr(ms)  {
  if (ms<4000)   return 'À l\'instant';
  if (ms<60000)  return `Il y a ${Math.floor(ms/1000)} s`;
  return `Il y a ${Math.floor(ms/60000)} min`;
}

function renderDash(d) {
  if (!d.valid) {
    document.getElementById('dash-inner').innerHTML = `
      <div class="no-data">
        <div class="no-data-icon">📡</div>
        <h2>En attente de données LoRa</h2>
        <p>Démarrez la station et patientez quelques secondes.</p>
      </div>`;
    return;
  }

  const dc  = +d.depth;
  const dm  = dc / 100;
  const age = +d.age_ms;
  const noD = dc < 0;

  // Update sidebar footer
  document.getElementById('sf-count').textContent = d.count;
  document.getElementById('sf-rssi').textContent  = d.rssi + ' dBm';
  document.getElementById('sf-freq').textContent  = window._curFreq ? window._curFreq.toFixed(3)+' MHz' : '---';

  // Live badge
  const badge = document.getElementById('liveBadge');
  badge.className = 'live-badge on';
  document.getElementById('liveText').textContent = 'En direct';

  document.getElementById('dash-inner').innerHTML = `
    <div class="last-rx">
      <span>Dernière réception : <strong>${d.date} · ${d.time_s}</strong></span>
      <span class="rx-age ${ageClass(age)}">${ageStr(age)}</span>
    </div>

    <div class="hero">
      <div>
        <div class="hero-lbl">Hauteur de neige</div>
        <div class="hero-val">${noD?'---':fmt1(dc)}<span>${noD?'':'cm'}</span></div>
        ${noD?'':`<div class="hero-sub">${fmt2(dm)} m</div>`}
      </div>
      <div class="hero-meta">
        <div class="hero-meta-row">📦 Paquets reçus</div>
        <div style="font-size:22px;font-weight:600;color:var(--blue);margin-bottom:10px">${d.count}</div>
        <div class="hero-meta-row">📡 RSSI <strong>${d.rssi} dBm</strong></div>
        <div class="hero-meta-row">〰 SNR  <strong>${fmt1(d.snr)} dB</strong></div>
      </div>
    </div>

    <div class="grid">
      <div class="card col-2 c-blue">
        <div class="card-label"><span class="card-icon">🌐</span> Coordonnées GPS</div>
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
            Ouvrir dans Google Maps →
          </a>
        </div>
      </div>

      <div class="card c-cyan">
        <div class="card-label"><span class="card-icon">⛰</span> Altitude</div>
        <div class="card-value">${fmt1(d.alt)}<span class="unit">m</span></div>
        <div class="card-sub">Au-dessus du niveau de la mer</div>
      </div>

      <div class="card c-yellow">
        <div class="card-label"><span class="card-icon">💨</span> Vitesse GPS</div>
        <div class="card-value">${fmt1(d.speed)}<span class="unit">km/h</span></div>
        <div class="card-sub">Déplacement de la station</div>
      </div>

      <div class="card c-green">
        <div class="card-label"><span class="card-icon">🛰</span> Satellites GPS</div>
        <div class="card-value">${d.sats}<span class="unit">sat</span></div>
        <div class="card-sub">HDOP : ${fmt2(d.hdop)}</div>
      </div>

      <div class="card">
        <div class="card-label"><span class="card-icon">📶</span> Qualité du signal</div>
        <div class="sig-wrap">
          <div class="sig-row">
            <span class="sig-lbl">RSSI</span>
            <div class="sig-track"><div class="sig-fill"
              style="width:${rssiPct(d.rssi)}%;background:${rssiColor(d.rssi)}"></div></div>
            <span class="sig-val">${d.rssi} dBm</span>
          </div>
          <div class="sig-row">
            <span class="sig-lbl">SNR</span>
            <div class="sig-track"><div class="sig-fill"
              style="width:${snrPct(d.snr)}%;background:var(--blue)"></div></div>
            <span class="sig-val">${fmt1(d.snr)} dB</span>
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-label"><span class="card-icon">🕐</span> Horodatage</div>
        <div class="card-value" style="font-size:20px;letter-spacing:-.3px">${d.time_s}</div>
        <div class="card-sub">${d.date} — heure locale</div>
      </div>
    </div>

    <div class="raw-wrap">
      <div class="raw-lbl">Trame brute LoRa reçue</div>
      <div class="raw-box">${d.raw}</div>
    </div>
  `;
}

async function fetchDash() {
  try {
    const r = await fetch('/data');
    const d = await r.json();
    lastData = d;
    if (activePanel === 'dash') renderDash(d);
  } catch(e) {
    const badge = document.getElementById('liveBadge');
    if (badge) { badge.className='live-badge'; document.getElementById('liveText').textContent='Hors ligne'; }
  }
}
setInterval(fetchDash, 2000);
fetchDash();

// ══ SCANNER ═════════════════════════════════════
async function startScan() {
  const btn  = document.getElementById('btnScan');
  const stat = document.getElementById('scanStatus');
  btn.disabled = true;
  stat.className = 'scan-status scanning';
  stat.textContent = 'Scan en cours… (~3 s)';
  document.getElementById('scanChart').innerHTML =
    '<div style="color:var(--yellow);font-size:13px;width:100%;text-align:center;padding-top:80px">⏳ Analyse du spectre LoRa…</div>';

  try {
    const r = await fetch('/scan');
    const d = await r.json();
    renderScanChart(d.scan);
    stat.className = 'scan-status';
    const best = d.scan.reduce((a,b)=>b.rssi>a.rssi?b:a,d.scan[0]);
    stat.textContent = `✓ Terminé · Signal max : ${best.rssi.toFixed(0)} dBm @ ${best.freq.toFixed(3)} MHz`;
  } catch(e) {
    stat.className = 'scan-status';
    stat.textContent = '✗ Erreur de scan';
    document.getElementById('scanChart').innerHTML =
      '<div style="color:var(--red);font-size:13px;width:100%;text-align:center;padding-top:80px">Erreur lors du scan</div>';
  }
  btn.disabled = false;
}

function renderScanChart(data) {
  if (!data || !data.length) return;
  const minR = -130, maxR = -40;
  const range = maxR - minR;
  const chartH = 200;
  let html = '';
  data.forEach(pt => {
    const pct  = Math.max(0, Math.min(100, ((pt.rssi - minR) / range) * 100));
    const h    = Math.max(4, Math.round((pct / 100) * chartH));
    const col  = pt.rssi > -85 ? 'var(--green)' : pt.rssi > -105 ? 'var(--yellow)' : 'rgba(255,255,255,0.2)';
    const tip  = `${pt.freq.toFixed(3)} MHz: ${pt.rssi.toFixed(0)} dBm`;
    html += `
      <div class="scan-bar-group">
        <div class="scan-bar" style="height:${h}px;background:${col}" data-tip="${tip}"></div>
        <div class="scan-freq">${pt.freq.toFixed(1)}</div>
      </div>`;
  });
  document.getElementById('scanChart').innerHTML = html;
}

// ══ FREQUENCY TUNER ══════════════════════════════
let pendingFreq = 868.125;
window._curFreq = 868.125;

function onSlider(v) {
  pendingFreq = +v;
  document.getElementById('freqInput').value = (+v).toFixed(3);
  updatePresetHighlight(+v);
}
function onFreqInput(v) {
  pendingFreq = +v;
  document.getElementById('freqSlider').value = v;
  updatePresetHighlight(+v);
}
function setFreqPreset(f) {
  pendingFreq = f;
  document.getElementById('freqSlider').value = f;
  document.getElementById('freqInput').value  = f.toFixed(3);
  updatePresetHighlight(f);
}
function updatePresetHighlight(f) {
  document.querySelectorAll('.freq-preset').forEach(el => {
    const val = parseFloat(el.textContent);
    el.classList.toggle('active-preset', Math.abs(val - f) < 0.01);
  });
}

async function fetchCurrentFreq() {
  try {
    const r = await fetch('/data');
    const d = await r.json();
    if (d.cur_freq) {
      window._curFreq = +d.cur_freq;
      document.getElementById('freqDisplay').innerHTML =
        `${(+d.cur_freq).toFixed(3)}<span>MHz</span>`;
      document.getElementById('freqSlider').value = d.cur_freq;
      document.getElementById('freqInput').value  = (+d.cur_freq).toFixed(3);
      pendingFreq = +d.cur_freq;
      updatePresetHighlight(+d.cur_freq);
    }
  } catch(e) {}
}

async function applyFreq() {
  const f = pendingFreq;
  if (f < 848 || f > 870) { alert('Fréquence hors plage (848–870 MHz)'); return; }
  const btn = document.getElementById('btnApply');
  btn.disabled = true;
  btn.textContent = '⏳ Application…';
  try {
    const r = await fetch(`/setfreq?f=${f.toFixed(3)}`);
    const d = await r.json();
    if (d.ok) {
      window._curFreq = f;
      document.getElementById('freqDisplay').innerHTML = `${f.toFixed(3)}<span>MHz</span>`;
      document.getElementById('sf-freq').textContent   = `${f.toFixed(3)} MHz`;
      btn.textContent = '✓ Appliqué !';
      setTimeout(()=>{ btn.textContent='✓ Appliquer'; btn.disabled=false; }, 2000);
    } else {
      throw new Error('fail');
    }
  } catch(e) {
    btn.textContent = '✗ Erreur';
    setTimeout(()=>{ btn.textContent='✓ Appliquer'; btn.disabled=false; }, 2000);
  }
}

// Init frequency panel on load
fetchCurrentFreq();

// ══ MAP ══════════════════════════════════════════
async function initOrUpdateMap() {
  const d = lastData || await fetch('/data').then(r=>r.json()).catch(()=>null);
  if (!d || !d.valid || !d.lat || +d.lat === 0) {
    document.getElementById('map').innerHTML =
      '<div style="display:flex;align-items:center;justify-content:center;height:100%;' +
      'color:var(--text-dim);font-size:14px">Pas de fix GPS disponible</div>';
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
      iconSize:[30,30], iconAnchor:[15,15], className:''
    });
    mapMarker = L.marker([lat, lon], {icon: snowIcon}).addTo(leafletMap);
    mapMarker.bindPopup(`
      <b>Station de neige</b><br>
      Profondeur : ${(+d.depth).toFixed(1)} cm<br>
      Alt : ${(+d.alt).toFixed(0)} m<br>
      ${d.date} ${d.time_s}
    `).openPopup();
    mapInitialized = true;
  } else {
    mapMarker.setLatLng([lat, lon]);
    leafletMap.setView([lat, lon]);
    leafletMap.invalidateSize();
  }
}

// ══ DATA / HISTORY ════════════════════════════════
async function refreshHistory() {
  try {
    const r = await fetch('/history');
    const d = await r.json();
    renderHistory(d);
  } catch(e) {}
}

function depthClass(v) {
  if (v < 0) return '';
  if (v > 50) return 'td-good';
  if (v > 10) return 'td-warn';
  return '';
}
function rssiClass(r) {
  if (r > -80)  return 'td-good';
  if (r > -100) return 'td-warn';
  return 'td-bad';
}

function renderHistory(data) {
  document.getElementById('histCountLabel').textContent = data.count || 0;
  if (!data.entries || !data.entries.length) {
    document.getElementById('histBody').innerHTML =
      '<tr><td colspan="11" style="text-align:center;padding:30px;color:var(--text-dim)">Aucun paquet reçu</td></tr>';
    return;
  }
  const rows = data.entries.map((e, i) => `
    <tr>
      <td>${data.count - data.entries.length + i + 1}</td>
      <td>${e.time_s}</td>
      <td>${e.date}</td>
      <td>${(+e.lat).toFixed(5)}</td>
      <td>${(+e.lon).toFixed(5)}</td>
      <td>${(+e.alt).toFixed(1)}</td>
      <td>${(+e.speed).toFixed(1)} km/h</td>
      <td class="${depthClass(+e.depth)}">${+e.depth>=0?(+e.depth).toFixed(1)+' cm':'---'}</td>
      <td>${e.sats}</td>
      <td class="${rssiClass(+e.rssi)}">${e.rssi} dBm</td>
      <td>${(+e.snr).toFixed(1)} dB</td>
    </tr>`).join('');
  document.getElementById('histBody').innerHTML = rows;
}
</script>
</body>
</html>
)HTMLEND";

// ═══════════════════════════════════════════════════════
//  PARSE PAYLOAD
//  CSV: lat,lon,alt,speed,depth,sats,hdop,date,time
// ═══════════════════════════════════════════════════════
void parsePayload(const String& payload) {
  char buf[128];
  payload.toCharArray(buf, sizeof(buf));

  float vals[7] = {};
  char  dateBuf[12] = "?";
  char  timeBuf[10] = "?";

  char* tok = strtok(buf, ",");
  int f = 0;
  while (tok && f < 9) {
    switch (f) {
      case 0: g_pkt.lat   = atof(tok); break;
      case 1: g_pkt.lon   = atof(tok); break;
      case 2: g_pkt.alt   = atof(tok); break;
      case 3: g_pkt.speed = atof(tok); break;
      case 4: g_pkt.depth = atof(tok); break;
      case 5: g_pkt.sats  = atoi(tok); break;
      case 6: g_pkt.hdop  = atof(tok); break;
      case 7: strncpy(g_pkt.date,   tok, sizeof(g_pkt.date)   - 1); break;
      case 8: strncpy(g_pkt.time_s, tok, sizeof(g_pkt.time_s) - 1); break;
    }
    tok = strtok(nullptr, ",");
    f++;
  }

  payload.toCharArray(g_pkt.raw, sizeof(g_pkt.raw));
  g_pkt.valid  = (f >= 7);
  g_pkt.lastMs = millis();
  g_pkt.count++;

  // Store in history
  HistEntry& h = g_hist[g_histHead];
  h.lat   = g_pkt.lat;   h.lon   = g_pkt.lon;
  h.alt   = g_pkt.alt;   h.speed = g_pkt.speed;
  h.depth = g_pkt.depth; h.sats  = g_pkt.sats;
  h.rssi  = g_pkt.rssi;  h.snr   = g_pkt.snr;
  strncpy(h.date,   g_pkt.date,   sizeof(h.date)   - 1);
  strncpy(h.time_s, g_pkt.time_s, sizeof(h.time_s) - 1);
  g_histHead = (g_histHead + 1) % MAX_HIST;
  if (g_histCount < MAX_HIST) g_histCount++;
}

// ═══════════════════════════════════════════════════════
//  RADIO HELPERS
// ═══════════════════════════════════════════════════════
bool radioInit(float freq) {
  int state = radio.begin(freq, LORA_BW, LORA_SF, LORA_CR,
                          LORA_SYNC, LORA_POWER, LORA_PREAMBLE);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[ LoRa ] begin() failed — code %d\n", state);
    return false;
  }
  radio.startReceive();
  Serial.printf("[ LoRa ] Ready  %.3f MHz  SF%d  BW%.0f\n", freq, LORA_SF, LORA_BW);
  return true;
}

void radioRxRestart() {
  radio.startReceive();
}

// ═══════════════════════════════════════════════════════
//  SCANNER — sweep 850–870 MHz, 1 MHz steps
// ═══════════════════════════════════════════════════════
void doScan() {
  g_scanning = true;
  Serial.println("[ SCAN ] Starting sweep 850–870 MHz...");

  for (int i = 0; i < SCAN_N; i++) {
    float f = SCAN_FROM + i * SCAN_STEP;
    g_scan[i].freq = f;

    radio.standby();
    radio.setFrequency(f);
    radio.startReceive();
    delay(SCAN_DWELL_MS);
    g_scan[i].rssi = radio.getRSSI(false);  // instantaneous RSSI

    Serial.printf("  %.1f MHz → %.0f dBm\n", f, g_scan[i].rssi);
    server.handleClient();  // keep watchdog happy
  }

  // Restore operating frequency
  radio.standby();
  radio.setFrequency(g_freq);
  radio.startReceive();

  g_scanDone = true;
  g_scanning = false;
  Serial.println("[ SCAN ] Done — RX restored");
}

// ═══════════════════════════════════════════════════════
//  WEB HANDLERS
// ═══════════════════════════════════════════════════════
void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  uint32_t age = g_pkt.lastMs > 0 ? (millis() - g_pkt.lastMs) : 9999999u;
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
    "\"cur_freq\":%.3f,"
    "\"raw\":\"%s\""
    "}",
    g_pkt.valid ? "true" : "false",
    g_pkt.lat, g_pkt.lon,
    g_pkt.alt, g_pkt.speed,
    g_pkt.depth,
    g_pkt.sats, g_pkt.hdop,
    g_pkt.date, g_pkt.time_s,
    g_pkt.rssi, g_pkt.snr,
    g_pkt.count, age,
    g_freq,
    g_pkt.raw
  );
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", json);
}

void handleScan() {
  if (g_scanning) {
    server.send(503, "application/json", "{\"error\":\"scan in progress\"}");
    return;
  }
  doScan();

  String json = "{\"scan\":[";
  for (int i = 0; i < SCAN_N; i++) {
    if (i) json += ',';
    char entry[48];
    snprintf(entry, sizeof(entry), "{\"freq\":%.3f,\"rssi\":%.1f}",
             g_scan[i].freq, g_scan[i].rssi);
    json += entry;
  }
  json += "]}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleSetFreq() {
  if (!server.hasArg("f")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing f\"}");
    return;
  }
  float f = server.arg("f").toFloat();
  if (f < 848.0f || f > 870.0f) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"out of range\"}");
    return;
  }
  g_freq = f;

  radio.standby();
  int state = radio.setFrequency(g_freq);
  radio.startReceive();

  Serial.printf("[ FREQ ] Changed to %.3f MHz (state %d)\n", g_freq, state);

  char json[64];
  snprintf(json, sizeof(json), "{\"ok\":true,\"freq\":%.3f}", g_freq);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleHistory() {
  // Return last g_histCount packets in chronological order
  String json = "{\"count\":";
  json += g_pkt.count;
  json += ",\"entries\":[";

  // Reconstruct chronological order from circular buffer
  int start = (g_histCount < MAX_HIST) ? 0 : g_histHead;
  for (int i = 0; i < g_histCount; i++) {
    int idx = (start + i) % MAX_HIST;
    HistEntry& e = g_hist[idx];
    if (i) json += ',';
    char entry[256];
    snprintf(entry, sizeof(entry),
      "{\"lat\":%.5f,\"lon\":%.5f,\"alt\":%.1f,\"speed\":%.1f,"
      "\"depth\":%.1f,\"sats\":%d,\"rssi\":%d,\"snr\":%.1f,"
      "\"date\":\"%s\",\"time_s\":\"%s\"}",
      e.lat, e.lon, e.alt, e.speed,
      e.depth, e.sats, e.rssi, e.snr,
      e.date, e.time_s
    );
    json += entry;
  }
  json += "]}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", json);
}

void handleDownload() {
  // Stream CSV file to browser
  server.sendHeader("Content-Disposition", "attachment; filename=station_neige.csv");
  server.sendHeader("Cache-Control", "no-cache");

  String csv = "date,heure,latitude,longitude,altitude_m,vitesse_kmh,neige_cm,satellites,rssi_dbm,snr_db\n";

  int start = (g_histCount < MAX_HIST) ? 0 : g_histHead;
  for (int i = 0; i < g_histCount; i++) {
    int idx = (start + i) % MAX_HIST;
    HistEntry& e = g_hist[idx];
    char row[200];
    snprintf(row, sizeof(row),
      "%s,%s,%.5f,%.5f,%.1f,%.1f,%.1f,%d,%d,%.1f\n",
      e.date, e.time_s,
      e.lat, e.lon, e.alt, e.speed,
      e.depth, e.sats, e.rssi, e.snr
    );
    csv += row;
  }

  server.send(200, "text/csv", csv);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ═══════════════════════════════════════════════════════
//  LORA RX POLL
// ═══════════════════════════════════════════════════════
void checkLoRa() {
  if (g_scanning) return;  // don't interfere during scan
  if (!radio.available()) return;

  String payload;
  int state = radio.readData(payload);

  if (state == RADIOLIB_ERR_NONE) {
    g_pkt.rssi = (int)radio.getRSSI();
    g_pkt.snr  = radio.getSNR();
    Serial.printf("[ LoRa ] RX  RSSI=%d dBm  SNR=%.1f dB  len=%d\n",
                  g_pkt.rssi, g_pkt.snr, payload.length());
    Serial.printf("         %s\n", payload.c_str());
    parsePayload(payload);
    digitalWrite(LED_PIN, HIGH);
    delay(40);
    digitalWrite(LED_PIN, LOW);
  } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    Serial.println("[ LoRa ] CRC error — dropped");
  } else {
    Serial.printf("[ LoRa ] Read error — code %d\n", state);
  }
  radio.startReceive();  // always re-arm
}

// ═══════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);

  // Power up external VCC rail (antenna switch on Heltec V3)
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);  // LOW = ON
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // ── SPI + Radio ────────────────────────────────────
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  if (!radioInit(g_freq)) {
    // Blink to signal init failure but continue
    for (int i = 0; i < 8; i++) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(150); }
  }

  // ── WiFi: try STA first, fall back to AP ───────────
  WiFi.mode(WIFI_STA);
  Serial.printf("[ WiFi ] Connecting to \"%s\"...\n", WIFI_STA_SSID);

  if (strlen(WIFI_STA_PASS) > 0)
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
  else
    WiFi.begin(WIFI_STA_SSID);   // open network

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiIsAP = false;
    Serial.printf("[ WiFi ] STA connected — IP %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[ HTTP ] Open  http://%s\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin("stationneige")) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("[ mDNS ] http://stationneige.local");
    }
  } else {
    // Fallback: create own AP
    wifiIsAP = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.printf("[ WiFi ] AP mode — SSID \"%s\"  pass \"%s\"\n", WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.printf("[ HTTP ] Open  http://%s\n", WiFi.softAPIP().toString().c_str());
  }

  // ── Routes ─────────────────────────────────────────
  server.on("/",        HTTP_GET, handleRoot);
  server.on("/data",    HTTP_GET, handleData);
  server.on("/scan",    HTTP_GET, handleScan);
  server.on("/setfreq", HTTP_GET, handleSetFreq);
  server.on("/history", HTTP_GET, handleHistory);
  server.on("/download",HTTP_GET, handleDownload);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println();
  Serial.println("═══════════════════════════════════════════════");
  Serial.println("  Snow Receiver v2 — prêt");
  if (wifiIsAP) {
    Serial.printf("  WiFi  : %s / %s\n", WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.printf("  URL   : http://%s\n", WiFi.softAPIP().toString().c_str());
  } else {
    Serial.printf("  URL   : http://%s\n", WiFi.localIP().toString().c_str());
    Serial.println("  URL   : http://stationneige.local");
  }
  Serial.println("═══════════════════════════════════════════════");
}

// ═══════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════
void loop() {
  server.handleClient();
  checkLoRa();
}
