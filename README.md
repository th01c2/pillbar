# pillbar

[![flake](https://img.shields.io/badge/nix-flake-5277C3?logo=nixos&logoColor=white)](flake.nix)
[![wayland](https://img.shields.io/badge/Wayland-layer--shell-00AEEF)](https://wayland.freedesktop.org/)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](src)
[![license](https://img.shields.io/badge/license-MIT-green)](LICENSE)

A **notch**-style status bar for Hyprland: a single black notch that hugs the top
edge of the screen, sizes itself to its content, and reserves exactly the space
it occupies.

Written in C++20 against `wlr-layer-shell`, `wl_shm` and Cairo/Pango with a
single `epoll` loop for everything. No Qt/GTK, no Electron, no scripting
runtime, no config file.

## Highlights

- **Notch, not an overlay.** Flush with the top edge, rounded only at the
  bottom, anchored `TOP|LEFT|RIGHT` with an exclusive zone, so Hyprland reserves
  the strip and tiled windows never slide under it. No window rules needed.
- **Content-sized and softly animated.** Item widths are intrinsic (icon +
  text); the notch is only as wide as it needs to be and eases to its new size
  in ~160 ms when the window title, workspace count or recorder state changes.
- **Event-driven.** Hyprland's event socket, udev, D-Bus signals, PulseAudio
  callbacks and `timerfd`s. Otherwise the process sits in `epoll_wait`.
- **Compiled-in settings.** Palette, fonts, glyphs, geometry and item order live
  in the source; there is no runtime config to keep in sync.

## Items

| Item | Shows | Notes |
| --- | --- | --- |
| Battery | Nerd Font glyph + percent | Green while charging. AC online counts as charging even at a charge limit. 10 %-step icon ladder; udev-driven with an adaptive fallback timer. |
| Volume | Speaker glyph + percent | PulseAudio subscription; muted shows `muted` in a dim shade. |
| Network | Wi-Fi strength or ethernet glyph | NetworkManager over D-Bus. Ethernet wins when both are up, else Wi-Fi 1–4, else `off`. Tooltip: SSID, band, link rate, IP, live up/down rates. |
| Bluetooth | Glyph | BlueZ D-Bus enumeration. |
| Workspaces | Numerals | Only workspaces holding windows or focused, so the item grows as you open more. Left click switches, scroll goes prev/next. |
| Window | App name + title | Title capped at 20 characters, UTF-8 safe; the app name is drawn in a dimmer shade. |
| Recorder | Pulsing red dot | Appears only while a screen recorder runs; left click stops it. |
| Clock | `HH:MM` | Minute-boundary timer; tooltip adds the date, live seconds and CPU/GPU. |

## Requirements

Hyprland (workspaces, window, dispatchers), NetworkManager (network), PipeWire
or PulseAudio (volume), BlueZ (Bluetooth), and a Nerd Font for the icons. The
flake pulls in the rest: `wayland-client`, `cairo`, `pango`, `libsystemd`,
`libudev`, `libpulse`, `fontconfig`.

## Build and run

### With the flake (recommended)

```sh
nix build .#pillbar
pkill -x pillbar; setsid -f ./result/bin/pillbar
```

`nix build` produces the runnable binary with its full runtime closure.

### Iterating on the source

```sh
nix develop
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`build/pillbar` links against the dev shell's glibc, so run it from inside
`nix develop` — or use `nix build` for a standalone binary.

### From your own NixOS flake

```nix
inputs.pillbar.url = "github:YOURUSER/Sebar";

# ...
environment.systemPackages = [ inputs.pillbar.packages.${pkgs.system}.default ];
```

Time-savers:

- While developing, point the input at your checkout —
  `url = "path:/home/you/Sebar"` — and `nixos-rebuild` picks up the working tree
  without committing.
- With a `github:` input, run `nix flake update pillbar` after pushing, or the
  lock file keeps you on the previous commit.
- `nix build` only copies files tracked by git, so **new source files must be
  `git add`ed** or the build will not see them.

### Hyprland

```conf
# ~/.config/hypr/pillbar.conf   (source it, or paste inline)
exec-once = pillbar

# Keep the notch crisp: no compositor blur or glass.
layerrule = blur off, pillbar
layerrule = blur off, pillbar-tooltip
layerrule = ignorealpha 0.0, pillbar-tooltip
layerrule = ignorezero, pillbar-tooltip
```

Logging: `PILLBAR_LOG=error|warn|info|debug` (default `warn`).

## Customizing

Everything is compiled in. Edit, rebuild, restart:

```sh
nix build .#pillbar && (pkill -x pillbar; setsid -f ./result/bin/pillbar)
```

| What | Where |
| --- | --- |
| Notch fill, radius, height, top margin | `src/app/config.hpp` — `bar_color`, `notch_radius_px`, `height_px`, `margin_top_frac` |
| Palette (text, dim, workspaces, battery, recorder) | `src/app/config.hpp` — the `Color` members |
| Fonts, sizes, icon font list and glyphs | `src/app/config.hpp` — `fonts`, `icon_fonts`, `*_glyph`, `icon_size_frac` |
| Behaviour (title cap, recorder pulse, workspaces, tooltips) | `src/app/config.hpp` — `window_title_max_chars`, `recorder_pulse_ms`, `min_workspaces`, `tooltip_*` |
| Item order and per-item slots | `Config::order` in `src/app/config.hpp`; slots in `src/app/config.cpp` |

Colours are written as hex through a small helper, so there is no float maths to
do by hand:

```cpp
Color bar_color = rgb(0x100F0F);         // notch fill
Color recorder_color = rgb(0xff3b30);    // bright end of the recorder pulse
Color recorder_color_dim = rgb(0x7a0d08);
```

## Interactions

| Where | Action |
| --- | --- |
| Any item | Hover for 250 ms → tooltip fades in under the notch |
| Volume | Scroll ±5 %, middle click toggles mute |
| Workspaces | Left click switches, scroll cycles |
| Recorder | Left click stops the recording (SIGTERM) |
| Clock / CPU / GPU | Tooltip streams live while hovered |

## How it works

```
 sources ──typed change──► SourceManager (16 ms coalesce) ──► state diff
                                                                  │
                                              intrinsic layout ──► renderer
```

| Part | Transport | Trigger |
| --- | --- | --- |
| Workspaces, window | Hyprland `.socket2.sock` | `workspace(v2)`, `openwindow`, `closewindow`, `movewindow`, `createworkspace`, `destroyworkspace`, `focusedmon`, `monitor*` re-query everything; `activewindow(v2)` re-queries only the window, so a title that rewrites itself does not cost four socket queries. |
| Battery / AC | libudev `power_supply` uevents | Re-reads `/sys/class/power_supply`; one adaptive fallback timer runs only while the battery is not full. |
| Volume | PulseAudio `pa_context_subscribe` | libpulse sockets are driven by a custom `pa_mainloop_api` registered in the epoll loop. |
| Network | NetworkManager D-Bus | `PropertiesChanged` on the daemon, device and access point. |
| Bluetooth | BlueZ D-Bus | `PropertiesChanged` → `GetManagedObjects` re-enumeration. |
| Recorder | `/proc` scan | 2 Hz detection while idle (with a pid→name cache); ~25 Hz only while a recorder is running, driving the red pulse. |
| Clock | `timerfd(CLOCK_REALTIME)` | Absolute deadline on the minute, `TFD_TIMER_CANCEL_ON_SET` so clock jumps resync. |
| CPU / GPU | hover-gated `timerfd` | `/proc/stat`, hwmon, cpufreq, GPU sysfs/NVML — opened only while the tooltip is on screen. |
| Resume | login1 D-Bus | `PrepareForSleep(false)` refreshes every source. |

### Idle cost

On a quiet session the bar idles at well under 1 % of one core and around 30 MiB
RSS. Wakeups come from the 1 Hz stats, the 2 Hz recorder check, the minute
clock, and real events — a window rewriting its title ten times a second wakes
the bar ten times a second, which is event handling rather than polling. Redraws
are limited to the items that actually changed.

## Project layout

```
src/
  app/       event loop, source manager, compiled-in defaults, logging
  model/     shared state structs
  render/    Cairo renderer, text, intrinsic layout
  sources/   Hyprland, power, pulse, network, bluetooth, recorder, clock, stats, GPU
  wayland/   display, layer surface, shm canvas, pointer
protocols/   wayland-scanner XML used to generate the client bindings
hyprland/    example Hyprland snippet
```

## Known issues

- **Bluetooth does not render yet.** The BlueZ `GetManagedObjects` parsing never
  reports an adapter, so the item stays hidden. Everything else is unaffected.
- **Recorder detection** covers `wl-screenrec`, `wf-recorder`,
  `gpu-screen-recorder`, `kooha`, `wl-recorder` and `simplescreenrecorder`. OBS
  is deliberately excluded, because its process exists even when it is not
  recording.

## License

MIT — see [LICENSE](LICENSE).
