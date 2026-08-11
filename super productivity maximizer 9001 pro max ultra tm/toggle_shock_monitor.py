#!/usr/bin/env python3
import glob
import json
import os
import signal
import subprocess
import sys
import time

import serial

SERIAL_PORT = "/dev/ttyUSB0"  # your esp32's port, change if different
BAUD_RATE = 115200
PIDFILE = "/tmp/shock_monitor.pid"
POLL_INTERVAL = 2  # 1s was too fast, esp32 started dropping lines

# TODO: pidfile in /tmp means this breaks across reboots on some distros that clear /tmp
# on boot, should probably use $XDG_RUNTIME_DIR instead


def find_serial_port():
    # use the default port, if it exists
    if os.path.exists(SERIAL_PORT):
        return SERIAL_PORT
    # otherwise, look for any plugged in usb serial device
    candidates = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
    return candidates[0] if candidates else None


def get_active_window_class():
    # ask hyprland which window is focused right now
    out = subprocess.run(
        ["hyprctl", "activewindow", "-j"], capture_output=True, text=True
    )
    try:
        # we only care about the app's class name
        return json.loads(out.stdout).get("class", "")
    except json.JSONDecodeError:
        return ""


def run_monitor():
    # save our pid, so toggle can find and kill us later
    with open(PIDFILE, "w") as f:
        f.write(str(os.getpid()))
    print(f"monitoring started pid {os.getpid()}")

    port = find_serial_port()
    ser = serial.Serial(port, BAUD_RATE, timeout=1) if port else None
    if ser is None:
        print("no esp32 serial port found, will keep retrying")

    try:
        # keep running until something kills us
        while True:
            if ser is None or not ser.is_open:
                # esp32 not connected, try again
                port = find_serial_port()
                if port:
                    try:
                        ser = serial.Serial(port, BAUD_RATE, timeout=1)
                    except serial.SerialException:
                        ser = None

            window = get_active_window_class()
            # print(f"debug: window={window!r}")  # handy when hyprctl acts up
            if ser is not None:
                try:
                    # send the window name to the esp32
                    ser.write((window + "\n").encode())
                except serial.SerialException:
                    ser = None  # esp32 unplugged, try again next loop
            time.sleep(POLL_INTERVAL)
    finally:
        # close the port and remove the pidfile on exit
        if ser is not None:
            ser.close()
        if os.path.exists(PIDFILE):
            os.remove(PIDFILE)


def toggle():
    # a pidfile means a monitor is already running, so stop it
    if os.path.exists(PIDFILE):
        with open(PIDFILE) as f:
            pid = int(f.read().strip())
        try:
            os.kill(pid, signal.SIGTERM)
            print(f"monitoring stopped killed pid {pid}")
        except ProcessLookupError:
            # the pid it pointed to is already gone
            print("stale pidfile cleaning up")
        # TODO: tiny race here, the monitor can remove its own pidfile right
        # as we're killing it, just ignore that and move on for now
        try:
            os.remove(PIDFILE)
        except FileNotFoundError:
            pass
    else:
        # nothing running, so start one in the background
        pid = os.fork()
        if pid == 0:
            # child, detach and run the monitor loop
            os.setsid()
            run_monitor()
            sys.exit(0)
        else:
            # parent, just report the child's pid
            print(f"monitoring started in background pid {pid}")


if __name__ == "__main__":
    toggle()
