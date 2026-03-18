//========================================================================
// A0120AG 6270057 -- Comprehensive C25B + Sensor Diagnostic
//
// Wiring (same as before):
//   C25B RO     -> D6 (GPIO12)
//   C25B DI     -> D7 (GPIO13)
//   C25B DE+RE  -> D5 (GPIO14)
//   C25B VCC    -> VU (5V)
//   C25B GND    -> GND
//   A0120AG Yellow -> C25B A
//   A0120AG White  -> C25B B
//   A0120AG Red    -> 12V+
//   A0120AG Black  -> 12V- and ESP GND
//
// Serial Monitor: 115200 baud
//========================================================================

#include <SoftwareSerial.h>

#define RS485_RX_PIN   12  // D6 - connected to C25B RO
#define RS485_TX_PIN   13  // D7 - connected to C25B DI
#define RS485_DIR_PIN  14  // D5 - connected to C25B DE+RE

SoftwareSerial rs485(RS485_RX_PIN, RS485_TX_PIN);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("\n=============================================="));
  Serial.println(F("  A0120AG + C25B Full Diagnostic"));
  Serial.println(F("==============================================\n"));

  pinMode(RS485_DIR_PIN, OUTPUT);

  //--------------------------------------------------
  // TEST 1: Verify GPIO pins are working
  //--------------------------------------------------
  Serial.println(F("=== TEST 1: GPIO Pin Test ==="));
  Serial.println(F("Toggling DE pin (D5/GPIO14) 5 times..."));
  Serial.println(F("(Measure with multimeter on C25B DE pin)"));
  for (int i = 0; i < 5; i++) {
    digitalWrite(RS485_DIR_PIN, HIGH);
    Serial.printf("  D5 = HIGH (should read ~3.3V) [%d/5]\n", i + 1);
    delay(500);
    digitalWrite(RS485_DIR_PIN, LOW);
    Serial.printf("  D5 = LOW  (should read ~0V)   [%d/5]\n", i + 1);
    delay(500);
  }
  Serial.println(F("  If voltage didn't change, D5 pin or wire is bad.\n"));

  //--------------------------------------------------
  // TEST 2: SoftwareSerial TX pin test
  //--------------------------------------------------
  Serial.println(F("=== TEST 2: SoftwareSerial TX Test ==="));
  Serial.println(F("Sending bytes on D7 (GPIO13)..."));
  Serial.println(F("(Measure with multimeter on C25B DI pin - should see ~3.3V idle)"));
  rs485.begin(9600);
  digitalWrite(RS485_DIR_PIN, HIGH);
  delay(10);
  for (int i = 0; i < 10; i++) {
    rs485.write(0x55);
  }
  rs485.flush();
  delay(10);
  digitalWrite(RS485_DIR_PIN, LOW);
  Serial.println(F("  Sent 10 bytes of 0x55.\n"));

  //--------------------------------------------------
  // TEST 3: Loopback with various timings
  //--------------------------------------------------
  Serial.println(F("=== TEST 3: Loopback Test (various timings) ==="));
  Serial.println(F("For this test: SHORT C25B A to B with a wire.\n"));

  int timings[] = {1, 5, 10, 50, 100};
  for (int t = 0; t < 5; t++) {
    while (rs485.available()) rs485.read();

    Serial.printf("  Timing: %dms post-TX delay... ", timings[t]);

    // Send
    digitalWrite(RS485_DIR_PIN, HIGH);
    delay(timings[t]);
    rs485.write(0x55);
    rs485.write(0xAA);
    rs485.flush();
    delay(timings[t]);

    // Switch to receive
    digitalWrite(RS485_DIR_PIN, LOW);
    delay(100);

    int count = 0;
    unsigned long start = millis();
    while (millis() - start < 200) {
      if (rs485.available()) {
        uint8_t b = rs485.read();
        Serial.printf("%02X ", b);
        count++;
      }
      yield();
    }
    if (count == 0) Serial.print("(no echo)");
    Serial.println();
  }

  //--------------------------------------------------
  // TEST 4: Listen-only mode (no C25B TX needed)
  // If sensor auto-outputs, we should see data even
  // if C25B TX side is broken
  //--------------------------------------------------
  Serial.println(F("\n=== TEST 4: Passive Listen (sensor auto-output) ==="));
  Serial.println(F("Listening at each baud for 5 seconds...\n"));

  uint32_t bauds[] = {9600, 4800, 19200, 2400, 115200};
  int numBauds = 5;

  for (int b = 0; b < numBauds; b++) {
    rs485.begin(bauds[b]);
    digitalWrite(RS485_DIR_PIN, LOW);  // receive mode
    delay(100);

    while (rs485.available()) rs485.read();  // flush

    Serial.printf("  Baud %lu: ", bauds[b]);

    unsigned long start = millis();
    int count = 0;
    uint8_t buf[64];

    while (millis() - start < 5000) {
      if (rs485.available()) {
        if (count < 64) buf[count] = rs485.read();
        count++;
      }
      yield();
    }

    if (count > 0) {
      Serial.printf("GOT %d bytes! -> ", count);
      for (int i = 0; i < min(count, 32); i++) {
        Serial.printf("%02X ", buf[i]);
      }
      Serial.println();

      // Try parse
      Serial.print("    ASCII: ");
      for (int i = 0; i < min(count, 32); i++) {
        char c = buf[i];
        if (c >= 32 && c <= 126) Serial.print(c);
        else if (c == '\r') Serial.print("[CR]");
        else if (c == '\n') Serial.print("[LN]");
        else Serial.printf("[%02X]", c);
      }
      Serial.println();

      // Check for 0xFF binary frames
      for (int i = 0; i < min(count, 60); i++) {
        if (buf[i] == 0xFF && i + 3 < count) {
          uint16_t dist = (buf[i + 1] << 8) | buf[i + 2];
          uint8_t cs = (0xFF + buf[i + 1] + buf[i + 2]) & 0xFF;
          if (cs == buf[i + 3]) {
            Serial.printf("    >> VALID FRAME: distance = %u mm (%.1f cm)\n", dist, dist / 10.0f);
          }
        }
      }

      Serial.printf("\n  ** SENSOR DETECTED at %lu baud! **\n", bauds[b]);
      Serial.println(F("  Entering continuous mode...\n"));
      continuousRead(bauds[b]);
      return;
    } else {
      Serial.println(F("(nothing)"));
    }
  }

  //--------------------------------------------------
  // TEST 5: Try reading directly on D6 without C25B
  // (just to see if the pin itself can receive)
  //--------------------------------------------------
  Serial.println(F("\n=== TEST 5: Raw GPIO12 (D6) state ==="));
  Serial.println(F("Reading D6 pin state 20 times (100ms apart)..."));
  Serial.println(F("(Should be HIGH if C25B RO is idle, LOW if pulled down)"));
  pinMode(RS485_RX_PIN, INPUT);
  for (int i = 0; i < 20; i++) {
    Serial.printf("  D6 = %s\n", digitalRead(RS485_RX_PIN) ? "HIGH" : "LOW");
    delay(100);
  }

  //--------------------------------------------------
  // TEST 6: Try Modbus commands at 9600
  //--------------------------------------------------
  Serial.println(F("\n=== TEST 6: Modbus RTU commands (9600) ==="));
  rs485.begin(9600);
  delay(200);

  // Try multiple slave addresses
  for (uint8_t addr = 1; addr <= 5; addr++) {
    tryModbus(addr, 0x0000);
    tryModbus(addr, 0x0001);
    tryModbus(addr, 0x0100);
  }

  //--------------------------------------------------
  // RESULT
  //--------------------------------------------------
  Serial.println(F("\n=============================================="));
  Serial.println(F("  DIAGNOSTIC COMPLETE"));
  Serial.println(F("=============================================="));
  Serial.println(F(""));
  Serial.println(F("If ALL tests show no data:"));
  Serial.println(F("  -> C25B module is dead. Replace it."));
  Serial.println(F("     Get a MAX485 or HW-0519 module."));
  Serial.println(F(""));
  Serial.println(F("If TEST 4 shows data but TEST 3 loopback fails:"));
  Serial.println(F("  -> C25B RX works but TX is broken."));
  Serial.println(F("  -> Sensor is auto-output, so TX isn't needed!"));
  Serial.println(F(""));
  Serial.println(F("If TEST 5 shows D6 stuck LOW:"));
  Serial.println(F("  -> C25B RO output may be shorted or dead."));
  Serial.println(F("  -> Try a different ESP8266 GPIO for RX."));
  Serial.println(F("=============================================="));
}

