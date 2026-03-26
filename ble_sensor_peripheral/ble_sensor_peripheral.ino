/*
 * ============================================================
 *  BLE Environmental Sensor Peripheral
 *  Arduino Nano BLE 33 (or Nano BLE 33 Sense)
 * ============================================================
 *
 *  WHAT THIS SKETCH DOES
 *  ---------------------
 *  Implements a BLE peripheral exposing a custom Environmental
 *  Sensor GATT service with three characteristics:
 *
 *    1. Temperature   – float32, READ + NOTIFY  (°C, simulated)
 *    2. Humidity      – float32, READ + NOTIFY  (%RH, simulated)
 *    3. Light Level   – uint16, READ + NOTIFY   (lux, simulated)
 *    4. Control       – uint8,  WRITE            (0=stop, 1=start)
 *
 *  Data is updated every UPDATE_INTERVAL_MS milliseconds and
 *  pushed via BLE notifications to any connected central.
 *
 *  LED BEHAVIOUR
 *  -------------
 *    Built-in LED (LED_BUILTIN) — ON while a central is connected
 *    Red LED   (LEDR)           — Blinks when a notification is sent
 *    Green LED (LEDG)           — ON when streaming is enabled
 *
 *  BLE CONCEPTS ILLUSTRATED
 *  ------------------------
 *    • GAP roles       : Peripheral (advertiser) ↔ Central (scanner/initiator)
 *    • GATT roles      : Server (Arduino) ↔ Client (Python / phone)
 *    • Characteristics : READ, NOTIFY, WRITE properties
 *    • CCCD            : Client enables notifications by writing 0x0001
 *    • Advertising     : Device name + Service UUID broadcast in ADV_IND
 *
 *  CUSTOM UUIDs  (128-bit, randomly generated — unique to this exercise)
 *  ----------------------------------------------------------------------
 *    Service          : 19B10000-E8F2-537E-4F6C-D104768A1214
 *    Temperature char : 19B10001-E8F2-537E-4F6C-D104768A1214
 *    Humidity char    : 19B10002-E8F2-537E-4F6C-D104768A1214
 *    Light Level char : 19B10003-E8F2-537E-4F6C-D104768A1214
 *    Control char     : 19B10004-E8F2-537E-4F6C-D104768A1214
 *
 *  DEPENDENCIES
 *  ------------
 *    Board  : Arduino Mbed OS Nano Boards (Board Manager)
 *    Library: ArduinoBLE  (Library Manager)
 *
 *  SERIAL MONITOR
 *  --------------
 *    Baud rate: 115200
 * ============================================================
 */

#include <ArduinoBLE.h>

// ── Configuration ─────────────────────────────────────────────────────────────
#define DEVICE_NAME          "BLE-EnvSensor"
#define UPDATE_INTERVAL_MS   500          // Notification rate (ms)
#define SERIAL_BAUD          115200

// ── Custom UUIDs ──────────────────────────────────────────────────────────────
#define SERVICE_UUID         "19B10000-E8F2-537E-4F6C-D104768A1214"
#define TEMP_UUID            "19B10001-E8F2-537E-4F6C-D104768A1214"
#define HUMIDITY_UUID        "19B10002-E8F2-537E-4F6C-D104768A1214"
#define LIGHT_UUID           "19B10003-E8F2-537E-4F6C-D104768A1214"
#define CONTROL_UUID         "19B10004-E8F2-537E-4F6C-D104768A1214"

// ── GATT Service & Characteristics ────────────────────────────────────────────
BLEService envService(SERVICE_UUID);

//  Temperature: 4 bytes (float, little-endian), READ + NOTIFY
BLECharacteristic tempChar(TEMP_UUID,
  BLERead | BLENotify, 4);

//  Humidity: 4 bytes (float, little-endian), READ + NOTIFY
BLECharacteristic humChar(HUMIDITY_UUID,
  BLERead | BLENotify, 4);

//  Light level: 2 bytes (uint16, little-endian), READ + NOTIFY
BLECharacteristic lightChar(LIGHT_UUID,
  BLERead | BLENotify, 2);

//  Control: 1 byte (uint8), WRITE
//    0x00 → pause streaming
//    0x01 → resume streaming (default)
BLEByteCharacteristic controlChar(CONTROL_UUID,
  BLEWrite);

// ── Runtime state ─────────────────────────────────────────────────────────────
bool     streaming        = true;   // Whether to send notifications
uint32_t lastUpdateMs     = 0;      // Timestamp of last notification
uint32_t notifyCount      = 0;      // Total notifications sent

// Simulated sensor baseline values (will drift over time)
float    simTempBase      = 22.5f;
float    simHumBase       = 55.0f;
uint16_t simLightBase     = 400;

// ── Helper: float → 4-byte little-endian buffer ───────────────────────────────
void floatToBytes(float value, uint8_t* buf) {
  memcpy(buf, &value, 4);
}

// ── Helper: simulate realistic sensor drift ───────────────────────────────────
/*
 * Uses a simple random walk so values change gradually,
 * keeping them within realistic ranges. This mimics real
 * sensor behaviour without needing physical hardware.
 */
