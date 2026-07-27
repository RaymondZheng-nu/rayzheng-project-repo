# live-iso only: bakes the spiralos flake source and disko's nixos module
# into /etc so spiralos-installer can build+install a target system fully
# offline, then auto-launches the installer on tty1 (debian-installer style).
{ config, lib, pkgs, self, ... }:

{
  environment.etc."spiralos/flake-self".source = self.outPath;
  environment.etc."spiralos/disko-src".source = pkgs.disko.src;
  environment.etc."spiralos/nixpkgs-src".source = pkgs.path;

  environment.systemPackages = [ pkgs.spiralos-installer pkgs.disko ];

  systemd.services.spiralos-installer = {
    description = "SpiralOS installer";
    after = [ "getty@tty1.service" ];
    conflicts = [ "getty@tty1.service" ];
    serviceConfig = {
      Type = "oneshot";
      ExecStart = "${pkgs.spiralos-installer}/bin/spiralos-installer";
      StandardInput = "tty";
      StandardOutput = "tty";
      TTYPath = "/dev/tty1";
      TTYReset = true;
      TTYVHangup = true;
      TTYVTDisallocate = true;
      Restart = "no";
    };
    wantedBy = [ "multi-user.target" ];
  };
}
