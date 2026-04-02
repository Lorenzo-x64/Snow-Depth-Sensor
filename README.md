# Capteur d'enneigement ❄️🌨️

<div align="center">
  <picture>
    <source 
      media="(prefers-color-scheme: dark)" 
      srcset="https://github.com/lorenzor0912/Projet-IT-Neige/blob/0a817e1d05e45fb6e63a99a292cdd9ac2ce48b34/ReadMe_IMG/It%20neige.svg" />
    <img 
      src="https://github.com/lorenzor0912/Projet-IT-Neige/blob/0a817e1d05e45fb6e63a99a292cdd9ac2ce48b34/ReadMe_IMG/It%20neige.svg" 
      alt="Logo principal" 
      width="400" 
      height="400" />
  </picture>
  
  <p align="center">
    <strong>Solution autonome de mesure d'enneigement pour environnements extrêmes</strong>
  </p>
  
  <p align="center">
    <img src="https://img.shields.io/badge/Température--30°C_à_+50°C-blue?style=flat-square" alt="Température"/>
    <img src="https://img.shields.io/badge/Portée-7.5m-green?style=flat-square" alt="Portée"/>
    <img src="https://img.shields.io/badge/Autonomie-4_mois-orange?style=flat-square" alt="Autonomie"/>
    <img src="https://img.shields.io/badge/Étanchéité-IP66-lightblue?style=flat-square" alt="IP66"/>
  </p>
</div>

---

## 📋 Table des matières

