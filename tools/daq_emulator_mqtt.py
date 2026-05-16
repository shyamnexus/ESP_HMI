#!/usr/bin/env python3
"""
DAQ MQTT Device Emulator

Publishes simulated sensor data to an MQTT broker at a fixed interval,
using the JSON format expected by the ESP32 HMI firmware:

    {"id":"EMU01","ch":[{"n":"Voltage","v":3.30,"u":"V"},...]}

Requirements:
    pip install paho-mqtt

You also need a running MQTT broker on your laptop:
    macOS   : brew install mosquitto && brew services start mosquitto
    Ubuntu  : sudo apt install mosquitto mosquitto-clients && sudo systemctl start mosquitto
    Windows : https://mosquitto.org/download/
    Docker  : docker run -d -p 1883:1883 eclipse-mosquitto

Usage:
    python daq_emulator_mqtt.py
    python daq_emulator_mqtt.py --broker 192.168.1.50 --topic daq/line1/data --device-id EMU02

On the ESP32 HMI, go to Settings → enter your Wi-Fi credentials and connect.
Then add a device: Type: MQTT, Broker URI: mqtt://<laptop IP>:1883, Topic: daq/EMU01/data
"""

import json
import time
import math
import random
import argparse

try:
    import paho.mqtt.client as mqtt
except ImportError:
    raise SystemExit(
        "paho-mqtt is not installed.\n"
        "Run:  pip install paho-mqtt"
    )


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
        v += random.uniform(-0.005, 0.005) * ch["amp"]
        values.append({"n": ch["n"], "v": round(v, 3), "u": ch["u"]})
    return values


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="DAQ MQTT device emulator for ESP32 HMI")
    parser.add_argument("--broker",    default="localhost",      help="MQTT broker host (default: localhost)")
    parser.add_argument("--port",      type=int, default=1883,   help="MQTT broker port (default: 1883)")
    parser.add_argument("--topic",     default="daq/EMU01/data", help="Publish topic (default: daq/EMU01/data)")
    parser.add_argument("--device-id", default="EMU01",          help="Device ID in JSON payload (default: EMU01)")
    parser.add_argument("--interval",  type=float, default=1.0,  help="Publish interval in seconds (default: 1.0)")
    args = parser.parse_args()

    connected = False

    def on_connect(client, userdata, flags, rc):
        nonlocal connected
        if rc == 0:
            connected = True
            print(f"  [+] Connected to broker {args.broker}:{args.port}")
        else:
            print(f"  [-] Broker connection failed (rc={rc})")

    def on_disconnect(client, userdata, rc):
        nonlocal connected
        connected = False
        if rc != 0:
            print(f"  [!] Unexpected disconnect (rc={rc}), will retry...")

    client = mqtt.Client(client_id=args.device_id, clean_session=True)
    client.on_connect    = on_connect
    client.on_disconnect = on_disconnect

    print("=" * 50)
    print("  DAQ MQTT Device Emulator")
    print("=" * 50)
    print(f"  Device ID  : {args.device_id}")
    print(f"  Broker     : mqtt://{args.broker}:{args.port}")
    print(f"  Topic      : {args.topic}")
    print(f"  Interval   : {args.interval}s")
    print(f"  Channels   : {', '.join(c['n'] for c in CHANNELS)}")
    print()
    print("  On the ESP32 HMI add a device:")
    print(f"    Type       : MQTT")
    print(f"    Broker URI : mqtt://{args.broker}:{args.port}")
    print(f"    Topic      : {args.topic}")
    print()
    print("  Press Ctrl+C to stop.")
    print("=" * 50)

    try:
        client.connect(args.broker, args.port, keepalive=60)
        client.loop_start()

        start_time = time.time()
        while True:
            if connected:
                elapsed = time.time() - start_time
                payload = {"id": args.device_id, "ch": channel_values(elapsed)}
                msg = json.dumps(payload, separators=(",", ":"))
                client.publish(args.topic, msg, qos=0)
                print(f"  Published: {msg}")
            else:
                print("  Waiting for broker connection...")
            time.sleep(args.interval)

    except KeyboardInterrupt:
        print("\nShutdown.")
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
