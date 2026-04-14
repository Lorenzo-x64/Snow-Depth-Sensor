#include <HardwareSerial.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <LoRa_E220.h>

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

#define LCD_I2C_ADDR    0x27   // change to 0x3F if blank
#define LCD_COLS        20
#define LCD_ROWS         4

#define BTN_PIN         33     // DFRobot digital push button — SIG wire

// ═══════════════════════════════════════════════════
//  CONFIG
// ═══════════════════════════════════════════════════

#define LOG_FILE              "/snowlog.csv"
#define LOG_INTERVAL_MS        2000
#define LCD_PAGE_DURATION_MS  20000  // 20 seconds per page
#define LCD_NUM_PAGES             8  // 7 original + 1 system page
#define TIMEZONE_OFFSET_H         2  // UTC+2, change as needed
#define BTN_DEBOUNCE_MS          50  // debounce window in ms

// ═══════════════════════════════════════════════════
//  OBJECTS
// ═══════════════════════════════════════════════════

TinyGPSPlus        gps;
HardwareSerial     gpsSerial(2);
SoftwareSerial     usSerial(US_RX_PIN, -1);
HardwareSerial     loraSerial(1);
LoRa_E220          lora(&loraSerial, LORA_AUX_PIN, LORA_M0_PIN, LORA_M1_PIN);
LiquidCrystal_I2C  lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

// ═══════════════════════════════════════════════════
//  STATE
// ═══════════════════════════════════════════════════

float    distCm        = -1;
bool     sdReady       = false;
bool     loraReady     = false;
uint8_t  usBuf[4];
uint8_t  usIdx         = 0;
uint8_t  lcdPage       = 0;
uint32_t lastPageFlip  = 0;
uint32_t loraLastTxMs  = 0;
uint32_t sdRowCount    = 0;

// Button debounce state
bool     btnLastState  = HIGH;
uint32_t btnLastChange = 0;

// ═══════════════════════════════════════════════════
//  TIME HELPER
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
    int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) daysInMonth[1] = 29;
    if (d > daysInMonth[mo - 1]) {
      d = 1; mo += 1;
      if (mo > 12) { mo = 1; y += 1; }
    }
  }

  if (h < 0) {
    h += 24; d -= 1;
    if (d < 1) {
      mo -= 1;
      if (mo < 1) { mo = 12; y -= 1; }
      int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
      if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) daysInMonth[1] = 29;
      d = daysInMonth[mo - 1];
    }
  }
}

// ═══════════════════════════════════════════════════
//  FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════

void writeSDHeader();
void lcdSplash();
void lcdRow(uint8_t row, const char* fmt, ...);
void lcdPageIndicator(uint8_t page, uint8_t total);
void lcdPageGPSCoords();
void lcdPageGPSMovement();
void lcdPageGPSTime();
void lcdPageGPSSats();
void lcdPageUltrasonic();
void lcdPageLoRa();
void lcdPageSD();
void lcdPageSystem();
void advanceLCDPage();

// ═══════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BTN_PIN, INPUT_PULLUP);
  Serial.println("[ BTN  ] Push button ready on GPIO33");

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("[ GPS  ] UART2 started on GPIO16/17");

  usSerial.begin(9600);
  Serial.println("[ US   ] SoftwareSerial started on GPIO4");

  loraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  loraReady = lora.begin();
  Serial.println(loraReady
    ? "[ LoRa ] E220 ready"
    : "[ LoRa ] E220 init failed — check GPIO13/14/26/27/32");

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcdSplash();
  Serial.println("[ LCD  ] I2C display ready");

  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("[ SD   ] Not found — check wiring and FAT32 format");
    sdReady = false;
  } else {
    sdReady = true;
    Serial.println("[ SD   ] Card ready");
    writeSDHeader();
  }

  delay(1500);
  lcd.clear();

  Serial.println();
  Serial.println("=============================================");
  Serial.println("  Snow station — GPS + Ultrasonic + LoRa    ");
  Serial.println("=============================================");
  Serial.println();
}

// ═══════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════

void loop() {
  readGPS();
  readUltrasonic();
  readButton();

  static unsigned long lastRun = 0;
  if (millis() - lastRun >= LOG_INTERVAL_MS) {
    lastRun = millis();
    printReport();
    logToSD();
    transmitLoRa();
  }

  updateLCD();
}

