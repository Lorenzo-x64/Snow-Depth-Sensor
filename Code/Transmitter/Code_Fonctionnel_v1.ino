//Le vendredi 3 avril

#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SD.h>
#include <SPI.h>

// ── GPS on UART2 ──────────────────────────────────────
#define GPS_RX_PIN      16
#define GPS_TX_PIN      17
#define GPS_BAUD        9600

// ── Ultrasonic (SEN0313) on UART1 ────────────────────
#define US_RX_PIN       4
#define US_TX_PIN       2
#define US_BAUD         9600

// ── SD card SPI ───────────────────────────────────────
#define SD_CS_PIN       5

// ── LCD I2C ───────────────────────────────────────────
// If screen stays blank after upload, change 0x27 to 0x3F
#define LCD_ADDR        0x27

// ── Snow sensor baseline (bare-ground distance) ───────
#define BASELINE_CM     100.0f

// ── Timezone offset ───────────────────────────────────
// France: UTC+2 in summer (CEST), UTC+1 in winter (CET)
#define UTC_OFFSET_HOURS  2

TinyGPSPlus        gps;
HardwareSerial     gpsSerial(2);
HardwareSerial     usSerial(1);
LiquidCrystal_I2C  lcd(LCD_ADDR, 20, 4);

uint8_t  usBuffer[4];
uint8_t  usIndex     = 0;
float    lastDistCm  = -1.0f;
float    snowDepthCm = -1.0f;
bool     sdReady     = false;

const char* LOG_FILE = "/snowlog.csv";

// ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  usSerial.begin(US_BAUD,   SERIAL_8N1, US_RX_PIN,  US_TX_PIN);

  // LCD
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("  Snow station v1   ");
  lcd.setCursor(0, 1); lcd.print("  GPS+Ultrasonic    ");
  lcd.setCursor(0, 2); lcd.print("  Waiting for fix...");
  lcd.setCursor(0, 3); lcd.print("                    ");

  // SD
  if (SD.begin(SD_CS_PIN)) {
    sdReady = true;
    if (!SD.exists(LOG_FILE)) {
      File f = SD.open(LOG_FILE, FILE_WRITE);
      if (f) {
        f.println("timestamp_local,lat,lng,altitude_m,"
                  "speed_kmh,satellites,hdop,"
                  "distance_cm,snow_depth_cm");
        f.close();
      }
    }
    Serial.println("[SD] Ready.");
  } else {
    Serial.println("[SD] Init FAILED — check wiring.");
  }

  Serial.println("[SYS] Snow station started.");
}

// ─────────────────────────────────────────────────────
void loop() {
  readGPS();
  readUltrasonic();

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 2000UL) {
    lastUpdate = millis();
    updateLCD();
    printSerial();
    logToSD();
  }
}

// ─────────────────────────────────────────────────────
void readGPS() {
  while (gpsSerial.available() > 0)
    gps.encode(gpsSerial.read());
}

// ─────────────────────────────────────────────────────
//  SEN0313 — 4-byte packet: [0xFF][H][L][checksum]
//  distance mm = (H<<8)|L
// ─────────────────────────────────────────────────────
void readUltrasonic() {
  while (usSerial.available() > 0) {
    uint8_t b = usSerial.read();
    if (usIndex == 0 && b != 0xFF) continue;
    usBuffer[usIndex++] = b;
    if (usIndex == 4) {
      usIndex = 0;
      uint8_t chk = (usBuffer[0] + usBuffer[1] + usBuffer[2]) & 0xFF;
      if (chk == usBuffer[3]) {
        float distCm = ((usBuffer[1] << 8) | usBuffer[2]) / 10.0f;
        if (distCm >= 28.0f && distCm <= 750.0f) {
          lastDistCm        = distCm;
          float depth       = BASELINE_CM - distCm;
          snowDepthCm       = (depth > 0.0f) ? depth : 0.0f;
        }
      }
    }
  }
}

// ─────────────────────────────────────────────────────
//  Returns local hour with UTC offset applied
// ─────────────────────────────────────────────────────
int localHour() {
  return (gps.time.hour() + UTC_OFFSET_HOURS) % 24;
}

