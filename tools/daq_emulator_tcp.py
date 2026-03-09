#!/usr/bin/env python3
"""
DAQ TCP Device Emulator

Listens on a TCP port and responds to "READ\n" commands with simulated
sensor data in the JSON format expected by the ESP32 HMI firmware:

    Request  (ESP32 → laptop) : READ\n
    Response (laptop → ESP32) : {"id":"EMU01","ch":[{"n":"Voltage","v":3.30,"u":"V"},...]}\\n

Usage:
    python daq_emulator_tcp.py
    python daq_emulator_tcp.py --port 8081 --device-id EMU02

Run one instance per device you want to emulate (each on a different port).
On the ESP32 HMI, go to Devices → Add Device → Type: TCP, then enter your
laptop's IP address and the port number.

Your laptop and the ESP32 must be on the same Wi-Fi network.
Find your laptop's IP with:
    macOS/Linux : ip addr   or   ifconfig
    Windows     : ipconfig
"""

import socket
import threading
import json
import time
import math
import random
import argparse


# ---------------------------------------------------------------------------
# Simulated channel data  (edit freely to match your real devices)
# ---------------------------------------------------------------------------
CHANNELS = [
    {"n": "Voltage",     "u": "V",    "base": 3.30,   "amp": 0.20,  "freq": 0.50, "phase": 0.0},
    {"n": "Current",     "u": "A",    "base": 0.50,   "amp": 0.10,  "freq": 0.30, "phase": 1.0},
    {"n": "Temperature", "u": "C",    "base": 25.0,   "amp": 5.0,   "freq": 0.10, "phase": 0.5},
    {"n": "Pressure",    "u": "kPa",  "base": 101.33, "amp": 0.50,  "freq": 0.20, "phase": 2.0},
    {"n": "Humidity",    "u": "%",    "base": 50.0,   "amp": 10.0,  "freq": 0.07, "phase": 3.0},
]


def channel_values(elapsed: float) -> list:
    values = []
    for ch in CHANNELS:
        v = ch["base"] + ch["amp"] * math.sin(elapsed * ch["freq"] + ch["phase"])
        v += random.uniform(-0.005, 0.005) * ch["amp"]   # small noise
        values.append({"n": ch["n"], "v": round(v, 3), "u": ch["u"]})
    return values


# ---------------------------------------------------------------------------
# Per-client handler
# ---------------------------------------------------------------------------
def handle_client(conn: socket.socket, addr, device_id: str, start_time: float):
    print(f"  [+] {addr} connected")
    buf = b""
    try:
        while True:
            chunk = conn.recv(64)
            if not chunk:
                break
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                cmd = line.strip().decode("utf-8", errors="ignore")
                if cmd == "READ":
                    elapsed = time.time() - start_time
                    payload = {"id": device_id, "ch": channel_values(elapsed)}
                    response = json.dumps(payload, separators=(",", ":")) + "\n"
                    conn.sendall(response.encode())
    except (ConnectionResetError, BrokenPipeError, OSError):
        pass
    finally:
        conn.close()
        print(f"  [-] {addr} disconnected")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="DAQ TCP device emulator for ESP32 HMI")
    parser.add_argument("--host",      default="0.0.0.0",  help="Bind address (default: 0.0.0.0)")
    parser.add_argument("--port",      type=int, default=8080, help="TCP port (default: 8080)")
    parser.add_argument("--device-id", default="EMU01",    help="Device ID reported in JSON (default: EMU01)")
    args = parser.parse_args()

    start_time = time.time()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(8)

    print("=" * 50)
    print("  DAQ TCP Device Emulator")
    print("=" * 50)
    print(f"  Device ID  : {args.device_id}")
    print(f"  Listening  : {args.host}:{args.port}")
    print(f"  Channels   : {', '.join(c['n'] for c in CHANNELS)}")
    print()
    print("  On the ESP32 HMI add a device:")
    print(f"    Type: TCP")
    print(f"    Host: <this laptop's IP>")
    print(f"    Port: {args.port}")
    print()
    print("  Press Ctrl+C to stop.")
    print("=" * 50)

    try:
        while True:
            conn, addr = srv.accept()
            t = threading.Thread(
                target=handle_client,
                args=(conn, addr, args.device_id, start_time),
                daemon=True,
            )
            t.start()
    except KeyboardInterrupt:
        print("\nShutdown.")
    finally:
        srv.close()


if __name__ == "__main__":
    main()
