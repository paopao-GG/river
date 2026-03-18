# BUP CPE River Monitoring System

> 3-node IoT system for real-time river water level and flow monitoring with a web dashboard.

---

## Goal

- Measure **water level** and **water flow rate** at 3 locations along a river
- Stream sensor data to a cloud database in real time
- Display live and historical data on a web dashboard
- Alert when water levels reach warning/critical thresholds

---

## Architecture

```
[Node 1 - Centro]───┐
[Node 2 - Napo]─────┼──WiFi──▶ Firebase RTDB ──▶ WebApp Dashboard (Vercel)
[Node 3 - Balinad]──┘
```

Each node reads its sensors, sends data over WiFi to Firebase, waits 1 minute, then repeats (no deep sleep — uses delay loop).

---

## Stations

| Node | Station Name | Location |
|------|-------------|----------|
| Node 1 | Centro Station | Centro |
| Node 2 | Napo Station | Napo |
| Node 3 | Balinad Station | Balinad |

---

## Hardware (Per Node)

| Component | Model | Role |
|---|---|---|
| Microcontroller | ESP8266 (NodeMCU 1.0) | WiFi + sensor control |
| Water Level Sensor | AJ-SR04M Waterproof Ultrasonic | Non-contact distance to water surface |
| Water Flow Sensor | YF-DN50 (2" Hall-effect turbine) | Inline pipe flow measurement |

### Power System (TBD)

| Component | Suggested |
|---|---|
| Solar Panel | 6V / 2W |
| Charge Controller | TP4056 + DW01A |
| Battery | 18650 Li-Ion 3000mAh (x2 parallel) |
| Regulator | HT7333 or MCP1700 (3.3V LDO, low quiescent) |

### Enclosure

- IP67 ABS junction box
- PG7/PG9 cable glands for wire entry
- Desiccant packs inside
- Mounted above expected flood line
- Sun/rain shield over ultrasonic probe

---

## Software Stack

| Layer | Technology |
|---|---|
| Firmware | Arduino C++ (v1 — production) |
| Cloud DB | Firebase Realtime Database |
| WebApp | Single-page HTML + Firebase SDK |
| Charts | Chart.js |
| Hosting | Vercel |

---

## Firmware

### Version: v1 (Production)

Located in `firmware/v1/node-01/`, `node-02/`, `node-03/`.

**Sensor**: AJ-SR04M waterproof ultrasonic (trigger/echo GPIO).

**Operating mode**: Delay loop (no deep sleep). Reads sensors and sends to Firebase every **1 minute**.

Firmware loop:
1. Boot — init pins, LittleFS, connect WiFi, init Firebase
2. Read AJ-SR04M (5 samples, median filter, discard outliers)
3. Read YF-DN50 pulse count (interrupt-based, 1s sampling window)
4. Send reading to Firebase RTDB
5. If WiFi drops, reconnect; if send fails, buffer to LittleFS
6. Wait 60 seconds, repeat

Key libraries:
- `Firebase-ESP8266` (mobizt) — Firebase RTDB communication
- `ESP8266WiFi` — WiFi connectivity
- `LittleFS` — offline data buffering

### Version: v2 (Testing only)

Located in `firmware/v2/station-01/`, `station-02/`, `station-03/`.

**Sensor**: A0120AG RS485 Ultrasonic (via C25B TTL-RS485 converter, Modbus RTU).

**Status**: Testing/development only — not deployed.

---

## Feasibility Review

### AJ-SR04M Ultrasonic Sensor

**Specs**: 3.0-5.5V, 20cm-600cm range, +/-1cm accuracy, IP67 probe (board is NOT waterproof), 75-degree beam angle.

**Verdict: Suitable with caveats.**

Strengths:
- Low cost (~$3-5), low power (<8mA active, <20uA in UART sleep mode)
- Non-contact measurement, no parts in the water
- 6m range covers most small river/stream applications
- 3.3V compatible with ESP8266

Issues to mitigate:
- **Temperature drift** -- speed of sound changes ~0.6 m/s per degree C. Add a DS18B20 temperature sensor and apply software compensation: `distance = (pulse_us * (331.3 + 0.606 * temp_C)) / 20000`
- **Sporadic glitch readings** -- hardware-level issue producing wildly wrong values. Mitigate with median filtering: take 5-10 samples, discard outliers, average the rest
- **Condensation on probe face** -- degrades signal over time. Install a rain/sun shield above the transducer, schedule periodic cleaning
- **Turbulent water surfaces** -- scatter echoes. Mount sensor above a calm section, avoid rapids/spillways
- **20cm dead zone** -- probe must be at least 20cm above highest expected water level

### YF-DN50 Water Flow Sensor

**Specs**: 2" pipe, 10-200 L/min range, +/-5% accuracy, Hall-effect turbine, ~12 pulses/liter, DC 3.5-24V.

**Verdict: Usable with proper installation design.**

The YF-DN50 is an inline pipe sensor -- it needs water flowing *through* it in a sealed pipe. Each node includes a **diversion pipe setup**:

- Install a 2" intake pipe submerged in the river with a mesh screen/filter on the inlet to block debris
- Route the pipe through the YF-DN50 sensor
- Let water exit back to the river via gravity or siphon effect
- The measured flow through the pipe serves as a **proportional sample** of river flow

**Pulse formula**: `frequency_Hz = 0.2 * flow_Lmin` (~12 pulses per liter)

**Wiring**: Signal wire to GPIO4 (D2). Uses interrupt-based pulse counting.

### ESP8266

**Verdict: Sufficient for this design.**

Key specs:
- **Usable GPIOs**: ~6 (GPIO4, 5, 12, 13, 14, 16) -- enough for 2 digital sensors
- **ADC**: 1 channel, 10-bit (A0) -- available for battery voltage monitoring
- **WiFi**: 802.11 b/g/n, ~50-100m range with PCB antenna
- **Flash**: LittleFS available for offline data buffering

**Pin assignment**:
| Pin | Function |
|---|---|
| GPIO4 (D2) | YF-DN50 flow pulse (interrupt) |
| GPIO5 (D1) | AJ-SR04M trigger |
| GPIO12 (D6) | AJ-SR04M echo |
| A0 | Battery voltage via voltage divider (optional) |

### Firebase RTDB

**Verdict: Good fit for this scale.**

- Free tier covers this project: 1GB storage, 10GB/month bandwidth
- 3 devices x every 1 min = 4,320 readings/day = ~13 MB/month growth
- Real-time listeners enable live dashboard updates
- ESP8266 Firebase library (Firebase-ESP8266 by mobizt) is mature

---

## Water Level Calculation

The firmware sends the **raw ultrasonic distance** (sensor-to-water-surface) to Firebase. The webapp converts this to actual water level:

```
water_level = sensor_height - measured_distance
```

- `sensor_height` is configurable per node in Firebase config (default: 200 cm)
- `max_water_level` = 200 cm (2 meters)
- Values are clamped to 0–200 cm range

---

## WebApp Dashboard

**URL**: Hosted on Vercel

**Features**:
- Real-time station cards showing water level, flow rate, RSSI, and last seen
- Animated water tank visualization with color gradient (green → yellow → red)
- Historical time-series charts (1H, 6H, 24H, 7D, 30D)
- Water level prediction (linear regression) for Centro Station
- CSV data export
- Alert banner when water level exceeds threshold

---

## Data Structure (Firebase RTDB)

```json
{
  "devices": {
    "station-01": {
      "readings": {
        "-pushKey": {
          "waterLevel_cm": 142.5,
          "flowRate_lpm": 85.3,
          "rssi_dbm": -62,
          "timestamp": 1709280000000
        }
      },
      "config": {
        "location": "Centro Station",
        "sensorHeight_cm": 200,
        "alertThreshold_cm": 150
      }
    },
    "station-02": {
      "config": {
        "location": "Napo Station"
      }
    },
    "station-03": {
      "config": {
        "location": "Balinad Station"
      }
    }
  }
}
```

---

## Estimated BOM (Per Node)

| Component | Est. Cost |
|---|---|
| ESP8266 Dev Board (NodeMCU 1.0) | $3 |
| AJ-SR04M Waterproof Ultrasonic Sensor | $4 |
| YF-DN50 Water Flow Sensor | $10 |
| 6V/2W Solar Panel | $3 |
| TP4056 Charge Module | $0.50 |
| 18650 Battery x2 | $4 |
| HT7333 Regulator | $0.50 |
| IP67 Enclosure + Cable Glands | $3 |
| 2" PVC pipe + mesh filter (flow diversion) | $3 |
| Misc (wires, connectors, desiccant) | $2 |
| **Total per node** | **~$33** |
| **Total for 3 nodes** | **~$99** |

> Cloud cost: **$0/month** on Firebase free tier at this scale.
