#!/usr/bin/env python3
import glob
import json
import os
import signal
import subprocess
import sys
import time

import serial

SERIAL_PORT = "/dev/ttyUSB0"  # set to your ESP32's serial port
BAUD_RATE = 115200
PIDFILE = "/tmp/shock_monitor.pid"
POLL_INTERVAL = 2


def find_serial_port():
    if os.path.exists(SERIAL_PORT):
        return SERIAL_PORT
    candidates = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
    return candidates[0] if candidates else None


def get_active_window_class():
    out = subprocess.run(
        ["hyprctl", "activewindow", "-j"], capture_output=True, text=True
    )
    try:
        return json.loads(out.stdout).get("class", "")
    except json.JSONDecodeError:
        return ""


def run_monitor():
    with open(PIDFILE, "w") as f:
        f.write(str(os.getpid()))
    print(f"Monitoring started pid {os.getpid()}")

    port = find_serial_port()
    ser = serial.Serial(port, BAUD_RATE, timeout=1) if port else None
    if ser is None:
        print("No ESP32 serial port found, will keep retrying")

    try:
        while True:
            if ser is None or not ser.is_open:
                port = find_serial_port()
                if port:
                    try:
                        ser = serial.Serial(port, BAUD_RATE, timeout=1)
                    except serial.SerialException:
                        ser = None

            window = get_active_window_class()
            if ser is not None:
                try:
                    ser.write((window + "\n").encode())
                except serial.SerialException:
                    ser = None  # ESP32 unplugged, retry next tick
            time.sleep(POLL_INTERVAL)
    finally:
        if ser is not None:
            ser.close()
        if os.path.exists(PIDFILE):
            os.remove(PIDFILE)


def toggle():
    if os.path.exists(PIDFILE):
        with open(PIDFILE) as f:
            pid = int(f.read().strip())
        try:
            os.kill(pid, signal.SIGTERM)
            print(f"Monitoring stopped killed pid {pid}")
        except ProcessLookupError:
            print("Stale pidfile cleaning up")
        os.remove(PIDFILE)
    else:
        pid = os.fork()
        if pid == 0:
            os.setsid()
            run_monitor()
            sys.exit(0)
        else:
            print(f"Monitoring started in background pid {pid}")


if __name__ == "__main__":
    toggle()
