<div align="center">

  # Snow Depth Sensor

</div>

<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://github.com/lorenzor0912/Projet-IT-Neige/blob/0a817e1d05e45fb6e63a99a292cdd9ac2ce48b34/ReadMe_IMG/It%20neige.svg" />
    <img src="https://github.com/lorenzor0912/Projet-IT-Neige/blob/0a817e1d05e45fb6e63a99a292cdd9ac2ce48b34/ReadMe_IMG/It%20neige.svg" alt="Logo principal" width="400" height="400" />
  </picture>

  <p><strong>Solution autonome de mesure d'enneigement pour environnements extrêmes</strong></p>

  <p>
    <img src="https://img.shields.io/badge/Température--30°C_à_+50°C-blue?style=flat-square" alt="Température"/>
    <img src="https://img.shields.io/badge/Portée-7.5m-green?style=flat-square" alt="Portée"/>
    <img src="https://img.shields.io/badge/Autonomie-4_mois-orange?style=flat-square" alt="Autonomie"/>
    <img src="https://img.shields.io/badge/Étanchéité-IP66-lightblue?style=flat-square" alt="IP66"/>
  </p>
</div>

---

## Table des matières

- [Vue d'ensemble](#vue-densemble)
- [Hardware](#hardware)
  - [Capteur Principal](#capteur-principal)
  - [Microcontrôleur](#microcontrôleur)
  - [Stockage](#stockage)
  - [Communications](#communications)
  - [Alimentation](#alimentation)
  - [Matériaux & Boîtier](#matériaux--boîtier)
- [Software](#software)
- [Installation & Déploiement](#installation--déploiement)
- [Licence](#licence)

---

## Vue d'ensemble

Station de mesure d'enneigement autonome conçue pour fonctionner dans des conditions climatiques (jusqu'à -30°C) et piloté à distance.

| Caractéristique | Spécification |
|-----------------|---------------|
| Plage de mesure | 30 cm à 750 cm (7,5 m) |
| Résolution | +-1 cm |
| Température d'opération | -30°C à +50°C |
| Étanchéité | IP66 |
| Autonomie |  mois (mode normal) |
| Fréquence de mesure | Configurable (défaut : 1 heures) |
| Connectivité | LoRaWAN |
| Stockage local | Carte SD haute endurance |

---

## Hardware

### Capteur ultrasonique

**Modèle** : SEN0313 / A01NYUB ou (JSN-SR04T possible)

![Capteur SEN0313](https://github.com/lorenzor0912/Projet-IT-Neige/blob/f1702dfe2ce56fabe681698466927644a630968b/ReadMe_IMG/SEN0313.JPG)

<details>
<summary>Spécifications techniques détaillées</summary>

**Caractéristiques**
- Type : Capteur ultrasonique étanche IP67
- Plage de mesure : 28 cm à 750 cm
- Résolution : 1 cm / Précision : ±1%
- Angle de détection : 70° (avec cône fourni)

**Électrique**
- Tension d'alimentation : 3,3 V à 5 V DC
- Consommation : <15 mA (actif) / <5 mA (veille)
- Interface : UART (9600 bps par défaut)

**Environnemental**
- Température d'opération : -15°C à +60°C
- Étanchéité : IP67 (immersion jusqu'à 1 m pendant 30 min)
- Résistance : poussière, brouillard, fumée

**Avantages**
- Mesure directe en UART (pas de calcul de temps de vol)
- Cône amovible pour optimiser la directivité
- Meilleure pénétration que les HC-SR04 classiques
- Alimentation flexible (3,3V–5V)

**Documentation**
- [Guide officiel DF Robot](https://www.dfrobot.com/product-1934.html)
- [Datasheet PDF](https://wiki.dfrobot.com/A01NYUB%20Waterproof%20Ultrasonic%20Sensor%20SKU:%20SEN0313)

</details>

---

### Microcontrôleur


**Référence suggérée** : ESP32-DevKitC ou ESP32-WROVER pour stockage PSRAM additionnel ou module basse consomation 

ATTENTION! NE SONT PAS RECCOMANDE DES MODULES TOUS EN UN STYLE HELTEC LILYGO OU WAVESHARE (problème de librairies constaté sur heltec pour nous par ex)

---

### Communications

Le système utilise des technologies basse consommation pour maximiser l'autonomie.

#### LoRaWAN (mode principal retenu)

- **Module** : RFM95W ou LILYGO T-Beam
- Très faible consommation (~20–50 mA en transmission)
- Portée longue distance (>10 km en terrain dégagé)
- Pas d'abonnement cellulaire, idéal pour mesures espacées (4h)
- Inconvénient : infrastructure gateway requise, débit limité

#### Meshtastic 

- Voir si intégration possible intéréssant car maillé

#### 4G/LTE (option alternative)

- **Module** : SIM7600E-H ou SIM800L
- Couverture étendue, débit élevé, géolocalisation GPS intégrée
- Consommation élevée (~100–500 mA) et coût d'abonnement (~10€/mois)

<details>
<summary>Tableau comparatif détaillé</summary>

| Critère | LoRaWAN | Meshtastic | 4G/LTE |
|---------|---------|------------|--------|
| Portée | 2–15 km | 5–50 km (maillé) | 10–20 km |
| Consommation | 🟢 Faible (20–50 mA) | 🟢 Faible (30–80 mA) | 🔴 Élevée (100–500 mA) |
| Coût opérationnel | 🟢 Gratuit | 🟢 Gratuit | 🔴 ~10€/mois |
| Infrastructure | 🟡 Gateway requis | 🟡 Multi-nœuds | 🟢 Existante |
| Latence | 🟡 Minutes | 🟡 Variable | 🟢 Temps réel |


</details>

**Recommandation** : LoRaWAN en priorité pour l'efficacité énergétique, Meshtastic pour les déploiements multi-stations en montagne.

---

### Stockage

**Solution** : Carte microSD haute endurance

- Fréquence de mesure : 1 toutes les heures
- Format : CSV avec timestamp + distance + température

---

### Alimentation

**Autonomie cible : 4 mois**

Mais réalistiquement infaisable sans panneau solaire et grosse batterie une grosse optimisation est a prévoir sur la partie code pour essayer de consomer le moins possible 


---

### Matériaux & Boîtier

Le boîtier doit résister à des conditions extrêmes : neige, UV, humidité, températures de -30°C, pendant 10 ans.

#### Comparatif des matériaux d'impression 3D

| Critère | ASA-CF | PETG-CF | PET | ABS | PLA | ASA (std) | PC | PETG (std) | Nylon (PA) |
|---------|--------|---------|-----|-----|-----|-----------|----|-----------|-----------| 
| Résistance au froid (-30°C) | 🟢🟢 | 🟢🟢 | 🟢 | 🟢 | 🔴🔴 | 🟢🟢 | 🟢🟢 | 🟢🟢 | 🟢🟢 |
| Durabilité UV (10 ans ext.) | 🟢🟢 | 🟢 | 🔴 | 🔴 | 🔴🔴 | 🟢🟢 | 🟡 | 🟡 | 🟡 |
| Résistance à l'humidité | 🟢🟢 | 🟢🟢 | 🟢 | 🟢 | 🔴 | 🟢🟢 | 🟡 | 🟢🟢 | 🔴 |
| Stabilité dimensionnelle | 🟢🟢 | 🟢🟢 | 🟢 | 🟡 | 🔴 | 🟢🟢 | 🟢 | 🟢🟢 | 🔴 |
| Facilité d'impression | 🔴 | 🔴 | 🟢 | 🟡 | 🟢🟢 | 🟡 | 🔴🔴 | 🟢🟢 | 🔴 |
| Résistance mécanique | 🟢🟢 | 🟢🟢 | 🟢 | 🟢 | 🟡 | 🟢 | 🟢🟢 | 🟢 | 🟢🟢 |

Légende : 🟢🟢 Excellent · 🟢 Bon · 🟡 Moyen · 🔴 Faible · 🔴🔴 À éviter

<details>
<summary>Notes techniques détaillées</summary>

**Résistance au froid**
- PLA : devient cassant en dessous de 0°C → **À ÉVITER**
- PC & Nylon : restent flexibles même à -30°C
- ASA/ASA-CF : température de fléchissement >100°C

**Durabilité UV**
- ASA & ASA-CF : meilleure résistance (usage automobile) — stabilisants UV intégrés, pas de jaunissement après 5+ ans
- PLA, ABS, PET : dégradation rapide (jaunissement, fragilisation en <1 an)
- Nylon : variable selon type (PA12 > PA6)

**Résistance à l'humidité**
- ASA-CF : ~0,3–0,5% (excellent)
- PETG : très hydrophobe (~0,2%)
- Nylon PA6 : jusqu'à 8% ⚠️ (gonflement, perte de rigidité)
- PLA : gonflement + perte de propriétés mécaniques

**Facilité d'impression**
- Facile (PLA, PETG) : plateau 50–60°C, pas d'enceinte requise
- Enceinte chauffée requise (ASA, ABS, PC, Nylon) : 40–60°C, plateau 80–110°C
- Matériaux CF : buse acier trempé ou rubis obligatoire (fibres abrasives)
- PC & Nylon : >250°C, séchage 6–12h à 70°C avant impression

</details>


---

## Software

Le 13/03 Premier code fonctionel a prévoir une implémentation d'ecran tactile et de module lorawan Ebyte

### Fonctionnalités prévues

- [ ] **Acquisition de données**
  - Lecture des données SEN0313 (valeur directe en cm via UART)
  - Timestamping précis (RTC DS3231 externe)
  - Moyennage sur N échantillons (filtrage bruit)
- [ ] **Gestion de l'énergie**
  - Deep sleep ESP32 entre mesures (4h)
  - Wake-up timer configurable
  - Surveillance batterie (ADC + diviseur pont)
- [ ] **Stockage**
  - Écriture CSV sur carte SD
  - Rotation logs automatique (fichiers journaliers)
  - Protection buffer en cas de coupure
- [ ] **Communication**
  - Envoi périodique via LoRaWAN ou Meshtastic
  - Protocole configuré selon gateway disponible
  - Retry logic avec backoff exponentiel (en gros c ton tel quand il se bloque c exponantiel)

---

## Installation & Déploiement

### Liste du matériel nécessaire

| Composant | Qté | Prix unitaire | Lien |
|-----------|-----|---------------|------|
| Capteur SEN0313 ou | 1 | ~30€ | [none](https://www..html) |
| ESP32-DevKitC | 1 | ~5€ | [none](https://www..com) |
| Module LoRa  | 1 | ~15–25€ | [none](https://www..fr) |
| Carte SD 8Go Industrial | 1 | ~13€ | [none](https://www..fr) |
| Batterie LiFePO4 12V 20Ah | 1 | ~100€ | [ none] |
| Boîtier ASA-CF (impression) | 1 | ~15€ (filament) | À imprimer |
| Connectique étanche | Divers | ~10€ | [none](https://fr.com) |
| **TOTAL** | | **~a calculer** | |

---

## Licence

Ce projet est sous licence **GNU General Public License v3.0** — voir le fichier [LICENSE](LICENSE) pour plus de détails.

---

<div align="center">

### Développé par

<img src="https://github.com/Lorenzo-x64/Projet-IT-Neige/blob/a68dd4287c40711deb7713e88d299c58865ecca4/ReadMe_IMG/Sti%20Labs.svg" alt="Logo Sti2D Labs" width="600" height="600" />

<p><strong>Merci!</strong></p>

</div>

<div align="right">
  <a href="#top">↑ Retour en haut</a>
</div>