float randomWalk(float current, float minVal, float maxVal,
                 float stepSize) {
  float delta = ((float)random(-1000, 1001) / 1000.0f) * stepSize;
  float next  = current + delta;
  // Reflect at boundaries to stay in range
  if (next < minVal) next = minVal + (minVal - next);
  if (next > maxVal) next = maxVal - (next - maxVal);
  return next;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(SERIAL_BAUD);
  // Wait up to 2 s for Serial (USB CDC); continue without it
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 2000));

  Serial.println("==============================================");
  Serial.println(" BLE Environmental Sensor — Arduino Nano BLE");
  Serial.println("==============================================");

  // Configure built-in LEDs (active LOW on Nano BLE 33)
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LEDR, OUTPUT);
  pinMode(LEDG, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(LEDR, HIGH);   // OFF
  digitalWrite(LEDG, HIGH);   // OFF

  // ── Initialise BLE ──────────────────────────────────────────
  if (!BLE.begin()) {
    Serial.println("[ERROR] BLE initialisation failed!");
    // Halt with fast red blinks to signal error
    while (true) {
      digitalWrite(LEDR, LOW);  delay(100);
      digitalWrite(LEDR, HIGH); delay(100);
    }
  }

  // ── Configure GAP ───────────────────────────────────────────
  // Set the local name broadcast in advertising packets
  BLE.setLocalName(DEVICE_NAME);

  // Advertise the primary service UUID so centrals can filter by it
  BLE.setAdvertisedService(envService);

  // ── Build GATT attribute table ──────────────────────────────
  // Add characteristics to service
  envService.addCharacteristic(tempChar);
  envService.addCharacteristic(humChar);
  envService.addCharacteristic(lightChar);
  envService.addCharacteristic(controlChar);

  // Add service to the BLE device
  BLE.addService(envService);

  // ── Set initial characteristic values ───────────────────────
  uint8_t buf[4];
  floatToBytes(simTempBase, buf);
  tempChar.writeValue(buf, 4);

  floatToBytes(simHumBase, buf);
  humChar.writeValue(buf, 4);

  uint8_t lightBuf[2];
  lightBuf[0] = simLightBase & 0xFF;
  lightBuf[1] = (simLightBase >> 8) & 0xFF;
  lightChar.writeValue(lightBuf, 2);

  controlChar.writeValue(0x01);   // Default: streaming ON

  // ── Start advertising ────────────────────────────────────────
  BLE.advertise();

  Serial.println("[BLE]  Advertising started");
  Serial.print  ("[BLE]  Device name  : "); Serial.println(DEVICE_NAME);
  Serial.print  ("[BLE]  Service UUID : "); Serial.println(SERVICE_UUID);
  Serial.println("[BLE]  Waiting for central to connect...");
  Serial.println("----------------------------------------------");
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void loop() {
  // Poll for BLE events (connection, write, etc.)
  BLEDevice central = BLE.central();

  if (central) {
    // ── Central connected ────────────────────────────────────
    digitalWrite(LED_BUILTIN, HIGH);   // LED ON = connected
    digitalWrite(LEDG, streaming ? LOW : HIGH);

    Serial.print("[GAP]  Central connected   : ");
    Serial.println(central.address());

    // ── Stay connected while the link is active ───────────────
    while (central.connected()) {

      // Handle incoming WRITE on Control characteristic
      if (controlChar.written()) {
        uint8_t cmd = controlChar.value();
        if (cmd == 0x00) {
          streaming = false;
          digitalWrite(LEDG, HIGH);    // Green OFF
          Serial.println("[CTRL] Streaming PAUSED (0x00 received)");
        } else if (cmd == 0x01) {
          streaming = true;
          digitalWrite(LEDG, LOW);     // Green ON
          Serial.println("[CTRL] Streaming RESUMED (0x01 received)");
        } else {
          Serial.print("[CTRL] Unknown command: 0x");
          Serial.println(cmd, HEX);
        }
      }

      // Send notifications at the configured interval
      uint32_t now = millis();
      if (streaming && (now - lastUpdateMs >= UPDATE_INTERVAL_MS)) {
        lastUpdateMs = now;
        sendSensorNotifications();
      }
    }

    // ── Central disconnected ─────────────────────────────────
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(LEDG, HIGH);
    Serial.print("[GAP]  Central disconnected: ");
    Serial.println(central.address());
    Serial.println("[BLE]  Restarting advertising...");
    BLE.advertise();
  }
}

// ── Send all three sensor notifications ───────────────────────────────────────
void sendSensorNotifications() {
  // Update simulated sensor values
  simTempBase  = randomWalk(simTempBase,  15.0f,  35.0f,  0.3f);
  simHumBase   = randomWalk(simHumBase,   20.0f,  90.0f,  0.8f);
  simLightBase = (uint16_t)randomWalk((float)simLightBase,
                                      50.0f, 1200.0f, 15.0f);

  // ── Temperature ──────────────────────────────────────────────
  uint8_t tBuf[4];
  floatToBytes(simTempBase, tBuf);
  tempChar.writeValue(tBuf, 4);

  // ── Humidity ─────────────────────────────────────────────────
  uint8_t hBuf[4];
  floatToBytes(simHumBase, hBuf);
  humChar.writeValue(hBuf, 4);

  // ── Light Level ───────────────────────────────────────────────
  uint8_t lBuf[2];
  lBuf[0] = simLightBase & 0xFF;
  lBuf[1] = (simLightBase >> 8) & 0xFF;
  lightChar.writeValue(lBuf, 2);

  notifyCount++;

  // Print to Serial in CSV format for easy monitoring
  Serial.print("[DATA] #");
  Serial.print(notifyCount);
  Serial.print("  Temp: ");
  Serial.print(simTempBase, 2);
  Serial.print(" °C  |  Hum: ");
  Serial.print(simHumBase, 2);
  Serial.print(" %RH  |  Light: ");
  Serial.print(simLightBase);
  Serial.println(" lux");

  // Brief red blink to signal notification sent
  digitalWrite(LEDR, LOW);
  delay(20);
  digitalWrite(LEDR, HIGH);
}
