"""
ble_sensor_client.py
====================
BLE GATT client for the Arduino BLE Environmental Sensor exercise.

WHAT THIS SCRIPT DOES
---------------------
  1. Scans for the Arduino peripheral by service UUID or device name.
  2. Connects and subscribes to Temperature, Humidity, and Light Level
     notifications via the BLE GATT protocol.
  3. Plots all three channels in real-time using a rolling window.
  4. Logs every received value to a timestamped CSV file.
  5. Writes 0x00 / 0x01 to the Control characteristic on keypress
     (press 'p' to pause, 'r' to resume) — demonstrates GATT WRITE.

BLE CONCEPTS ILLUSTRATED
------------------------
  • BleakScanner  → GAP Observer role (passive scanning)
  • BleakClient   → GAP Central + GATT Client role
  • start_notify  → writes 0x0001 to CCCD (descriptor 0x2902)
  • write_gatt_char → GATT Write operation
  • Notification callback → asynchronous event-driven data reception

REQUIREMENTS
------------
  pip install bleak matplotlib

USAGE
-----
  python ble_sensor_client.py                    # auto-discover
  python ble_sensor_client.py --address XX:XX:XX # connect directly
  python ble_sensor_client.py --duration 60      # run for 60 seconds

KNOWN PLATFORM NOTES
--------------------
  Windows : uses WinRT backend (built into bleak, no extra steps)
  macOS   : uses CoreBluetooth; device address will be a UUID string
  Linux   : requires BlueZ ≥ 5.43; run with sudo if permission denied
"""

import asyncio
import argparse
import csv
import struct
import sys
import time
from collections import deque
from datetime import datetime
from pathlib import Path

import matplotlib
matplotlib.use("TkAgg")          # Use TkAgg backend; change to "Qt5Agg" if needed
import matplotlib.pyplot as plt
import matplotlib.animation as animation

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

# ── UUIDs — must match the Arduino sketch exactly ─────────────────────────────
SERVICE_UUID   = "19b10000-e8f2-537e-4f6c-d104768a1214"
TEMP_UUID      = "19b10001-e8f2-537e-4f6c-d104768a1214"
HUMIDITY_UUID  = "19b10002-e8f2-537e-4f6c-d104768a1214"
LIGHT_UUID     = "19b10003-e8f2-537e-4f6c-d104768a1214"
CONTROL_UUID   = "19b10004-e8f2-537e-4f6c-d104768a1214"

DEVICE_NAME    = "BLE-EnvSensor"

# ── Configuration ─────────────────────────────────────────────────────────────
WINDOW_SIZE    = 100    # Number of data points shown in rolling plot
SCAN_TIMEOUT   = 10.0   # Seconds to scan before giving up
RECONNECT_WAIT = 3.0    # Seconds to wait before reconnect attempt

# ── Shared data buffers (deques act as rolling windows) ───────────────────────
timestamps  = deque(maxlen=WINDOW_SIZE)
temp_data   = deque(maxlen=WINDOW_SIZE)
hum_data    = deque(maxlen=WINDOW_SIZE)
light_data  = deque(maxlen=WINDOW_SIZE)

# ── Global state ──────────────────────────────────────────────────────────────
csv_writer  = None
csv_file    = None
ble_client  = None     # Shared reference used by the animation thread
sample_count = 0

# ── CSV setup ─────────────────────────────────────────────────────────────────
def open_csv_log() -> tuple:
    """Create a timestamped CSV log file and return (file, writer)."""
    ts  = datetime.now().strftime("%Y%m%d_%H%M%S")
    path = Path(f"ble_log_{ts}.csv")
    f    = open(path, "w", newline="")
    w    = csv.writer(f)
    w.writerow(["timestamp_s", "iso_time",
                "temperature_C", "humidity_pct", "light_lux"])
    print(f"[LOG]  Writing data to: {path.resolve()}")
    return f, w

# ── Notification decoders ─────────────────────────────────────────────────────
def decode_float(data: bytearray) -> float:
    """Decode a 4-byte little-endian float sent by the Arduino."""
    return struct.unpack("<f", bytes(data))[0]

def decode_uint16(data: bytearray) -> int:
    """Decode a 2-byte little-endian unsigned integer."""
    return struct.unpack("<H", bytes(data))[0]

# ── Notification callbacks ────────────────────────────────────────────────────
def on_temperature(sender, data: bytearray):
    global sample_count, csv_writer
    temp  = decode_float(data)
    now   = time.time()
    iso   = datetime.now().isoformat(timespec="milliseconds")
    sample_count += 1

    temp_data.append(temp)
    timestamps.append(now)

    print(f"[NOTIFY] #{sample_count:04d}  Temp: {temp:6.2f} °C", end="")

    # CSV row is written when all three values arrive;
    # we use a simple approach — write on temperature callback.
    # humidity and light are written at the same rate so the last
    # values in the deques correspond to the same sample.
    hum   = hum_data[-1]   if hum_data   else 0.0
    light = light_data[-1] if light_data else 0
    if csv_writer:
        csv_writer.writerow([f"{now:.3f}", iso, f"{temp:.3f}",
                             f"{hum:.3f}", light])
        csv_file.flush()    # Ensure data is written even if script crashes

