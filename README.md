# BLEBeacon_Exercise1

## From Example Eddystone BLE URL beacon advertising




An end-to-end Bluetooth Low Energy system where an **Arduino Nano BLE 33** acts as a GATT **peripheral** (server) broadcasting simulated environmental sensor data, and a **Python script** on your PC acts as a GATT **central** (client) that subscribes to notifications, plots three sensor channels in real time, and logs all data to a CSV file.

```
┌─────────────────────────────┐        BLE (2.4 GHz)        ┌──────────────────────────────┐
│   Arduino Nano BLE 33       │ ──── ADV_IND ──────────────► │   PC                         │
│                             │                              │                              │
│  GAP role: Peripheral       │ ◄─── CONNECT_REQ ─────────── │  GAP role: Central           │
│  GATT role: Server          │                              │  GATT role: Client           │
│                             │ ──── NOTIFY (temp) ────────► │                              │
│  Custom Service             │ ──── NOTIFY (hum)  ────────► │  Python (bleak)              │
│  ├─ Temperature (NOTIFY)    │ ──── NOTIFY (light)────────► │  ├─ Real-time rolling plot   │
│  ├─ Humidity    (NOTIFY)    │                              │  └─ CSV logger               │
│  ├─ Light Level (NOTIFY)    │ ◄─── WRITE (Control) ─────── │                              │
│  └─ Control     (WRITE)     │                              │                              │
└─────────────────────────────┘                              └──────────────────────────────┘
```
**Clone the repo to follow the exercise**

## Part 1 — Arduino setup

### Step 1.1 — Install the board package

1. Open **Arduino IDE 2.x** and go to **Tools → Board → Boards Manager**.
2. Search for `Arduino Mbed OS Nano Boards`.
3. Install the package (version 4.x or later).
4. Select **Tools → Board → Arduino Mbed OS Nano Boards → Arduino Nano BLE**.

### Step 1.2 — Install the ArduinoBLE library

1. Go to **Tools → Manage Libraries**.
2. Search for `ArduinoBLE`.
3. Install the library by Arduino (version 1.3.x or later).

### Step 1.3 — Open the sketch

1. Open `ble_sensor_peripheral/ble_sensor_peripheral.ino` in Arduino IDE (if using Arduino Nano33 BLE).
2. Read through the comments at the top — they explain each BLE concept as it appears in the code.
   > **Tip:** use `ble_sensor_peripheralIoT/ble_sensor_peripheralIoT.ino` if using Arduino Nano IoT device

### Step 1.4 — Flash the Arduino

1. Connect the Arduino Nano BLE 33 via USB.

   > **Tip:** If the board is not recognised, double-press the reset button to enter bootloader mode. The built-in orange LED will pulse slowly when in bootloader mode.

2. Click **Upload** (→ button).
3. Open **Tools → Serial Monitor** at **115200 baud**.
4. You should see:

   ```
   ==============================================
    BLE Environmental Sensor — Arduino Nano BLE
   ==============================================
   [BLE]  Advertising started
   [BLE]  Device name  : BLE-EnvSensor
   [BLE]  Service UUID : 19B10000-E8F2-537E-4F6C-D104768A1214
   [BLE]  Waiting for central to connect...
   ----------------------------------------------
   ```

### Step 1.5 — Verify with your phone 

Before moving to Python, confirm the peripheral is advertising correctly using **nRF Connect for Mobile**:

1. Open the app and tap **Scan**.
2. Find **BLE-EnvSensor** in the list (__this name might be different on your device__).
3. Note the RSSI value and the advertised service UUID.
4. Tap **Connect**, then expand the service `19B10000...`.
5. Tap the **↓** (notify) button on the Temperature characteristic.
6. You should see values updating every 500 ms.
7. Write `0x00` to the Control characteristic and verify that notifications stop on the Arduino Serial Monitor.
8. Disconnect the phone before continuing to Part 2.

## Part 2 — Python setup

### Step 2.1 — Check your Python version

```bash
python --version
# Must be 3.10 or higher
```

### Step 2.2 — Install dependencies

```bash
#cd to your python environment 
pip install -r requirements.txt
```

Verify bleak is installed:

```bash
python -c "import bleak; print(bleak.__version__)"
```

### Step 2.3 — Platform-specific notes