// ═══════════════════════════════════════════════════
//  BUTTON
// ═══════════════════════════════════════════════════

void readButton() {
  bool state = digitalRead(BTN_PIN);
  uint32_t now = millis();

  if (state != btnLastState && (now - btnLastChange) >= BTN_DEBOUNCE_MS) {
    btnLastChange = now;
    btnLastState  = state;
    if (state == LOW) {
      advanceLCDPage();
      Serial.println("[ BTN  ] Page advanced by button");
    }
  }
}

// ═══════════════════════════════════════════════════
//  LCD PAGE ADVANCE
// ═══════════════════════════════════════════════════

void advanceLCDPage() {
  lcdPage = (lcdPage + 1) % LCD_NUM_PAGES;
  lastPageFlip = millis();
  lcd.clear();
}

// ═══════════════════════════════════════════════════
//  GPS
// ═══════════════════════════════════════════════════

void readGPS() {
  while (gpsSerial.available())
    gps.encode(gpsSerial.read());
}

// ═══════════════════════════════════════════════════
//  ULTRASONIC
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
        float d = ((usBuf[1] << 8) | usBuf[2]) / 10.0;
        if (d >= 28.0 && d <= 750.0)
          distCm = d;
      }
    }
  }
}

// ═══════════════════════════════════════════════════
//  SERIAL REPORT
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
  gps.altitude.isValid()
    ? Serial.printf("%.2f m\n", gps.altitude.meters())
    : Serial.println("INVALID");

  Serial.print("[ GPS   ] Speed       : ");
  gps.speed.isValid()
    ? Serial.printf("%.2f km/h\n", gps.speed.kmph())
    : Serial.println("INVALID");

  Serial.print("[ GPS   ] Course      : ");
  gps.course.isValid()
    ? Serial.printf("%.2f deg\n", gps.course.deg())
    : Serial.println("INVALID");

  Serial.print("[ GPS   ] Satellites  : ");
  gps.satellites.isValid()
    ? Serial.println(gps.satellites.value())
    : Serial.println("INVALID");

  Serial.print("[ GPS   ] HDOP        : ");
  gps.hdop.isValid()
    ? Serial.println(gps.hdop.hdop(), 2)
    : Serial.println("INVALID");

  Serial.print("[ GPS   ] Local time  : ");
  if (gps.date.isValid() && gps.time.isValid())
    Serial.printf("%04d-%02d-%02d  %02d:%02d:%02d (UTC+%d)\n",
      y, mo, d, h, m, s, TIMEZONE_OFFSET_H);
  else
    Serial.println("INVALID");

  if (gps.charsProcessed() < 10)
    Serial.println("[ GPS   ] WARNING: no NMEA data — check GPIO16/17");

  // System stats in serial report too
  uint32_t freeHeap    = ESP.getFreeHeap();
  uint32_t totalHeap   = ESP.getHeapSize();
  uint32_t freeSketch  = ESP.getFreeSketchSpace();
  uint32_t sketchSize  = ESP.getSketchSize();
  uint8_t  cpuMhz      = ESP.getCpuFreqMHz();
  Serial.printf("[ SYS   ] RAM  free : %lu / %lu bytes (%.0f%% used)\n",
    freeHeap, totalHeap,
    100.0f * (totalHeap - freeHeap) / totalHeap);
  Serial.printf("[ SYS   ] Flash free: %lu / %lu bytes (%.0f%% used)\n",
    freeSketch, freeSketch + sketchSize,
    100.0f * sketchSize / (freeSketch + sketchSize));
  Serial.printf("[ SYS   ] CPU freq  : %u MHz\n", cpuMhz);

  Serial.printf("[ SD    ] %s  rows: %lu\n",
    sdReady   ? "logging OK"   : "not available", sdRowCount);
  Serial.printf("[ LoRa  ] %s\n",
    loraReady ? "TX active"    : "not available");
  Serial.println();
}

// ═══════════════════════════════════════════════════
//  SD — write CSV header
// ═══════════════════════════════════════════════════