def on_humidity(sender, data: bytearray):
    hum = decode_float(data)
    hum_data.append(hum)
    print(f"  Hum: {hum:6.2f} %RH", end="")

def on_light(sender, data: bytearray):
    lux = decode_uint16(data)
    light_data.append(lux)
    print(f"  Light: {lux:5d} lux")

# ── BLE scan & connect ────────────────────────────────────────────────────────
async def find_device(target_address: str | None) -> str:
    """
    Scan for the peripheral and return its BLE address.
    If target_address is provided, verify it is visible before connecting.
    """
    print(f"[SCAN]  Scanning for '{DEVICE_NAME}' "
          f"(service UUID: {SERVICE_UUID})...")
    print(f"[SCAN]  Timeout: {SCAN_TIMEOUT} s")

    found = await BleakScanner.find_device_by_filter(
        lambda dev, adv: (
            (target_address and dev.address.upper() == target_address.upper())
            or (not target_address and (
                (dev.name and DEVICE_NAME.lower() in dev.name.lower())
                or SERVICE_UUID in [str(u).lower()
                                    for u in adv.service_uuids]
            ))
        ),
        timeout=SCAN_TIMEOUT,
    )

    if found is None:
        raise RuntimeError(
            f"Device '{DEVICE_NAME}' not found within {SCAN_TIMEOUT} s.\n"
            "  • Make sure the Arduino is powered and the sketch is running.\n"
            "  • Check that Bluetooth is enabled on this PC.\n"
            "  • Try passing --address XX:XX:XX:XX:XX:XX explicitly."
        )

    print(f"[SCAN]  Found: {found.name}  addr={found.address}")
    return found.address

async def run_ble(address: str, duration: float):
    """Connect to the peripheral, subscribe to all notifications, and stream."""
    global ble_client, csv_file, csv_writer

    csv_file, csv_writer = open_csv_log()

    async with BleakClient(address) as client:
        ble_client = client
        print(f"[GAP]   Connected to {address}")
        print(f"[GATT]  MTU: {client.mtu_size} bytes")

        # ── Discover and print the GATT attribute table ────────────────────
        print("\n[GATT]  Attribute table:")
        for svc in client.services:
            marker = " ◄ our service" if svc.uuid.lower() == SERVICE_UUID else ""
            print(f"  Service {svc.uuid}{marker}")
            for char in svc.characteristics:
                props = ", ".join(char.properties)
                print(f"    ├─ {char.uuid}  [{props}]  handle=0x{char.handle:04X}")
                for desc in char.descriptors:
                    print(f"    │   └─ descriptor {desc.uuid}  "
                          f"handle=0x{desc.handle:04X}")
        print()

        # ── Subscribe to notifications (writes 0x0001 to each CCCD) ───────
        print("[GATT]  Enabling notifications on all sensor characteristics...")
        await client.start_notify(TEMP_UUID,     on_temperature)
        await client.start_notify(HUMIDITY_UUID, on_humidity)
        await client.start_notify(LIGHT_UUID,    on_light)
        print("[GATT]  Subscribed — data stream starting.\n")
        print("  Press Ctrl+C to disconnect gracefully.\n")

        # ── Stream for the requested duration ──────────────────────────────
        end_time = time.time() + duration if duration > 0 else float("inf")
        try:
            while client.is_connected and time.time() < end_time:
                await asyncio.sleep(0.1)
        except asyncio.CancelledError:
            pass
        finally:
            # ── Gracefully unsubscribe ────────────────────────────────────
            print("\n[GATT]  Unsubscribing from notifications...")
            await client.stop_notify(TEMP_UUID)
            await client.stop_notify(HUMIDITY_UUID)
            await client.stop_notify(LIGHT_UUID)

    ble_client = None
    if csv_file:
        csv_file.close()
        print(f"[LOG]   CSV file closed ({sample_count} samples written).")

# ── Matplotlib real-time plot ─────────────────────────────────────────────────
def build_figure():
    """Create and return the figure, axes, and line objects."""
    fig, (ax_temp, ax_hum, ax_light) = plt.subplots(
        3, 1, figsize=(10, 7), sharex=False
    )
    fig.suptitle(f"BLE Environmental Sensor — {DEVICE_NAME}",
                 fontsize=12, fontweight="bold")
    fig.patch.set_facecolor("#F8F8F8")

    def style_ax(ax, label, unit, color):
        ax.set_ylabel(f"{label}\n({unit})", fontsize=9)
        ax.set_facecolor("#FAFAFA")
        ax.tick_params(labelsize=8)
        ax.grid(True, linestyle="--", linewidth=0.5, alpha=0.6)
        line, = ax.plot([], [], color=color, linewidth=1.4, label=label)
        ax.legend(loc="upper left", fontsize=8)
        return line

    line_t = style_ax(ax_temp,  "Temperature", "°C",  "#E05C33")
    line_h = style_ax(ax_hum,   "Humidity",    "%RH", "#2E86AB")
    line_l = style_ax(ax_light, "Light Level", "lux", "#F0A500")

    ax_light.set_xlabel("Sample #", fontsize=9)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    return fig, (ax_temp, ax_hum, ax_light), (line_t, line_h, line_l)

