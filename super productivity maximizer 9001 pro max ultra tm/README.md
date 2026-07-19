# super productivity maximizer 9001 pro max ultra tm

a project that shocks you with a tens unit to try to maximize productivity - this is not a recommendation, its dangerous, and you should not make this

## do not make this

this wires a relay into the power switch leads of a tens unit and lets a laptop trigger it automatically based on what app is open - that means it can shock you unattended, with no human judgement in the loop, based on nothing but a window title match

tens units carry real medical risk and contraindications (pacemakers/other implanted electronic devices, pregnancy, epilepsy, heart problems, and more) - automating one removes the safety of a person manually deciding when to trigger it

no license is granted, this repo is for educational purposes only, and the author is not liable for any injury or other harm resulting from building or using this

## How it (hopefully)  works

```
Laptop (Hyprland) --HTTP POST--> ESP32 --GPIO--> Relay --> TENS7000 pot switch
```

1. The Python script on the laptop polls the currently active window via
   Hyprland's `hyprctl activewindow -j` and sends the window class to the
   ESP32 over WiFi.
2. The ESP32 checks the window name against a hardcoded list of
   unproductive apps.
3. If it's a match (and the cooldown/app-change conditions are met), the
   ESP32 pulses a GPIO pin to close a 5V relay.
4. The relay is wired in parallel with the TENS7000's built-in
   potentiometer switch leads, so closing the relay mimics a manual click
   of the power switch — triggering the unit.

## Files

- **`esp32_shock_relay.ino`** — Arduino sketch flashed to the ESP32.
  Runs a small WiFi HTTP server with a `/window` endpoint. Compares the
  posted window name (lowercased) against the `unproductiveApps` list and
  pulses `RELAY_PIN` if there's a match, subject to a cooldown so it
  doesn't re-trigger every poll tick while you're still on the same app.
  Edit `ssid`, `password`, and `unproductiveApps` before flashing.

- **`toggle_shock_monitor.py`** — Run this on the laptop to start or stop
  monitoring. First run forks a background process that polls the active
  window every 2 seconds and POSTs it to the ESP32; running the script
  again while monitoring is active kills that background process. Uses a
  PID file at `/tmp/shock_monitor.pid` to track state. Set `ESP32_IP` to
  match the ESP32's address (printed over Serial on boot).

## Hardware setup

- ESP32 GPIO 26 -> relay module `IN`
- Relay `VCC`/`GND` -> ESP32 5V/GND (shared ground with ESP32 is required)
- Relay `COM`/`NO` -> soldered onto the TENS7000 potentiometer's switch
  leads (the two pins that toggle continuity when the knob is clicked,
  separate from the 3 resistive wiper pins), wired in parallel with the
  existing switch so manual operation still works.