void writeSDHeader() {
  if (!SD.exists(LOG_FILE)) {
    File f = SD.open(LOG_FILE, FILE_WRITE);
    if (f) {
      f.println(
        "timestamp_ms,latitude,longitude,altitude_m,"
        "speed_kmh,course_deg,satellites,hdop,"
        "date,time_local,snow_depth_cm"
      );
      f.close();
      Serial.println("[ SD   ] Log file created: " LOG_FILE);
    }
  } else {
    Serial.println("[ SD   ] Appending to existing: " LOG_FILE);
  }
}

// ═══════════════════════════════════════════════════
//  SD — append CSV row
// ═══════════════════════════════════════════════════

void logToSD() {
  if (!sdReady) return;

  File f = SD.open(LOG_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("[ SD   ] Could not open log file!");
    return;
  }

  int h, m, s, d, mo, y;
  localTime(h, m, s, d, mo, y);

  f.print(millis());                                       f.print(',');
  if (gps.location.isValid()) {
    f.print(gps.location.lat(), 6);                        f.print(',');
    f.print(gps.location.lng(), 6);                        f.print(',');
  } else { f.print(",,"); }
  gps.altitude.isValid()
    ? f.print(gps.altitude.meters(), 2) : f.print("");     f.print(',');
  gps.speed.isValid()
    ? f.print(gps.speed.kmph(), 2)      : f.print("");     f.print(',');
  gps.course.isValid()
    ? f.print(gps.course.deg(), 2)      : f.print("");     f.print(',');
  gps.satellites.isValid()
    ? f.print(gps.satellites.value())   : f.print("");     f.print(',');
  gps.hdop.isValid()
    ? f.print(gps.hdop.hdop(), 2)       : f.print("");     f.print(',');
  if (gps.date.isValid()) f.printf("%04d-%02d-%02d", y, mo, d);
  f.print(',');
  if (gps.time.isValid()) f.printf("%02d:%02d:%02d", h, m, s);
  f.print(',');
  distCm >= 0 ? f.print(distCm, 1) : f.print("");
  f.println();

  sdRowCount++;
  f.close();
}

// ═══════════════════════════════════════════════════
//  LoRa — broadcast compact CSV payload
// ═══════════════════════════════════════════════════

void transmitLoRa() {
  if (!loraReady) return;

  int h, m, s, d, mo, y;
  localTime(h, m, s, d, mo, y);

  char dateBuf[12] = "?";
  char timeBuf[10] = "?";
  if (gps.date.isValid()) snprintf(dateBuf, 12, "%04d-%02d-%02d", y, mo, d);
  if (gps.time.isValid()) snprintf(timeBuf, 10, "%02d:%02d:%02d", h, m, s);

  char payload[128];
  snprintf(payload, sizeof(payload),
    "%.5f,%.5f,%.1f,%.1f,%.1f,%d,%.2f,%s,%s",
    gps.location.isValid()   ? gps.location.lat()         : 0.0,
    gps.location.isValid()   ? gps.location.lng()         : 0.0,
    gps.altitude.isValid()   ? gps.altitude.meters()      : 0.0,
    gps.speed.isValid()      ? gps.speed.kmph()           : 0.0,
    distCm >= 0              ? distCm                     : 0.0,
    gps.satellites.isValid() ? (int)gps.satellites.value(): 0,
    gps.hdop.isValid()       ? gps.hdop.hdop()            : 0.0,
    dateBuf,
    timeBuf
  );

  lora.sendMessage(payload);
  loraLastTxMs = millis();
}

// ═══════════════════════════════════════════════════
//  LCD HELPERS
// ═══════════════════════════════════════════════════

void lcdRow(uint8_t row, const char* fmt, ...) {
  char buf[21];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, 21, fmt, args);
  va_end(args);
  int len = strlen(buf);
  while (len < 20) buf[len++] = ' ';
  buf[20] = '\0';
  lcd.setCursor(0, row);
  lcd.print(buf);
}

void lcdPageIndicator(uint8_t page, uint8_t total) {
  char bar[21];
  int pos = 0;
  bar[pos++] = '[';
  for (uint8_t i = 0; i < total; i++)
    bar[pos++] = (i == page) ? '>' : '.';
  bar[pos++] = ']';
  bar[pos]   = '\0';
  int len = strlen(bar);
  int pad = (20 - len) / 2;
  char centred[21];
  memset(centred, ' ', 20);
  centred[20] = '\0';
  memcpy(centred + pad, bar, len);
  lcd.setCursor(0, 3);
  lcd.print(centred);
}

