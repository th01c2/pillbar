# pillbar

A floating, detached, pill-shaped (stadium) top status bar for Wayland/Hyprland,
written in C++20 with `wlr-layer-shell`, `wl_shm` + Cairo/Pango and a single
event-driven `epoll` loop. Tiles: battery, volume, Wi-Fi, Bluetooth, workspaces,
active window and a 24-hour clock, with hover tooltips on every item.

The bar is a plain `wl_surface` painted with Cairo into `wl_shm` buffers; there
is no Qt/GTK/Electron and no scripting runtime. Redraws happen only when state
changes, and the process makes **zero wakeups while idle** except the
once-per-minute clock tick.

## Layout assumption

The reference screenshot anchors the five original groups at fixed percentages
of the bar width. Adding the Wi-Fi and Bluetooth glyphs needs a little more
room, so by default the pill is widened from ~26% to **30%** of the output width
and the group fractions are shifted slightly (all values live in
`config.toml` under `[items.*]`). Everything else matches the reference: stadium
radius = height/2, ~30px tall, ~1.2% top margin, flat opaque `#202020`, pixel
glyphs, AA-disabled text.

## Build and run (NixOS)

```sh
nix develop                      # or: nix-shell
cmake -B build -G Ninja
cmake --build build
./build/pillbar                  # run it (needs a running Wayland session)
```

Install as a package:

```sh
nix build .#                     # builds the `pillbar` package
# or, without flakes:
nix-build -E 'with import <nixpkgs> {}; callPackage ./default.nix {}'
```

Run it under Hyprland (see `hyprland/pillbar.conf`):

```conf
exec-once = pillbar
layerrule = blur off, pillbar
layerrule = blur off, pillbar-tooltip
layerrule = ignorezero, pillbar-tooltip
```

Logging is controlled by `PILLBAR_LOG=error|warn|info|debug` (default `warn`).

## Configuration

The config file is `$XDG_CONFIG_HOME/pillbar/config.toml` (default
`~/.config/pillbar/config.toml`). Copy `config/config.toml` there. It is watched
with inotify and reloaded live on save. Every option is documented in the file:
bar geometry/colour/shadow, font family list + size + letter spacing, palette,
per-item `x0`/`x1`/`enabled`, tooltip delay/radius/padding and behaviour toggles.

## Interactions

* Hover any item for 250ms → tooltip fades in below the pill at that item.
* Volume: scroll = ±5%, middle click = toggle mute.
* Workspaces: left click switches, scroll cycles `workspace e±1`.
* Clock tooltip shows full date/time with live seconds; the bar clock updates
  only once per minute.
* CPU/GPU appear inside the clock/system tooltip and are sampled only while the
  tooltip is open.

## How each source gets its events (no polling)

| Item | Transport | Events |
| --- | --- | --- |
| Hyprland (workspaces, window, monitors) | unix socket `.socket2.sock`, non-blocking, line-parsed, reconnect w/ backoff | `workspace(v2)`, `activewindow(v2)`, `openwindow`, `closewindow`, `movewindow`, `createworkspace`, `destroyworkspace`, `focusedmon`, `monitoradded/removed`, `fullscreen`. `.socket.sock` is used **only** for initial `j/...` queries and `dispatch`. `hyprctl` is never polled. |
| Battery / AC | libudev `NETLINK_KOBJECT_UEVENT` monitor on subsystem `power_supply` | uevent → re-read `/sys/class/power_supply/*`. Because some firmware emits sparse capacity uevents, a **single adaptive timerfd** is armed only while charging/discharging and not full (60s, 30s below 20%) and disarmed otherwise. |
| Volume | PulseAudio `pa_context_subscribe` (sink + server) | The libpulse sockets are registered in the epoll loop through a custom `pa_mainloop_api` (`src/sources/pulse.cpp`). No `pactl` polling. |
| Wi-Fi | **NetworkManager over D-Bus** (chosen path over raw nl80211) | `PropertiesChanged` signals on the NM daemon, wireless device and access point. Signal strength is re-read only after such a signal; IP address is read with `getifaddrs` only when the tooltip opens. |
| Bluetooth | BlueZ D-Bus | `PropertiesChanged` and `InterfacesAdded/Removed`; a signal triggers `GetManagedObjects` re-enumeration. |
| Clock | `timerfd(CLOCK_REALTIME)` | Armed absolutely to the next minute boundary with `TFD_TIMER_CANCEL_ON_SET`, so NTP/manual clock changes re-sync it. |
| CPU / GPU | hover-gated `timerfd` | `/proc/stat` deltas + hwmon + cpufreq, and amdgpu sysfs / NVML-via-dlopen / i915+ xe hwmon. Sampled **only** while the relevant tooltip is open; disarmed on leave. |
| Resume | D-Bus `org.freedesktop.login1` | `PrepareForSleep(false)` forces a full refresh of every source. |

