{ config, lib, pkgs, ... }:

with lib;

{
  options.spiralos.profiles.embedded.enable = mkEnableOption "Embedded/firmware development profile";

  config = mkIf config.spiralos.profiles.embedded.enable {
    environment.systemPackages = with pkgs; [
      # toolchains / build
      platformio
      arduino-cli
      gcc-arm-embedded
      # flashing / debugging
      openocd
      stlink
      dfu-util
      avrdude
      esptool
      # serial consoles
      picocom
      minicom
    ];

    # flashing boards over usb/serial needs the hardware-access papercut fixes.
    spiralos.maker.enable = true;
  };
}