def animate(frame, axes, lines):
    """Animation callback — called by FuncAnimation every interval."""
    ax_temp, ax_hum, ax_light = axes
    line_t, line_h, line_l    = lines

    n = len(temp_data)
    if n == 0:
        return lines

    xs = list(range(n))

    def update_line(ax, line, data, pad_frac=0.1):
        ys = list(data)
        # Pad ys to match xs length if callbacks are slightly out of sync
        while len(ys) < len(xs):
            ys.insert(0, ys[0] if ys else 0)
        ys = ys[-len(xs):]
        line.set_data(xs, ys)
        if ys:
            lo, hi = min(ys), max(ys)
            margin = max((hi - lo) * pad_frac, 0.5)
            ax.set_xlim(0, max(xs[-1], WINDOW_SIZE - 1))
            ax.set_ylim(lo - margin, hi + margin)

    update_line(ax_temp,  line_t, temp_data)
    update_line(ax_hum,   line_h, hum_data)
    update_line(ax_light, line_l, light_data)

    # Update title with latest values and sample count
    try:
        title = (f"Temperature: {list(temp_data)[-1]:.2f} °C   "
                 f"Humidity: {list(hum_data)[-1]:.2f} %RH   "
                 f"Light: {list(light_data)[-1]:d} lux   "
                 f"[{sample_count} samples]")
        axes[0].get_figure().suptitle(title, fontsize=9)
    except IndexError:
        pass

    return lines

# ── Control: pause / resume from keyboard ─────────────────────────────────────
async def keyboard_control():
    """
    Simple keyboard listener.
    Works on all platforms via asyncio stdin reader.
    Press 'p' + Enter to pause, 'r' + Enter to resume.
    """
    loop = asyncio.get_event_loop()
    while True:
        line = await loop.run_in_executor(None, sys.stdin.readline)
        cmd  = line.strip().lower()
        if ble_client and ble_client.is_connected:
            if cmd == "p":
                await ble_client.write_gatt_char(CONTROL_UUID,
                                                  bytearray([0x00]))
                print("[CTRL]  Sent PAUSE  (0x00) to Control characteristic")
            elif cmd == "r":
                await ble_client.write_gatt_char(CONTROL_UUID,
                                                  bytearray([0x01]))
                print("[CTRL]  Sent RESUME (0x01) to Control characteristic")
            else:
                print("  Type 'p' + Enter to pause, 'r' + Enter to resume.")
        else:
            print("[CTRL]  No active BLE connection.")

# ── Entry point ───────────────────────────────────────────────────────────────
async def main_async(args):
    address = args.address

    if not address:
        address = await find_device(None)

    # Run BLE client and keyboard control concurrently
    ble_task  = asyncio.create_task(run_ble(address, args.duration))
    ctrl_task = asyncio.create_task(keyboard_control())

    try:
        await ble_task
    except (BleakError, RuntimeError) as exc:
        print(f"\n[ERROR] {exc}")
    finally:
        ctrl_task.cancel()

def main():
    parser = argparse.ArgumentParser(
        description="BLE Environmental Sensor Client — Exercise 2"
    )
    parser.add_argument(
        "--address", "-a", default=None,
        help="BLE address of the Arduino (e.g. AA:BB:CC:DD:EE:FF). "
             "If omitted, the script auto-discovers by device name."
    )
    parser.add_argument(
        "--duration", "-d", type=float, default=0,
        help="Run for this many seconds then disconnect (0 = run forever)."
    )
    args = parser.parse_args()

    # ── Build the matplotlib figure (must be on main thread) ──────────────
    fig, axes, lines = build_figure()
    ani = animation.FuncAnimation(
        fig, animate,
        fargs=(axes, lines),
        interval=200,           # Refresh plot every 200 ms
        blit=False,
        cache_frame_data=False,
    )

    # ── Run BLE event loop in a background thread ──────────────────────────
    import threading

    def run_event_loop():
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            loop.run_until_complete(main_async(args))
        except KeyboardInterrupt:
            pass
        finally:
            loop.close()
            plt.close("all")

    ble_thread = threading.Thread(target=run_event_loop, daemon=True)
    ble_thread.start()

    print("[PLOT]  Opening real-time plot window.")
    print("[CTRL]  In the terminal, type 'p' + Enter to pause, "
          "'r' + Enter to resume streaming.\n")

    try:
        plt.show()           # Blocks until the plot window is closed
    except KeyboardInterrupt:
        pass
    finally:
        print("\n[EXIT]  Plot closed. Waiting for BLE thread to finish...")
        ble_thread.join(timeout=5)
        print("[EXIT]  Done.")

if __name__ == "__main__":
    main()
