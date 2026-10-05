#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/config.hpp"
#include "app/event_loop.hpp"
#include "app/inotify_watcher.hpp"
#include "model/state.hpp"
#include "render/layout.hpp"
#include "render/renderer.hpp"
#include "sources/audio.hpp"
#include "sources/bluetooth.hpp"
#include "sources/dbus.hpp"
#include "sources/gpu.hpp"
#include "sources/hyprland.hpp"
#include "sources/login1.hpp"
#include "sources/manager.hpp"
#include "sources/power.hpp"
#include "sources/stats.hpp"
#include "sources/wifi.hpp"
#include "wayland/display.hpp"
#include "wayland/layer_surface.hpp"
#include "wayland/shm.hpp"

namespace pillbar {

// One floating pill (and its tooltip) for a single output.
struct Bar {
  Output* output = nullptr;
  int output_w = 0;
  int output_h = 0;

  std::unique_ptr<LayerSurface> surface;
  std::unique_ptr<Canvas> canvas;
  std::unique_ptr<LayerSurface> tooltip_surface;
  std::unique_ptr<Canvas> tooltip_canvas;

  BarLayout layout;
  int bar_w = 0;
  int bar_h = 0;
  double scale = 1.0;
  bool configured = false;
  Item dirty = Item::All;

  // Hover / tooltip state.
  Item hover = Item::None;
  int hover_ws = -1;
  bool tooltip_visible = false;
  double tooltip_opacity = 0.0;
  TimerFd hover_delay;
  TimerFd fade_timer;
  TooltipMetrics tooltip_metrics;
  std::vector<std::string> tooltip_lines;
  int tooltip_x = 0;
  int tooltip_y = 0;
};

// Application object: owns the event loop, display, renderer, sources and bars.
class App {
 public:
  App();
  ~App();
  int run();

 private:
  bool init();
  void reload_config();
  void setup_display_callbacks();
  void setup_pointer_callbacks();
  void setup_sources();
  void rebuild_bars();
  void create_bar(Output& output);
  void destroy_bars();

  void on_state_change(Item items);
  void redraw_bar(Bar& bar, Item items);
  void redraw_all();
  void update_tooltip(Bar& bar);
  void show_tooltip(Bar& bar);
  void hide_tooltip(Bar& bar);
  std::vector<std::string> tooltip_lines(Item item, Bar& bar) const;

  Bar* bar_for_surface(wl_surface* surface);
  Item hit_test(Bar& bar, double x, double y);

  void on_pointer_enter(wl_surface* surface, double x, double y);
  void on_pointer_motion(wl_surface* surface, double x, double y);
  void on_pointer_leave(wl_surface* surface);
  void on_pointer_button(wl_surface* surface, std::uint32_t button, double x, double y);
  void on_pointer_scroll(wl_surface* surface, int steps, double x, double y);

  Config config_;
  std::string config_path_;
  AppState state_;
  EventLoop loop_;
  WaylandDisplay display_;
  Renderer renderer_;
  LayoutEngine layout_engine_;
  DbusBus dbus_;
  std::unique_ptr<SourceManager> sources_;
  std::unique_ptr<HyprlandSource> hyprland_;
  std::unique_ptr<AudioSource> audio_;
  std::unique_ptr<WifiSource> wifi_;
  std::unique_ptr<BluetoothSource> bluetooth_;
  std::unique_ptr<CpuSource> cpu_;
  std::unique_ptr<GpuSource> gpu_;
  std::unique_ptr<Login1Source> login1_;
  std::unique_ptr<InotifyWatcher> config_watcher_;
  SignalFd signals_;
  TimerFd output_rebuild_timer_;
  TimerFd tooltip_seconds_timer_;
  std::vector<std::unique_ptr<Bar>> bars_;
  Bar* tooltip_bar_ = nullptr;
  bool tooltip_uses_seconds_ = false;
};

}  // namespace pillbar

