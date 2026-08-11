{ writeShellApplication, nix, nixos-rebuild, sudo }:

writeShellApplication {
  name = "spiralos-rebuild";
  runtimeInputs = [ nix nixos-rebuild sudo ];
  text = builtins.readFile ./rebuild.sh;
}
