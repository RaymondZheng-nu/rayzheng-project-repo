{ config, lib, pkgs, ... }:

with lib;

{
  options.spiralos.profiles.cad.enable =
    mkEnableOption "CAD / 3D printing profile";

  config = mkIf config.spiralos.profiles.cad.enable {
    # parametric cad plus slicers for fdm 3d printing.
    environment.systemPackages = with pkgs; [
      freecad
      openscad
      prusa-slicer
      orca-slicer
    ];
  };
}