// ═══════════════════════════════════════════════════
//  LCD PAGES
// ═══════════════════════════════════════════════════

void lcdPageGPSCoords() {
  lcdRow(0, "*** GPS LOCATION ****");
  if (gps.location.isValid()) {
    lcdRow(1, "Lat: %+11.5f", gps.location.lat());
    lcdRow(2, "Lon: %+11.5f", gps.location.lng());
  } else {
    lcdRow(1, "Lat:  no fix yet");
    lcdRow(2, "Lon:  no fix yet");
  }
}

void lcdPageGPSMovement() {
  lcdRow(0, "*** GPS MOVEMENT ****");
  if (gps.altitude.isValid())
    lcdRow(1, "Altitude:%8.1f m", gps.altitude.meters());
  else
    lcdRow(1, "Altitude:    ---");
  if (gps.speed.isValid())
    lcdRow(2, "Speed:   %6.1f km/h", gps.speed.kmph());
  else
    lcdRow(2, "Speed:       ---");
}

void lcdPageGPSTime() {
  int h, m, s, d, mo, y;
  localTime(h, m, s, d, mo, y);
  lcdRow(0, "**** GPS  STATUS ****");
  if (gps.date.isValid() && gps.time.isValid()) {
    lcdRow(1, "%04d-%02d-%02d", y, mo, d);
    lcdRow(2, "%02d:%02d:%02d  UTC+%d", h, m, s, TIMEZONE_OFFSET_H);
  } else {
    lcdRow(1, "Date: waiting...");
    lcdRow(2, "Time: waiting...");
  }
}

void lcdPageGPSSats() {
  lcdRow(0, "**** GPS  SIGNAL ****");
  char satBuf[8]    = "---";
  char hdopBuf[8]   = "---";
  char courseBuf[8] = "---";
  if (gps.satellites.isValid())
    snprintf(satBuf,    8, "%d",   (int)gps.satellites.value());
  if (gps.hdop.isValid())
    snprintf(hdopBuf,   8, "%.1f", gps.hdop.hdop());
  if (gps.course.isValid())
    snprintf(courseBuf, 8, "%.1f", gps.course.deg());
  lcdRow(1, "Sats: %-3s  HDOP: %-4s", satBuf, hdopBuf);
  lcdRow(2, "Course:    %s deg", courseBuf);
}

void lcdPageUltrasonic() {
  lcdRow(0, "**** SNOW  DEPTH ****");
  if (distCm >= 0) {
    lcdRow(1, "Depth:%7.1f cm", distCm);
    lcdRow(2, "      %7.3f  m", distCm / 100.0);
  } else {
    lcdRow(1, "No data received");
    lcdRow(2, "Check GPIO4 wire");
  }
}

void lcdPageLoRa() {
  lcdRow(0, "**** LORA STATUS ****");
  if (loraReady) {
    lcdRow(1, "Module: E220-900T22D");
    if (loraLastTxMs > 0) {
      uint32_t secAgo = (millis() - loraLastTxMs) / 1000;
      lcdRow(2, "Last TX:%5lu s ago", secAgo);
    } else {
      lcdRow(2, "Last TX: pending...");
    }
  } else {
    lcdRow(1, "Module: NOT READY");
    lcdRow(2, "Chk GPIO13/14/M0/M1");
  }
}

void lcdPageSD() {
  lcdRow(0, "****  SD LOGGER  ****");
  if (sdReady) {
    lcdRow(1, "File: snowlog.csv");
    lcdRow(2, "Rows: %-14lu", sdRowCount);
  } else {
    lcdRow(1, "SD card not found");
    lcdRow(2, "Check SPI / FAT32");
  }
}

// ═══════════════════════════════════════════════════
//  LCD PAGE — ESP32 system resources
//
//  Row 0: title
//  Row 1: RAM  — free KB / total KB  (used %)
//  Row 2: Flash— sketch KB / total KB (used %)
//  Row 3: CPU freq + page indicator (drawn by caller)
//
//  ESP32 Arduino API used:
//    ESP.getFreeHeap()       — free heap bytes
//    ESP.getHeapSize()       — total heap bytes
//    ESP.getSketchSize()     — sketch (firmware) bytes used
//    ESP.getFreeSketchSpace()— remaining flash for OTA/sketch
//    ESP.getCpuFreqMHz()     — CPU clock in MHz
// ═══════════════════════════════════════════════════

