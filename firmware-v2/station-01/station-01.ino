//========================================================================
// CPE River Monitoring System
// Station Firmware v2 -- ESP8266 (NodeMCU 1.0) | station-01 | Upstream
//
// Sensors:
//   A0120AG RS485 Ultrasonic (via C25B TTL-RS485) -> Water level (cm)
//   YF-DN50 Hall-effect Flow Sensor               -> Flow rate (L/min)
//
// Cycle: Wake -> Read sensors -> Connect WiFi -> Push to Firebase -> Sleep 5min
//
// Libraries (install via Arduino Library Manager):
//   - Firebase ESP8266 Client  by mobizt
//   - SoftwareSerial            (built-in)
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
#include <SoftwareSerial.h>

//========================================================================
// !! STATION CONFIGURATION -- CHANGE THESE BEFORE FLASHING !!
//========================================================================

#define STATION_ID  "station-01"  // Deployment: Upstream

// TODO: REPLACE with your WiFi credentials
const char* WIFI_SSID     = "TP-Link_SATUITO";
const char* WIFI_PASSWORD = "paopaopao122002";

// Firebase project credentials
#define FIREBASE_API_KEY  "AIzaSyC0rNq9LGrIvzg20-7kAgfWnf1TqDt8eOM"
#define FIREBASE_DB_URL   "river-d1bc6-default-rtdb.asia-southeast1.firebasedatabase.app"

//========================================================================
// PIN DEFINITIONS
//========================================================================

// A0120AG RS485 via C25B TTL-RS485 converter
#define RS485_RX_PIN  12   // GPIO12 (D6) <- C25B TXD
#define RS485_TX_PIN  13   // GPIO13 (D7) -> C25B RXD

// C25B direction control (DE + RE tied together)
#define RS485_DIR_PIN  14  // GPIO14 (D5) -- HIGH=transmit, LOW=receive

// YF-DN50 flow sensor
#define FLOW_PIN       4   // GPIO4  (D2) -- YF-DN50 signal (+ 10k pullup to 3.3V)

//========================================================================
// SENSOR CONSTANTS
//========================================================================

// A0120AG Modbus RTU settings
#define MODBUS_ADDR        0x01   // Default slave address (check your sensor)
#define MODBUS_BAUD        9600   // Default baud rate
#define MODBUS_TIMEOUT_MS  500    // Response timeout
#define ULTRASONIC_SAMPLES 5      // Number of readings to average

// YF-DN50 flow sensor
#define FLOW_WINDOW_MS         1000     // 1s for testing, 5000 for deployment
#define FLOW_CALIBRATION_HZ    0.2f     // freq_Hz = 0.2 * flow_Lpm

//========================================================================
// TIMING
//========================================================================

#define SLEEP_DURATION_US  10000000ULL   // 10s testing -- 300000000ULL for deployment
#define WIFI_TIMEOUT_MS    15000

//========================================================================
// OFFLINE BUFFER
//========================================================================

#define BUFFER_FILE  "/buffer.csv"
#define MAX_BUFFERED 100

//========================================================================
// GLOBALS
//========================================================================

FirebaseData   fbdo;
FirebaseAuth   auth;
FirebaseConfig fbConfig;

SoftwareSerial rs485Serial(RS485_RX_PIN, RS485_TX_PIN);

volatile unsigned long pulseCount = 0;

//========================================================================
// INTERRUPT SERVICE ROUTINE
//========================================================================

ICACHE_RAM_ATTR void flowPulseISR() {
  pulseCount++;
}

//========================================================================
// SETUP
//========================================================================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n========================================"));
  Serial.println(F("  CPE River Monitor Wake Cycle"));
  Serial.print(F("  Station: "));
  Serial.println(F(STATION_ID));
  Serial.println(F("========================================"));

  // Initialize RS485 serial and direction pin
  rs485Serial.begin(MODBUS_BAUD);
  pinMode(RS485_DIR_PIN, OUTPUT);
  digitalWrite(RS485_DIR_PIN, LOW);  // start in receive mode

  // Pin setup
  pinMode(FLOW_PIN, INPUT_PULLUP);

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
    flushBuffer();
    bool sent = sendToFirebase(waterLevel, flowRate, rssi);
    if (!sent) {
      Serial.println(F("[Firebase] Send failed -- buffering reading"));
      bufferReading(waterLevel, flowRate, rssi);
    }
  } else {
    Serial.println(F("[WiFi] Failed -- buffering reading offline"));
    bufferReading(waterLevel, flowRate, rssi);
  }

  // For testing: loop instead of deep sleep
  // goToSleep();  // uncomment for deployment
}

