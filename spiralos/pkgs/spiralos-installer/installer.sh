#!/usr/bin/env bash
set -euo pipefail

# step by step tty installer, runs from the live iso
# partitions the disk, writes a flake, runs disko-install
#
# destructive, erases the selected disk, only proceeds if you type the disk
# name back to confirm, if disko-install itself fails partway through, the
# disk gets wiped back to blank rather than left half-partitioned (see the
# disko-install block below) - that's still all the safety net there is,
# nothing here protects other disks or backs up data before erasing

WORKDIR=$(mktemp -d /root/spiralos-install.XXXXXX)
trap 'rm -rf "$WORKDIR"' EXIT

FLAKE_SELF=/etc/spiralos/flake-self
DISKO_SRC=/etc/spiralos/disko-src

step() {
  whiptail --title "spiralos installer" --infobox "$1" 8 70
  sleep 0.5
}

# 1. welcome

whiptail --title "welcome to spiralos" --msgbox \
  "this installer will walk you through setting up spiralos, step by step\n\n  1) keyboard layout\n  2) hostname\n  3) timezone\n  4) user account\n  5) disk selection\n  6) software profiles\n  7) install\n\nwarning, this will erase the disk you select, make sure you have backups" \
  18 72

# 2. keyboard layout

KEYMAP=$(whiptail --title "keyboard layout" --menu "select your keyboard layout" 18 60 8 \
  us "US English" \
  us-intl "US English (international)" \
  uk "United Kingdom" \
  de "German" \
  fr "French" \
  es "Spanish" \
  dvorak "Dvorak" \
  3>&1 1>&2 2>&3)

# 3. hostname

# HOSTNAME ends up quoted in host-config.nix, a literal '"' in it would
# break out of that nix string, so it's restricted to a valid dns label
while true; do
  HOSTNAME=$(whiptail --title "hostname" --inputbox \
    "what should this machine be called" 10 60 "spiralos" \
    3>&1 1>&2 2>&3)
  if [[ "$HOSTNAME" =~ ^[a-z0-9]([a-z0-9-]*[a-z0-9])?$ ]]; then
    break
  fi
  whiptail --title "hostname" --msgbox "that's not a valid hostname, lowercase letters, numbers, and dashes only, try again" 8 60
done

# 4. timezone

TIMEZONE=$(whiptail --title "timezone" --menu "select your timezone" 20 60 10 \
  America/Los_Angeles "US Pacific" \
  America/Denver "US Mountain" \
  America/Chicago "US Central" \
  America/New_York "US Eastern" \
  UTC "UTC" \
  Europe/London "UK" \
  Europe/Berlin "Central Europe" \
  Asia/Tokyo "Japan" \
  Asia/Shanghai "China" \
  Australia/Sydney "Australia Eastern" \
  3>&1 1>&2 2>&3)

# 5. user account

# USERNAME ends up unquoted in host-config.nix as users.users.${USERNAME},
# a bare space or dot there is a nix syntax error, not just a cosmetic
# problem, so this has to be a real posix-username pattern, not free text
while true; do
  USERNAME=$(whiptail --title "user account" --inputbox \
    "choose a username, lowercase letters, numbers, underscore, or dash, starting with a letter or underscore" 10 60 \
    3>&1 1>&2 2>&3)
  if [[ "$USERNAME" =~ ^[a-z_][a-z0-9_-]*$ ]]; then
    break
  fi
  whiptail --title "user account" --msgbox "that's not a valid username, try again" 8 60
done

while true; do
  PASS1=$(whiptail --title "password" --passwordbox "choose a password for ${USERNAME}" 10 60 3>&1 1>&2 2>&3)
  PASS2=$(whiptail --title "password" --passwordbox "confirm password" 10 60 3>&1 1>&2 2>&3)
  if [ "$PASS1" = "$PASS2" ] && [ -n "$PASS1" ]; then
    break
  fi
  whiptail --title "password" --msgbox "passwords did not match or were empty, try again" 8 60
