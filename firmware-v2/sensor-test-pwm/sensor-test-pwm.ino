// ============================================================
// A0120AG PWM + UART Combined Test
// Tests ALL possible pin combinations and both protocols
// Wiring: Red->3V3, Black->GND, Yellow->D6, White->D7
// ============================================================

#include <SoftwareSerial.h>

// We'll test both pin arrangements
SoftwareSerial uart1(D6, D7);  // RX=D6, TX=D7
SoftwareSerial uart2(D7, D6);  // RX=D7, TX=D6 (swapped)

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println(F("=== A0120AG COMBINED TEST (PWM + UART) ==="));
  Serial.println(F("Wiring: Red->3V3, Black->GND, Yellow->D6, White->D7"));
  Serial.println();

  // ---- TEST 1: PWM on D6 ----
  Serial.println(F("--- TEST 1: PWM on D6 (Yellow) ---"));
  testPWM(D6, "D6");

  // ---- TEST 2: PWM on D7 ----
  Serial.println(F("--- TEST 2: PWM on D7 (White) ---"));
  testPWM(D7, "D7");

  // ---- TEST 3: UART with D6=RX, D7=TX ----
  Serial.println(F("--- TEST 3: UART D6=RX, D7=TX ---"));
  testUART(uart1, "D6=RX,D7=TX");

  // ---- TEST 4: UART with D7=RX, D6=TX (swapped) ----
  Serial.println(F("--- TEST 4: UART D7=RX, D6=TX ---"));
  testUART(uart2, "D7=RX,D6=TX");

  // ---- TEST 5: Trigger + Echo (like HC-SR04) ----
  Serial.println(F("--- TEST 5: Trigger/Echo D7=TRIG, D6=ECHO ---"));
  testTrigEcho(D7, D6);

  Serial.println(F("--- TEST 6: Trigger/Echo D6=TRIG, D7=ECHO ---"));
  testTrigEcho(D6, D7);

  // ---- TEST 7: Raw GPIO state ----
  Serial.println(F("--- TEST 7: Raw GPIO state (5 seconds) ---"));
  Serial.println(F("Watching D6 and D7 for any activity..."));
  pinMode(D6, INPUT);
  pinMode(D7, INPUT);
  unsigned long start = millis();
  int lastD6 = -1, lastD7 = -1;
  int changes = 0;
  while (millis() - start < 5000) {
    int d6 = digitalRead(D6);
    int d7 = digitalRead(D7);
    if (d6 != lastD6 || d7 != lastD7) {
      Serial.printf("  t=%4lums  D6=%d  D7=%d\n", millis() - start, d6, d7);
      lastD6 = d6;
      lastD7 = d7;
      changes++;
      if (changes > 50) {
        Serial.println(F("  (lots of activity, stopping log)"));
        break;
      }
    }
    delayMicroseconds(100);
  }
  if (changes == 0) {
    Serial.println(F("  No changes detected -- sensor may not be powered"));
  }
  Serial.printf("  Total state changes: %d\n\n", changes);

  Serial.println(F("=== ALL TESTS COMPLETE ==="));
  Serial.println(F("If nothing worked:"));
  Serial.println(F("  1. Is the sensor getting power? Measure voltage on Red wire."));
  Serial.println(F("  2. Try powering from 5V (VU pin) instead of 3V3"));
  Serial.println(F("  3. Try connecting Yellow or White to D2 or D5 instead"));
  Serial.println(F("  4. Sensor may be defective"));
}

void testPWM(int pin, const char* name) {
  pinMode(pin, INPUT);
  // Wait for up to 3 seconds for a pulse
  for (int attempt = 0; attempt < 3; attempt++) {
    unsigned long duration = pulseIn(pin, HIGH, 1000000UL);  // 1 second timeout
    if (duration > 0) {
      float distCM = duration / 58.0;
      float distMM = duration / 5.8;
      Serial.printf("  PULSE! duration=%luus -> %.1f cm (%.0f mm)\n", duration, distCM, distMM);
      Serial.printf("  >>> PWM WORKING on %s <<<\n\n", name);
      return;
    }
  }
  Serial.printf("  No pulse on %s\n\n", name);
}

void testUART(SoftwareSerial &ss, const char* label) {
  const long bauds[] = {9600, 19200, 38400, 57600, 115200};
  for (int b = 0; b < 5; b++) {
    ss.begin(bauds[b]);
    delay(50);
    while (ss.available()) ss.read();  // flush

    unsigned long start = millis();
    int count = 0;
    uint8_t buf[32];
    while (millis() - start < 1500 && count < 32) {
      if (ss.available()) {
        buf[count++] = ss.read();
      }
    }
    ss.end();

    if (count > 0) {
      Serial.printf("  %s @ %ld baud: %d bytes -> ", label, bauds[b], count);
      for (int i = 0; i < count && i < 16; i++) {
        Serial.printf("%02X ", buf[i]);
      }
      Serial.println();

      // Try to decode DYP protocol (0xFF header)
      for (int i = 0; i < count - 3; i++) {
        if (buf[i] == 0xFF) {
          uint16_t dist = (buf[i+1] << 8) | buf[i+2];
          uint8_t cs = (buf[i] + buf[i+1] + buf[i+2]) & 0xFF;
          if (cs == buf[i+3]) {
            Serial.printf("  >>> DECODED: %u mm (%.1f cm) <<<\n", dist, dist / 10.0);
          }
        }
      }
      Serial.printf("  >>> UART WORKING: %s @ %ld baud <<<\n\n", label, bauds[b]);
      return;
    }
  }
  Serial.printf("  No UART data on %s\n\n", label);
}

void testTrigEcho(int trigPin, int echoPin) {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(trigPin, LOW);
  delayMicroseconds(5);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 500000UL);
  if (duration > 0) {
    float distCM = duration / 58.0;
    Serial.printf("  ECHO! duration=%luus -> %.1f cm\n", duration, distCM);
    Serial.printf("  >>> TRIGGER/ECHO WORKING <<<\n\n");
  } else {
    Serial.println(F("  No echo response\n"));
  }
}

void loop() {
  delay(10000);
  Serial.println(F("Restarting tests..."));
  setup();
}
