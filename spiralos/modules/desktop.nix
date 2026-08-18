# optional gl3 desktop session - early and opt-in, off by default so the
# base install stays a lean headless/cli system. only wired up for
# spiralos's own in-repo flake right now (nixosConfigurations.live), not
# for the offline installer's generated configs, since gl3 has no public
# remote yet for those to fetch - see the comment on the gl3 input in
# flake.nix
{ config, lib, pkgs, inputs, ... }:

with lib;

let
  cfg = config.spiralos.desktop.gl3;
  gl3Pkg = inputs.gl3.packages.${pkgs.stdenv.hostPlatform.system}.default;
in {
  options.spiralos.desktop.gl3.enable =
    mkEnableOption "gl3 as the graphical login session (early, opt-in - gl3 is still being built out)";

  config = mkIf cfg.enable {
    environment.systemPackages = [ gl3Pkg ];

    # tuigreet: text-mode greeter, no gtk/qt toolkit dependency, matches
    # gl3's own no-frills approach and keeps this option cheap to turn on
    services.greetd = {
      enable = true;
      settings.default_session = {
        command = "${pkgs.tuigreet}/bin/tuigreet --time --remember --cmd ${gl3Pkg}/bin/gl3";
        user = "greeter";
      };
    };
  };
}
