{ writeShellApplication, newt, util-linux, mkpasswd, disko, gnused, gnugrep, coreutils }:

writeShellApplication {
  name = "spiralos-installer";
  runtimeInputs = [ newt util-linux mkpasswd disko gnused gnugrep coreutils ];
  text = builtins.readFile ./installer.sh;
}
