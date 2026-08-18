# live-iso only: bakes the spiralos flake source and disko's nixos module
# into /etc so spiralos-installer can build+install a target system without
# vendoring all of nixpkgs too, then auto-launches the installer on tty1
# (debian-installer style).
{ config, lib, pkgs, self, ... }:

{
  environment.etc."spiralos/flake-self".source = self.outPath;
  environment.etc."spiralos/disko-src".source = pkgs.disko.src;
  # nixpkgs itself is intentionally NOT vendored here (unlike disko/spiralos
  # above) - the full source tree adds ~150-200MB to the iso for offline
  # install, spiralos-installer instead fetches it from github at install
  # time and checks connectivity first, see installer.sh

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
