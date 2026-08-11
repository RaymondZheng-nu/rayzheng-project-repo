#!/usr/bin/env bash
set -euo pipefail

# step by step tty installer, runs from the live iso
# partitions the disk, writes a flake, runs disko-install
#
# TODO: destructive, erases the selected disk, only proceeds if you type
# the disk name back to confirm, no other safety net exists yet

WORKDIR=$(mktemp -d /root/spiralos-install.XXXXXX)
trap 'rm -rf "$WORKDIR"' EXIT

FLAKE_SELF=/etc/spiralos/flake-self
DISKO_SRC=/etc/spiralos/disko-src
NIXPKGS_SRC=/etc/spiralos/nixpkgs-src

step() {
  whiptail --title "spiralos installer" --infobox "$1" 8 70
  sleep 0.5
}

# 1. welcome

whiptail --title "welcome to spiralos" --msgbox \
  "this installer will walk you through setting up spiralos, step by step:\n\n  1. keyboard layout\n  2. hostname\n  3. timezone\n  4. user account\n  5. disk selection\n  6. software profiles\n  7. install\n\nwarning, this WILL erase the disk you select, make sure you have backups." \
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

HOSTNAME=$(whiptail --title "hostname" --inputbox \
  "what should this machine be called?" 10 60 "spiralos" \
  3>&1 1>&2 2>&3)

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

USERNAME=$(whiptail --title "user account" --inputbox \
  "choose a username" 10 60 \
  3>&1 1>&2 2>&3)

while true; do
  PASS1=$(whiptail --title "password" --passwordbox "choose a password for ${USERNAME}" 10 60 3>&1 1>&2 2>&3)
  PASS2=$(whiptail --title "password" --passwordbox "confirm password" 10 60 3>&1 1>&2 2>&3)
  if [ "$PASS1" = "$PASS2" ] && [ -n "$PASS1" ]; then
    break
  fi
  whiptail --title "password" --msgbox "passwords did not match or were empty, try again." 8 60
done
PASSHASH=$(printf '%s' "$PASS1" | mkpasswd -m sha-512 --stdin)
unset PASS1 PASS2

# 6. disk selection

disk_args=()
while read -r name size model; do
  disk_args+=("/dev/$name" "$size $model")
done < <(lsblk -dn -o NAME,SIZE,MODEL -e 7,11 2>/dev/null)

if [ "${#disk_args[@]}" -eq 0 ]; then
  whiptail --title "error" --msgbox "no disks found." 8 50
  exit 1
fi

DISK=$(whiptail --title "select installation disk" --menu \
  "ALL DATA on the selected disk will be erased." 18 65 6 \
  "${disk_args[@]}" \
  3>&1 1>&2 2>&3)

# 7. erase confirmation

CONFIRM=$(whiptail --title "confirm erase" --inputbox \
  "this will ERASE ALL DATA on ${DISK}.\n\ntype the disk path (${DISK}) to confirm, or cancel to abort." \
  12 65 3>&1 1>&2 2>&3) || { echo "aborted."; exit 1; }

if [ "$CONFIRM" != "$DISK" ]; then
  whiptail --title "aborted" --msgbox "disk name did not match, installation aborted, nothing changed." 8 60
  exit 1
fi

# 8. software profiles, reuses the same preset flow as spiralos-wizard

preset=$(whiptail --title "what will you mainly use this for?" --menu \
  "pick a starting preset, you can fine-tune profiles on the next screen" 20 70 7 \
  workstation "general engineering workstation (rust, python, web)" \
  data-science "data science / ML (python, ml)" \
  maker "maker, electronics, embedded, CAD/3D printing" \
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
  "hostname:  ${HOSTNAME}\ntimezone:  ${TIMEZONE}\nkeyboard:  ${KEYMAP}\nuser:      ${USERNAME}\ndisk:      ${DISK} (WILL BE ERASED)\nprofiles:  ${profiles_display:-none}\n\nproceed with installation?" \
  16 65 || { echo "aborted."; exit 1; }

# 10. write target flake + install

step "writing target system configuration..."

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

cat > "$WORKDIR/flake.nix" <<EOF
{
  inputs.nixpkgs.url = "path:${NIXPKGS_SRC}";
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

step "partitioning ${DISK} and installing spiralos, this will take a while..."

disko-install \
  --mode format \
  --flake "${WORKDIR}#target" \
  --disk main "${DISK}" \
  --write-efi-boot-entries

# TODO: no rollback path if disko-install fails partway through - you're left
# with a half-partitioned disk and have to clean it up by hand. haven't hit
# this in testing but it's the scariest part of this script.

# skip the wizard, we already asked for profiles here
mkdir -p /mnt/etc/spiralos
date > /mnt/etc/spiralos/wizard-done

whiptail --title "install complete" --msgbox \
  "spiralos has been installed to ${DISK}.\n\nremove the installation media and reboot to start using it." \
  10 65

if whiptail --title "reboot now?" --yesno "reboot now?" 8 40; then
  reboot
fi
