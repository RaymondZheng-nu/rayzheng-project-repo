{ config, lib, pkgs, ... }:

with lib;

{
  imports = [
    ./profiles
    ./maker.nix
    ./wizard.nix
    ./desktop.nix
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
      spiralos-rebuild
    ];

    programs.direnv.enable = true;

    nix.settings.experimental-features = [ "nix-command" "flakes" ];

    # compressed ram-backed swap, so a low-ram machine can survive a big
    # nixos-rebuild or a heavy build (rust/cad toolchains especially) instead
    # of getting oom-killed, without needing a physical swap partition
    zramSwap.enable = true;

    # keep man pages (actually useful day to day, and cheap - plain text),
    # drop the bulkier stuff nobody on old/low-spec hardware needs: every
    # package's full html doc tree, and the nixos manual (it's online too)
    documentation.doc.enable = false;
    documentation.nixos.enable = false;

    # crimson-themed prompt, matching gl3's border color and the "pierce the
    # heavens" branding - kept to a plain ansi escape, no external prompt
    # tool (starship etc) so it stays fast and dependency-free on login.
    # programs.bash.promptInit (not environment.interactiveShellInit) so
    # this only touches bash - the \[ \] escapes here are bash-specific and
    # would break other shells' prompts if another shell gets enabled later
    programs.bash.promptInit = ''
      PS1='\[\e[1;38;5;196m\]\u@\h\[\e[0m\] \[\e[38;5;218m\]\w\[\e[0m\] \[\e[1;38;5;196m\]❯\[\e[0m\] '
    '';

    # quiet, plymouth-splash boot instead of a wall of kernel text - using
    # plymouth's own stock "spinner" theme rather than a custom one, a
    # hand-written plymouth theme (its own little script language) can't be
    # visually verified without a real framebuffer/VM boot to watch it on,
    # and shipping unverified boot-critical code is a bad trade for a
    # cosmetic feature. if it fails to start, nixos' initrd drops to a
    # normal console same as it always did, this doesn't remove that
    boot.plymouth.enable = true;
    boot.plymouth.theme = "spinner";
    boot.kernelParams = [ "quiet" "splash" ];

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
