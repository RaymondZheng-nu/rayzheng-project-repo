{ writeShellApplication, newt, pciutils, nixos-rebuild, gnused, gnugrep, coreutils }:

writeShellApplication {
  name = "spiralos-wizard";
  runtimeInputs = [ newt pciutils nixos-rebuild gnused gnugrep coreutils ];
  text = builtins.readFile ./wizard.sh;
}
