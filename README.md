*IDA Lab — Università del Salento — Internet of Things A.A. 2025/2026*

# BLEBeacon_Exercise1

## From Example Eddystone BLE URL beacon advertising


# BLE Classroom_Exercise2

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
2. Find **BLE-EnvSensor** in the list (*_this name might be different on your device_*).
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

Copy the address printed in the first run and use it directly (*_addresses shown here are just examples_*):

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

## Part 5 — Deep Dive Tasks

Work through these tasks in order. Each one builds on the concepts from the lecture.

---

### Task A — Observe the GATT attribute table

> **Goal:** Understand how a GATT service maps to handles and descriptors.

1. Run the Python script and read the printed attribute table.
2. Identify which handle corresponds to which characteristic UUID.
3. Find the CCCD descriptors (UUID `00002902-0000-1000-8000-00805f9b34fb`).
4. **Question:** Why does the Control characteristic (WRITE-only) not have a CCCD descriptor?

---

### Task B — Pause and resume streaming

> **Goal:** Demonstrate a GATT WRITE operation from Python to Arduino.

1. While the script is running and the plot is live, switch to the terminal window.
2. Type `p` and press **Enter**.
3. Observe that the Arduino Serial Monitor prints `[CTRL] Streaming PAUSED` and the plot stops updating.
4. Type `r` and press **Enter** to resume.
5. **Explain** what byte value was written to which characteristic handle, and how the Arduino's `controlChar.written()` check in `loop()` detected it.

---
### Task C — Modify the advertising interval

> **Goal:** Understand the trade-off between advertising frequency and power.

1. In the Arduino sketch, find the `BLE.advertise()` call.
2. Add `BLE.setAdvertisingInterval(160);` before it (160 × 0.625 ms = 100 ms).
3. Reflash and observe on nRF Connect for Mobile how quickly the device appears in the scan list.
4. Change the interval to `1600` (1000 ms) and repeat.
5. **Question:** What is the impact on battery life? What is the minimum interval allowed by the BLE spec?

---

### Task D — Change the notification rate

> **Goal:** Relate `UPDATE_INTERVAL_MS` to connection events and data freshness.

1. In the Arduino sketch, change `UPDATE_INTERVAL_MS` from `500` to `100`.
2. Reflash, reconnect, and observe the plot refresh rate and the CSV density.
3. Change it to `2000` and observe again.
4. **Question:** How does the notification rate relate to the connection interval negotiated during `CONNECT_REQ`? (Hint: see lecture slide 43.)

---

### Task E — Add a fourth characteristic (challenge)

> **Goal:** Extend the GATT service with a new characteristic.

1. In the Arduino sketch, add a **Battery Level** characteristic:
   - UUID: `19B10005-E8F2-537E-4F6C-D104768A1214`
   - Properties: `BLERead | BLENotify`
   - Data: 1 byte, simulated percentage 0–100 (decrements over time)
2. Add a corresponding notification callback in the Python script.
3. Add a fourth subplot to the matplotlib figure.
4. **Stretch goal:** Use the standard Battery Service UUID `0x180F` and standard Battery Level characteristic UUID `0x2A19` instead, and verify that nRF Connect for Mobile displays it with the battery icon.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `Device not found within 10 s` | Arduino not advertising | Check Serial Monitor shows `[BLE] Advertising started` |
| `Device not found within 10 s` | Bluetooth disabled on PC | Enable Bluetooth in OS settings |
| `org.bluez.Error.NotPermitted` (Linux) | BlueZ permissions | Run `sudo python ble_sensor_client.py` or add user to `bluetooth` group |
| Plot window opens but never updates | Bleak thread not starting | Check that `bleak` and `matplotlib` are both installed in the same Python environment |
| `AttributeError: 'NoneType' has no attribute 'is_connected'` | Keypress before connection | Wait for `[GATT] Subscribed` message before typing `p` / `r` |
| Arduino uploads but Serial shows nothing | Wrong baud rate | Set Serial Monitor to 115200 |
| Arduino not recognised by IDE | Missing board package | Re-run Boards Manager install step | Cable had no data lines
| Values stop updating on plot but Arduino keeps printing | CCCD was not written | Disconnect and reconnect; check `start_notify` did not throw an exception |

---

## Custom UUIDs reference

| Resource | UUID |
|----------|------|
| Environmental Sensor Service | `19B10000-E8F2-537E-4F6C-D104768A1214` |
| Temperature characteristic | `19B10001-E8F2-537E-4F6C-D104768A1214` |
| Humidity characteristic | `19B10002-E8F2-537E-4F6C-D104768A1214` |
| Light Level characteristic | `19B10003-E8F2-537E-4F6C-D104768A1214` |
| Control characteristic | `19B10004-E8F2-537E-4F6C-D104768A1214` |

> These UUIDs were randomly generated for this exercise. In a production device you would generate your own using `uuidgen` (macOS/Linux) or an online UUID generator.

---

## Data encoding reference

| Characteristic | Type | Size | Encoding |
|---------------|------|------|----------|
| Temperature | float32 | 4 bytes | IEEE 754 single precision, little-endian |
| Humidity | float32 | 4 bytes | IEEE 754 single precision, little-endian |
| Light Level | uint16 | 2 bytes | Unsigned integer, little-endian |
| Control | uint8 | 1 byte | `0x00` = pause, `0x01` = resume |

---

## Further reading

- ArduinoBLE library reference: <https://www.arduino.cc/reference/en/libraries/arduinoble/>
- bleak documentation: <https://bleak.readthedocs.io>
- Bluetooth Core Specification — GATT overview: <https://www.bluetooth.com/specifications/specs/core-specification/>
- Assigned Numbers (standard UUIDs): <https://www.bluetooth.com/specifications/assigned-numbers>
- nRF Connect for Mobile: <https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Mobile>

*IDA Lab — Università del Salento — Internet of Things A.A. 2025/2026*
