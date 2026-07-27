{ config, lib, pkgs, ... }:

with lib;

{
  imports = [
    ./profiles
    ./maker.nix
    ./wizard.nix
  ];

  options.spiralos = {
    enable = mkEnableOption "SpiralOS defaults (branding, base packages, engineer- and maker-friendly settings)" // {
      default = true;
    };
  };

  config = mkIf config.spiralos.enable {
    # core base packages every spiralos install gets, regardless of profile.
    environment.systemPackages = with pkgs; [
      git
      neovim
      ripgrep
      fd
      tmux
      htop
      direnv
    ];

    programs.direnv.enable = true;

    nix.settings.experimental-features = [ "nix-command" "flakes" ];

    environment.etc."motd".text = ''
      Welcome to SpiralOS — a declarative NixOS spin for engineers and makers.
      Pierce the heavens.

      Toggle stacks under `spiralos.profiles.*` (rust, python, web, ml,
      embedded, electronics, cad) and enable `spiralos.maker.enable` for
      out-of-the-box serial/USB dev-board access.
    '';

    system.nixos.distroId = "spiralos";
    system.nixos.distroName = "SpiralOS";
  };
}
