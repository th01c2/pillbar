{ lib
, stdenv
, cmake
, ninja
, pkg-config
, wayland
, wayland-scanner
, cairo
, pango
, systemd
, libnl
, libpulseaudio
, fontconfig
, freetype
}:

stdenv.mkDerivation {
  pname = "pillbar";
  version = "0.1.0";
  src = lib.cleanSourceWith {
    src = ./.;
    filter = path: _type:
      let base = baseNameOf (toString path);
      in !(builtins.elem base [ "build" "build-clang" "result" ]);
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    wayland-scanner
  ];

  buildInputs = [
    wayland
    cairo
    pango
    fontconfig
    freetype
    systemd
    libnl
    libpulseaudio
  ];

  cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];

  meta = with lib; {
    description = "Floating pill-shaped Wayland status bar for Hyprland";
    longDescription = ''
      An event-driven layer-shell status bar rendering a detached stadium
      (pill) with battery, volume, Wi-Fi, Bluetooth, workspaces, active window
      and a clock, plus hover tooltips.
    '';
    license = licenses.mit;
    platforms = platforms.linux;
    mainProgram = "pillbar";
  };
}