//========================================================================
// LOOP -- repeat readings every 10 seconds (testing mode)
// For deployment: uncomment goToSleep() above and empty this loop
//========================================================================

void loop() {
  delay(10000);

  float waterLevel = readWaterLevel();
  float flowRate   = readFlowRate();

  Serial.printf("[Sensor] Water Level : %.1f cm\n", waterLevel);
  Serial.printf("[Sensor] Flow Rate   : %.1f L/min\n", flowRate);

  int rssi = (WiFi.status() == WL_CONNECTED) ? (int)WiFi.RSSI() : 0;

  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  if (WiFi.status() == WL_CONNECTED) {
    bool sent = sendToFirebase(waterLevel, flowRate, rssi);
    if (!sent) {
      Serial.println(F("[Firebase] Send failed"));
    }
  }
}

//========================================================================
// readWaterLevel() -- A0120AG via Modbus RTU over RS485
// Sends read holding register command, parses distance in mm,
// converts to cm. Takes multiple samples and averages.
// Returns -1.0 if sensor is unresponsive.
//========================================================================

float readWaterLevel() {
  float samples[ULTRASONIC_SAMPLES];
  int validCount = 0;

  for (int i = 0; i < ULTRASONIC_SAMPLES; i++) {
    float dist = readA0120AG_cm();
    samples[validCount] = dist;
    if (dist >= 0) validCount++;
    delay(200);  // give sensor time between readings
    yield();
  }

  if (validCount == 0) {
    Serial.println(F("[A0120AG] All samples invalid!"));
    return -1.0f;
  }

  // Sort valid samples and take the middle ones
  for (int i = 0; i < validCount - 1; i++) {
    for (int j = 0; j < validCount - 1 - i; j++) {
      if (samples[j] > samples[j + 1]) {
        float tmp = samples[j];
        samples[j] = samples[j + 1];
        samples[j + 1] = tmp;
      }
    }
  }

  // Average middle values (trim 1 from each end if enough samples)
  int skip = (validCount >= 5) ? 1 : 0;
  float sum = 0;
  int used = 0;
  for (int i = skip; i < validCount - skip; i++) {
    sum += samples[i];
    used++;
  }

  return sum / used;
}

//========================================================================
// readA0120AG_cm()
// Sends Modbus RTU request to A0120AG sensor and reads distance.
// Register 0x0001 holds distance in mm.
// Returns distance in cm, or -1.0 on error.
//========================================================================

float readA0120AG_cm() {
  // Modbus RTU: Read Holding Register (0x03)
  // [Addr] [Func] [RegHi] [RegLo] [CountHi] [CountLo] [CRC Lo] [CRC Hi]
  uint8_t request[] = {
    MODBUS_ADDR,  // Slave address
    0x03,         // Function code: Read Holding Registers
    0x00, 0x01,   // Starting register: 0x0001 (distance)
    0x00, 0x01    // Number of registers: 1
  };

  // Calculate CRC16 and append
  uint16_t crc = modbusCRC16(request, 6);
  uint8_t frame[8];
  memcpy(frame, request, 6);
  frame[6] = crc & 0xFF;        // CRC low byte
  frame[7] = (crc >> 8) & 0xFF; // CRC high byte

  // Clear any stale data in RX buffer
  while (rs485Serial.available()) rs485Serial.read();

  // Switch to transmit mode, send request, then back to receive
  digitalWrite(RS485_DIR_PIN, HIGH);
  delayMicroseconds(500);          // let DE settle
  rs485Serial.write(frame, 8);
  rs485Serial.flush();
  delay(10);                       // critical: wait after flush before switching
  digitalWrite(RS485_DIR_PIN, LOW);
  delayMicroseconds(500);          // let RE settle

  // Wait for response (7 bytes expected: addr + func + byteCount + 2 data + 2 CRC)
  unsigned long start = millis();
  while (rs485Serial.available() < 7) {
    if (millis() - start > MODBUS_TIMEOUT_MS) {
      Serial.println(F("[A0120AG] Modbus timeout"));
      return -1.0f;
    }
    yield();
  }

  // Read response
  uint8_t response[7];
  for (int i = 0; i < 7; i++) {
    response[i] = rs485Serial.read();
  }

  // Validate response
  if (response[0] != MODBUS_ADDR || response[1] != 0x03 || response[2] != 0x02) {
    Serial.printf("[A0120AG] Invalid response: %02X %02X %02X\n",
                  response[0], response[1], response[2]);
    return -1.0f;
  }

  // Verify CRC
  uint16_t respCRC = modbusCRC16(response, 5);
  uint16_t recvCRC = response[5] | (response[6] << 8);
  if (respCRC != recvCRC) {
    Serial.println(F("[A0120AG] CRC mismatch"));
    return -1.0f;
  }

  // Distance in mm (big-endian)
  uint16_t distMM = (response[3] << 8) | response[4];
  float distCM = distMM / 10.0f;

  // Reject unreasonable values (blind zone ~12cm, max ~2000cm)
  if (distCM < 12.0f || distCM > 2000.0f) {
    Serial.printf("[A0120AG] Out of range: %.1f cm\n", distCM);
    return -1.0f;
  }

  Serial.printf("[A0120AG] Distance: %u mm (%.1f cm)\n", distMM, distCM);
  return distCM;
}

