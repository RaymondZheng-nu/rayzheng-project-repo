#!/usr/bin/env python3
import json
import os
import signal
import subprocess
import sys
import time

import requests

ESP32_IP = "192.168.1.50"  # set to your ESP32's IP
PIDFILE = "/tmp/shock_monitor.pid"
POLL_INTERVAL = 2


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
    print(f"Monitoring started (pid {os.getpid()})")
    try:
        while True:
            window = get_active_window_class()
            try:
                requests.post(
                    f"http://{ESP32_IP}/window",
                    data=window,
                    timeout=1,
                )
            except requests.RequestException:
                pass  # ESP32 unreachable, skip this tick
            time.sleep(POLL_INTERVAL)
    finally:
        if os.path.exists(PIDFILE):
            os.remove(PIDFILE)


def toggle():
    if os.path.exists(PIDFILE):
        with open(PIDFILE) as f:
            pid = int(f.read().strip())
        try:
            os.kill(pid, signal.SIGTERM)
            print(f"Monitoring stopped (killed pid {pid})")
        except ProcessLookupError:
            print("Stale pidfile, cleaning up")
        os.remove(PIDFILE)
    else:
        pid = os.fork()
        if pid == 0:
            os.setsid()
            run_monitor()
            sys.exit(0)
        else:
            print(f"Monitoring started in background (pid {pid})")


if __name__ == "__main__":
    toggle()
