{ config, lib, pkgs, ... }:

with lib;

{
  options.spiralos.profiles.rust.enable = mkEnableOption "Rust development profile";

  config = mkIf config.spiralos.profiles.rust.enable {
    environment.systemPackages = with pkgs; [
      rustc
      cargo
      rust-analyzer
      rustfmt
      clippy
      pkg-config
    ];
  };
}
