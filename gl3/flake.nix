{
  description = "gl3 - the SpiralOS Wayland compositor (wlroots)";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };

      deps = with pkgs; [
        wlroots
        wayland
        wayland-protocols
        wayland-scanner
        libinput
        libxkbcommon
        pixman
        udev
      ];
    in {
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "gl3";
        version = "0.1.0";
        src = ./.;
        nativeBuildInputs = [ pkgs.pkg-config ];
        buildInputs = deps;
        installPhase = ''
          runHook preInstall
          install -Dm755 gl3 $out/bin/gl3
          runHook postInstall
        '';
        meta.description = "The SpiralOS Wayland compositor";
      };

      devShells.${system}.default = pkgs.mkShell {
        packages = deps ++ (with pkgs; [ pkg-config gnumake gdb foot bear clang-tools ]);
      };
    };
}
