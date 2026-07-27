{ config, lib, pkgs, ... }:

with lib;

{
  options.spiralos.profiles.electronics.enable =
    mkEnableOption "Electronics / PCB design profile";

  config = mkIf config.spiralos.profiles.electronics.enable {
    # pcb/eda work plus bench instruments (logic analyzers, scopes via sigrok).
    environment.systemPackages = with pkgs; [
      kicad
      sigrok-cli
      pulseview
    ];

    # logic analyzers / scopes need the maker udev + group setup to be usable
    # without root.
    spiralos.maker.enable = true;
  };
}