// ─────────────────────────────────────────────────────
//  LCD layout — 20 chars x 4 rows:
//
//  Row 0: "LAT: +48.858600    "
//  Row 1: "LNG:  +2.294500    "
//  Row 2: "ALT:  35.2m   9SAT "
//  Row 3: "SNW: 12.4cm 10:45:00"
// ─────────────────────────────────────────────────────
void updateLCD() {
  char buf[21];

  // Row 0 — Latitude
  lcd.setCursor(0, 0);
  if (gps.location.isValid())
    snprintf(buf, sizeof(buf), "LAT:%+12.6f ", gps.location.lat());
  else
    snprintf(buf, sizeof(buf), "LAT:  --no fix--    ");
  lcd.print(buf);

  // Row 1 — Longitude
  lcd.setCursor(0, 1);
  if (gps.location.isValid())
    snprintf(buf, sizeof(buf), "LNG:%+12.6f ", gps.location.lng());
  else
    snprintf(buf, sizeof(buf), "LNG:  --no fix--    ");
  lcd.print(buf);

  // Row 2 — Altitude + satellites
  lcd.setCursor(0, 2);
  int sats = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
  if (gps.altitude.isValid())
    snprintf(buf, sizeof(buf), "ALT:%6.1fm  %2dSAT", gps.altitude.meters(), sats);
  else
    snprintf(buf, sizeof(buf), "ALT:  ----    %2dSAT", sats);
  lcd.print(buf);

  // Row 3 — Snow depth + local time
  lcd.setCursor(0, 3);
  char timeStr[9];
  if (gps.time.isValid())
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
      localHour(), gps.time.minute(), gps.time.second());
  else
    snprintf(timeStr, sizeof(timeStr), "--:--:--");

  if (lastDistCm >= 0.0f)
    snprintf(buf, sizeof(buf), "SNW:%5.1fcm %s", snowDepthCm, timeStr);
  else
    snprintf(buf, sizeof(buf), "SNW: ----   %s", timeStr);
  lcd.print(buf);
}

// ─────────────────────────────────────────────────────
void printSerial() {
  Serial.println("------------------------------------");

  Serial.print("[SNOW] Distance   : ");
  if (lastDistCm >= 0.0f)
    Serial.printf("%.1f cm  |  snow depth: %.1f cm\n",
      lastDistCm, snowDepthCm);
  else
    Serial.println("no data yet");

  Serial.print("[GPS]  Location   : ");
  if (gps.location.isValid())
    Serial.printf("%.6f, %.6f\n",
      gps.location.lat(), gps.location.lng());
  else
    Serial.println("no fix yet");

  Serial.print("[GPS]  Altitude   : ");
  if (gps.altitude.isValid())
    Serial.printf("%.2f m\n", gps.altitude.meters());
  else
    Serial.println("INVALID");

  Serial.print("[GPS]  Speed      : ");
  if (gps.speed.isValid())
    Serial.printf("%.2f km/h\n", gps.speed.kmph());
  else
    Serial.println("INVALID");

  Serial.print("[GPS]  Course     : ");
  if (gps.course.isValid())
    Serial.printf("%.2f deg\n", gps.course.deg());
  else
    Serial.println("INVALID");

  Serial.print("[GPS]  Local time : ");
  if (gps.date.isValid() && gps.time.isValid())
    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
      gps.date.year(),  gps.date.month(),  gps.date.day(),
      localHour(), gps.time.minute(), gps.time.second());
  else
    Serial.println("INVALID");

  Serial.printf("[GPS]  Sats: %d   HDOP: %.2f\n",
    gps.satellites.isValid() ? (int)gps.satellites.value() : 0,
    gps.hdop.isValid()       ? gps.hdop.hdop()             : 0.0f);

  Serial.printf("[SD]   %s\n\n", sdReady ? "OK" : "not available");
}

// ─────────────────────────────────────────────────────
void logToSD() {
  if (!sdReady) return;

  File f = SD.open(LOG_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("[SD] Write failed.");
    return;
  }

  // Timestamp (local time)
  if (gps.date.isValid() && gps.time.isValid())
    f.printf("%04d-%02d-%02d %02d:%02d:%02d",
      gps.date.year(),  gps.date.month(),  gps.date.day(),
      localHour(), gps.time.minute(), gps.time.second());
  else
    f.print("NO_FIX");

  // GPS + sensor data — lat/lng use %+13.6f to avoid ###### in Excel
  f.printf(",%+13.6f,%+13.6f,%.2f,%.2f,%d,%.2f,%.1f,%.1f\n",
    gps.location.isValid()   ? gps.location.lat()         : 0.0f,
    gps.location.isValid()   ? gps.location.lng()         : 0.0f,
    gps.altitude.isValid()   ? gps.altitude.meters()      : 0.0f,
    gps.speed.isValid()      ? gps.speed.kmph()           : 0.0f,
    gps.satellites.isValid() ? (int)gps.satellites.value(): 0,
    gps.hdop.isValid()       ? gps.hdop.hdop()            : 0.0f,
    lastDistCm  < 0.0f ? 0.0f : lastDistCm,
    snowDepthCm < 0.0f ? 0.0f : snowDepthCm);

  f.close();
}
