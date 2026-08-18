{
  description = "SpiralOS - an engineer-focused NixOS spin";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

    # gl3 has no public remote yet (it's a sibling folder in the same
    # personal monorepo), so this only works when spiralos is evaluated
    # from a checkout that has gl3 next to it, like this repo - that's why
    # this stays out of the offline installer's generated flakes (see
    # pkgs/spiralos-installer/installer.sh), only the in-repo
    # nixosConfigurations.live below can use it, via spiralos.desktop.gl3.enable
    gl3.url = "path:../gl3";
  };

  outputs = { self, nixpkgs, ... }@inputs:
    let
      system = "x86_64-linux";
      overlay = import ./overlays;
      baseModules = [
        { nixpkgs.overlays = [ overlay ]; }
        ./modules/spiralos.nix
      ];
    in {
      overlays.default = overlay;

      nixosModules = {
        default = ./modules/spiralos.nix;
        profiles = ./modules/profiles;
      };

      # `nixos-rebuild switch --flake .#live` or install from the generated iso.
      nixosConfigurations.live = nixpkgs.lib.nixosSystem {
        inherit system;
        specialArgs = { inherit inputs self; };
        modules = baseModules ++ [ ./iso/configuration.nix ];
      };

      packages.${system} = {
        # `nix build .#iso` - the installation-cd-minimal module imported by
        # ./iso/configuration.nix is the native nixpkgs iso builder, so this
        # just reuses the nixosConfigurations.live evaluation above instead
        # of pulling in nixos-generators as a separate dependency for what's
        # already a single nixpkgs-native attribute
        iso = self.nixosConfigurations.live.config.system.build.isoImage;

        spiralos-wizard =
          (import nixpkgs { inherit system; overlays = [ overlay ]; }).spiralos-wizard;

        spiralos-installer =
          (import nixpkgs { inherit system; overlays = [ overlay ]; }).spiralos-installer;

        spiralos-rebuild =
          (import nixpkgs { inherit system; overlays = [ overlay ]; }).spiralos-rebuild;
      };
    };
}
