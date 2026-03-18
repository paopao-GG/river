# Wiring Guide -- Firmware V2 (A0120AG + C25B + ESP8266)

## Components

| Component | Description |
|-----------|-------------|
| ESP8266 NodeMCU 1.0 (12E) | Microcontroller on NodeMCU Base V1 |
| A0120AG RS485 | Industrial ultrasonic distance sensor (4 wires) |
| C25B TTL-RS485 | TTL to RS485 converter module |
| YF-DN50 | Hall-effect water flow sensor |
| 12V Power Supply | Powers the A0120AG sensor |

---

## A0120AG Sensor Wires (4 wires, no label)

| Wire Color | Function | Description |
|------------|----------|-------------|
| **Red** | VCC (12-24V) | Power positive |
| **Black** | GND | Power ground |
| **Yellow** | RS485 A+ | Data line A (positive) |
| **White** | RS485 B- | Data line B (negative) |

---

## C25B Module Pins

**Side 1 (TTL side):** DI, DE, RE, RO

| Pin | Function |
|-----|----------|
| DI | Data In (TTL TX → this pin) |
| DE | Driver Enable (HIGH = transmit mode) |
| RE | Receiver Enable (LOW = receive mode) |
| RO | Read Out (data from RS485 → TTL RX) |

**Side 2 (RS485 side):** GND, A, B, VCC

| Pin | Function |
|-----|----------|
| GND | Ground |
| A | RS485 Data A+ |
| B | RS485 Data B- |
| VCC | 3.3V power for the module |

---

## Full Wiring Diagram

```
                                    C25B MODULE
                               ┌─────────────────┐
                               │                  │
  ESP8266          TTL Side    │    RS485 Side     │    A0120AG Sensor
  ────────         ─────────   │    ──────────     │    ──────────────
                               │                  │
  D7 (GPIO13) ───► DI         │                  │
                               │         A  ──────┼──► Yellow (A+)
  D6 (GPIO12) ◄─── RO         │         B  ──────┼──► White  (B-)
                               │                  │
  D5 (GPIO14) ───► DE ─┐      │       VCC ◄──────┼─── 3.3V (from ESP)
                        ├──┐   │       GND ◄──────┼─── GND  (shared)
  D5 (GPIO14) ───► RE ─┘  │   │                  │
                           │   └─────────────────┘
                           │
              (DE and RE   │
               wired       │
               together)   │


  12V POWER SUPPLY                    A0120AG SENSOR
  ────────────────                    ──────────────
  (+) 12V ────────────────────────►   Red    (VCC 12-24V)
  (-) GND ──────┬─────────────────►   Black  (GND)
                │                     Yellow (A+)  ──► C25B pin A
                │                     White  (B-)  ──► C25B pin B
                │
                └──► ESP8266 GND (shared ground!)


  ESP8266                             YF-DN50 FLOW SENSOR
  ────────                            ─────────────────
  D2 (GPIO4) ◄────────────────────── Signal (Yellow)
  3.3V ──┬── 10kΩ resistor ──┬────── VCC    (Red)
         │                    │
         │              (pullup)
         │                    │
         └────────────────────┘
  GND ────────────────────────────── GND    (Black)
```

---

## Step-by-Step Connections

### Step 1: A0120AG Sensor → C25B Module

| A0120AG Wire | Connect To |
|--------------|------------|
| Yellow | C25B **A** pin |
| White | C25B **B** pin |

### Step 2: A0120AG Sensor → 12V Power Supply

| A0120AG Wire | Connect To |
|--------------|------------|
| Red | 12V Power Supply **(+) positive** |
| Black | 12V Power Supply **(-) negative** |

### Step 3: C25B Module → ESP8266

| C25B Pin | Connect To | ESP8266 Label |
|----------|------------|---------------|
| RO | GPIO12 | **D6** |
| DI | GPIO13 | **D7** |
| DE | GPIO14 | **D5** |
| RE | GPIO14 | **D5** (same pin as DE, wire them together) |
| VCC | 3.3V | **3V3** |
| GND | GND | **GND** |

### Step 4: YF-DN50 Flow Sensor → ESP8266

| YF-DN50 Wire | Connect To | ESP8266 Label |
|--------------|------------|---------------|
| Yellow (Signal) | GPIO4 | **D2** |
| Red (VCC) | 3.3V | **3V3** |
| Black (GND) | GND | **GND** |
| 10kΩ Resistor | Between **D2** and **3V3** | (pullup resistor) |

### Step 5: Shared Ground (CRITICAL)

All GND pins must be connected together:

| GND Source | Connect To |
|------------|------------|
| 12V Power Supply (-) | ESP8266 **GND** |
| C25B **GND** | ESP8266 **GND** |
| YF-DN50 **Black** | ESP8266 **GND** |
| A0120AG **Black** | 12V Power Supply **(-) negative** |

### ESP8266 Pin Summary

| ESP8266 Pin | Label | Connected To |
|-------------|-------|--------------|
| GPIO12 | D6 | C25B RO |
| GPIO13 | D7 | C25B DI |
| GPIO14 | D5 | C25B DE + RE |
| GPIO4 | D2 | YF-DN50 Signal |
| 3V3 | 3V3 | C25B VCC, YF-DN50 VCC, pullup resistor |
| GND | GND | C25B GND, YF-DN50 GND, 12V PSU (-) |

---

## Important Notes

1. **Shared Ground** -- The 12V power supply GND, ESP8266 GND, C25B GND, and A0120AG GND must ALL be connected together. Without a common ground, RS485 communication will fail.

2. **DE and RE tied together** -- Connect both DE and RE pins on the C25B to the same ESP8266 GPIO pin (D5). The firmware sets this HIGH when transmitting a Modbus request and LOW when waiting for a response.

3. **Do NOT connect 12V to ESP8266** -- The 12V supply is ONLY for the A0120AG sensor. The ESP8266 and C25B run on 3.3V from the NodeMCU's onboard regulator.

4. **Wire colors on A0120AG** -- Red=Power, Black=GND, Yellow=A+, White=B-. If your sensor has different colors, check the datasheet. The RS485 pair is always the two non-power wires.

5. **10kΩ pullup on flow sensor** -- The YF-DN50 has an open-collector output. A 10kΩ resistor between D2 and 3.3V ensures clean pulse signals. You can use the ESP8266's internal pullup (already enabled in firmware via `INPUT_PULLUP`) but an external resistor is more reliable.

