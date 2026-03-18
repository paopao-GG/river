// ============================================================
// A0120AG UART Version Test
// Wiring: Yellow(TX)->D6, White(RX)->D7, Red->12V+, Black->12V- & ESP GND
// No C25B module needed!
// ============================================================

#include <SoftwareSerial.h>

#define RX_PIN D6  // Sensor TX -> ESP RX
#define TX_PIN D7  // Sensor RX -> ESP TX

SoftwareSerial sensorSerial(RX_PIN, TX_PIN);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println(F("=== A0120AG UART Sensor Test ==="));
  Serial.println(F("Wiring: Yellow->D6, White->D7, Red->12V+, Black->GND"));
  Serial.println(F("No C25B module needed for UART version"));
  Serial.println();

  // Try common baud rates
  const long bauds[] = {9600, 19200, 38400, 57600, 115200};
  const int numBauds = 5;

  for (int b = 0; b < numBauds; b++) {
    Serial.printf("Testing baud %ld ... ", bauds[b]);
    sensorSerial.begin(bauds[b]);
    delay(100);

    // Flush any garbage
    while (sensorSerial.available()) sensorSerial.read();

    // Wait up to 2 seconds for data
    unsigned long start = millis();
    int bytesReceived = 0;
    uint8_t buf[32];

    while (millis() - start < 2000 && bytesReceived < 32) {
      if (sensorSerial.available()) {
        buf[bytesReceived++] = sensorSerial.read();
      }
    }

    if (bytesReceived > 0) {
      Serial.printf("GOT %d bytes! -> ", bytesReceived);
      for (int i = 0; i < bytesReceived; i++) {
        Serial.printf("%02X ", buf[i]);
      }
      Serial.println();
      Serial.printf(">>> SENSOR FOUND at baud %ld <<<\n\n", bauds[b]);

      // Stay on this baud and start continuous reading
      Serial.println(F("Starting continuous read...\n"));
      continuousRead(bauds[b]);
      return;  // never reaches here
    } else {
      Serial.println(F("no data"));
    }
    sensorSerial.end();
  }

  Serial.println(F("\nNo data received on any baud rate."));
  Serial.println(F("Check:"));
  Serial.println(F("  1. 12V power to sensor (Red=+, Black=-)"));
  Serial.println(F("  2. Shared GND between 12V supply and ESP"));
  Serial.println(F("  3. Yellow wire -> D6"));
  Serial.println(F("  4. Try swapping Yellow and White wires"));
  Serial.println(F("\nRestarting in 10 seconds..."));
  delay(10000);
  ESP.restart();
}

void continuousRead(long baud) {
  sensorSerial.begin(baud);
  uint8_t buf[4];
  int idx = 0;

  while (true) {
    if (sensorSerial.available()) {
      uint8_t b = sensorSerial.read();

      // DYP protocol: 0xFF header, DataH, DataL, Checksum
      if (b == 0xFF) {
        idx = 0;
        buf[idx++] = b;
      } else if (idx > 0 && idx < 4) {
        buf[idx++] = b;
      }

      if (idx == 4) {
        uint8_t checksum = (buf[0] + buf[1] + buf[2]) & 0xFF;
        uint16_t distMM = (buf[1] << 8) | buf[2];

        Serial.printf("[RAW: %02X %02X %02X %02X] ", buf[0], buf[1], buf[2], buf[3]);

        if (checksum == buf[3]) {
          Serial.printf("Distance: %u mm (%.1f cm)\n", distMM, distMM / 10.0);
        } else {
          Serial.printf("BAD CHECKSUM (got %02X, expected %02X)\n", buf[3], checksum);
        }
        idx = 0;
      }
    }

    // Also print any non-protocol bytes (in case it uses ASCII output)
    // Already handled above, but let's also check for ASCII lines
    delay(1);
  }
}

void loop() {
  // Not used -- continuousRead runs forever, or setup restarts
}
