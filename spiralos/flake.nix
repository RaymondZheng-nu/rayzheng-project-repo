{
  description = "SpiralOS - an engineer-focused NixOS spin";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    nixos-generators = {
      url = "github:nix-community/nixos-generators";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, nixos-generators, ... }@inputs:
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
        # `nix build .#iso`
        iso = nixos-generators.nixosGenerate {
          inherit system;
          specialArgs = { inherit inputs self; };
          modules = baseModules ++ [ ./iso/configuration.nix ];
          format = "iso";
        };

        spiralos-wizard =
          (import nixpkgs { inherit system; overlays = [ overlay ]; }).spiralos-wizard;

        spiralos-installer =
          (import nixpkgs { inherit system; overlays = [ overlay ]; }).spiralos-installer;
      };
    };
}
