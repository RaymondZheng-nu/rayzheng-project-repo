{ config, lib, pkgs, ... }:

with lib;

{
  options.spiralos.profiles.python.enable = mkEnableOption "Python development profile";

  config = mkIf config.spiralos.profiles.python.enable {
    environment.systemPackages = with pkgs; [
      (python3.withPackages (ps: with ps; [ pip virtualenv ]))
      python3Packages.black
      pyright
      uv
    ];
  };
}
