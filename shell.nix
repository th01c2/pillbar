{ pkgs ? import <nixpkgs> { } }:
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    pkg-config
    wayland-scanner
  ];

  buildInputs = with pkgs; [
    wayland
    cairo
    pango
    systemd
    libpulseaudio
    curl
  ];

  shellHook = ''
    echo "pillbar dev shell: $(cmake --version | head -1)"
  '';
}
