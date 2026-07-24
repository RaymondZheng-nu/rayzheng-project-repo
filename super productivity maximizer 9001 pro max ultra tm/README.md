# super productivity maximizer 9001 pro max ultra tm

a project that shocks you with a tens unit to try to maximize productivity, this is not a recommendation, its dangerous, and you should not make this

## do not make this

this wires a relay into the power switch leads of a tens unit and lets a laptop trigger it automatically based on what app is open, that means it can shock you unattended, with no human judgement in the loop, based on nothing but a window title match

tens units carry real medical risk and contraindications, pacemakers and other implanted electronic devices, pregnancy, epilepsy, heart problems, and more, automating one removes the safety of a person manually deciding when to trigger it

no license is granted, this repo is for educational purposes only, and the author is not liable for any injury or other harm resulting from building or using this

## how it hopefully works

```
laptop (Hyprland) --HTTP POST--> ESP32 --GPIO--> relay --> TENS7000 battery lead
```

1. the Python script on the laptop polls the currently active window via
   Hyprland's `hyprctl activewindow -j` and sends the window class to the
   ESP32 over WiFi
2. the ESP32 checks the window name against a hardcoded list of
   unproductive apps
3. once you've stayed on a match for `DWELL_BEFORE_SHOCK_MS`, the ESP32
   closes a single relay channel spliced into the TENS7000's battery
   line, then re-shocks every `SHOCK_REPEAT_MS` while you stay on it
4. before use, manually turn both TENS pot knobs on to your desired
   intensity and leave them there - the relay is the only thing gating
   power, so the unit stays fully unpowered at idle regardless of pot
   position, and only the battery relay closing turns it on
5. the sketch logs every step (received window, match/no match, dwell
   timer progress, relay open/close) over serial at 115200 baud, useful
   for confirming the whole chain end to end without guessing

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

only one relay channel is used:

- ESP32 GPIO 26 --> relay `IN1`, relay `COM1`/`NO1` --> spliced into
  the TENS7000's battery positive lead
- relay `VCC`/`GND` --> ESP32 5V/GND, shared ground with ESP32 is
  required
- your relay module's `NO`/`NC` silkscreen labels may not match actual
  behavior, verify empirically (idle state should be off) before
  trusting the label
- solder joints matter more than they seem like they should here, a
  cold or under-wetted joint can pass a casual continuity check and
  still fail under real load, reflow until the joint is a smooth shiny
  fillet on both surfaces, not a dull blob sitting on top
