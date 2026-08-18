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
        # wlroots doesn't install a header for wlr-layer-shell-v1, gl3
        # generates its own copy at build time from this xml (see Makefile)
        wlr-protocols
        libinput
        libxkbcommon
        pixman
        udev
        libjpeg
      ];
    in {
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "gl3";
        version = "0.1.0";
        src = ./.;
        nativeBuildInputs = [ pkgs.pkg-config ];
        buildInputs = deps;
        makeFlags = [ "DATADIR=${placeholder "out"}/share/gl3" ];
        installPhase = ''
          runHook preInstall
          install -Dm755 gl3 $out/bin/gl3
          install -Dm644 assets/background.jpg $out/share/gl3/background.jpg
          runHook postInstall
        '';
        meta.description = "The SpiralOS Wayland compositor";
      };

      devShells.${system}.default = pkgs.mkShell {
        # foot and fuzzel here are only for trying out the default
        # terminal/launcher/help keybinds, gl3 itself doesn't link against
        # either, it just spawns them as commands (fuzzel over the
        # wlr-layer-shell-v1 protocol gl3 implements, see src/gl3.c)
        packages = deps ++ (with pkgs; [ pkg-config gnumake gdb foot fuzzel bear clang-tools ]);
      };
    };
}
