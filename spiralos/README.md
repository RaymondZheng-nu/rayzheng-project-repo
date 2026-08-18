# spiralos

spiralos is a customized version of nixos (a linux operating system) made
for engineers and people who build electronics/hardware projects. think of
it like blackarch is to arch linux — it's not a whole new operating system,
it's regular nixos with extra stuff bolted on: ready-made software bundles,
hardware fixes, and a nicer installer.

because it's real nixos underneath, anything you'd normally write for plain
nixos still works on spiralos without changes. spiralos only ever *adds*
its own settings (all under `spiralos.*`), it never changes or removes how
normal nixos settings work.

it does two things:

- **for software engineers** — turn on ready-made toolsets with one line each: `spiralos.profiles.rust`, `.python`, `.web`, `.ml`.
- **for makers/hardware people** — profiles for `electronics`, `embedded`, and `cad` (3d printing) work, plus `spiralos.maker`, a setting that fixes the annoying hardware problems every beginner hits on a normal linux install (explained below). the point is: plug in your arduino/esp32/whatever, and it just works, no extra setup, no admin password needed.

## what's in this folder

- `flake.nix` — the main entry point. defines the actual spiralos system and the installer disk image (iso).
- `modules/spiralos.nix` — the core settings every spiralos install gets (branding, basic apps, defaults).
- `modules/profiles/` — the optional toolsets: `rust`, `python`, `web`, `ml` for engineers, `embedded`, `electronics`, `cad` for makers.
- `modules/maker.nix` — the hardware-fix settings (explained below), turned on automatically by the maker profiles.
- `overlays/` — spiralos's own custom versions of some software packages.
- `iso/configuration.nix` — settings just for the installer disk image, starts the installer automatically.
- `modules/installer.nix` — packs everything the installer needs onto the disk image, aside from nixpkgs itself, which it fetches over the network to keep the disk image smaller.
- `pkgs/` — spiralos's own custom tools: `spiralos-wizard`, `spiralos-installer`, `spiralos-rebuild`.

## how to use it

turn on a toolset in your own nixos settings file:

```nix
{
  imports = [ spiralos.nixosModules.default ];
  spiralos.profiles.rust.enable = true;
  spiralos.profiles.web.enable = true;
}
```

## gl3 as a desktop session (`spiralos.desktop.gl3`)

early and opt-in, off by default. turn it on with
`spiralos.desktop.gl3.enable = true` to boot straight into a `tuigreet`
text-mode login screen that launches gl3 — no gtk/qt greeter, matches gl3's
own no-frills approach.

this only works from this repo's own flake (`nixosConfigurations.live`, or
your own config built from `spiralos.nixosModules.default` in a checkout
that has `gl3/` sitting next to `spiralos/`), not from the offline
installer's generated configs — gl3 doesn't have a public git remote yet,
just a folder in this same personal monorepo, so there's nowhere for the
installer to fetch it from offline. see the comment on the `gl3` input in
`flake.nix`.

## the maker hardware fixes (`spiralos.maker`)

turn it on with `spiralos.maker.enable = true` (the `embedded` and
`electronics` toolsets turn it on for you automatically). here's what
breaks on a normal linux install, and what this fixes:

- **turns off modemmanager** — normally, plugging in an arduino or esp32 makes linux think it might be a phone/modem and poke at it, which breaks flashing new code onto the board. this stops that.
- **stops brltty from stealing your device** — brltty is software for braille displays, but it often mistakes cheap usb-serial chips (used by lots of dev boards) for a braille display and grabs them, making your board disappear. this stops that too.
- **sets up device permissions** — creates a `plugdev` group and the right rules so common dev boards (rp2040, arduino, and others) and debug probes (st-link, j-link, and others) are usable without extra setup.
- **adds your user account to the right groups** — so you can use serial/usb devices without needing the admin password every time. the installer does this for the account it creates; the setup wizard does it for whichever account is already logged in.

net result: plug in a dev board, flash it, no admin password, no fighting the operating system.

## building it

```sh
# build the installer disk image
nix build .#iso

# or install/update the settings on the computer you're already running
sudo nixos-rebuild switch --flake .#live
```

## roadmap (future plans)

- [x] step-by-step installer (`spiralos-installer`) — asks for keyboard/hostname/timezone/user/disk/software, then installs
- [x] first-boot setup wizard (`spiralos-wizard`) — lets you reconfigure toolsets after installing, detects your graphics card and whether you're on a laptop
- [x] gl3 available as an opt-in desktop session (`spiralos.desktop.gl3.enable`), from this repo's own flake only
- [ ] gl3 as the actual *default* desktop, and wired into the offline installer too — needs gl3 to get a real git remote first
- [x] `spiralos-rebuild` — a friendlier way to update your system and see what's about to change
- [ ] a graphical (not just text) installer
- [ ] a shared download cache so custom packages don't have to rebuild from scratch every time
- [x] terminal prompt (crimson, matches gl3's border color) and a login message (motd)
- [x] quiet, plymouth-splash boot instead of a wall of kernel text (stock "spinner" theme, not a custom one - see the `boot.plymouth` block in `modules/spiralos.nix` for why)
- [ ] more visual branding: a real *custom* boot theme (needs a VM/real hardware to actually watch it boot and check it isn't broken), wallpaper (gl3 already has its own default wallpaper, this is about the rest of the system)

## the installer

`spiralos-installer` starts automatically when you boot the installer disk
image. it's a text-based, step-by-step setup, similar to debian's installer:

1. welcome screen
2. keyboard layout
3. computer name
4. timezone
5. username and password
6. pick a disk to install to — **this erases the disk**, you have to type the disk's name back to confirm you mean it
7. pick which toolsets to install (same options as the setup wizard below)
8. a summary screen, then it installs everything

needs an internet connection to install — it fetches nixpkgs itself over
the network instead of shipping the whole thing on the disk image, which
keeps the image a lot smaller. if you're not already connected, the
installer notices and offers to open `nmtui` for wifi before it gets to
that step.

once it's done, the new system boots straight into the toolsets you picked
— it also remembers that setup already happened, so it won't ask you the
same questions again on first boot.

## the first-boot setup wizard

`spiralos-wizard` is for changing your toolsets later, or for setting them
up if you installed spiralos a different way. it runs by itself, once,
before you log in for the first time. it:

1. checks your graphics card (nvidia/amd/intel) and whether you're on a laptop.
2. asks what you'll mainly use the computer for, and lets you fine-tune which toolsets to turn on.
3. saves your choices to a settings file and makes sure your main settings file includes it.
4. offers to apply the changes right away.

you can run it again anytime later with `spiralos-wizard --force`.

## the update tool

`spiralos-rebuild` is a friendlier wrapper around the normal nixos update
command. it shows you exactly what's about to change before it changes
anything, and makes undoing an update a single word instead of a command
you have to remember:

- `spiralos-rebuild diff` — show what would change, without actually applying it
- `spiralos-rebuild switch` — show what would change, then apply it (this is what happens if you don't type anything after `spiralos-rebuild`)
- `spiralos-rebuild boot` / `spiralos-rebuild test` — apply it, but either only take effect after a restart, or only take effect now without becoming permanent
- `spiralos-rebuild rollback` — undo the last update and go back to how things were

it comes installed by default on every spiralos system.
