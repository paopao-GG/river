# BUP CPE River Monitoring System — Complete Build Guide

> Follow this guide in order. Each section must be completed before moving to the next.

---

## Repository Structure

```
bup-cpe-river/
├── docs/                     Documentation
│   ├── GUIDE.md              This build guide
│   ├── project.md            Project overview and requirements
│   ├── prediction-method.md  How the water level prediction works
│   ├── wiring-diagram.txt    V1 wiring (AJ-SR04M, ASCII art)
│   └── wiring-guide-v2.md    V2 wiring (A0120AG RS485 + C25B)
├── firebase/                 Firebase configuration
│   ├── database.rules.json   RTDB security rules
│   └── seed-config.json      Initial node config (import into RTDB)
├── firmware/
│   ├── v1/                   V1 firmware (AJ-SR04M ultrasonic, solar+battery)
│   │   ├── node-01/          Upstream (Centro)
│   │   ├── node-02/          Midstream (Napo)
│   │   ├── node-03/          Downstream (Balinad)
│   │   └── sensor-test/      Standalone sensor test sketch
│   └── v2/                   V2 firmware (A0120AG RS485, 12V powered)
│       ├── station-01/       Upstream (Centro)
│       ├── station-02/       Midstream (Napo)
│       ├── station-03/       Downstream (Balinad)
│       ├── sensor-test/      RS485 sensor test
│       ├── sensor-test-pwm/  PWM sensor test
│       ├── sensor-test-simple/
│       └── sensor-test-uart/ UART sensor test
└── webapp/
    └── index.html            Real-time dashboard (single-page)
```

**V1 vs V2:** V1 uses the AJ-SR04M waterproof ultrasonic sensor (direct GPIO trigger/echo) with solar+battery power. V2 uses the A0120AG industrial RS485 ultrasonic sensor (via C25B TTL-RS485 converter) with a 12V external power supply. Both versions share the same YF-DN50 flow sensor and Firebase backend.

---

## Table of Contents