| Platform | Notes |
|----------|-------|
| **Windows 10/11** | No extra steps. bleak uses the WinRT Bluetooth API automatically. |
| **macOS** | Device address will be a UUID string (e.g. `AABBCCDD-...`), not a MAC address. This is a CoreBluetooth restriction. |
| **Linux** | Requires BlueZ ≥ 5.43. If you see `org.bluez.Error.NotPermitted`, run with `sudo python ble_sensor_client.py`. |

### Step 2.4 — Read through the Python script

Open `ble_sensor_client.py` and trace through the four main sections:

1. **`find_device()`** — uses `BleakScanner` (GAP Observer role) to find the Arduino by service UUID or device name.
2. **`run_ble()`** — connects (`BleakClient`), prints the full GATT attribute table, calls `start_notify()` on three characteristics (which writes 0x0001 to each CCCD), then loops while connected.
3. **Notification callbacks** (`on_temperature`, `on_humidity`, `on_light`) — called asynchronously by bleak each time the Arduino sends a notification. Decodes raw bytes and appends to rolling deques.
4. **`animate()`** — called by matplotlib every 200 ms to redraw the three-channel rolling plot.

---

## Part 3 — Run the client
### Step 3.1 — Auto-discovery 
Make sure the Arduino is powered and advertising, then:

```bash
python ble_sensor_client.py
```

Expected terminal output:

```
[SCAN]  Scanning for 'BLE-EnvSensor' (service UUID: 19b10000-...)
[SCAN]  Timeout: 10.0 s
[SCAN]  Found: BLE-EnvSensor  addr=AA:BB:CC:DD:EE:FF
[LOG]   Writing data to: /path/to/ble_log_20250101_120000.csv
[GAP]   Connected to AA:BB:CC:DD:EE:FF
[GATT]  MTU: 23 bytes

[GATT]  Attribute table:
  Service 19b10000-e8f2-537e-4f6c-d104768a1214  ◄ our service
    ├─ 19b10001-...  [read, notify]  handle=0x000B
    │   └─ descriptor 00002902-...  handle=0x000C
    ├─ 19b10002-...  [read, notify]  handle=0x000E
    │   └─ descriptor 00002902-...  handle=0x000F
    ├─ 19b10003-...  [read, notify]  handle=0x0011
    │   └─ descriptor 00002902-...  handle=0x0012
    └─ 19b10004-...  [write]         handle=0x0014

[GATT]  Enabling notifications on all sensor characteristics...
[GATT]  Subscribed — data stream starting.

[NOTIFY] #0001  Temp:  22.78 °C  Hum:  55.34 %RH  Light:   412 lux
[NOTIFY] #0002  Temp:  22.65 °C  Hum:  55.89 %RH  Light:   398 lux
...
```

A plot window will open showing three rolling charts updating in real time.

### Step 3.2 — Connect by address (faster reconnect)

Copy the address printed in the first run and use it directly:

```bash
# macOS
python ble_sensor_client.py --address "AABBCCDD-1234-5678-ABCD-AABBCCDDEEFF"

# Windows / Linux
python ble_sensor_client.py --address "AA:BB:CC:DD:EE:FF"
```

### Step 3.3 — Run for a fixed duration

```bash
python ble_sensor_client.py --duration 30   # Collect 30 seconds of data
```

---

After a session, a file named `ble_log_YYYYMMDD_HHMMSS.csv` will be in the same directory as the Python script.

```csv
timestamp_s,iso_time,temperature_C,humidity_pct,light_lux
1704067200.123,2025-01-01T12:00:00.123,22.78,55.34,412
1704067200.623,2025-01-01T12:00:00.623,22.65,55.89,398
...
```
## Part 4 — Understanding the CSV output

You can open this in Excel / LibreOffice Calc, or analyse it with pandas:

```python
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("ble_log_20250101_120000.csv")
df["iso_time"] = pd.to_datetime(df["iso_time"])
df.set_index("iso_time", inplace=True)
df[["temperature_C", "humidity_pct"]].plot()
plt.show()
```
## Further reading

- ArduinoBLE library reference: <https://www.arduino.cc/reference/en/libraries/arduinoble/>
- bleak documentation: <https://bleak.readthedocs.io>
- Bluetooth Core Specification — GATT overview: <https://www.bluetooth.com/specifications/specs/core-specification/>
- Assigned Numbers (standard UUIDs): <https://www.bluetooth.com/specifications/assigned-numbers>
- nRF Connect for Mobile: <https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Mobile>

