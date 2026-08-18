# gl3

gl3 is the spiralos wayland compositor. a "compositor" is the program that
draws windows on your screen and decides where they go — this one arranges
windows automatically in a grid, like `dwm` and `i3` do, instead of letting
you stack them on top of each other.

it's built on [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots), a
toolkit that other window managers like sway and hyprland also use. the name
is short on purpose, matching other linux window managers like `i3` and
`dwm`. the "pierce the heavens" theme and crimson colors are a nod to the
anime *gurren lagann*, which is also where the name spiralos comes from.

it started out as `tinywl`, a small example compositor that ships with
wlroots, and is slowly being turned into a real window manager. it's still
early and being tested as it goes.

## what works today

- builds against wlroots 0.20 and starts up correctly (tested in a mode with no real screen, it waits for apps to connect like it normally would).
- **automatic tiling**: the first window you open becomes the big "master" window on the left; every window after that stacks in a column on the right, with small gaps between them. windows resize automatically as you open and close them.
- a default background image (`assets/background.jpg`), or a plain crimson color if the image can't be loaded. scaled to fit each monitor separately, so it looks right regardless of how many monitors you have or what resolution they are.
- **borders**: a thin colored line around every window — bright red on whichever one you're using, dark gray on the rest, and the rest also get slightly dimmed so it's obvious which window is active.
- **no double title bars**: gl3 tells apps that support the xdg-decoration protocol (most modern GTK/Qt apps) to skip drawing their own title bar, since gl3 already draws its own border. apps that don't support the protocol at all may still draw their own — there's no way to negotiate with those.
- **a settings file**: `~/.config/gl3/gl3.conf` (copy `gl3.conf.example` to get started) lets you change things like the terminal it opens, spacing, border colors, the background image, and every keyboard shortcut below, without needing to edit the code or rebuild anything. if you make a typo, gl3 just ignores that one line and tells you, it won't refuse to start.
- **keyboard shortcuts**, all held with the "super" key (usually the windows/command key), all rebindable in the settings file except workspace switching (`super+1-0`, tied to the number row like every other tiling wm):
  - `super+return` — open a terminal
  - `super+space` — open an app launcher
  - `super+q` — close the window you're using
  - `super+j` — switch to the next window
  - `super+m` — swap the window you're using with the master window
  - `super+h` / `super+l` — make the master window smaller / bigger
  - `super+1` through `super+0` — switch to workspace 1 through 10 (workspaces are separate sets of windows, like different desks)
  - `super+shift+1` through `super+shift+0` — send the current window to a workspace, without switching to it yourself
  - `super+?` — show this keybind list on screen
  - `super+escape` — quit gl3
