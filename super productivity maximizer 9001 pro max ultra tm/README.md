# super productivity maximizer 9001 pro max ultra tm

a project that shocks you with a tens unit to try to maximize productivity, this is not a recommendation, its dangerous, and you should not make this

## do not make this

this wires a relay into the power switch leads of a tens unit and lets a laptop trigger it automatically based on what app is open, that means it can shock you unattended, with no human judgement in the loop, based on nothing but a window title match

tens units carry real medical risk and contraindications, pacemakers and other implanted electronic devices, pregnancy, epilepsy, heart problems, and more, automating one removes the safety of a person manually deciding when to trigger it

no license is granted, this repo is for educational purposes only, and the author is not liable for any injury or other harm resulting from building or using this

## how it hopefully works

```
laptop (Hyprland) --HTTP POST--> ESP32 --GPIO--> relays --> TENS7000 battery + both pot switches
```

1. the Python script on the laptop polls the currently active window via
   Hyprland's `hyprctl activewindow -j` and sends the window class to the
   ESP32 over WiFi
2. the ESP32 checks the window name against a hardcoded list of
   unproductive apps
3. once you've stayed on a match for `DWELL_BEFORE_SHOCK_MS`, the ESP32
   pulses 3 GPIO pins together to close 3 relay channels, then
   re-shocks every `SHOCK_REPEAT_MS` while you stay on it
4. two channels are wired in parallel with the TENS7000's two pot
   switches (one per channel), so closing them mimics turning the knobs
   on, manual operation still works too
5. a third channel is spliced into the TENS7000's battery line instead,
   so the unit is fully unpowered at idle even if both pot knobs are
   left turned up (letting you pre-set intensity) - the pots alone can't
   turn it on, only the battery relay closing can

## files

- **`esp32_shock_relay.ino`** Arduino sketch flashed to the ESP32,
  runs a small WiFi HTTP server with a `/window` endpoint, compares the
  posted window name, lowercased, against the `unproductiveApps` list
  and fires the relays if there's a dwell-time match, edit `ssid`,
  `password`, and `unproductiveApps` before flashing

- **`toggle_shock_monitor.py`** run this on the laptop to start or stop
  monitoring, first run forks a background process that polls the active
  window every 2 seconds and POSTs it to the ESP32, running the script
  again while monitoring is active kills that background process, uses a
  PID file at `/tmp/shock_monitor.pid` to track state, set `ESP32_IP` to
  match the ESP32's address, printed over Serial on boot

## hardware setup

uses a 4-channel relay module, all inputs tied to the same ESP32 GPIO
signal per channel below, so all three fire in sync:

- ESP32 GPIO 26 --> relay `IN1`, relay `COM1`/`NO1` --> pot A's switch
  leads (the two pins that toggle continuity when the knob is clicked,
  separate from the 3 resistive wiper pins)
- ESP32 GPIO 26 --> relay `IN2` --> relay `COM2`/`NO2` --> pot B's
  switch leads, same as above
- ESP32 GPIO 27 --> relay `IN3` --> relay `COM3`/`NO3` --> spliced into
  the TENS7000's battery positive lead
- relay `VCC`/`GND` --> ESP32 5V/GND, shared ground with ESP32 is
  required
- your relay module's `NO`/`NC` silkscreen labels may not match actual
  behavior, verify empirically per channel (idle state should be off)
  before trusting the label