void loop() {
  delay(10000);
}

void continuousRead(uint32_t baud) {
  rs485.begin(baud);
  digitalWrite(RS485_DIR_PIN, LOW);

  uint8_t frameBuf[4];
  int framePos = 0;
  unsigned long lastPrint = 0;

  while (true) {
    if (rs485.available()) {
      uint8_t b = rs485.read();

      // Try to sync on 0xFF header for binary frames
      if (b == 0xFF) {
        framePos = 0;
        frameBuf[framePos++] = b;
      } else if (framePos > 0 && framePos < 4) {
        frameBuf[framePos++] = b;

        if (framePos == 4) {
          uint16_t dist = (frameBuf[1] << 8) | frameBuf[2];
          uint8_t cs = (frameBuf[0] + frameBuf[1] + frameBuf[2]) & 0xFF;
          if (cs == frameBuf[3]) {
            Serial.printf("[BINARY] Distance: %u mm (%.1f cm)\n", dist, dist / 10.0f);
          } else {
            Serial.printf("[BINARY] Bad checksum: %02X %02X %02X %02X\n",
                          frameBuf[0], frameBuf[1], frameBuf[2], frameBuf[3]);
          }
          framePos = 0;
        }
      } else {
        // Not in a binary frame -- print as ASCII/hex
        if (b >= 32 && b <= 126) {
          Serial.print((char)b);
        } else if (b == '\r') {
          // skip
        } else if (b == '\n') {
          Serial.println();
        } else {
          Serial.printf("[%02X]", b);
        }
        framePos = 0;
      }
    }
    yield();
  }
}

void tryModbus(uint8_t addr, uint16_t reg) {
  uint8_t req[] = {
    addr, 0x03,
    (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
    0x00, 0x01
  };
  uint16_t crc = crc16(req, 6);
  uint8_t frame[8];
  memcpy(frame, req, 6);
  frame[6] = crc & 0xFF;
  frame[7] = (crc >> 8) & 0xFF;

  while (rs485.available()) rs485.read();

  Serial.printf("  Addr=%02X Reg=%04X [TX: ", addr, reg);
  for (int i = 0; i < 8; i++) Serial.printf("%02X ", frame[i]);
  Serial.print("]: ");

  digitalWrite(RS485_DIR_PIN, HIGH);
  delay(5);
  rs485.write(frame, 8);
  rs485.flush();
  delay(5);
  digitalWrite(RS485_DIR_PIN, LOW);

  int count = 0;
  unsigned long start = millis();
  while (millis() - start < 1000) {
    if (rs485.available()) {
      Serial.printf("%02X ", rs485.read());
      count++;
      if (count > 20) break;
    }
    yield();
  }
  Serial.println(count == 0 ? "(no response)" : "");
}

uint16_t crc16(const uint8_t* data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) { crc >>= 1; crc ^= 0xA001; }
      else { crc >>= 1; }
    }
  }
  return crc;
}
