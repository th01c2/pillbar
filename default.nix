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
, libpulseaudio
, curl
}:

stdenv.mkDerivation {
  pname = "pillbar";
  version = "0.1.0";
  src = lib.cleanSourceWith {
    src = ./.;
    filter = path: _type:
      let base = baseNameOf (toString path);
      in !(base == "build" || base == ".git" || lib.hasPrefix "build-" base
        || lib.hasPrefix "result" base);
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
    systemd
    libpulseaudio
    curl
  ];

  cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];

  meta = with lib; {
    description = "Notch-style Wayland status bar for Hyprland";
    longDescription = ''
      An event-driven layer-shell status bar with battery, volume, Wi-Fi,
      Bluetooth, workspaces, recorder and clock items, plus hover tooltips.
    '';
    license = licenses.mit;
    platforms = platforms.linux;
    mainProgram = "pillbar";
  };
}
