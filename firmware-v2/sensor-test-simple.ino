// ============================================================
// A0120AG RS485 Sensor Test -- Improved timing & diagnostics
//
// Based on working examples from Arduino forum for similar
// DYP/Chinese RS485 ultrasonic sensors.
//
// Wiring:
//   C25B TTL side:
//     VCC -> ESP VU (5V)
//     GND -> ESP GND
//     DI  -> ESP D7 (GPIO13)
//     RO  -> ESP D6 (GPIO12)
//     DE  -> ESP D5 (GPIO14)  } wired together
//     RE  -> ESP D5 (GPIO14)  }
//
//   C25B RS485 side:
//     A -> Sensor Yellow
//     B -> Sensor White
//     (If no response, swap A/B)
//
//   Sensor power:
//     Red   -> 5V (ESP VU) or external 12V+
//     Black -> GND (MUST share GND with ESP)
// ============================================================

#include <SoftwareSerial.h>

#define RX_PIN  D6
#define TX_PIN  D7
#define DE_PIN  D5

SoftwareSerial rs485(RX_PIN, TX_PIN);

// ---- Modbus CRC16 ----
uint16_t modbusCRC(uint8_t* buf, int len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) { crc >>= 1; crc ^= 0xA001; }
      else          { crc >>= 1; }
    }
  }
  return crc;
}

// ---- Send Modbus request and read response ----
int sendAndReceive(uint8_t addr, uint16_t reg, uint8_t* resp, int respSize) {
  uint8_t frame[8];
  frame[0] = addr;
  frame[1] = 0x03;          // Read Holding Registers
  frame[2] = reg >> 8;
  frame[3] = reg & 0xFF;
  frame[4] = 0x00;
  frame[5] = 0x01;           // Read 1 register
  uint16_t crc = modbusCRC(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = (crc >> 8) & 0xFF;

  // Flush any stale RX data
  while (rs485.available()) rs485.read();

  // TX mode
  digitalWrite(DE_PIN, HIGH);
  delayMicroseconds(500);    // let DE settle

  // Send frame
  rs485.write(frame, 8);
  rs485.flush();             // wait until all bytes sent

  // Critical: delay AFTER flush, BEFORE switching to RX
  delay(10);

  // RX mode
  digitalWrite(DE_PIN, LOW);
  delayMicroseconds(500);    // let RE settle

  // Wait for response with timeout
  unsigned long start = millis();
  int count = 0;
  while (millis() - start < 300 && count < respSize) {
    yield();
    if (rs485.available()) {
      resp[count++] = rs485.read();
    }
  }
  return count;
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println();
  Serial.println("=========================================");
  Serial.println("  A0120AG RS485 Improved Test");
  Serial.println("=========================================");
  Serial.println("C25B: VCC->5V, DI->D7, RO->D6, DE+RE->D5");
  Serial.println("Sensor: Yellow->A, White->B (swap if needed)");
  Serial.println();

  pinMode(DE_PIN, OUTPUT);
  digitalWrite(DE_PIN, LOW);
}

void loop() {
  // =============================================
  // TEST 1: Targeted scan (most likely settings)
  // =============================================
  Serial.println("=== TEST 1: Targeted Modbus scan ===");

  long bauds[] = {9600, 19200, 115200};
  uint8_t addrs[] = {0x01, 0x02, 0x00};
  uint16_t regs[] = {0x0001, 0x0100, 0x0101, 0x0000, 0x0002, 0x0003};

  for (int b = 0; b < 3; b++) {
    rs485.begin(bauds[b]);
    delay(100);
    Serial.print("-- Baud ");
    Serial.print(bauds[b]);
    Serial.println(" --");

    for (int a = 0; a < 3; a++) {
      for (int r = 0; r < 6; r++) {
        yield();

        uint8_t resp[16];
        Serial.print("  Addr=");
        Serial.print(addrs[a], HEX);
        Serial.print(" Reg=");
        Serial.print(regs[r], HEX);
        Serial.print(": ");

        int n = sendAndReceive(addrs[a], regs[r], resp, 16);

        if (n > 0) {
          for (int i = 0; i < n; i++) {
            if (resp[i] < 0x10) Serial.print("0");
            Serial.print(resp[i], HEX);
            Serial.print(" ");
          }
          Serial.println(" << GOT DATA!");

          if (n >= 5 && resp[1] == 0x03) {
            uint16_t val = (resp[3] << 8) | resp[4];
            Serial.print("  >>> DISTANCE: ");
            Serial.print(val);
            Serial.print(" mm (");
            Serial.print(val / 10.0);
            Serial.println(" cm) <<<");
          }
        } else {
          Serial.println("(none)");
        }
        delay(50);
      }
    }
    rs485.end();
  }

  // =============================================
  // TEST 2: Passive listen (auto-output mode?)
  // =============================================
  Serial.println();
  Serial.println("=== TEST 2: Passive listen 10 sec ===");

  long listenBauds[] = {9600, 19200, 115200};
  for (int b = 0; b < 3; b++) {
    rs485.begin(listenBauds[b]);
    digitalWrite(DE_PIN, LOW);
    delay(50);

    Serial.print("  Listening at ");
    Serial.print(listenBauds[b]);
    Serial.print("... ");

    unsigned long start = millis();
    int count = 0;
    uint8_t buf[64];

    while (millis() - start < 3000 && count < 64) {
      yield();
      if (rs485.available()) {
        buf[count++] = rs485.read();
      }
    }
    rs485.end();

    if (count > 0) {
      Serial.print(count);
      Serial.print(" bytes: ");
      for (int i = 0; i < count && i < 32; i++) {
        if (buf[i] < 0x10) Serial.print("0");
        Serial.print(buf[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
    } else {
      Serial.println("nothing");
    }
  }

  // =============================================
  // TEST 3: Loopback test (is C25B working?)
  // =============================================
  Serial.println();
  Serial.println("=== TEST 3: C25B loopback ===");
  Serial.println("  (Disconnect sensor A/B wires for this test)");
  Serial.println("  (Short A to B on C25B to create loopback)");

  rs485.begin(9600);
  delay(100);

  // Send test bytes
  uint8_t test[] = {0xAA, 0x55, 0x01, 0x02};
  digitalWrite(DE_PIN, HIGH);
  delayMicroseconds(500);
  rs485.write(test, 4);
  rs485.flush();
  delay(10);
  digitalWrite(DE_PIN, LOW);
  delayMicroseconds(500);

  delay(50);
  int count = 0;
  while (rs485.available() && count < 10) {
    uint8_t b = rs485.read();
    if (count == 0) Serial.print("  Echo: ");
    if (b < 0x10) Serial.print("0");
    Serial.print(b, HEX);
    Serial.print(" ");
    count++;
  }

  if (count > 0) {
    Serial.println(" << C25B works!");
  } else {
    Serial.println("  No echo -- C25B may be dead or wiring wrong");
    Serial.println("  Check: DI->D7, RO->D6, DE+RE->D5, VCC->5V");
  }

  rs485.end();

  Serial.println();
  Serial.println("=== DONE. Repeating in 15 sec ===");
  Serial.println("If no response: try swapping A/B wires,");
  Serial.println("or power sensor from 12V external supply.");
  Serial.println();
  delay(15000);
}
