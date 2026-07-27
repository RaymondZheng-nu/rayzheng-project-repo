#!/usr/bin/env bash
set -euo pipefail

# spiralos-installer: a debian-installer-style, step-by-step tty installer
# that runs from the live iso. it partitions/formats a disk (via disko),
# writes a target-system flake, and runs disko-install.
#
# this is destructive: it erases the disk you select. it only proceeds
# past the erase-confirmation step if you explicitly type the disk name.

WORKDIR=$(mktemp -d /root/spiralos-install.XXXXXX)
trap 'rm -rf "$WORKDIR"' EXIT

FLAKE_SELF=/etc/spiralos/flake-self
DISKO_SRC=/etc/spiralos/disko-src
NIXPKGS_SRC=/etc/spiralos/nixpkgs-src

step() {
  whiptail --title "SpiralOS Installer" --infobox "$1" 8 70
  sleep 0.5
}

# --- 1. welcome -------------------------------------------------------

whiptail --title "Welcome to SpiralOS" --msgbox \
  "This installer will walk you through setting up SpiralOS on this machine, step by step:\n\n  1. Keyboard layout\n  2. Hostname\n  3. Timezone\n  4. User account\n  5. Disk selection\n  6. Software profiles\n  7. Install\n\nWARNING: this WILL erase the disk you select. Make sure you have backups." \
  18 72

# --- 2. keyboard layout -------------------------------------------------

KEYMAP=$(whiptail --title "Keyboard layout" --menu "Select your keyboard layout" 18 60 8 \
  us "US English" \
  us-intl "US English (international)" \
  uk "United Kingdom" \
  de "German" \
  fr "French" \
  es "Spanish" \
  dvorak "Dvorak" \
  3>&1 1>&2 2>&3)

# --- 3. hostname ----------------------------------------------------------

HOSTNAME=$(whiptail --title "Hostname" --inputbox \
  "What should this machine be called?" 10 60 "spiralos" \
  3>&1 1>&2 2>&3)

# --- 4. timezone ------------------------------------------------------

TIMEZONE=$(whiptail --title "Timezone" --menu "Select your timezone" 20 60 10 \
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

# --- 5. user account --------------------------------------------------

USERNAME=$(whiptail --title "User account" --inputbox \
  "Choose a username" 10 60 \
  3>&1 1>&2 2>&3)

while true; do
  PASS1=$(whiptail --title "Password" --passwordbox "Choose a password for ${USERNAME}" 10 60 3>&1 1>&2 2>&3)
  PASS2=$(whiptail --title "Password" --passwordbox "Confirm password" 10 60 3>&1 1>&2 2>&3)
  if [ "$PASS1" = "$PASS2" ] && [ -n "$PASS1" ]; then
    break
  fi
  whiptail --title "Password" --msgbox "Passwords did not match or were empty. Try again." 8 60
done
PASSHASH=$(printf '%s' "$PASS1" | mkpasswd -m sha-512 --stdin)
unset PASS1 PASS2

# --- 6. disk selection -------------------------------------------------

disk_args=()
while read -r name size model; do
  disk_args+=("/dev/$name" "$size $model")
done < <(lsblk -dn -o NAME,SIZE,MODEL -e 7,11 2>/dev/null)

if [ "${#disk_args[@]}" -eq 0 ]; then
  whiptail --title "Error" --msgbox "No disks found." 8 50
  exit 1
fi

DISK=$(whiptail --title "Select installation disk" --menu \
  "ALL DATA on the selected disk will be erased." 18 65 6 \
  "${disk_args[@]}" \
  3>&1 1>&2 2>&3)

# --- 7. erase confirmation -----------------------------------------------

CONFIRM=$(whiptail --title "Confirm erase" --inputbox \
  "This will ERASE ALL DATA on ${DISK}.\n\nType the disk path (${DISK}) to confirm, or Cancel to abort." \
  12 65 3>&1 1>&2 2>&3) || { echo "Aborted."; exit 1; }

if [ "$CONFIRM" != "$DISK" ]; then
  whiptail --title "Aborted" --msgbox "Disk name did not match. Installation aborted, nothing was changed." 8 60
  exit 1
fi

# --- 8. software profiles (reuses the same preset flow as spiralos-wizard) --

preset=$(whiptail --title "What will you mainly use this for?" --menu \
  "Pick a starting preset (you can fine-tune profiles on the next screen)" 20 70 7 \
  workstation "General engineering workstation (rust, python, web)" \
  data-science "Data science / ML (python, ml)" \
  maker "Maker: electronics, embedded, CAD/3D printing" \
  embedded "Embedded / firmware development" \
  minimal "Minimal - no profiles, just the SpiralOS base" \
  custom "Choose profiles manually" \
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

selected=$(whiptail --title "Enable profiles" --checklist \
  "Space to toggle, Enter to confirm" 18 70 7 \
  "${checklist_args[@]}" \
  3>&1 1>&2 2>&3)

# --- 9. summary -----------------------------------------------------------

profiles_display=$(echo "$selected" | tr -d '"')
whiptail --title "Ready to install" --yesno \
  "Hostname:  ${HOSTNAME}\nTimezone:  ${TIMEZONE}\nKeyboard:  ${KEYMAP}\nUser:      ${USERNAME}\nDisk:      ${DISK} (WILL BE ERASED)\nProfiles:  ${profiles_display:-none}\n\nProceed with installation?" \
  16 65 || { echo "Aborted."; exit 1; }

# --- 10. write target flake + install -------------------------------------

step "Writing target system configuration..."

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

  # if any maker profile enables spiralos.maker, this account gets serial/usb
  # device access. harmless when maker is disabled (the option is only read
  # when spiralos.maker.enable is true).
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

step "Partitioning ${DISK} and installing SpiralOS (this will take a while)..."

disko-install \
  --mode format \
  --flake "${WORKDIR}#target" \
  --disk main "${DISK}" \
  --write-efi-boot-entries

# skip the post-install wizard since we already asked for profiles here.
mkdir -p /mnt/etc/spiralos
date > /mnt/etc/spiralos/wizard-done

whiptail --title "Install complete" --msgbox \
  "SpiralOS has been installed to ${DISK}.\n\nRemove the installation media and reboot to start using it." \
  10 65

if whiptail --title "Reboot now?" --yesno "Reboot now?" 8 40; then
  reboot
fi
