# spiralos maker base: fixes the hardware-access papercuts that every maker
# rediscovers the hard way on a stock linux install. this is the part that
# makes "my dev board just works" true out of the box — not package lists.
{ config, lib, pkgs, ... }:

with lib;

let
  cfg = config.spiralos.maker;
in {
  options.spiralos.maker = {
    enable = mkEnableOption "SpiralOS maker hardware-access defaults (serial/USB, udev, groups)";

    user = mkOption {
      type = types.nullOr types.str;
      default = null;
      description = ''
        Username to auto-add to the dialout/plugdev groups so it can access
        serial and USB devices without root. Leave null if you manage groups
        yourself. The installer sets this to the account it creates.
      '';
    };
  };

  config = mkIf cfg.enable {
    # 1. modemmanager probes /dev/ttyusb* and /dev/ttyacm* on plug-in, which
    #    corrupts arduino/esp flashing. makers universally turn it off.
    systemd.services.ModemManager.enable = false;

    # 2. brltty claims cheap ch340/ch341 usb-serial adapters as braille
    #    displays, making the device vanish. mask it.
    systemd.services.brltty.enable = mkForce false;

    # 3. non-root access to serial + usb dev boards.
    users.groups.plugdev = { };

    # packages that ship their own udev rules for common debug probes/boards.
    services.udev.packages = with pkgs; [
      openocd     # st-link, j-link, cmsis-dap, ftdi probes
      stlink      # st-link v2/v3
    ];

    # extra rules for boards that don't ship their own.
    services.udev.extraRules = ''
      # Raspberry Pi RP2040 in BOOTSEL / picoprobe
      SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", MODE="0660", GROUP="plugdev", TAG+="uaccess"
      # FTDI USB-serial (FT232/FT2232/etc.)
      SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", MODE="0660", GROUP="plugdev", TAG+="uaccess"
      # CH340/CH341 USB-serial
      SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", MODE="0660", GROUP="plugdev", TAG+="uaccess"
      # CP210x USB-serial (many ESP32 boards)
      SUBSYSTEM=="usb", ATTRS{idVendor}=="10c4", MODE="0660", GROUP="plugdev", TAG+="uaccess"
      # Arduino (official VID)
      SUBSYSTEM=="usb", ATTRS{idVendor}=="2341", MODE="0660", GROUP="plugdev", TAG+="uaccess"
    '';

    # 4. auto-add the named user to the device-access groups.
    users.users = mkIf (cfg.user != null) {
      ${cfg.user}.extraGroups = [ "dialout" "plugdev" ];
    };
  };
}