1. [Parts List](#1-parts-list)
2. [Wiring the Node](#2-wiring-the-node)
3. [Arduino IDE Setup](#3-arduino-ide-setup)
4. [Firebase Setup](#4-firebase-setup)
5. [Flashing the Firmware (V1)](#5-flashing-the-firmware-v1)
6. [Flashing the Firmware (V2)](#6-flashing-the-firmware-v2)
7. [Bench Testing](#7-bench-testing)
8. [Running the Dashboard](#8-running-the-dashboard)
9. [Deploying to Firebase Hosting](#9-deploying-to-firebase-hosting)
10. [Field Deployment](#10-field-deployment)
11. [Troubleshooting](#11-troubleshooting)

---

## 1. Parts List

Build **3 identical nodes**. Each node requires:

| Component | Qty | Notes |
|---|---|---|
| NodeMCU 1.0 (ESP-12E) | 1 | Not Wemos D1 Mini — guide uses NodeMCU ADC range |
| AJ-SR04M Waterproof Ultrasonic | 1 | Comes with waterproof probe + cable |
| YF-DN50 Flow Sensor (2") | 1 | Hall-effect turbine, inline pipe |
| TP4056 + DW01A Charge Board | 1 | The combo board has both on it |
| HT7333 LDO Regulator | 1 | TO-92 or SOT-89 package |
| 18650 Li-Ion Cell 3000mAh | 2 | Any reputable brand |
| 6V / 2W Solar Panel | 1 | Small panel, bare wires or JST |
| 100kΩ Resistor | 2 | For battery voltage divider |
| 10kΩ Resistor | 1 | Pull-up for flow sensor signal |
| IP67 ABS Junction Box | 1 | ~150×110×70mm or larger |
| PG7 or PG9 Cable Glands | 3–4 | Waterproof cable entry |
| 2" PVC Pipe + fittings | ~60cm | For flow sensor diversion |
| Mesh screen / filter | 1 | Cover intake pipe inlet |
| Desiccant (silica gel) pack | 2 | Inside the enclosure |
| Jumper wires / hookup wire | — | 22 AWG stranded preferred |
| Solder + soldering iron | — | No breadboards in field |

---

## 2. Wiring the Node

Build one node at a time. Wire on a breadboard first to test, then solder permanently before enclosing.

### 2.1 Power System

```
[6V Solar Panel +] ──→ TP4056 IN+
[6V Solar Panel −] ──→ TP4056 IN−

TP4056 B+ ──→ 18650 Cell #1 (+)
             18650 Cell #2 (+)    ← both cells in PARALLEL (same +/-)
TP4056 B− ──→ 18650 Cell #1 (−)
             18650 Cell #2 (−)

TP4056 OUT+ ──→ Battery+ rail     ← this is your 3.7–4.2V main rail
TP4056 OUT− ──→ GND bus
```

Then regulate down to 3.3V for the ESP8266:

```
Battery+ rail ──→ HT7333 IN pin
HT7333 GND    ──→ GND bus
HT7333 OUT    ──→ 3.3V bus        ← powers ESP8266, AJ-SR04M, YF-DN50
```

### 2.2 ESP8266 (NodeMCU 1.0)

| NodeMCU Pin | Connect To | Why |
|---|---|---|
| 3V3 | 3.3V bus | Power from HT7333 |
| GND | GND bus | Common ground |
| D0 (GPIO16) | RST (same board) | **Deep sleep wake — MANDATORY** |
| D1 (GPIO5) | AJ-SR04M TRIG | Trigger ultrasonic ping |
| D6 (GPIO12) | AJ-SR04M ECHO | Receive ultrasonic echo |
| D2 (GPIO4) | YF-DN50 SIGNAL (yellow) | Flow pulse input |
| A0 | Voltage divider midpoint | Battery level reading |

> **The D0→RST wire is the most commonly missed step.** Without it, the ESP8266 enters deep sleep and never wakes up again. Use a short direct wire from D0 to RST on the same NodeMCU board.

### 2.3 AJ-SR04M Ultrasonic Sensor

The sensor has two parts: a **PCB board** (goes inside the enclosure) and a **waterproof probe** (hangs outside on a cable).

| AJ-SR04M Pin | Connect To |
|---|---|
| VCC | 3.3V bus |
| GND | GND bus |
| TRIG | D1 (GPIO5) |
| ECHO | D6 (GPIO12) |

> **Check the R19 jumper** on the AJ-SR04M PCB. For Mode 1 (HC-SR04 compatible, which this firmware uses), R19 must be **open/not populated**. Most units ship this way. If yours has a resistor soldered at R19, check the datasheet for which mode it selects.

### 2.4 YF-DN50 Flow Sensor

Three wires come out of the YF-DN50:

| Wire Color | Connect To |
|---|---|
| Red (VCC) | 3.3V bus |
| Black (GND) | GND bus |
| Yellow (Signal) | D2 (GPIO4) |

Also add the **10kΩ pull-up resistor**: one leg to D2 (GPIO4), other leg to 3.3V bus. This prevents phantom pulse counts when no water is flowing.

> If the sensor reads 0 L/min even when water is flowing, try powering Red (VCC) from Battery+ rail (3.7–4.2V) instead of 3.3V. The signal wire with the 10kΩ pull-up to 3.3V is always safe for the ESP8266 input regardless of VCC level.

### 2.5 Battery Voltage Divider

This lets A0 read the battery level without exceeding the 3.3V input limit:

```
Battery+ rail
      │
   [100kΩ R1]
      │
      ├──── A0 pin
      │
   [100kΩ R2]
      │
     GND
```

At full charge (4.2V), A0 sees 2.1V — safe for the NodeMCU's 3.3V A0 input.

### 2.6 Complete Wiring Diagram

```
[Solar 6V] ──→ [TP4056] ──→ [18650 x2 parallel]
                                      │
                                 Battery+ rail
                                 /            \
                            [HT7333]       [R1 100kΩ]
                               │                │
                            3.3V bus          [A0]
                               │                │
                       ┌───────┴──────┐     [R2 100kΩ]
                       │  NodeMCU 1.0 │          │
                       │              │         GND
  AJ-SR04M VCC ←───── │ 3V3          │
  AJ-SR04M TRIG ←──── │ D1 (GPIO5)   │
  AJ-SR04M ECHO ────→ │ D6 (GPIO12)  │
                       │              │
  YF-DN50 SIG ──────→ │ D2 (GPIO4) ──┼── [10kΩ] ── 3.3V
                       │              │
                       │ D0 (GPIO16) ─┼──────────────→ RST
                       │              │
                       │ GND ─────────┼──────────────→ GND bus
                       └──────────────┘
```

---

## 3. Arduino IDE Setup

Do this once on your computer before flashing any boards.

### 3.1 Install Arduino IDE

Download from [arduino.cc/en/software](https://www.arduino.cc/en/software). Install version 2.x.

### 3.2 Add ESP8266 Board Support

1. Open Arduino IDE → **File > Preferences**
2. In "Additional boards manager URLs" paste:
   ```
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
3. Click OK
4. Go to **Tools > Board > Boards Manager**
5. Search `esp8266` → Install **"esp8266 by ESP8266 Community"** (version 3.x)

### 3.3 Install the Firebase Library

1. Go to **Sketch > Include Library > Manage Libraries**
2. Search: `Firebase ESP8266 Client`
3. Install **"Firebase ESP8266 Client" by Mobizt** — version 4.4.x or later

> This library includes `FirebaseJson` — you do NOT need to install ArduinoJson separately.

### 3.4 Select Board Settings

Every time you open a sketch to flash:

- **Tools > Board** → `NodeMCU 1.0 (ESP-12E Module)`
- **Tools > CPU Frequency** → `80 MHz`
- **Tools > Flash Size** → `4MB (FS:2MB OTA:~1019KB)`
- **Tools > Upload Speed** → `115200`
- **Tools > Port** → select the COM port that appears when you plug in the NodeMCU

---

## 4. Firebase Setup

Do this once. All 3 nodes share the same Firebase project.

### 4.1 Create the Firebase Project

1. Go to [console.firebase.google.com](https://console.firebase.google.com)
2. Click **Add project**
3. Name it `bup-cpe-river` (or anything you like)
4. Disable Google Analytics (not needed)
5. Click **Create project** → wait for it to finish

### 4.2 Create the Realtime Database

1. In the left sidebar: **Build → Realtime Database**
2. Click **Create Database**
3. Choose region: **asia-southeast1 (Singapore)** — closest to Philippines
4. Start in **Test mode** (allows read/write without auth for now)
5. Click **Enable**

You will see a URL like:
```
https://bup-cpe-river-default-rtdb.asia-southeast1.firebasedatabase.app
```
**Copy this URL** — you need it for the firmware and dashboard.
https://river-d1bc6-default-rtdb.asia-southeast1.firebasedatabase.app/

### 4.3 Enable Anonymous Authentication

The firmware signs in anonymously so it can write to the database.

1. Left sidebar: **Build → Authentication**
2. Click **Get started**
3. Under **Sign-in method**, click **Anonymous**
4. Toggle **Enable** → click **Save**

### 4.4 Get Your Web API Key

1. Click the **gear icon** (⚙) next to "Project Overview" → **Project settings**
2. Under **General** tab, scroll to **Your apps**
3. Click the web icon **`</>`** to register a web app
4. Name it `river-dashboard`
5. Check **Also set up Firebase Hosting** → click **Register app**
6. You will see a `firebaseConfig` block like:

```javascript
const firebaseConfig = {
  apiKey: "AIzaSy...",
  authDomain: "bup-cpe-river.firebaseapp.com",
  databaseURL: "https://bup-cpe-river-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "bup-cpe-river",
  storageBucket: "bup-cpe-river.appspot.com",
  messagingSenderId: "123456789",
  appId: "1:123456789:web:abcdef"
};
```

**Copy and save this entire block** — you'll paste it into `webapp/index.html`.

The `apiKey` value alone is also what goes into the firmware's `FIREBASE_API_KEY`.

### 4.5 Import the Node Config

This seeds the database with location names and alert thresholds for all 3 nodes before the devices ever send data.

1. In Firebase Console → **Realtime Database** → click the **three-dot menu (⋮)** at top right
2. Click **Import JSON**
3. Click **Browse** and select: `firebase/seed-config.json`
4. Click **Import**

You should now see this in the database:
```
devices/
  station-01/config/location: "Centro Station"
  station-02/config/location: "Napo Station"
  station-03/config/location: "Balinad Station"
```

> Edit the location names and `alertThreshold_cm` values directly in the Firebase Console to match your actual deployment site.

### 4.6 Apply Database Rules

For now (development), keep Test mode rules. Before making the dashboard public, apply the production rules from `firebase/database.rules.json`:

1. In Firebase Console → **Realtime Database → Rules** tab
2. Replace the rules with the production block from the comments in `firebase/database.rules.json`
3. Click **Publish**

---

## 5. Flashing the Firmware (V1)

> **V1 hardware** uses the AJ-SR04M ultrasonic sensor with direct GPIO trigger/echo.
> For the V2 hardware (A0120AG RS485 sensor), skip to [Section 6](#6-flashing-the-firmware-v2).

Each of the 3 NodeMCU boards gets its own sketch from the `firmware/v1/` folder.

### 5.1 Fill In Your Credentials

Open each sketch file and replace the placeholder values at the top:

**`firmware/v1/node-01/node-01.ino`** (and same for node-02, node-03):

```cpp
const char* WIFI_SSID     = "YourActualWiFiName";
const char* WIFI_PASSWORD = "YourActualWiFiPassword";

#define FIREBASE_API_KEY  "AIzaSy..."           // from step 4.4
#define FIREBASE_DB_URL   "bup-cpe-river-default-rtdb.asia-southeast1.firebasedatabase.app"
// NOTE: no "https://" prefix in FIREBASE_DB_URL
```

The `NODE_ID` is already set in each file:
- `node-01.ino` → `"station-01"` (Upstream Station)
- `node-02.ino` → `"station-02"` (Midstream Station)
- `node-03.ino` → `"station-03"` (Downstream Station)

Do not change `NODE_ID`.

### 5.2 For Bench Testing — Shorten the Sleep

Change the sleep time to 10 seconds so you don't wait 5 minutes between readings:

```cpp
// Change this line:
#define SLEEP_DURATION_US  300000000ULL   // 5 minutes

// To this for testing:
#define SLEEP_DURATION_US  10000000ULL    // 10 seconds
```

**Remember to change it back to `300000000ULL` before field deployment.**

### 5.3 Flash Each Board

1. Plug the NodeMCU into your computer via USB
2. Open `firmware/v1/node-01/node-01.ino` in Arduino IDE
3. Confirm board settings (see Section 3.4)
4. Select the correct COM port: **Tools → Port**
5. Click **Upload** (→ arrow button)
6. Wait for "Done uploading" in the status bar
7. Repeat for node-02 and node-03 using their respective sketch folders

---

## 6. Flashing the Firmware (V2)

> **V2 hardware** uses the A0120AG RS485 industrial ultrasonic sensor via a C25B TTL-RS485 converter module. It requires a 12V power supply for the sensor and SoftwareSerial for Modbus communication. See `docs/wiring-guide-v2.md` for the full wiring diagram.

Each of the 3 boards gets its own sketch from the `firmware/v2/` folder.

### 6.1 Additional Library

V2 firmware uses `SoftwareSerial` (built-in with ESP8266 board package) — no extra library install needed beyond what's in Section 3.

### 6.2 Fill In Your Credentials

Open each sketch file and replace the placeholder values at the top:

**`firmware/v2/station-01/station-01.ino`** (and same for station-02, station-03):

```cpp
const char* WIFI_SSID     = "YourActualWiFiName";
const char* WIFI_PASSWORD = "YourActualWiFiPassword";

#define FIREBASE_API_KEY  "AIzaSy..."           // from step 4.4
#define FIREBASE_DB_URL   "bup-cpe-river-default-rtdb.asia-southeast1.firebasedatabase.app"
```

The `STATION_ID` is already set in each file:
- `station-01.ino` → `"station-01"` (Upstream)
- `station-02.ino` → `"station-02"` (Midstream)
- `station-03.ino` → `"station-03"` (Downstream)

Do not change `STATION_ID`.

### 6.3 For Bench Testing — Shorten the Sleep

Same as V1 — change `SLEEP_DURATION_US` to `10000000ULL` (10 seconds) for testing.

### 6.4 Flash Each Board

1. Plug the NodeMCU into your computer via USB
2. Open `firmware/v2/station-01/station-01.ino` in Arduino IDE
3. Confirm board settings (see Section 3.4)
4. Select the correct COM port: **Tools → Port**
5. Click **Upload** (→ arrow button)
6. Wait for "Done uploading" in the status bar
7. Repeat for station-02 and station-03 using their respective sketch folders

### 6.5 V2 Pin Differences

| ESP8266 Pin | V1 (AJ-SR04M) | V2 (A0120AG via C25B) |
|---|---|---|
| D1 (GPIO5) | Ultrasonic TRIG | *unused* |
| D5 (GPIO14) | *unused* | C25B DE + RE (direction) |
| D6 (GPIO12) | Ultrasonic ECHO | C25B RO (RS485 RX) |
| D7 (GPIO13) | *unused* | C25B DI (RS485 TX) |
| D2 (GPIO4) | YF-DN50 flow | YF-DN50 flow (same) |
| A0 | Battery divider | *unused in V2* |

> **Important:** V2 requires a **12V external power supply** for the A0120AG sensor. Do NOT connect 12V to the ESP8266. See `docs/wiring-guide-v2.md` for full details.

---

## 7. Bench Testing

Test each node on your desk before field deployment. You need the node wired up but do not need water — use your hand or a flat object to test the ultrasonic sensor.

### 7.1 Open Serial Monitor

After uploading, go to **Tools → Serial Monitor**. Set baud rate to **115200**.

Press the **RST button** on the NodeMCU to trigger a fresh wake cycle.

You should see output like this:

```
========================================
  River Monitor Wake Cycle
  Node: station-01
========================================
[Flow] Pulses=0  Freq=0.00Hz  Flow=0.0L/min
[Sensor] Water Level : 142.5 cm
[Sensor] Flow Rate   : 0.0 L/min
[Sensor] Battery     : 3.82 V
[WiFi] Connecting to YourWiFiName.....
[WiFi] Connected. IP: 192.168.1.105
[WiFi] RSSI: -62 dBm
[Firebase] Authenticated (anonymous)
[Firebase] Sent OK. Key: -NxK3mAbc123...
[Sleep] Entering deep sleep for 10 seconds...
```

### 7.2 Test the Ultrasonic Sensor

Hold a flat object (book, wall, your palm) at different distances above the probe:
- At ~30cm → Serial should read ~30cm
- At ~100cm → Serial should read ~100cm
- Closer than 20cm → reads `-1` or invalid (dead zone — this is correct)

If readings are erratic, check: TRIG → D1, ECHO → D6, VCC → 3.3V.

### 7.3 Test the Flow Sensor

Blow through the YF-DN50 pipe or spin the turbine impeller with a finger:
- You should see non-zero pulses and flow L/min
- With no flow/movement: should read `0.0 L/min`

If it shows phantom pulses with nothing moving, check the 10kΩ pull-up resistor is connected D2 → 3.3V.

### 7.4 Test the Battery Reading

Check the battery voltage output against a multimeter reading on the actual battery:
- Full charge 18650: ~4.1–4.2V
- USB powered only: you may see ~3.3V (that's normal when no battery is connected)

### 7.5 Test Firebase Data

1. Open [console.firebase.google.com](https://console.firebase.google.com) → your project → **Realtime Database**
2. After a successful send, you should see:
   ```
   devices/
     station-01/
       readings/
         -NxK3mAbc123.../
           waterLevel_cm: 142.5
           flowRate_lpm: 0
           battery_v: 3.82
           rssi_dbm: -62
           timestamp: 1709280000000
   ```
3. Every 10 seconds (bench test interval), a new entry appears

### 7.6 Test Offline Buffer

1. Change the WiFi SSID to something wrong: `"WRONG_SSID"`
2. Re-upload the sketch
3. Serial should show: `[WiFi] TIMEOUT` then `[Buffer] Reading saved`
4. Change back to correct SSID, re-upload
5. Serial should show: `[Buffer] Flushing N offline reading(s)...` then `[Buffer] Flushed OK`
6. Check Firebase — buffered readings appear in the database

---

## 8. Running the Dashboard

### 8.1 Fill In Firebase Config

Open `webapp/index.html` in any text editor (Notepad++, VS Code, etc.).

Find this block near the bottom of the file:

```javascript
const firebaseConfig = {
  apiKey:            "YOUR_FIREBASE_WEB_API_KEY",
  authDomain:        "YOUR_PROJECT_ID.firebaseapp.com",
  databaseURL:       "https://YOUR_PROJECT_ID-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId:         "YOUR_PROJECT_ID",
  storageBucket:     "YOUR_PROJECT_ID.appspot.com",
  messagingSenderId: "YOUR_MESSAGING_SENDER_ID",
  appId:             "YOUR_APP_ID"
};
```

Replace it with the full `firebaseConfig` block you copied in step 4.4.

### 8.2 Run Locally

The Firebase SDK requires an HTTP server — it does **not** work when opened as `file://` directly from the file manager.

**Option A — Firebase CLI (recommended, needed for deployment anyway):**

Install Node.js from [nodejs.org](https://nodejs.org) first, then:

```bash
npm install -g firebase-tools
firebase login
cd "c:\Users\satui\OneDrive\Desktop\04 COMISSION_PROJECTS\bup-cpe-river\webapp"
firebase serve
```

Open the URL it shows (usually `http://localhost:5000`).

**Option B — VS Code Live Server extension:**

Install the "Live Server" extension in VS Code → right-click `index.html` → **Open with Live Server**.

### 8.3 What You Should See

- **3 node cards** — each showing location name, water level, flow rate, battery bar, RSSI, and last-seen time
- **Alert banner** (hidden by default) — appears red if any node's water level exceeds its threshold
- **Time-series chart** — select a node and time range (1H / 6H / 24H / 7D); shows water level or flow rate over time
- Cards update **in real time** as each node sends a new reading

If cards show `--` for all values, the Firebase credentials are wrong or the database has no data yet — run the firmware bench test first.

---

## 9. Deploying to Firebase Hosting

This makes the dashboard accessible from any browser at a public URL for free.

### 9.1 Install Firebase CLI

If you haven't already:

```bash
npm install -g firebase-tools
```

### 9.2 Login and Initialize

```bash
firebase login
```

A browser window opens — sign in with the same Google account used for the Firebase project.

```bash
cd "c:\Users\satui\OneDrive\Desktop\04 COMISSION_PROJECTS\bup-cpe-river\webapp"
firebase init hosting
```

Answer the prompts:
- **Which Firebase project?** → Select `bup-cpe-river` (or whatever you named it)
- **What do you want as your public directory?** → type `.` (current folder, since index.html is here)
- **Configure as single-page app?** → `N`
- **Set up automatic builds with GitHub?** → `N`
- **Overwrite index.html?** → `N` (very important — keep your dashboard file)

### 9.3 Deploy

```bash
firebase deploy --only hosting
```

Output will look like:

```
✔ Deploy complete!

Project Console: https://console.firebase.google.com/project/bup-cpe-river/overview
Hosting URL: https://bup-cpe-river.web.app
```

Open the **Hosting URL** in any browser — your dashboard is now live.

Share this URL with supervisors, professors, or anyone who needs to monitor the river data.

### 9.4 Redeploying After Changes

Any time you edit `index.html`, just run:

```bash
firebase deploy --only hosting
```

---

## 10. Field Deployment

Do this after all 3 nodes pass bench testing.

### 10.1 Revert to 5-Minute Sleep

In each firmware file, change back:

```cpp
#define SLEEP_DURATION_US  300000000ULL   // 5 minutes
```

Re-upload to all 3 boards.

### 10.2 Solder Everything

Replace all breadboard jumpers with soldered connections. Use stranded 22 AWG wire.
- Apply hot glue or epoxy over solder joints for vibration resistance
- Tin all bare wire ends before inserting into terminal blocks

### 10.3 Assemble the Enclosure

1. Thread wires through **cable glands** before connecting them (easy to forget)
2. Mount NodeMCU, AJ-SR04M PCB, TP4056, and HT7333 on standoffs or with double-sided foam tape inside the IP67 box
3. Place **desiccant packs** inside
4. Close and seal the box — tighten cable glands

### 10.4 Mount at the Site

**Ultrasonic probe:**
- Mount pointing **straight down** above the water
- Must be at least **20cm above the highest expected water level** (dead zone)
- Install a cone-shaped **rain shield** above the probe face to block raindrops hitting it directly

**Solar panel:**
- Face **south** (Philippines is in Northern Hemisphere)
- Tilt ~15 degrees from flat
- Ensure no shadow falls on it during peak sun hours (10am–2pm)

**Enclosure:**
- Mount the box at least **50cm above expected flood line**
- Secure with stainless steel brackets and UV-resistant zip ties

**YF-DN50 diversion pipe:**
1. Install a 2" PVC intake pipe submerged in the river, **mesh screen on the inlet**
2. Pipe runs out of the water, through the YF-DN50 sensor (check the **flow arrow** on the sensor body)
3. Exit pipe returns water to the river by gravity
4. Secure all piping with metal hose clamps to stakes or the riverbank structure

### 10.5 First Power-On at Site

1. Power on with USB or connect the battery
2. If you have a phone hotspot at the site, confirm the WiFi connects by checking Firebase Console for a new reading within 1–2 minutes
3. Leave the device running for 30 minutes and verify readings are arriving consistently
4. Check the dashboard on your phone using the deployed URL

### 10.6 Update Node Locations in Firebase

After physical placement, update the GPS coordinates in the Firebase Console:

1. Go to **Realtime Database → devices → station-01 → config**
2. Edit `lat` and `lng` to the actual GPS coordinates of that station
3. Repeat for station-02 and station-03

---

## 11. Troubleshooting

### ESP8266 / Firmware

| Symptom | Likely Cause | Fix |
|---|---|---|
| Never wakes from sleep | D0 not wired to RST | Check the D0→RST wire |
| WiFi always times out | Wrong SSID/password or no signal | Verify credentials; test with phone hotspot |
| `Firebase: Error: connection refused` | Wrong DB URL or API key | Check `FIREBASE_DB_URL` has no `https://` prefix |
| `Firebase: Error: token not ready` | Anonymous auth not enabled | Enable in Firebase Console → Authentication |
| Ultrasonic reads -1 constantly | Sensor not connected / wrong pins | Check TRIG→D1, ECHO→D6, VCC→3.3V |
| Ultrasonic reads too high/wild | Condensation on probe / turbulence | Clean probe face; mount above turbulent area |
| Flow reads 0 always | Pull-up resistor missing or wrong pin | Confirm 10kΩ from D2 to 3.3V |
| Battery voltage reads 0 or wrong | Divider not connected / wrong ADC ref | Check both 100kΩ resistors and Battery+ rail |

### ESP8266 / Firmware V2 (A0120AG RS485)

| Symptom | Likely Cause | Fix |
|---|---|---|
| RS485 reads 0 or timeout | No shared ground between 12V PSU and ESP | Connect 12V PSU (-) to ESP GND |
| RS485 reads garbage values | DE/RE not wired together or wrong pin | Both DE and RE must go to D5 (GPIO14) |
| A0120AG no response | Wrong Modbus address or baud rate | Default is address `0x01`, baud `9600` — check sensor datasheet |
| Sensor works on bench but not in field | 12V supply too weak or cable too long | Use a regulated 12V supply; keep RS485 cable under 10m |

### Dashboard

| Symptom | Likely Cause | Fix |
|---|---|---|
| Cards all show `--` | No data in Firebase yet or wrong config | Run firmware first; check firebaseConfig in index.html |
| Cards show data but chart is empty | No readings in the selected time range | Change time range to 24H; wait for more readings |
| Alert banner not showing | Threshold in Firebase config is too high | Edit `alertThreshold_cm` in Firebase Console → node config |
| "Last seen" shows stale (red) | Node stopped sending | Check node power and WiFi |
| Dashboard not updating in real time | Firebase SDK blocked by browser | Open via HTTP server (firebase serve), not file:// |

### Firebase

| Symptom | Likely Cause | Fix |
|---|---|---|
| `Permission denied` write error | DB rules too strict | Set rules to test-mode temporarily |
| Data appears in DB but wrong node | NODE_ID / STATION_ID mismatch | Verify each .ino has the correct NODE_ID (v1) or STATION_ID (v2) defined |
| `seed-config.json` import fails | JSON has comments (invalid JSON) | The `database.rules.json` has comments — that file is NOT what you import. Import `seed-config.json` only |