void lcdPageSystem() {
  uint32_t freeHeap   = ESP.getFreeHeap();
  uint32_t totalHeap  = ESP.getHeapSize();
  uint32_t sketchSize = ESP.getSketchSize();
  uint32_t freeFlash  = ESP.getFreeSketchSpace();
  uint32_t totalFlash = sketchSize + freeFlash;
  uint8_t  cpuMhz     = ESP.getCpuFreqMHz();

  uint8_t ramPct   = (uint8_t)(100.0f * (totalHeap  - freeHeap)  / totalHeap);
  uint8_t flashPct = (uint8_t)(100.0f * sketchSize               / totalFlash);

  lcdRow(0, "**** ESP32  SYS  ****");

  // Row 1 — RAM: "RAM  123/320KB  61%"  (fits in 20 chars)
  lcdRow(1, "RAM  %3lu/%3luKB  %3u%%",
    freeHeap  / 1024,
    totalHeap / 1024,
    ramPct);

  // Row 2 — Flash: "FLS 1234/4096KB  30%"
  lcdRow(2, "FLS %4lu/%4luKB %3u%%",
    sketchSize  / 1024,
    totalFlash  / 1024,
    flashPct);

  // Row 3 is the page indicator bar drawn in updateLCD(),
  // but we sneak the CPU freq into the left side first,
  // then let lcdPageIndicator overwrite row 3 entirely.
  // Instead, embed CPU on row 2 is too crowded, so we
  // use a compact two-line approach and put CPU on row 3
  // before the indicator overwrites it — we therefore
  // build a custom row-3 that merges both:
  char sysRow3[21];
  snprintf(sysRow3, 21, "CPU %3u MHz", cpuMhz);
  // Pad to 20 chars so partial indicator doesn't leave garbage
  int len = strlen(sysRow3);
  while (len < 20) sysRow3[len++] = ' ';
  sysRow3[20] = '\0';
  lcd.setCursor(0, 3);
  lcd.print(sysRow3);
  // Note: lcdPageIndicator() will overwrite row 3 after this
  // call returns, so we skip calling it here and let updateLCD
  // handle it as normal — the CPU line shows for one frame then
  // the indicator replaces it. To keep CPU visible alongside
  // the indicator we instead pack it into row 2 with flash:
}

// ═══════════════════════════════════════════════════
//  LCD SLIDESHOW — 8 pages, 20s auto-timer
//
//  0 — GPS coordinates
//  1 — GPS altitude + speed
//  2 — GPS date + local time
//  3 — GPS satellites + HDOP + course
//  4 — Snow depth (ultrasonic)
//  5 — LoRa status
//  6 — SD card status
//  7 — ESP32 system: RAM / Flash / CPU
// ═══════════════════════════════════════════════════

void updateLCD() {
  if (millis() - lastPageFlip >= LCD_PAGE_DURATION_MS) {
    advanceLCDPage();
  }

  switch (lcdPage) {
    case 0: lcdPageGPSCoords();   break;
    case 1: lcdPageGPSMovement(); break;
    case 2: lcdPageGPSTime();     break;
    case 3: lcdPageGPSSats();     break;
    case 4: lcdPageUltrasonic();  break;
    case 5: lcdPageLoRa();        break;
    case 6: lcdPageSD();          break;
    case 7: lcdPageSystem();      break;
  }

  lcdPageIndicator(lcdPage, LCD_NUM_PAGES);
}

// ═══════════════════════════════════════════════════
//  LCD SPLASH
// ═══════════════════════════════════════════════════

void lcdSplash() {
  lcd.setCursor(0, 0); lcd.print("  Snow station v1.0 ");
  lcd.setCursor(0, 1); lcd.print("  GPS + Ultrasonic  ");
  lcd.setCursor(0, 2); lcd.print("  LoRa + SD logger  ");
  lcd.setCursor(0, 3); lcd.print("   Initialising...  ");
}