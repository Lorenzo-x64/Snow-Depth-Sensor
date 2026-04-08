#include <SPI.h>
#include <SD.h>

#define SD_CS 5
int hauteur_capteur = 200;

int lire_SEN0313_simule() {
  return 50;
}

void afficher_csv() {
  File f = SD.open("/Test_P.csv", FILE_READ);
  if (!f) return;
  Serial.println("--- Contenu Test_P.csv ---");
  while (f.available()) {
    Serial.write(f.read());
  }
  f.close();
  Serial.println("--------------------------");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  SPI.begin(18, 19, 23, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("Carte SD non detectee");
    while (1) delay(1000);
  }
  Serial.println("Carte SD OK");
}

void loop() {
  int distance_cm  = lire_SEN0313_simule();
  int epaisseur_mm = (hauteur_capteur - distance_cm) * 10;

  File f = SD.open("/Test_P.csv", FILE_APPEND);
  if (f) {
  f.print(millis() / 1000);
  f.print("s,");
  f.println(epaisseur_mm);
    f.close();
    Serial.println("Ligne ajoutee");
  } else {
    Serial.println("Erreur SD");
  }

  afficher_csv();

  delay(10000);
}
