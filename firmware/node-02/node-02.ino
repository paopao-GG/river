//========================================================================
// BUP CPE River Monitoring System
// Node Firmware -- ESP8266 (NodeMCU 1.0) | node-02 | Midstream Station
//
// Sensors:
//   AJ-SR04M Waterproof Ultrasonic  -> Water level (cm)
//   YF-DN50 Hall-effect Flow Sensor -> Flow rate (L/min)
//
// Cycle: Wake -> Read sensors -> Connect WiFi -> Push to Firebase -> Sleep 5min
//
// Libraries (install via Arduino Library Manager):
//   - Firebase ESP8266 Client  by mobizt
//
// Board settings:
//   Board:      NodeMCU 1.0 (ESP-12E Module)
//   CPU Freq:   80 MHz
//   Flash Size: 4MB (FS:2MB OTA:~1019KB)
//   Upload:     115200
//========================================================================

#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <LittleFS.h>

//========================================================================
// !! NODE CONFIGURATION -- CHANGE THESE BEFORE FLASHING !!
//========================================================================

// Change this for each of the 3 devices: "node-01", "node-02", "node-03"
#define NODE_ID  "station-02"  // Deployment: Midstream Station

// TODO: REPLACE with your WiFi credentials
const char* WIFI_SSID     = "Room 2";
const char* WIFI_PASSWORD = "RedRoom2";

// Firebase project credentials
#define FIREBASE_API_KEY  "AIzaSyC0rNq9LGrIvzg20-7kAgfWnf1TqDt8eOM"
#define FIREBASE_DB_URL   "river-d1bc6-default-rtdb.asia-southeast1.firebasedatabase.app"
// (no "https://" prefix)

//========================================================================
// PIN DEFINITIONS
//========================================================================

#define TRIG_PIN   5    // GPIO5  (D1) -- AJ-SR04M trigger
#define ECHO_PIN   12   // GPIO12 (D6) -- AJ-SR04M echo
#define FLOW_PIN   4    // GPIO4  (D2) -- YF-DN50 signal (+ 10k pullup to 3.3V)

//========================================================================
// SENSOR CONSTANTS
//========================================================================

#define ULTRASONIC_SAMPLES     7        // must be odd for clean median
#define FLOW_WINDOW_MS         5000     // pulse counting window (5 seconds)
#define FLOW_CALIBRATION_HZ    0.2f     // freq_Hz = 0.2 * flow_Lpm

//========================================================================
// TIMING
//========================================================================

// Deep sleep duration: 5 minutes
// For bench testing, change to 10000000ULL (10 seconds)
#define SLEEP_DURATION_US  300000000ULL

#define WIFI_TIMEOUT_MS    15000   // give up on WiFi after 15 seconds

//========================================================================
// OFFLINE BUFFER
//========================================================================

#define BUFFER_FILE  "/buffer.csv"
#define MAX_BUFFERED 100   // max lines stored before dropping oldest

//========================================================================
// GLOBALS
//========================================================================

FirebaseData   fbdo;
FirebaseAuth   auth;
FirebaseConfig fbConfig;

volatile unsigned long pulseCount = 0;

//========================================================================
// INTERRUPT SERVICE ROUTINE
// Must be in RAM (ICACHE_RAM_ATTR) on ESP8266
//========================================================================

ICACHE_RAM_ATTR void flowPulseISR() {
  pulseCount++;
}

//========================================================================
// SETUP -- runs once per wake cycle (deep sleep resets to here)
//========================================================================

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n========================================"));
  Serial.println(F("  River Monitor Wake Cycle"));
  Serial.print(F("  Node: "));
  Serial.println(F(NODE_ID));
  Serial.println(F("========================================"));

  // Pin setup
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(FLOW_PIN, INPUT_PULLUP);
  digitalWrite(TRIG_PIN, LOW);

  // Mount LittleFS for offline buffer
  if (!LittleFS.begin()) {
    Serial.println(F("[LittleFS] Mount failed -- offline buffer unavailable"));
  }

  // ---- Read sensors BEFORE WiFi (saves power) ----
  float waterLevel = readWaterLevel();
  float flowRate   = readFlowRate();

  Serial.printf("[Sensor] Water Level : %.1f cm\n", waterLevel);
  Serial.printf("[Sensor] Flow Rate   : %.1f L/min\n", flowRate);

  // ---- Connect to WiFi ----
  bool wifiOk = connectToWiFi();
  int rssi = wifiOk ? (int)WiFi.RSSI() : 0;
  if (wifiOk) {
    Serial.printf("[WiFi] RSSI: %d dBm\n", rssi);
  }

  // ---- Send or buffer ----
  if (wifiOk) {
    initFirebase();
    flushBuffer();   // send any previously offline readings first
    bool sent = sendToFirebase(waterLevel, flowRate, rssi);
    if (!sent) {
      Serial.println(F("[Firebase] Send failed -- buffering reading"));
      bufferReading(waterLevel, flowRate, rssi);
    }
  } else {
    Serial.println(F("[WiFi] Failed -- buffering reading offline"));
    bufferReading(waterLevel, flowRate, rssi);
  }

  goToSleep();
}