//========================================================================
// modbusCRC16()
// Standard Modbus CRC-16 calculation.
//========================================================================

uint16_t modbusCRC16(const uint8_t* data, uint16_t length) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

//========================================================================
// readFlowRate()
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
//========================================================================

void initFirebase() {
  fbConfig.database_url  = FIREBASE_DB_URL;
  fbConfig.signer.test_mode = true;

  Firebase.begin(&fbConfig, &auth);
  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(1024);
  Serial.println(F("[Firebase] Initialized (open rules, no auth)"));
}

//========================================================================
// sendToFirebase()
//========================================================================

bool sendToFirebase(float waterLevel, float flowRate, int rssi) {
  FirebaseJson json;
  json.set("waterLevel_cm", waterLevel);
  json.set("flowRate_lpm",  flowRate);
  json.set("rssi_dbm",      rssi);
  json.set("timestamp/.sv", "timestamp");

  String path = String("/devices/") + STATION_ID + "/readings";

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
//========================================================================

void bufferReading(float waterLevel, float flowRate, int rssi) {
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
    File fin = LittleFS.open(BUFFER_FILE, "r");
    File ftmp = LittleFS.open("/buffer_tmp.csv", "w");
    if (fin && ftmp) {
      fin.readStringUntil('\n');
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
//========================================================================

void flushBuffer() {
  if (!LittleFS.exists(BUFFER_FILE)) return;

  File f = LittleFS.open(BUFFER_FILE, "r");
  if (!f) return;

  FirebaseJson updateJson;
  int count = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    float wl = 0, fr = 0;
    int   rs = 0;
    sscanf(line.c_str(), "%f,%f,%d", &wl, &fr, &rs);

    String key = String(millis()) + String(count);

    updateJson.set("devices/" + String(STATION_ID) + "/readings/" + key + "/waterLevel_cm", wl);
    updateJson.set("devices/" + String(STATION_ID) + "/readings/" + key + "/flowRate_lpm",  fr);
    updateJson.set("devices/" + String(STATION_ID) + "/readings/" + key + "/rssi_dbm",      rs);
    updateJson.set("devices/" + String(STATION_ID) + "/readings/" + key + "/timestamp/.sv", "timestamp");

    count++;
    delay(1);
  }
  f.close();

  if (count == 0) {
    LittleFS.remove(BUFFER_FILE);
    return;
  }

  Serial.printf("[Buffer] Flushing %d offline reading(s)...\n", count);

  if (Firebase.updateNode(fbdo, "/", updateJson)) {
    Serial.printf("[Buffer] Flushed OK (%d readings)\n", count);
    LittleFS.remove(BUFFER_FILE);
  } else {
    Serial.printf("[Buffer] Flush failed: %s\n", fbdo.errorReason().c_str());
  }
}

//========================================================================
// goToSleep()
//========================================================================

void goToSleep() {
  Serial.printf("[Sleep] Entering deep sleep for %llu seconds...\n",
                SLEEP_DURATION_US / 1000000ULL);
  Serial.flush();
  WiFi.mode(WIFI_OFF);
  delay(100);
  ESP.deepSleep(SLEEP_DURATION_US);
}