- [🎯 Vue d'ensemble](#-vue-densemble)
- [✨ Caractéristiques principales](#-caractéristiques-principales)
- [🛠️ Hardware](#️-hardware)
  - [📸 Capteur ultrasonique](#-capteur-ultrasonique)
  - [🖥️ Microcontrôleur](#️-microcontrôleur)
  - [📡 Communications](#-communications)
  - [💾 Stockage](#-stockage)
  - [🔋 Alimentation](#-alimentation)
  - [🏗️ Matériaux & Boîtier](#️-matériaux--boîtier)
- [💻 Software](#-software)
- [📊 Comparatif des matériaux](#-comparatif-des-matériaux)
- [🚀 Installation & Déploiement](#-installation--déploiement)
- [🤝 Contribution](#-contribution)
- [📜 Licence](#-licence)

---

## 🎯 Vue d'ensemble

Ce projet propose une **station de mesure d'enneigement autonome** conçue pour fonctionner dans des conditions climatiques extrêmes (jusqu'à -30°C). Idéale pour :

- 🎿 **Stations de ski** : Suivi en temps réel de l'enneigement des pistes
- 🏔️ **Observation météorologique** : Collecte de données nivologiques
- 🌲 **Recherche environnementale** : Étude de l'évolution du manteau neigeux

---

## ✨ Caractéristiques principales

| Caractéristique | Spécification |
|-----------------|---------------|
| **Plage de mesure** | 28 cm à 750 cm (7,5 m) |
| **Résolution** | 1 cm |
| **Température d'opération** | -30°C à +50°C |
| **Étanchéité** | IP66 |
| **Autonomie** | 4 mois (mode normal) |
| **Fréquence de mesure** | Configurable (défaut : 4 heures) |
| **Connectivité** | LoRaWAN / Meshtastic (4G optionnel) |
| **Stockage local** | Carte SD haute endurance |

---

## 🛠️ Hardware

### 📸 Capteur ultrasonique

**Modèle** : SEN0313 (DF Robot) / A01NYUB

<details>
<summary><strong>📋 Spécifications techniques détaillées</strong></summary>

#### Caractéristiques principales
- **Type** : Capteur ultrasonique étanche IP67
- **Plage de mesure** : 28 cm à 750 cm
- **Résolution** : 1 cm
- **Précision** : ±1%
- **Angle de détection** : 70° (avec cône fourni)

#### Électrique
- **Tension d'alimentation** : 3,3 V à 5 V DC
- **Consommation** : <15 mA (actif) / <5 mA (veille)
- **Interface** : UART (9600 bps par défaut)

#### Environnemental
- **Température d'opération** : -15°C à +60°C
- **Étanchéité** : IP67 (immersion jusqu'à 1 m pendant 30 min)
- **Résistance** : Poussière, brouillard, fumée

#### Avantages
- Mesure directe en UART (pas de calcul de temps de vol)
- Cône amovible pour optimiser la directivité
- Meilleure pénétration que les HC-SR04 classiques
- Alimentation flexible (3,3V-5V)

#### Documentation
- 📖 [Guide officiel DF Robot](https://www.dfrobot.com/product-1934.html)
- 📄 [Datasheet PDF](https://wiki.dfrobot.com/A01NYUB%20Waterproof%20Ultrasonic%20Sensor%20SKU:%20SEN0313)

</details>

![Capteur SEN0313](https://github.com/lorenzor0912/Projet-IT-Neige/blob/f1702dfe2ce56fabe681698466927644a630968b/ReadMe_IMG/SEN0313.JPG)

---

### 🖥️ Microcontrôleur

#### Configuration recommandée
```
ESP32-WROOM-32D
- Flash : 4 MB
- RAM : 520 KB
- Deep Sleep : 10 µA
- GPIO : 34 pins
```

</details>

**Référence suggérée** : ESP32-DevKitC ou ESP32-WROVER pour stockage PSRAM additionnel

---

### 📡 Communications

Le système utilise des technologies basse consommation pour maximiser l'autonomie :

#### 📻 LoRaWAN (Mode principal retenu)
- **Module** : RFM95W ou LILYGO T-Beam
- **Avantages** :
  - Très faible consommation (~20-50 mA en transmission)
  - Portée longue distance (>10 km en terrain dégagé)
  - Pas d'abonnement cellulaire
  - Idéal pour mesures espacées (4h)
- **Inconvénients** : 
  - Infrastructure gateway requise
  - Débit limité (adapté à notre usage)

#### 🛰️ Meshtastic (Mode maillé retenu)
- **Module** : T-Beam / Heltec LoRa
- **Avantages** :
  - Réseau maillé décentralisé
  - Relais automatique entre stations
  - Excellent pour zones isolées
  - Faible consommation (~30-80 mA)
- **Inconvénients** : Nécessite plusieurs nœuds pour le maillage optimal

#### 🌐 4G/LTE (Option alternative)
- **Module** : SIM7600E-H ou SIM800L
- **Usage** : Solution de secours ou pour installations urbaines
- **Avantages** : 
  - Couverture étendue
  - Débit élevé
  - Géolocalisation GPS intégrée
- **Inconvénients** : 
  - ⚠️ Consommation élevée (~100-500 mA en transmission)
  - ⚠️ Coût d'abonnement (~10€/mois)

<details>
<summary><strong>🔧 Tableau comparatif détaillé</strong></summary>

| Critère | LoRaWAN | Meshtastic | 4G/LTE |
|---------|---------|------------|--------|
| **Portée** | 2-15 km | 5-50 km (maillé) | 10-20 km |
| **Consommation** | 🟢 Faible (20-50 mA) | 🟢 Faible (30-80 mA) | 🔴 Élevée (100-500 mA) |
| **Coût opérationnel** | 🟢 Gratuit | 🟢 Gratuit | 🔴 ~10€/mois |
| **Infrastructure** | 🟡 Gateway requis | 🟡 Multi-nœuds | 🟢 Existante |
| **Latence** | 🟡 Minutes | 🟡 Variable | 🟢 Temps réel |
| **Adapté mesure 4h** | 🟢🟢 Parfait | 🟢🟢 Parfait | 🟢 Surdimensionné |
| **Recommandé pour** | Zones rurales | Zones isolées | Zones urbaines |

</details>

**Recommandation** : LoRaWAN en priorité pour l'efficacité énergétique, Meshtastic pour les déploiements multi-stations en montagne.

---

### 💾 Stockage

**Solution** : Carte microSD haute endurance

#### 📊 Estimation de l'espace nécessaire

Hypothèses :
- Fréquence de mesure : 1 mesure toutes les 4 heures
- Durée : 4 mois (122 jours)
- Format de données : CSV avec timestamp + distance + température

```
Calcul :
- Mesures par jour : 24 / 4 = 6 mesures
- Total 4 mois : 6 × 122 = 732 mesures
- Taille par entrée : ~50 octets (horodatage ISO8601 + valeurs)
- Espace total : 732 × 50 ≈ 36.6 KB

Recommandation : Carte SD 8 Go minimum (marge confortable × 200,000)
```

#### 🛒 Cartes SD recommandées (haute endurance)




---

### 🔋 Alimentation

#### Autonomie cible : **4 mois**

<details>
<summary><strong>⚡ Calcul de consommation détaillé</strong></summary>

#### Profil de consommation (LoRaWAN/Meshtastic)

**Mode actif (mesure + transmission LoRa)** :
- ESP32 : 80 mA
- Capteur SEN0313 : 15 mA
- Module LoRa (transmission) : 120 mA (pic)
- Carte SD (écriture) : 50 mA
- **Total actif** : ~265 mA pendant 10 secondes

**Mode veille (deep sleep)** :
- ESP32 : 10 µA
- Capteur (désactivé) : 5 µA
- Module LoRa (sleep) : 1 µA
- **Total veille** : ~16 µA

#### Calcul quotidien (mesure toutes les 4 heures)

```
Mesures par jour : 6
Temps actif : 6 × 10 sec = 60 secondes (0.0167 heures)
Temps veille : 23.983 heures

Consommation jour :
- Actif : 265 mA × 0.0167 h = 4.4 mAh
- Veille : 0.016 mA × 23.983 h = 0.4 mAh
Total : ~4.8 mAh / jour

Consommation 4 mois :
4.8 × 122 jours = 586 mAh ≈ 0.59 Ah
```

#### Solution batterie recommandée

**Option 1 : Batterie LiFePO4 compacte** (recommandée)
- Modèle : 12V 20Ah LiFePO4
- Capacité utilisable : ~18 Ah (90% DoD)
- Autonomie réelle : ~2 ans (!)
- Avantages : 
  -  Excellente performance au froid
  -  Largement surdimensionné
  -  Compact et léger
- Prix : ~80-120€

**Option 2 : Panneaux solaires + Petite batterie**
- Panneau : 10W monocristallin
- Batterie : 12V 10Ah LiFePO4
- Contrôleur : MPPT 5A avec protection gel
- Autonomie : Illimitée (si ensoleillement >2h/jour)
- Prix total : ~150-200€

**Option 3 : Batteries 18650 (DIY)**
- Configuration : 3S2P (12V, 6000 mAh)
- 6× cellules 18650 haute qualité
- BMS 3S inclus
- Autonomie : ~1 an
- Prix : ~40-60€

</details>

**Recommandation finale** : Bah je sais pas!!!

### 🏗️ Matériaux & Boîtier

Le boîtier doit résister à des conditions extrêmes : neige, UV, humidité, températures de -30°C, pendant 10 ans.

#### 🧪 Comparatif des matériaux d'impression 3D

| Critère | ASA-CF | PETG-CF | PET | ABS | PLA | ASA (std) | PC | PETG (std) | Nylon (PA) |
|--------------------------------------|--------------|--------------|--------------|--------------|--------------|--------------|--------------|--------------|--------------| 
| **Résistance au froid (-30°C)** | 🟢🟢 | 🟢🟢 | 🟢 | 🟢 | 🔴🔴 | 🟢🟢 | 🟢🟢 | 🟢🟢 | 🟢🟢 |
| **Durabilité UV (10 ans ext.)** | 🟢🟢 | 🟢 | 🔴 | 🔴 | 🔴🔴 | 🟢🟢 | 🟡 | 🟡 | 🟡 |
| **Résistance à l'humidité** | 🟢🟢 | 🟢🟢 | 🟢 | 🟢 | 🔴 | 🟢🟢 | 🟡 | 🟢🟢 | 🔴 |
| **Stabilité dimensionnelle** | 🟢🟢 | 🟢🟢 | 🟢 | 🟡 | 🔴 | 🟢🟢 | 🟢 | 🟢🟢 | 🔴 |
| **Facilité d'impression** | 🔴 | 🔴 | 🟢 | 🟡 | 🟢🟢 | 🟡 | 🔴🔴 | 🟢🟢 | 🔴 |
| **Résistance mécanique** | 🟢🟢 | 🟢🟢 | 🟢 | 🟢 | 🟡 | 🟢 | 🟢🟢 | 🟢 | 🟢🟢 |

##### Légende
- 🟢🟢 Excellent
- 🟢 Bon
- 🟡 Moyen / Conditions requises
- 🔴 Faible / Déconseillé
- 🔴🔴 Très faible / À éviter

<details>
<summary><strong>📖 Notes techniques détaillées</strong></summary>

#### Résistance au froid
- **PLA** : Devient cassant en dessous de 0°C → **À ÉVITER**
- **PC & Nylon** : Restent flexibles même à -30°C
- **ASA/ASA-CF** : Température de fléchissement >100°C

#### Durabilité UV
- **ASA & ASA-CF** : 🏆 **Meilleure résistance** (usage automobile)
  - Contient stabilisants UV dans la matrice
  - Pas de jaunissement après 5+ ans d'exposition
- **PLA, ABS, PET** : 🔴 Dégradation rapide (jaunissement, fragilisation en <1 an)
- **Nylon** : Variable selon type (PA12 > PA6)

#### Résistance à l'humidité
- **Absorption d'eau** :
  - ASA-CF : ~0.3-0.5% (excellent)
  - PETG : Très hydrophobe (~0.2%)
  - Nylon PA6 : Jusqu'à 8% ⚠️ (gonflement, perte rigidité)
  - PLA : Gonflement + perte de propriétés mécaniques
- **Conséquence** : Le nylon PA6 nécessite un séchage systématique avant impression

#### Facilité d'impression
- **Facile** 🟢 : PLA, PETG standard
  - Température plateau : 50-60°C
  - Pas d'enceinte chauffée requise
  
- **Nécessite enceinte chauffée** 🔴 : ASA, ABS, PC, Nylon
  - Température enceinte : 40-60°C
  - Plateau : 80-110°C
  - Risque de warping élevé sans enceinte
  
- **Matériaux abrasifs (CF)** 🔴 : Nécessite buse renforcée
  - Buse acier trempé ou rubis obligatoire
  - Les fibres de carbone usent rapidement les buses laiton
  
- **PC & Nylon** 🔴🔴 : 
  - Température extrême : >250°C (hotend tout métal requis)
  - Séchage obligatoire : 6-12h à 70°C avant impression
  - Chambre fermée + plateau >100°C

</details>


##### ✅ 3 Meilleurs matériaux usage extérieur

1. **ASA-CF** (Champion toutes catégories)
   - **Meilleur compromis** : rigidité / UV / froid / humidité
   - Idéal pour pièces structurelles exposées
   - Coût : ~40-60€/kg
   - **Attention** : Enceinte chauffée + buse renforcée requise (fivre de carbone bien abbrasif)

2. **ASA standard** (Alternative économique) & (Probablement la meilleur option)
   - Si pas besoin de renfort carbone (pièces non-contraintes)
   - Résistance UV identique à l'ASA-CF
   - Plus facile à imprimer que la version CF
   - Coût : ~25-35€/kg 

3. **PETG-CF** (Compromis sans enceinte) & (Probablement la deuxieme meilleur option)
   - Alternative si imprimante sans enceinte chauffée
   - Bon pour pièces internes du boitier
   - Hydrophobe excellent
   - Coût : ~35-50€/kg (ca pique un peu en effet pour du petg)

##### À ÉVITER en extérieur

| Matériau | Raison principale |
|----------|-------------------|
| **PLA** | 🔴🔴 Dégradation rapide, cassant au froid, absorbe l'humidité |
| **ABS** | 🔴 Jaunissement et fragilisation aux UV en <2 ans |
| **Nylon PA6** | 🔴 Absorption d'humidité excessive (8%), instabilité dimensionnelle |

---

## 💻 Software

Au chomage technique ou en vacance?

### 🔧 Fonctionnalités prévues

- [ ] **Acquisition de données**
  - Lecture des données du capteur SEN0313 (valeur directe en cm via pin analogique/numérique)
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

## 🚀 Installation & Déploiement

### 📦 Liste du matériel nécessaire

| Composant | Quantité | Prix unitaire | Lien |
|-----------|----------|---------------|------|
| Capteur SEN0313 | 1 | ~30€ | [DFRobot](https://www.dfrobot.com/product-1934.html) |
| ESP32-DevKitC | 1 | ~5€ | [AliExpress](https://www.aliexpress.com) |
| Module LoRa RFM95W ou T-Beam | 1 | ~15-25€ | [Amazon](https://www.amazon.fr) |
| Carte SD 8Go Industrial | 1 | ~13€ | [Mouser](https://www.mouser.fr) |
| Batterie LiFePO4 12V 20Ah | 1 | ~100€ | [Spécialiste batteries] |
| Boîtier ASA-CF (impression) | 1 | ~15€ (filament) | À imprimer |
| Connectique étanche | Divers | ~10€ | [RS Components](https://fr.rs-online.com) |
| **TOTAL** | - | **~188-198€** | - |

---

## 📜 Licence

Ce projet est sous licence **GNU General Public License v3.0** - voir le fichier [LICENSE](LICENSE) pour plus de détails.

---

<div align="center">
  
### 🏔️ Développé avec ❄️ par Sti2D Labs

<div style="line-height: 0.9; font-family: 'Courier New', Courier, monospace; white-space: pre; color: #d0d0d0;">
<pre>
   _____ _   _ ___  _____    _           _         
  / ____| | (_)__ \|  __ \  | |         | |        
 | (___ | |_ _   ) | |  | | | |     __ _| |__  ___ 
  \___ \| __| | / /| |  | | | |    / _` | '_ \/ __|
  ____) | |_| |/ /_| |__| | | |___| (_| | |_) \__ \
 |_____/ \__|_|____|_____/  |______\__,_|_.__/|___/
                                                   
</pre>
</div>

<img 
  src="https://github.com/Lorenzo-x64/Projet-IT-Neige/blob/a68dd4287c40711deb7713e88d299c58865ecca4/ReadMe_IMG/Sti%20Labs.svg" 
  alt="Logo Sti2D Labs" 
  width="600" 
  height="600" />

<p align="center">
  <strong>Merci!</strong>
</p>

</div>

<div align="right">
  <a href="#top">↑ Retour en haut</a>
</div>

<div align="right">
  <a href="#top">↑ Raccoursi tres rapide super mega rapide trop rapide</a>
</div>
