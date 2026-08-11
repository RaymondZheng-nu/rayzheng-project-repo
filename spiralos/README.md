# spiralos

a nixos spin **for engineers and makers** — same relationship to nixos that blackarch
has to arch: a curated package/overlay layer, opt-in stack profiles, and a themed
installable iso, all built on real nixos/nixpkgs underneath. pierce the heavens.

because it's real nixos under the hood, any existing `configuration.nix`/flake written
for stock nixos works unmodified on spiralos — spiralos only *adds* options under its
own `spiralos.*` namespace, it never overrides core nixos options.

two halves to the identity:

- **engineer** — curated software stacks you toggle on: `spiralos.profiles.{rust,python,web,ml}`.
- **maker** — physical-computing profiles (`electronics`, `embedded`, `cad`) plus
  `spiralos.maker` — a base module that fixes the hardware-access papercuts every maker
  hits on a fresh linux install (see below). this is the real differentiator: your dev
  board just *works*, no root, no fighting modemmanager.

## layout

- `flake.nix` — entrypoint; defines the `live` nixos configuration and the `iso` package.
- `modules/spiralos.nix` — core spiralos module (branding, base packages, defaults).
- `modules/profiles/` — opt-in profiles: `rust`, `python`, `web`, `ml` (engineer) and `embedded`, `electronics`, `cad` (maker).
- `modules/maker.nix` — `spiralos.maker` hardware-access base (serial/usb, udev, groups); auto-enabled by the maker profiles.
- `overlays/` — package overlay for spiralos-specific packages/patches.
- `iso/configuration.nix` — live-iso specific config; runs `spiralos-installer` on tty1.
- `modules/installer.nix` — bakes the flake source + disko's module into `/etc` and autolaunches the installer, live-iso only.
- `pkgs/` — custom package derivations (`spiralos-wizard`, `spiralos-installer`, `spiralos-rebuild`).

## usage

enable a profile in your own `configuration.nix`:

```nix
{
  imports = [ spiralos.nixosModules.default ];
  spiralos.profiles.rust.enable = true;
  spiralos.profiles.web.enable = true;
}
```

## the maker base (`spiralos.maker`)

enable with `spiralos.maker.enable = true` (the `embedded` and `electronics`
profiles turn it on automatically). it fixes the hardware papercuts that every
maker rediscovers the hard way on a stock install:

- **disables modemmanager** — it probes `/dev/ttyusb*`/`/dev/ttyacm*` on plug-in and corrupts arduino/esp flashing.
- **masks brltty** — it claims cheap ch340/ch341 usb-serial adapters as braille displays, making the device vanish.
- **creates the `plugdev` group** and ships udev rules for common boards/probes (rp2040, ftdi, ch340, cp210x, arduino) plus openocd/stlink's own rules (st-link, j-link, cmsis-dap).
- **auto-adds a user to `dialout`+`plugdev`** via `spiralos.maker.user = "you"` so serial/usb access works without root. the installer sets this to the account it creates; the wizard detects your primary login user.

net effect: plug in a dev board and flash it, no root and no fighting the os.

## building

```sh
# build the iso
nix build .#iso

# or build/switch a live system directly
sudo nixos-rebuild switch --flake .#live
```

## roadmap (mint-tier ambitions)

- [x] debian-installer-style installer (`spiralos-installer`) — keyboard/hostname/timezone/user/disk/software, then partitions + installs
- [x] first-boot setup wizard (`spiralos-wizard`) — post-install profile reconfiguration, detects gpu/laptop
- [ ] own wm/de as the flagship shell (the `gl3` compositor, integrates later)
- [x] `spiralos-rebuild` wrapper — better diff/rollback ux over `nixos-rebuild`
- [ ] graphical installer (current one is tty/whiptail, like `d-i`'s text frontend)
- [ ] binary cache (cachix) for custom overlay packages
- [ ] branding: boot splash, wallpaper, motd, prompt defaults

## installer

`spiralos-installer` auto-launches on tty1 when you boot the live iso — a
text-mode, step-by-step flow modeled on the debian installer (`d-i`):

1. welcome
2. keyboard layout
3. hostname
4. timezone
5. username + password
6. disk selection — **destructive**; requires typing the disk path back to confirm
7. software profiles (same workstation/data-science/maker/embedded/minimal/custom presets as the wizard)
8. summary screen, then partitions/formats via [`disko`](https://github.com/nix-community/disko) and runs `disko-install`

it works fully offline: at iso build time, the spiralos flake's own source and
disko's nixos module are baked into `/etc/spiralos/` on the live image, so the
installer writes a small self-contained flake referencing those local paths
rather than fetching anything at install time.

the freshly installed system boots straight into your chosen profiles — the
installer marks the post-install wizard as already-done so you're not asked
the same questions twice.

## first-boot wizard

`spiralos-wizard` is for reconfiguring profiles later (or for systems that
didn't go through `spiralos-installer`). it runs automatically once, on tty1,
before login on a freshly installed system (see `modules/wizard.nix`). it:

1. detects gpu vendor (nvidia/amd/intel) and whether the machine is a laptop (battery present).
2. asks what you're using the machine for (workstation / data science / maker / embedded / minimal / custom) and lets you fine-tune which `spiralos.profiles.*` to enable.
3. writes `/etc/nixos/spiralos-profiles.nix` and makes sure `/etc/nixos/configuration.nix` imports it.
4. offers to run `nixos-rebuild switch` immediately.

re-run it anytime with `spiralos-wizard --force`.

## rebuild wrapper

`spiralos-rebuild` wraps `nixos-rebuild` so you see what a switch will
actually change before it happens, and so rollback is one word instead of
a flag you have to remember:

- `spiralos-rebuild diff` — build the new system, show the closure diff, don't switch
- `spiralos-rebuild switch` — build, show the diff, then switch (default)
- `spiralos-rebuild boot` / `spiralos-rebuild test` — same, but only set as boot default or activate without setting it
- `spiralos-rebuild rollback` — boot into the previous generation

it's installed by default on every spiralos system.