- **workspaces**: 10 separate spaces for windows, like i3 has, and multi-monitor aware the same way i3 is. each monitor shows its own workspace and tiles independently. switching to a workspace that's already showing on another monitor just moves you there instead of rearranging anything; switching to a hidden one shows it on whichever monitor you're currently using, and whatever was there before stays remembered for next time. each window remembers which workspace it was opened on. a row of small dots in the bottom-left corner of each monitor shows which workspace it's on — gl3 has no text rendering yet, so this is a dot indicator instead of a number, lit up in the active border color for whichever workspace is showing.
- **resizing with the mouse**: click and drag the gap between the master window and the stack to change how much space each side gets — the mouse version of `super+h/l`. gl3 doesn't support freely resizing or moving a single window around, only this.
- **app launcher**: `super+space` opens [fuzzel](https://codeberg.org/dnkl/fuzzel) by default — a fuzzy-search list of every command on your `PATH` plus desktop entries, type to filter and press enter to launch. gl3 doesn't have its own launcher UI, this just spawns whatever you point it at in the settings file — the default is a real floating popup via `wlr-layer-shell-v1`, not a window competing for tile space, since fuzzel speaks that protocol natively.
- **keybind help screen**: `super+?` opens a terminal showing every keybind. it also opens automatically the very first time you ever run gl3, so you're not left guessing what to press.
- **bars and other screen-anchored tools**: gl3 supports the `wlr-layer-shell-v1` protocol, so status bars, wallpaper tools, and popup launchers/menus that use it (waybar, wbg, wofi/rofi in layer-shell mode, etc) work and get their exclusive space (eg a bar reserving a strip at the top) automatically respected by the tiling — windows won't tile underneath a bar that reserves space.

## not done yet

- no support for older x11-only apps.
- no floating windows, and no dragging a single window around freely — this is intentional, see "resizing with the mouse" above.
- no rounded window corners — that needs custom drawing code on the graphics card (like hyprland has), which is a lot of extra work wlroots doesn't provide for free. not done yet.

## build & run

```sh
nix develop            # opens a terminal with everything gl3 needs already installed
make                   # builds ./gl3

# run it inside your current desktop, in its own window, to try it out safely:
./gl3 -s foot          # -s tells it to open a terminal automatically when it starts

# or build it the "proper" way, as a package:
nix build .#default    # -> ./result/bin/gl3
```

running gl3 from a plain login screen (no desktop already running) uses your
real monitor and keyboard/mouse directly. running it inside your normal
desktop opens it in its own window instead, like a little virtual screen —
that's the safe way to try it out without affecting your real desktop.

## config

copy `gl3.conf.example` to `~/.config/gl3/gl3.conf` and edit it — the file
itself explains every setting. gl3 only reads this file when it starts, so
you have to restart gl3 after changing it for the change to show up.

## testing status

- builds with no warnings against a real copy of wlroots 0.20.2
- the packaged build works too
- runs both in the no-screen test mode and inside a real desktop, in its own window
- opening a terminal and tiling both work, confirmed by actually looking at it on screen, borders and dimming included
- the settings file was tested too: correct settings work, and typos or bad values get skipped with a warning instead of crashing gl3
- one real bug was found from a screenshot and fixed: a window's border stayed too big after the window itself shrank. the cause was old leftover code (from the `tinywl` example gl3 was built on) that let apps resize themselves freely, which doesn't make sense for a tiling window manager. that code was removed and replaced with the mouse-drag resize feature described above.

- the app launcher and help screen were tested too: the shell commands they run were checked for valid syntax, the help text file gets written correctly, and the "only show help on first run" logic was tested from a completely fresh, empty settings folder to make sure it actually creates the folder and remembers not to show it again — an earlier version of this had a bug where it silently failed to remember on a brand new install, since it tried to create a folder inside another folder that didn't exist yet, that's fixed now
- layer-shell (bars/wallpaper tools/popup launchers) was tested against real clients, not just headless idling: `wbg` (a background-layer wallpaper tool), `fuzzel` (the new default launcher), and a regular tiled terminal all running at the same time without crashing. three real bugs were found and fixed this way: calling the protocol's configure function before a surface's first commit crashed with a wlroots assertion (both for layer-shell surfaces and, it turned out, for the *existing* xdg-decoration code too — `foot` sends its decoration `set_mode` request before its first commit, which was crashing gl3 even without any layer-shell client involved), an early version reconfigured on every single commit instead of only when something layout-relevant changed, which caused an infinite configure → ack → commit → configure loop with a real client, and `server_new_xdg_popup` didn't know how to find a layer surface's scene tree for its popups (only checked the xdg-toplevel case), which would've crashed on any layer-shell client with a dropdown/submenu
- confirmed via the wayland protocol trace that fuzzel actually requests the overlay layer, keyboard interactivity, and no anchor (centered) exactly as expected, and that it doesn't get pulled into the tiling layout the way a regular window would

not yet checked:

- the new mouse-drag resize feature and the help screen (still runs in a terminal, unchanged) — build and run with no errors, but haven't been watched on screen yet
- running gl3 from a real login screen using real hardware — that needs a spare computer or monitor to test on, hasn't been tried yet

## relationship to spiralos

gl3 lives in its own repo for now. the plan is to make it spiralos's default
desktop once it's solid enough to use every day.
