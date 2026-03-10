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
[Node 1]──┐
[Node 2]──┼──WiFi──▶ Firebase RTDB ──▶ WebApp Dashboard
[Node 3]──┘
```

Each node reads its sensors, wakes from deep sleep every 5 minutes, sends data over WiFi to Firebase, then returns to sleep.

---

## Hardware (Per Node)

| Component | Model | Role |
|---|---|---|
| Microcontroller | ESP8266 | WiFi + sensor control |
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
| Firmware | Arduino / PlatformIO (C++) |
| Cloud DB | Firebase Realtime Database |
| WebApp | React or Vue + Firebase SDK |
| Charts | Chart.js or Recharts |
| Alerts | Firebase Cloud Functions + FCM |

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

Recommended mode: **UART Mode 4** (on-demand serial output at 9600 baud) for best accuracy + low power.

### YF-DN50 Water Flow Sensor

**Specs**: 2" pipe, 10-200 L/min range, +/-5% accuracy, Hall-effect turbine, ~12 pulses/liter, DC 3.5-24V.

**Verdict: Usable with proper installation design.**

The YF-DN50 is an inline pipe sensor -- it needs water flowing *through* it in a sealed pipe. It cannot be dropped into an open river channel. To use it for river monitoring, the node must include a **diversion pipe setup**:

- Install a 2" intake pipe submerged in the river with a mesh screen/filter on the inlet to block debris
- Route the pipe through the YF-DN50 sensor
- Let water exit back to the river via gravity or siphon effect
- The measured flow through the pipe serves as a **proportional sample** of river flow, not absolute total flow

Practical considerations:
- **Sediment/debris** -- the intake mesh filter is critical; plan for periodic cleaning
- **Scale** -- the sensor measures the sample pipe flow (10-200 L/min), not total river discharge. This gives a relative flow indicator, useful for detecting changes/trends rather than absolute volume
- **Biofilm fouling** -- natural water causes biological growth on internal surfaces over time; schedule periodic flushing
- **Calibration** -- establish a baseline relationship between pipe flow readings and actual river flow through manual measurements during setup
- **Pulse formula**: `frequency_Hz = 0.2 * flow_Lmin` (~12 pulses per liter)

**Wiring with ESP8266**: Signal wire to any digital GPIO pin (e.g., D2/GPIO4). Use interrupt-based pulse counting for accuracy.

### ESP8266

**Verdict: Sufficient for this design.**

Key specs:
- **Usable GPIOs**: ~6 (GPIO4, 5, 12, 13, 14, 16) -- enough for 2 digital sensors
- **ADC**: 1 channel, 10-bit (A0) -- available for battery voltage monitoring
- **Deep sleep**: ~20uA (GPIO16 must be wired to RST for wake)
- **WiFi**: 802.11 b/g/n, ~50-100m range with PCB antenna
- **Flash**: SPIFFS/LittleFS available for offline data buffering

The AJ-SR04M uses 2 digital pins (trigger + echo) and the YF-DN50 uses 1 digital pin (pulse interrupt). This fits within the ESP8266's GPIO budget with pins to spare.

**Pin assignment (suggested)**:
| Pin | Function |
|---|---|
| GPIO4 (D2) | YF-DN50 flow pulse (interrupt) |
| GPIO5 (D1) | AJ-SR04M trigger |
| GPIO12 (D6) | AJ-SR04M echo |
| GPIO14 (D5) | DS18B20 temperature (optional) |
| A0 | Battery voltage via voltage divider |
| GPIO16 (D0) | Wired to RST for deep sleep wake |

### Firebase RTDB

**Verdict: Good fit for this scale.**

- Free tier covers this project easily: 1GB storage, 10GB/month bandwidth
- 3 devices x every 5 min = 864 readings/day = ~2.6 MB/month growth
- Real-time listeners enable live dashboard updates
- ESP8266 Firebase library (Firebase-ESP8266 by mobizt) is mature

Limitations:
- Not a time-series DB -- no built-in aggregation (avg, min, max over time)
- No data retention policies -- must implement cleanup manually
- Limited querying compared to Firestore
- ESP-side has no offline queue -- if WiFi drops, the reading is lost (mitigate with SPIFFS/LittleFS buffer)

---

## Implementation Notes

### Firmware (ESP8266)

Key libraries:
- `Firebase-ESP8266` (mobizt) -- Firebase RTDB communication
- `ESP8266WiFi` -- WiFi connectivity
- `NewPing` or manual trigger/echo -- AJ-SR04M reading
- `OneWire` + `DallasTemperature` -- DS18B20 (if added)

Firmware loop (per wake cycle):
1. Wake from deep sleep
2. Read AJ-SR04M (take 5-10 samples, median filter, discard outliers)
3. Read YF-DN50 pulse count (accumulated via interrupt during a sampling window)
4. Read battery voltage via A0
5. Connect to WiFi (use static IP + stored BSSID for faster reconnect)
6. Push reading to Firebase RTDB
7. If WiFi fails, buffer to LittleFS
8. Enter deep sleep for 5 minutes

### YF-DN50 Installation

Since this is an inline pipe sensor, each node needs a diversion pipe setup:
1. Submerge a 2" PVC intake pipe in the river with a **mesh screen** on the inlet
2. Route the pipe through the YF-DN50
3. Let water exit back to the river via gravity/siphon
4. Secure all piping to prevent displacement during high flow

### Sensor Reliability

- **Median filtering** for AJ-SR04M: take 7 readings, discard highest and lowest 2, average the middle 3
- **Temperature compensation** (optional but recommended): add DS18B20, cost ~$2
- **Debounce** for YF-DN50: use hardware interrupt on rising edge, count pulses over a fixed sampling window (e.g., 5 seconds)
- **Offline buffering**: store up to ~100 readings in LittleFS when WiFi is down, flush on reconnect

### Dashboard Features

The webapp should include:
- Real-time gauge widgets for current water level and flow rate per node
- Time-series line charts (24h, 7d, 30d) with threshold zones (green/yellow/red)
- Device health panel (battery, signal, last seen)
- Historical data table with CSV export
- Alert banner when water level exceeds configurable thresholds

---

## Estimated BOM (Per Node)

| Component | Est. Cost |
|---|---|
| ESP8266 Dev Board (NodeMCU/Wemos D1 Mini) | $3 |
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

---

## Open Questions

- [ ] Where exactly along the river will the 3 nodes be deployed? (affects WiFi range planning)
- [ ] Is there WiFi infrastructure nearby, or do we need a cellular fallback (SIM800L)?
- [ ] Is it feasible to install a 2" diversion pipe at each node location for the flow sensor?
- [ ] What are the flood warning thresholds for this specific river?
- [ ] What is the expected maximum water level variation? (needed to size sensor mounting height above the 20cm dead zone)

---

## Data Structure (Firebase RTDB)

```json
{
  "devices": {
    "node-01": {
      "readings": {
        "1709280000": {
          "waterLevel_cm": 142.5,
          "flowRate_lpm": 85.3,
          "battery_v": 3.82,
          "rssi_dbm": -62
        }
      },
      "config": {
        "location": "Upstream Bridge",
        "lat": 14.5995,
        "lng": 120.9842,
        "readingInterval_ms": 300000,
        "alertThreshold_cm": 200
      }
    }
  },
  "alerts": {
    "1709280300": {
      "deviceId": "node-01",
      "type": "warning",
      "waterLevel_cm": 205.2,
      "message": "Water level exceeded warning threshold"
    }
  }
}
```