Every source compares its new value against the previous one and only emits a
typed `Item` change when it actually changed; the `SourceManager` coalesces
bursts with a single ~16ms `timerfd`. The renderer then damages only the
affected item rectangles.

GPU vendor detection reads hwmon `name` files (`k10temp`/`coretemp`/`zenpower`
for CPU, `amdgpu`/`nvidia`/`i915`/`xe` for GPU) at startup and on udev hwmon
changes — never hardcoded `hwmonN` paths.

## Architecture

```
Source ──typed change──► SourceManager (16ms debounce) ──► State store (diff)
                                                              │
                                                   Layout engine ──► Renderer
```

Sources never touch rendering; the renderer never reads fds. Everything runs in
one thread on one `epoll` loop (Wayland fd, Hyprland sockets, netlink monitor,
D-Bus fd, PulseAudio fds, inotify, timerfds, signalfd).

## Verifying idle behaviour (zero polling)

1. Start the bar, then leave it untouched for a minute:

   ```sh
   PILLBAR_LOG=info ./build/pillbar &
   PID=$!
   strace -c -p $PID     # press Ctrl-C after ~30s
   ```

   You should see the process almost entirely in `epoll_wait`, with no repeated
   `clock_gettime`/`read`/`recv` polling. The only scheduled wakeup is the
   minute-aligned clock `timerfd`.

2. Count wakeups:

   ```sh
   powertop --time=60        # or: perf stat -p $PID -e context-switches sleep 60
   ```

   Idle wakeups should be ~1/minute. Nothing else fires unless a real event
   (battery, volume, network, Hyprland, pointer) arrives. On a *busy* session
   you will additionally see one wakeup per real event — e.g. an application
   that rewrites its window title ten times a second makes the Hyprland socket
   readable that often. That is correct event handling, not polling: the bar
   reads the socket, ignores the irrelevant event and never redraws.

3. Confirm no sampling when not hovering: `strace -f -p $PID -e openat` and make
   sure `/proc/stat`, `hwmon` and GPU sysfs files are only opened while a
   tooltip is on screen.

## Self-review checklist

* [x] **Zero polling for events** — every source is fd/event driven; the only
      timers are the minute clock, the 16ms debounce, the battery fallback
      (disarmed when idle), and hover-gated sampling/fade timers.
* [x] **Redraw only on dirty** — `App::on_state_change` redraws only the changed
      items' damaged rectangles (`wl_surface_damage_buffer`); idle → no commits.
* [x] **Tooltips on all items** — pointer enter/motion/leave hit-test every item
      rect; each item has a dedicated tooltip body.
* [x] **Pixel font, AA off** — Cairo antialias `NONE`, hinting `FULL`, fontconfig
      family list with Terminal-style fallbacks.
* [x] **Pill proportions** — radius = height/2 (stadium path), 30px tall,
      ~1.2% top margin, flat opaque fill, layer `top`, namespace `pillbar`,
      exclusive zone set so windows do not overlap.
* [x] **Hyprland workspaces via socket2** — `.socket2.sock` event stream,
      `.socket.sock` only for initial queries/dispatch.
* [x] Builds warning-free with GCC 15 and Clang (`-Wall -Wextra -Wpedantic`).

## Commit

```sh
git add -A
git commit -m "pillbar: floating pill status bar for Hyprland (event-driven Wayland/Cairo)"
```

## Notes / decisions

* Wi-Fi uses NetworkManager over D-Bus (the D-Bus alternative explicitly
  allowed by the spec) instead of raw nl80211, for a fully event-driven and much
  smaller footprint.
* Audio uses PulseAudio (via `pa_mainloop_api` integrated into epoll); PipeWire's
  Pulse compatibility layer or `pipewire-pulse` works transparently.
* "Primary output" means the first output returned by the registry, because
  Wayland exposes no primary-output flag.
* The tooltip fade is a short opacity animation driven by a timer armed only
  during the transition, so it never causes idle wakeups.
