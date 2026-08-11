# gl3

the spiralos wayland compositor — a small, `dwm`/`i3`-style **tiling** compositor
built on [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) (the same
library sway and hyprland use). the name follows the terse unix wm/display
convention (`i3`, `x11`, `dwm`); the spiral-and-drill "pierce the heavens" spirit
is an homage to *gurren lagann*, hence spiralos.

it started life as wlroots' reference `tinywl` and is being grown into a real
tiling wm. current state is an early proof-of-concept.

## what works today

- builds against wlroots 0.20 and runs (headless backend verified; boots into
  the event loop, serves a wayland socket).
- **master/stack tiling**: the most-recently-opened window is the master (left
  column, 60% width); the rest stack in the right column, with gaps. re-tiles
  automatically as windows open/close.
- default wallpaper (`assets/background.jpg`), falls back to a solid crimson color if the image fails to load.
- **super** ("spiral" modifier) keybindings:
  - `super+return` — spawn a terminal (`foot`)
  - `super+q` — close the focused window
  - `super+j` — cycle focus to the next window (without reshuffling tiles)
  - `super+h` / `super+l` — shrink / grow the master column
  - `super+1` through `super+0` — switch to workspace 1-10
  - `super+shift+1` through `super+shift+0` — move the focused window to a workspace (does not follow, matches i3's default)
  - `super+escape` — quit
- **workspaces**: 10 of them, i3-style. windows stay tagged to the workspace they opened on; switching workspaces hides everything not on it and re-tiles what's left.

## not done yet

- no multi-monitor-aware layout (tiles across the whole combined layout box).
- no layer-shell (bars/panels), no xwayland.
- no live config file — keybinds are `#define`s in `src/gl3.c`.
- no keybind to swap which window is master, or move a window from stack into master directly.
- no workspace indicator anywhere (no bar), you have to just remember which one you're on.

## build & run

```sh
nix develop            # drops you in a shell with wlroots, foot, gdb, etc.
make                   # builds ./gl3

# run nested inside an existing wayland or x11 session (opens a window):
./gl3 -s foot          # -s runs a startup command (here, a terminal)

# or build the packaged binary:
nix build .#default    # -> ./result/bin/gl3
```

running from a tty drives real hardware (drm/libinput); running inside an
existing session opens a nested window to act as a virtual display.

## testing status

verified in this environment: compiles cleanly (`-wall`, no warnings) against
real wlroots 0.20.2, the flake package builds, and it initializes + enters the
event loop under the headless backend. **not yet verified**: actual window
mapping and the tiling layout on screen — that needs a real display / nested
session with a gpu, which hasn't been run yet.

## relationship to spiralos

built as a standalone repo for now; the plan is to wire it in as spiralos's
flagship desktop (a `spiralos.desktop.gl3.enable` module) once it's a daily
driver.
