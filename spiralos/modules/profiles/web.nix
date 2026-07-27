{ config, lib, pkgs, ... }:

with lib;

{
  options.spiralos.profiles.web.enable = mkEnableOption "Web development profile";

  config = mkIf config.spiralos.profiles.web.enable {
    environment.systemPackages = with pkgs; [
      nodejs_22
      nodePackages.pnpm
      nodePackages.typescript-language-server
      deno
      bun
    ];
  };
}