done
PASSHASH=$(printf '%s' "$PASS1" | mkpasswd -m sha-512 --stdin)
unset PASS1 PASS2

# 6. disk selection

disk_args=()
while read -r name size model; do
  disk_args+=("/dev/$name" "$size $model")
done < <(lsblk -dn -o NAME,SIZE,MODEL -e 7,11 2>/dev/null)

if [ "${#disk_args[@]}" -eq 0 ]; then
  whiptail --title "error" --msgbox "no disks found" 8 50
  exit 1
fi

DISK=$(whiptail --title "select installation disk" --menu \
  "all data on the selected disk will be erased" 18 65 6 \
  "${disk_args[@]}" \
  3>&1 1>&2 2>&3)

# /dev/sdX-style names can point at a different physical disk after a
# reboot if drives are added, removed, or enumerated in a different order,
# by-id names are stable, so use one from here on if this disk has one
if [ -d /dev/disk/by-id ]; then
  for link in /dev/disk/by-id/*; do
    [ -e "$link" ] || continue
    if [ "$(readlink -f "$link")" = "$(readlink -f "$DISK")" ]; then
      DISK="$link"
      break
    fi
  done
fi

# 7. erase confirmation

CONFIRM=$(whiptail --title "confirm erase" --inputbox \
  "this will erase all data on ${DISK}\n\ntype the disk path (${DISK}) to confirm, or cancel to abort" \
  12 65 3>&1 1>&2 2>&3) || { echo "aborted"; exit 1; }

if [ "$CONFIRM" != "$DISK" ]; then
  whiptail --title "aborted" --msgbox "disk name did not match, installation aborted, nothing changed" 8 60
  exit 1
fi

# 8. software profiles, reuses the same preset flow as spiralos-wizard

preset=$(whiptail --title "what will you mainly use this for" --menu \
  "pick a starting preset, you can fine-tune profiles on the next screen" 20 70 7 \
  workstation "general engineering workstation (rust, python, web)" \
  data-science "data science / ml (python, ml)" \
  maker "maker, electronics, embedded, cad/3d printing" \
  embedded "embedded / firmware development" \
  minimal "minimal, no profiles, just the spiralos base" \
  custom "choose profiles manually" \
  3>&1 1>&2 2>&3)

case "$preset" in
  workstation) default_profiles="rust python web" ;;
  data-science) default_profiles="python ml" ;;
  maker) default_profiles="electronics embedded cad" ;;
  embedded) default_profiles="embedded" ;;
  *) default_profiles="" ;;
esac

checklist_args=()
for p in rust python web ml embedded electronics cad; do
  state="OFF"
  for d in $default_profiles; do
    [ "$p" = "$d" ] && state="ON"
  done
  checklist_args+=("$p" "$p profile" "$state")
done

selected=$(whiptail --title "enable profiles" --checklist \
  "space to toggle, enter to confirm" 18 70 7 \
  "${checklist_args[@]}" \
  3>&1 1>&2 2>&3)

# 9. summary

profiles_display=$(echo "$selected" | tr -d '"')
whiptail --title "ready to install" --yesno \
  "hostname,  ${HOSTNAME}\ntimezone,  ${TIMEZONE}\nkeyboard,  ${KEYMAP}\nuser,      ${USERNAME}\ndisk,      ${DISK} (will be erased)\nprofiles,  ${profiles_display:-none}\n\nproceed with installation" \
  16 65 || { echo "aborted"; exit 1; }

# 10. write target flake + install

step "writing target system configuration"

cat > "$WORKDIR/disko-config.nix" <<'EOF'
{
  disko.devices.disk.main = {
    device = "/dev/placeholder";
    type = "disk";
    content = {
      type = "gpt";
      partitions = {
        ESP = {
          size = "512M";
          type = "EF00";
          content = {
            type = "filesystem";
            format = "vfat";
            mountpoint = "/boot";
          };
        };
        root = {
          size = "100%";
          content = {
            type = "filesystem";
            format = "ext4";
            mountpoint = "/";
          };
        };
      };
    };
  };
}
EOF

# copy-pasted from wizard.sh's version of this loop, keep them in sync if you
# touch the profile-writing logic in either place
for p in $selected; do
  p="${p//\"/}"
  echo "  spiralos.profiles.${p}.enable = true;" >> "$WORKDIR/spiralos-profiles.nix.tmp"
done
{
  echo "{ ... }:"
  echo "{"
  cat "$WORKDIR/spiralos-profiles.nix.tmp" 2>/dev/null || true
  echo "}"
} > "$WORKDIR/spiralos-profiles.nix"
rm -f "$WORKDIR/spiralos-profiles.nix.tmp"

cat > "$WORKDIR/host-config.nix" <<EOF
{ config, pkgs, ... }:
{
  networking.hostName = "${HOSTNAME}";
  time.timeZone = "${TIMEZONE}";
  console.keyMap = "${KEYMAP}";
  i18n.defaultLocale = "en_US.UTF-8";

  users.users.${USERNAME} = {
    isNormalUser = true;
    extraGroups = [ "wheel" "networkmanager" ];
    hashedPassword = "${PASSHASH}";
  };

  # gives this account serial/usb access, only matters if a maker profile is on
  spiralos.maker.user = "${USERNAME}";

  boot.loader.systemd-boot.enable = true;
  boot.loader.efi.canTouchEfiVariables = true;
  nixpkgs.hostPlatform = "x86_64-linux";
  system.stateVersion = "25.11";
}
EOF

# nixpkgs is fetched from github here rather than vendored on the iso -
# vendoring the full source tree used to add ~150-200MB to the image for a
# guarantee (offline install) most people booting a fresh install disk don't
# need, since they're already online to have downloaded the iso in the first
# place. needs network access during this step now.
cat > "$WORKDIR/flake.nix" <<EOF
{
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  inputs.spiralos.url = "path:${FLAKE_SELF}";
  outputs = { self, nixpkgs, spiralos, ... }: {
    nixosConfigurations.target = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        spiralos.nixosModules.default
        (${DISKO_SRC} + "/module.nix")
        ./disko-config.nix
        ./host-config.nix
        ./spiralos-profiles.nix
      ];
    };
  };
}
EOF

# nixpkgs is now fetched over the network (see the flake.nix above), so
# check connectivity before disko-install gets partway through evaluating
# and fails with a much more confusing error
while ! curl -fsS --max-time 5 https://cache.nixos.org >/dev/null 2>&1; do
  if whiptail --title "no network connection" --yesno \
      "spiralos needs an internet connection to fetch nixpkgs before it can install\n\nopen nmtui to connect to wifi/ethernet now?" \
      12 65; then
    nmtui
  else
    echo "aborted, no network"
    exit 1
  fi
done

step "partitioning ${DISK} and installing spiralos, this will take a while"

# WORKDIR (and this log with it) is removed by the EXIT trap, so on failure
# below we copy it somewhere that survives past this script exiting
INSTALL_LOG="$WORKDIR/disko-install.log"
if ! disko-install \
  --mode format \
  --flake "${WORKDIR}#target" \
  --disk main "${DISK}" \
  --write-efi-boot-entries \
  2>&1 | tee "$INSTALL_LOG"; then

  SAVED_LOG="/root/spiralos-install-failed-$(date +%Y%m%d-%H%M%S).log"
  if cp "$INSTALL_LOG" "$SAVED_LOG" 2>/dev/null; then
    log_note="log saved to ${SAVED_LOG}"
  else
    SAVED_LOG=""
    log_note="couldn't save the disko-install log (is /root writable), only the cleanup steps below will be logged"
  fi

  whiptail --title "install failed" --msgbox \
    "disko-install failed partway through\n\n${DISK} is likely left half-partitioned, not its original state, and not a working spiralos install either\n\n${log_note}\n\nspiralos will now try to wipe ${DISK} back to a blank disk, so you're not left to clean up a broken partition table by hand" \
    16 70

  # disko-install may have gotten as far as mounting the new filesystems at
  # /mnt before failing (eg during the nixos-install phase), which would
  # make the disk "busy" for wipefs/sgdisk below - best-effort unmount first
  CLEANUP_LOG="${SAVED_LOG:-/dev/null}"
  umount_note=""
  if ! umount -R /mnt >>"$CLEANUP_LOG" 2>&1; then
    umount_note="\n\nnote, unmounting /mnt first didn't fully succeed, ${DISK} may still have busy partitions even if the wipe below reports success, double check with lsblk before trusting it's really blank"
  fi

  # you already typed ${DISK} back to confirm erasing it earlier in this
  # script, so wiping its partition table on failure isn't a new destructive
  # action - it's finishing the erase you already agreed to, instead of
  # leaving the disk half-done
  if wipefs --all --force "$DISK" >>"$CLEANUP_LOG" 2>&1 \
      && sgdisk --zap-all "$DISK" >>"$CLEANUP_LOG" 2>&1; then
    whiptail --title "disk cleaned up" --msgbox \
      "${DISK} has been wiped back to blank, you can re-run the installer to try again\n\n${log_note}${umount_note}" \
      14 70
  else
    whiptail --title "cleanup also failed" --msgbox \
      "automatic cleanup of ${DISK} failed too\n\nto manually clean it up before retrying, run\n\n  umount -R /mnt\n  wipefs --all --force ${DISK}\n  sgdisk --zap-all ${DISK}\n\n${log_note}" \
      16 70
  fi

  exit 1
fi

# 11. persist a rebuildable config on the target
#
# disko-install only partitions the disk and installs the built closure,
# it doesn't leave anything under /mnt/etc/nixos - without this step the
# freshly installed system would have no flake to nixos-rebuild from at
# all. nixpkgs and disko are well-known public flakes so they're
# referenced by url here instead of copied (nixpkgs alone is multiple
# gigabytes, copying it per install would be wasteful), spiralos itself
# is vendored as a plain directory copy since it lives in a subdirectory
# of a personal git repo with no guaranteed-current public url to lean on

step "writing persistent configuration to the target disk"

mkdir -p /mnt/etc/nixos
cp -r "$FLAKE_SELF" /mnt/etc/nixos/spiralos-src
sed "s|/dev/placeholder|${DISK}|" "$WORKDIR/disko-config.nix" > /mnt/etc/nixos/disko-config.nix
cp "$WORKDIR/host-config.nix" /mnt/etc/nixos/host-config.nix
cp "$WORKDIR/spiralos-profiles.nix" /mnt/etc/nixos/spiralos-profiles.nix

cat > /mnt/etc/nixos/flake.nix <<'EOF'
{
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  inputs.disko = {
    url = "github:nix-community/disko";
    inputs.nixpkgs.follows = "nixpkgs";
  };
  inputs.spiralos.url = "path:./spiralos-src";
  outputs = { self, nixpkgs, disko, spiralos, ... }: {
    nixosConfigurations.__HOSTNAME__ = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        spiralos.nixosModules.default
        disko.nixosModules.disko
        ./disko-config.nix
        ./host-config.nix
        ./spiralos-profiles.nix
      ];
    };
  };
}
EOF
# nixos-rebuild auto-selects nixosConfigurations.<hostname> when run with
# no --flake target, matching the hostname makes plain `nixos-rebuild
# switch` on the installed system just work without extra flags
sed -i "s/__HOSTNAME__/${HOSTNAME}/" /mnt/etc/nixos/flake.nix

# skip the wizard, we already asked for profiles here
mkdir -p /mnt/etc/spiralos
date > /mnt/etc/spiralos/wizard-done

whiptail --title "install complete" --msgbox \
  "spiralos has been installed to ${DISK}\n\nremove the installation media and reboot to start using it" \
  10 65

if whiptail --title "reboot now" --yesno "reboot now" 8 40; then
  reboot
fi
