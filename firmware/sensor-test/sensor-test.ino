//========================================================================
// BUP CPE River -- Sensor Test Sketch
// Board: NodeMCU 1.0 (ESP-12E Module) | 115200 baud
//
// Reads AJ-SR04M ultrasonic + YF-DN50 flow sensor every 3 seconds
// and prints results to Serial Monitor. No WiFi, no Firebase.
//
// Wiring:
//   AJ-SR04M  TRIG -> D1 (GPIO5)
//   AJ-SR04M  ECHO -> D6 (GPIO12)
//   YF-DN50   SIG  -> D2 (GPIO4) + 10k pullup to 3.3V
//========================================================================

#define TRIG_PIN   5    // GPIO5  (D1)
#define ECHO_PIN   12   // GPIO12 (D6)
#define FLOW_PIN   4    // GPIO4  (D2)

#define ULTRASONIC_SAMPLES   7
#define FLOW_WINDOW_MS       3000     // 3 second counting window
#define FLOW_CALIBRATION_HZ  0.2f    // YF-DN50: freq_Hz = 0.2 * flow_Lpm

volatile unsigned long pulseCount = 0;

ICACHE_RAM_ATTR void flowPulseISR() {
  pulseCount++;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(FLOW_PIN, INPUT_PULLUP);
  digitalWrite(TRIG_PIN, LOW);

  Serial.println(F("\n========================================"));
  Serial.println(F("  BUP CPE River -- Sensor Test"));
  Serial.println(F("  Ultrasonic: D1(TRIG) D6(ECHO)"));
  Serial.println(F("  Flow:       D2(SIG)"));
  Serial.println(F("========================================\n"));
}

void loop() {
  // ---- Ultrasonic ----
  float waterLevel = readWaterLevel();

  // ---- Flow ----
  float flowRate = readFlowRate();

  // ---- Print ----
  Serial.println(F("----------------------------------------"));
  if (waterLevel < 0) {
    Serial.println(F("[Ultrasonic] No valid reading (check wiring)"));
  } else {
    Serial.printf("[Ultrasonic] Distance: %.1f cm\n", waterLevel);
  }
  Serial.printf("[Flow]       Rate:     %.1f L/min  (pulses: %lu)\n", flowRate, pulseCount);
  Serial.println(F("----------------------------------------\n"));

  delay(3000);
}

//========================================================================
// readWaterLevel() -- 7 samples, median filter
//========================================================================
float readWaterLevel() {
  float samples[ULTRASONIC_SAMPLES];
  int validCount = 0;

  for (int i = 0; i < ULTRASONIC_SAMPLES; i++) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 30000);

    if (duration == 0) {
      samples[i] = -1.0f;
    } else {
      float dist = (duration * 0.0343f) / 2.0f;
      samples[i] = (dist >= 2.0f && dist <= 600.0f) ? dist : -1.0f;
    }

    // Print each raw sample for debugging
    Serial.printf("  sample[%d]: duration=%ld us -> %.1f cm\n",
                  i, duration, samples[i]);

    delay(60);
  }

  // Bubble sort
  for (int i = 0; i < ULTRASONIC_SAMPLES - 1; i++) {
    for (int j = 0; j < ULTRASONIC_SAMPLES - 1 - i; j++) {
      if (samples[j] > samples[j + 1] && samples[j + 1] >= 0) {
        float tmp = samples[j];
        samples[j] = samples[j + 1];
        samples[j + 1] = tmp;
      } else if (samples[j] < 0 && samples[j + 1] >= 0) {
        float tmp = samples[j];
        samples[j] = samples[j + 1];
        samples[j + 1] = tmp;
      }
    }
  }

  for (int i = 0; i < ULTRASONIC_SAMPLES; i++) {
    if (samples[i] >= 0) validCount++;
  }

  if (validCount == 0) return -1.0f;

  int startIdx = ULTRASONIC_SAMPLES - validCount;
  float sum = 0;
  int used = 0;
  int skip = (validCount >= 5) ? 2 : (validCount >= 3 ? 1 : 0);

  for (int i = startIdx + skip; i < ULTRASONIC_SAMPLES - skip; i++) {
    sum += samples[i];
    used++;
  }

  return (used > 0) ? (sum / used) : samples[startIdx + validCount / 2];
}

//========================================================================
// readFlowRate() -- count pulses over FLOW_WINDOW_MS
//========================================================================
float readFlowRate() {
  pulseCount = 0;
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowPulseISR, RISING);
  delay(FLOW_WINDOW_MS);
  detachInterrupt(digitalPinToInterrupt(FLOW_PIN));

  float windowSec = FLOW_WINDOW_MS / 1000.0f;
  float freqHz    = (float)pulseCount / windowSec;
  float flowLpm   = freqHz / FLOW_CALIBRATION_HZ;

  return flowLpm;
}