//========================================================================
// LOOP -- intentionally empty (deep sleep resets to setup)
//========================================================================

void loop() {}

//========================================================================
// readWaterLevel()
// 7 trigger/echo samples with median filter.
// Returns distance in cm from sensor face to water surface.
// Returns -1.0 if all samples are invalid.
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

    // 30ms timeout = max ~516cm, well beyond 600cm sensor limit
    long duration = pulseIn(ECHO_PIN, HIGH, 30000);

    if (duration == 0) {
      samples[i] = -1.0f;  // timeout / no echo
    } else {
      float dist = (duration * 0.0343f) / 2.0f;
      // Reject dead zone (<20cm) and out-of-range (>600cm)
      samples[i] = (dist >= 20.0f && dist <= 600.0f) ? dist : -1.0f;
    }

    delay(60);  // AJ-SR04M needs ~60ms between pings
  }

  // Bubble sort (7 elements -- tiny, fine here)
  for (int i = 0; i < ULTRASONIC_SAMPLES - 1; i++) {
    for (int j = 0; j < ULTRASONIC_SAMPLES - 1 - i; j++) {
      // Push -1 (invalid) to the front
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

  // Count valid samples (non -1)
  for (int i = 0; i < ULTRASONIC_SAMPLES; i++) {
    if (samples[i] >= 0) validCount++;
  }

  if (validCount == 0) {
    Serial.println(F("[Ultrasonic] All samples invalid!"));
    return -1.0f;
  }

  // Average the middle 3 valid samples (discard 2 extremes on each side)
  // Use median if fewer than 5 valid samples
  int startIdx = ULTRASONIC_SAMPLES - validCount;  // first valid index
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
// readFlowRate()
// Counts YF-DN50 pulses over FLOW_WINDOW_MS using hardware interrupt.
// Returns flow in L/min.
//========================================================================

float readFlowRate() {
  pulseCount = 0;
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowPulseISR, RISING);
  delay(FLOW_WINDOW_MS);
  detachInterrupt(digitalPinToInterrupt(FLOW_PIN));

  float windowSec = FLOW_WINDOW_MS / 1000.0f;
  float freqHz    = (float)pulseCount / windowSec;
  float flowLpm   = freqHz / FLOW_CALIBRATION_HZ;

  Serial.printf("[Flow] Pulses=%lu  Freq=%.2fHz  Flow=%.1fL/min\n",
                pulseCount, freqHz, flowLpm);
  return flowLpm;
}

//========================================================================
// connectToWiFi()
// Adapted from user's NodeMCU RFID project pattern, with 15s timeout.
// Returns true on success, false on timeout.
//========================================================================

bool connectToWiFi() {
  WiFi.mode(WIFI_OFF);
  delay(500);
  WiFi.mode(WIFI_STA);
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      Serial.println(F(" TIMEOUT"));
      WiFi.mode(WIFI_OFF);
      return false;
    }
    delay(500);
    Serial.print('.');
  }

  Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

//========================================================================
// initFirebase()
// No auth -- relies on open RTDB rules (".read": true, ".write": true).
// Avoids anonymous auth token exchange which can timeout on slow networks.
//========================================================================

void initFirebase() {
  fbConfig.database_url  = FIREBASE_DB_URL;
  fbConfig.signer.test_mode = true;  // skip token auth, use open rules

  Firebase.begin(&fbConfig, &auth);
  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(1024);
  Serial.println(F("[Firebase] Initialized (open rules, no auth)"));
}

//========================================================================
// sendToFirebase()
// Pushes one reading to /devices/NODE_ID/readings/ using a push key
// so each entry gets a unique auto-generated ID.
// Includes Firebase server timestamp for authoritative time.
//========================================================================

bool sendToFirebase(float waterLevel, float flowRate, int rssi) {
  FirebaseJson json;
  json.set("waterLevel_cm", waterLevel);
  json.set("flowRate_lpm",  flowRate);
  json.set("rssi_dbm",      rssi);
  // Server-side timestamp -- no NTP needed on the device
  json.set("timestamp/.sv", "timestamp");

  String path = String("/devices/") + NODE_ID + "/readings";

  // pushJSON creates a unique push-ID key (chronologically sortable)
  if (Firebase.pushJSON(fbdo, path, json)) {
    Serial.printf("[Firebase] Sent OK. Key: %s\n", fbdo.pushName().c_str());
    return true;
  } else {
    Serial.printf("[Firebase] Error: %s\n", fbdo.errorReason().c_str());
    return false;
  }
}

