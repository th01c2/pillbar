{
  description = "pillbar - floating pill-shaped Wayland status bar for Hyprland";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f:
        nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));
    in
    {
      packages = forAllSystems (pkgs: rec {
        pillbar = pkgs.callPackage ./default.nix { };
        default = pillbar;
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            wayland-scanner
            wayland-protocols
          ];

          buildInputs = with pkgs; [
            wayland
            cairo
            pango
            fontconfig
            freetype
            systemd
            libnl
            libpulseaudio
          ];

          shellHook = ''
            echo "pillbar dev shell: $(cmake --version | head -1)"
          '';
        };
      });

    };
}
