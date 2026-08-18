{ writeShellApplication, newt, util-linux, gptfdisk, mkpasswd, disko, gnused, gnugrep, coreutils, curl, networkmanager }:

writeShellApplication {
  name = "spiralos-installer";
  # gptfdisk (sgdisk) and util-linux (wipefs) are for cleaning up a
  # half-partitioned disk if disko-install fails partway through.
  # curl and networkmanager (nmtui) are for the network-connectivity check
  # before disko-install, now that nixpkgs is fetched over the network
  # instead of vendored on the iso
  runtimeInputs = [ newt util-linux gptfdisk mkpasswd disko gnused gnugrep coreutils curl networkmanager ];
  text = builtins.readFile ./installer.sh;
}
