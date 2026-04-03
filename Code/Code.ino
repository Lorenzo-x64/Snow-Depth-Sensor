#include <SPI.h>
#include <SD.h>

// SEN0313 UART - TX_PIN defini sur un pin inutilise
#define RX_PIN 45
#define TX_PIN 46  // pin libre, non connecte physiquement

// MicroSD SPI
#define SD_SCK  33
#define SD_MISO 34
#define SD_MOSI 35
#define SD_CS   26

int hauteur_capteur = 200;
int numero_mesure = 1;

int lire_SEN0313() {
  while (Serial1.available()) Serial1.read();

  unsigned long debut = millis();
  while (Serial1.available() < 4) {
    if (millis() - debut > 2000) {
      Serial.println("Timeout capteur");
      return -1;
    }
  }

  while (Serial1.available() >= 4) {
    if (Serial1.peek() == 0xFF) break;
    Serial1.read();
  }

  if (Serial1.available() < 4) return -1;

  uint8_t data[4];
  for (int i = 0; i < 4; i++) {
    data[i] = Serial1.read();
  }

  if (data[0] != 0xFF) return -1;

  uint8_t checksum = (data[0] + data[1] + data[2]) & 0xFF;
  if (checksum != data[3]) {
    Serial.println("Erreur checksum");
    return -1;
  }

  int distance_mm = (data[1] << 8) + data[2];
  if (distance_mm < 280 || distance_mm > 7500) {
    Serial.println("Distance hors plage");
    return -1;
  }

  return distance_mm / 10;
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("=== Demarrage ===");

  // ✅ TX_PIN sur pin libre mais defini
  Serial1.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.println("SEN0313 pret");

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("Carte SD non detectee");
    while (1) delay(1000);
  }
  Serial.println("Carte SD OK");

  if (!SD.exists("/Test_P.csv")) {
    File f = SD.open("/Test_P.csv", FILE_WRITE);
    if (f) {
      f.println("numero,distance_cm,epaisseur_mm");
      f.close();
    }
  }

  Serial.println("=== Systeme pret ===\n");
}

void loop() {
  Serial.println("--- Mesure ---");

  int distance_cm = lire_SEN0313();

  if (distance_cm == -1) {
    Serial.println("Mesure echouee");
    delay(5000);
    return;
  }

  Serial.print("Distance : ");
  Serial.print(distance_cm);
  Serial.println(" cm");

  int epaisseur_mm = (hauteur_capteur - distance_cm) * 10;

  if (epaisseur_mm < 0) {
    Serial.println("Epaisseur negative");
    delay(5000);
    return;
  }

  Serial.print("Epaisseur : ");
  Serial.print(epaisseur_mm);
  Serial.println(" mm");

  File f = SD.open("/Test_P.csv", FILE_APPEND);
  if (f) {
    f.print(numero_mesure);
    f.print(",");
    f.print(distance_cm);
    f.print(",");
    f.println(epaisseur_mm);
  }