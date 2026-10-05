{ pkgs ? import <nixpkgs> { } }:
pkgs.mkShell {
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
}
