// ============================================================
// A0120AG Simple Test -- one test at a time, crash-safe
// Wiring: Red->3V3 (or VU/5V), Black->GND, Yellow->D6, White->D7
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(3000);  // long delay to avoid crash loop
  Serial.println();
  Serial.println(F("=== SIMPLE SENSOR TEST ==="));
  Serial.println(F("Wiring: Red->3V3, Black->GND, Yellow->D6, White->D7"));
  Serial.println();

  // TEST: Raw GPIO monitoring
  Serial.println(F("Watching D6 and D7 for 10 seconds..."));
  pinMode(D6, INPUT);
  pinMode(D7, INPUT);

  unsigned long start = millis();
  int changes = 0;
  int lastD6 = -1, lastD7 = -1;

  while (millis() - start < 10000) {
    int d6 = digitalRead(D6);
    int d7 = digitalRead(D7);
    if (d6 != lastD6 || d7 != lastD7) {
      Serial.printf("  t=%4lums  D6=%d  D7=%d\n", millis() - start, d6, d7);
      lastD6 = d6;
      lastD7 = d7;
      changes++;
      if (changes > 100) {
        Serial.println(F("  (lots of activity!)"));
        break;
      }
    }
    delayMicroseconds(50);
  }

  Serial.printf("\nTotal changes: %d\n", changes);

  if (changes == 0) {
    Serial.println(F("NO activity. Sensor may not be powered or wired."));
    Serial.println(F("  - Check Red wire has 3.3V or 5V"));
    Serial.println(F("  - Check Black wire to GND"));
    Serial.println(F("  - Try VU (5V) instead of 3V3"));
  } else {
    Serial.println(F("Activity detected! Sensor is alive."));
    Serial.println(F("Trying PWM decode..."));
  }

  Serial.println(F("\n--- PWM Test on D6 ---"));
  pinMode(D6, INPUT);
  for (int i = 0; i < 5; i++) {
    unsigned long d = pulseIn(D6, HIGH, 1000000UL);
    if (d > 0) {
      Serial.printf("  Pulse: %luus = %.1f cm\n", d, d / 58.0);
    } else {
      Serial.println(F("  No pulse"));
    }
  }

  Serial.println(F("\n--- PWM Test on D7 ---"));
  pinMode(D7, INPUT);
  for (int i = 0; i < 5; i++) {
    unsigned long d = pulseIn(D7, HIGH, 1000000UL);
    if (d > 0) {
      Serial.printf("  Pulse: %luus = %.1f cm\n", d, d / 58.0);
    } else {
      Serial.println(F("  No pulse"));
    }
  }

  Serial.println(F("\nDone. Will repeat in 15 seconds."));
}

void loop() {
  delay(15000);
  setup();
}
