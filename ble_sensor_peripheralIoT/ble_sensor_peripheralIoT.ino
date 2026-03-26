#include <ArduinoBLE.h>

#define DEVICE_NAME        "BLE-EnvSensor-IoT"
#define UPDATE_INTERVAL_MS 500
#define SERIAL_BAUD        115200

#define SERVICE_UUID   "19B10000-E8F2-537E-4F6C-D104768A1214"
#define TEMP_UUID      "19B10001-E8F2-537E-4F6C-D104768A1214"
#define HUMIDITY_UUID  "19B10002-E8F2-537E-4F6C-D104768A1214"
#define LIGHT_UUID     "19B10003-E8F2-537E-4F6C-D104768A1214"
#define CONTROL_UUID   "19B10004-E8F2-537E-4F6C-D104768A1214"

BLEService envService(SERVICE_UUID);
BLECharacteristic tempChar(TEMP_UUID, BLERead | BLENotify, 4);
BLECharacteristic humChar(HUMIDITY_UUID, BLERead | BLENotify, 4);
BLECharacteristic lightChar(LIGHT_UUID, BLERead | BLENotify, 2);
BLEByteCharacteristic controlChar(CONTROL_UUID, BLEWrite);

bool streaming = true;
uint32_t lastUpdateMs = 0;

float simTempBase = 22.5f;
float simHumBase = 55.0f;
uint16_t simLightBase = 400;

void floatToBytes(float value, uint8_t* buf) {
  memcpy(buf, &value, 4);
}

float randomWalk(float current, float minVal, float maxVal, float stepSize) {
  float delta = ((float)random(-1000, 1001) / 1000.0f) * stepSize;
  float next  = current + delta;
  if (next < minVal) next = minVal + (minVal - next);
  if (next > maxVal) next = maxVal - (next - maxVal);
  return next;
}

void sendSensorNotifications() {
  simTempBase  = randomWalk(simTempBase,  18.0f, 30.0f, 0.15f);
  simHumBase   = randomWalk(simHumBase,   35.0f, 75.0f, 0.40f);
  simLightBase = constrain(simLightBase + random(-20, 21), 0, 1000);

  uint8_t fbuf[4];
  floatToBytes(simTempBase, fbuf);
  tempChar.writeValue(fbuf, 4);

  floatToBytes(simHumBase, fbuf);
  humChar.writeValue(fbuf, 4);

  uint8_t lbuf[2] = {
    (uint8_t)(simLightBase & 0xFF),
    (uint8_t)((simLightBase >> 8) & 0xFF)
  };
  lightChar.writeValue(lbuf, 2);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  if (!BLE.begin()) {
    while (1) {}
  }

  BLE.setLocalName(DEVICE_NAME);
  BLE.setAdvertisedService(envService);

  envService.addCharacteristic(tempChar);
  envService.addCharacteristic(humChar);
  envService.addCharacteristic(lightChar);
  envService.addCharacteristic(controlChar);
  BLE.addService(envService);

  controlChar.writeValue(0x01);
  BLE.advertise();
}

void loop() {
  BLEDevice central = BLE.central();

  if (central) {
    digitalWrite(LED_BUILTIN, HIGH);

    while (central.connected()) {
      if (controlChar.written()) {
        uint8_t cmd = controlChar.value();
        if (cmd == 0x00) streaming = false;
        if (cmd == 0x01) streaming = true;
      }

      uint32_t now = millis();
      if (streaming && (now - lastUpdateMs >= UPDATE_INTERVAL_MS)) {
        lastUpdateMs = now;
        sendSensorNotifications();
      }
    }

    digitalWrite(LED_BUILTIN, LOW);
    BLE.advertise();
  }
}
