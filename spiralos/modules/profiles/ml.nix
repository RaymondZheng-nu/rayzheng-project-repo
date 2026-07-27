{ config, lib, pkgs, ... }:

with lib;

{
  options.spiralos.profiles.ml.enable = mkEnableOption "Machine learning profile";

  config = mkIf config.spiralos.profiles.ml.enable {
    environment.systemPackages = with pkgs; [
      (python3.withPackages (ps: with ps; [ numpy pandas jupyter scikit-learn ]))
      uv
    ];

    # most users pull gpu-specific tooling (cuda, rocm) themselves via
    # overlays/host config, since it's hardware- and driver-dependent.
  };
}