//========================================================================
// bufferReading()
// Appends one CSV line to /buffer.csv on LittleFS.
// Format: waterLevel,flowRate,rssi
// Limits file to MAX_BUFFERED lines (drops oldest if full).
//========================================================================

void bufferReading(float waterLevel, float flowRate, int rssi) {
  // Count existing lines
  int lineCount = 0;
  File f = LittleFS.open(BUFFER_FILE, "r");
  if (f) {
    while (f.available()) {
      if (f.read() == '\n') lineCount++;
    }
    f.close();
  }

  if (lineCount >= MAX_BUFFERED) {
    Serial.printf("[Buffer] Full (%d lines) -- dropping oldest reading\n", lineCount);
    // Simple approach: read all, skip first line, rewrite
    File fin = LittleFS.open(BUFFER_FILE, "r");
    File ftmp = LittleFS.open("/buffer_tmp.csv", "w");
    if (fin && ftmp) {
      fin.readStringUntil('\n');  // skip first (oldest) line
      while (fin.available()) {
        ftmp.write(fin.read());
      }
    }
    if (fin)  fin.close();
    if (ftmp) ftmp.close();
    LittleFS.remove(BUFFER_FILE);
    LittleFS.rename("/buffer_tmp.csv", BUFFER_FILE);
  }

  File fw = LittleFS.open(BUFFER_FILE, "a");
  if (!fw) {
    Serial.println(F("[Buffer] Could not open file for write"));
    return;
  }

  fw.printf("%.1f,%.1f,%d\n", waterLevel, flowRate, rssi);
  fw.close();
  Serial.printf("[Buffer] Reading saved. Total buffered: %d\n", lineCount + 1);
}

//========================================================================
// flushBuffer()
// Reads all CSV lines from LittleFS and sends them to Firebase
// as a single multi-path update. Deletes the file if all succeed.
//========================================================================

void flushBuffer() {
  if (!LittleFS.exists(BUFFER_FILE)) return;

  File f = LittleFS.open(BUFFER_FILE, "r");
  if (!f) return;

  // Read all lines into a FirebaseJson multi-path update object
  FirebaseJson updateJson;
  int count = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    // Parse CSV: waterLevel,flowRate,rssi
    float wl = 0, fr = 0;
    int   rs = 0;
    sscanf(line.c_str(), "%f,%f,%d", &wl, &fr, &rs);

    // Use a sequential key so readings are ordered (millis-based, unique enough for flush)
    String key = String(millis()) + String(count);

    updateJson.set("devices/" + String(NODE_ID) + "/readings/" + key + "/waterLevel_cm", wl);
    updateJson.set("devices/" + String(NODE_ID) + "/readings/" + key + "/flowRate_lpm",  fr);
    updateJson.set("devices/" + String(NODE_ID) + "/readings/" + key + "/rssi_dbm",      rs);
    updateJson.set("devices/" + String(NODE_ID) + "/readings/" + key + "/timestamp/.sv", "timestamp");

    count++;
    delay(1);  // ensure unique millis() keys
  }
  f.close();

  if (count == 0) {
    LittleFS.remove(BUFFER_FILE);
    return;
  }

  Serial.printf("[Buffer] Flushing %d offline reading(s)...\n", count);

  // Multi-path update -- single Firebase write for all buffered readings
  if (Firebase.updateNode(fbdo, "/", updateJson)) {
    Serial.printf("[Buffer] Flushed OK (%d readings)\n", count);
    LittleFS.remove(BUFFER_FILE);
  } else {
    Serial.printf("[Buffer] Flush failed: %s\n", fbdo.errorReason().c_str());
    // Leave file intact -- will retry next wake cycle
  }
}

//========================================================================
// goToSleep()
// Shuts down radio and enters deep sleep for SLEEP_DURATION_US.
// On wake, execution restarts from setup().
// REQUIRES: GPIO16 (D0) wired to RST pin on the NodeMCU board.
//========================================================================

void goToSleep() {
  Serial.printf("[Sleep] Entering deep sleep for %llu seconds...\n",
                SLEEP_DURATION_US / 1000000ULL);
  Serial.flush();
  WiFi.mode(WIFI_OFF);
  delay(100);
  ESP.deepSleep(SLEEP_DURATION_US);
}
