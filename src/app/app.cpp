#include "app/app.hpp"

#include <sys/epoll.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "app/logging.hpp"
#include "render/glyphs.hpp"
#include "wayland/pointer.hpp"
#include "wayland/output.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace pillbar {
namespace {

constexpr std::uint32_t kLayerTop = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
constexpr std::uint32_t kLayerOverlay = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
// Fixed tooltip surface height (logical px). Keeping the layer surface size
// constant means we never re-commit geometry while mapped, so there is no
// configure/ack race; only the buffer content changes per hover.
constexpr int kTooltipSurfaceHeight = 260;

std::string format_clock_tooltip(const std::string& format) {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  char buffer[256];
  if (std::strftime(buffer, sizeof(buffer), format.c_str(), &tm) == 0) return {};
  return buffer;
}

}  // namespace

App::App() = default;
App::~App() = default;

int App::run() {
  if (!init()) return 1;
  loop_.run();
  destroy_bars();
  return 0;
}

bool App::init() {
  // Built-in defaults only: pillbar deliberately has no config file.
  config_ = Config{};
  config_apply_default_slots(config_);
  renderer_.configure(config_);

  if (!display_.connect(loop_)) return false;
  loop_.set_post_dispatch([this]() { display_.flush(); });
  setup_display_callbacks();
  setup_pointer_callbacks();

  if (signals_.create()) {
    loop_.add(signals_.get(), EPOLLIN, [this](std::uint32_t) {
      for (;;) {
        const int sig = signals_.read_signal();
        if (sig == 0) break;
        if (sig == SIGTERM || sig == SIGINT) {
          loop_.stop();
        } else if (sig == SIGHUP) {
          reload_config();
        } else if (sig == SIGPWR) {
          if (sources_) sources_->refresh_all();
        }
      }
    });
  }

  if (output_rebuild_timer_.valid()) {
    loop_.add(output_rebuild_timer_.get(), EPOLLIN, [this](std::uint32_t) {
      output_rebuild_timer_.consume();
      rebuild_bars();
    });
    output_rebuild_timer_.disarm();
  }

  if (tooltip_seconds_timer_.valid()) {
    loop_.add(tooltip_seconds_timer_.get(), EPOLLIN, [this](std::uint32_t) {
      tooltip_seconds_timer_.consume();
      if (tooltip_bar_ != nullptr && tooltip_bar_->tooltip_visible) {
        update_tooltip(*tooltip_bar_);
        if (tooltip_uses_seconds_) tooltip_seconds_timer_.arm_relative_ms(1000, true);
      } else {
        tooltip_seconds_timer_.disarm();
      }
    });
    tooltip_seconds_timer_.disarm();
  }

  setup_sources();
  rebuild_bars();
  display_.roundtrip();
  redraw_all();
  display_.flush();

  return true;
}

void App::setup_display_callbacks() {
  display_.on_outputs_changed = [this]() {
    if (output_rebuild_timer_.valid()) output_rebuild_timer_.arm_relative_ms(100, true);
  };
  for (const auto& output : display_.outputs()) {
    output->on_done = [this]() {
      if (output_rebuild_timer_.valid()) output_rebuild_timer_.arm_relative_ms(100, true);
    };
    output->on_changed = [this]() {
      if (output_rebuild_timer_.valid()) output_rebuild_timer_.arm_relative_ms(100, true);
    };
  }
}

void App::setup_pointer_callbacks() {
  Pointer* pointer = display_.pointer();
  if (pointer == nullptr) {
    LOG_WARN("no wl_pointer; hover/clicks disabled");
    return;
  }
  pointer->on_enter = [this](wl_surface* s, double x, double y) { on_pointer_enter(s, x, y); };
  pointer->on_motion = [this](wl_surface* s, double x, double y) { on_pointer_motion(s, x, y); };
  pointer->on_leave = [this](wl_surface* s) { on_pointer_leave(s); };
  pointer->on_button = [this](wl_surface* s, std::uint32_t b, double x, double y) {
    on_pointer_button(s, b, x, y);
  };
  pointer->on_scroll = [this](wl_surface* s, int steps, double x, double y) {
    on_pointer_scroll(s, steps, x, y);
  };
}

void App::setup_sources() {
  sources_ = std::make_unique<SourceManager>(loop_, [this](Item items) { on_state_change(items); });
  NotifyFn notify = sources_->notify_fn();

  hyprland_ = std::make_unique<HyprlandSource>(state_, notify, config_.min_workspaces, 15);
  audio_ = std::make_unique<AudioSource>(state_, notify);
  clock_ = std::make_unique<ClockSource>(state_, notify);
  cpu_ = std::make_unique<CpuSource>(state_, notify);
  gpu_ = std::make_unique<GpuSource>(state_, notify);
  power_ = std::make_unique<PowerSource>(state_, notify);
  dbus_.open_system(loop_);
  wifi_ = std::make_unique<WifiSource>(state_, notify, dbus_);
  bluetooth_ = std::make_unique<BluetoothSource>(state_, notify, dbus_);
  login1_ = std::make_unique<Login1Source>(dbus_, [this]() {
    if (sources_) sources_->refresh_all();
  });

  sources_->add(hyprland_.get());
  sources_->add(audio_.get());
  sources_->add(clock_.get());
  sources_->add(cpu_.get());
  sources_->add(gpu_.get());
  sources_->add(power_.get());
  sources_->add(wifi_.get());
  sources_->add(bluetooth_.get());
  sources_->add(login1_.get());
  sources_->start_all();
}

void App::rebuild_bars() {
  destroy_bars();
  const auto& outputs = display_.outputs();
  LOG_DEBUG("rebuild_bars: %zu output(s)", outputs.size());
  bool primary_done = false;
  for (const auto& output : outputs) {
    if (config_.monitors == "primary") {
      if (primary_done) break;
      if (!output->ready()) {
        LOG_DEBUG("output %s not ready yet", output->name().c_str());
        continue;
      }
      primary_done = true;
    }
    if (!output->ready()) {
      LOG_DEBUG("output %s not ready yet", output->name().c_str());
      continue;
    }
    create_bar(*output);
  }
}

void App::create_bar(Output& output) {
  auto bar = std::make_unique<Bar>();
  bar->output = &output;
  bar->output_w = output.logical_width();
  bar->output_h = output.logical_height();
  bar->scale = output.scale();
  if (bar->output_w <= 0 || bar->output_h <= 0) return;
  bar->bar_h = std::max(1, static_cast<int>(std::lround(config_.height_px)));
  bar->pill_w = renderer_.desired_width(config_, state_, bar->output_w);
  bar->pill_target = bar->pill_w;
  const int top = static_cast<int>(std::lround(bar->output_h * config_.margin_top_frac));
  LOG_DEBUG("creating bar on %s: output=%dx%d pill=%.0fx%d scale=%.2f",
            output.name().c_str(), bar->output_w, bar->output_h, bar->pill_w, bar->bar_h,
            bar->scale);

  // The layer surface spans the whole output width so the pill can animate its
  // width without re-committing layer geometry; only the pill area accepts
  // pointer input (set from the layout).
  Bar* raw = bar.get();
  bar->surface = std::make_unique<LayerSurface>(display_, &output, kLayerTop,
                                                config_.namespace_name, true);
  // Anchor to both horizontal edges and let the compositor size the surface to
  // the full output width; this is also what makes the exclusive zone below
  // actually reserve the strip instead of being ignored.
  bar->surface->set_anchors(ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  bar->surface->set_margin(top, 0, 0, 0);
  bar->surface->set_size(0, static_cast<std::uint32_t>(bar->bar_h));
  bar->surface->set_exclusive_zone(bar->bar_h + top);
  bar->surface->on_configure = [this, raw](std::uint32_t width, std::uint32_t height) {
    LOG_DEBUG("bar configure: %ux%u", width, height);
    raw->configured = true;
    if (height > 0) raw->bar_h = static_cast<int>(height);
    if (width > 0) raw->surface_w = static_cast<int>(width);
    if (raw->surface_w <= 0) raw->surface_w = raw->output_w;
    if (raw->canvas == nullptr) return;
    raw->surface->set_destination(raw->surface_w, raw->bar_h);
    const int pw = std::max(1, static_cast<int>(std::lround(raw->surface_w * raw->scale)));
    const int ph = std::max(1, static_cast<int>(std::lround(raw->bar_h * raw->scale)));
    raw->canvas->resize(pw, ph);
    redraw_bar(*raw, Item::All);
  };
  bar->surface->on_scale = [this, raw](double scale) {
    raw->scale = scale;
    if (raw->surface_w <= 0) raw->surface_w = raw->output_w;
    if (raw->canvas == nullptr) return;
    raw->surface->set_destination(raw->surface_w, raw->bar_h);
    const int pw = std::max(1, static_cast<int>(std::lround(raw->surface_w * raw->scale)));
    const int ph = std::max(1, static_cast<int>(std::lround(raw->bar_h * raw->scale)));
    raw->canvas->resize(pw, ph);
    if (raw->tooltip_canvas != nullptr) {
      raw->tooltip_surface->set_destination(raw->output_w, kTooltipSurfaceHeight);
      raw->tooltip_canvas->resize(
          std::max(1, static_cast<int>(std::lround(raw->output_w * raw->scale))),
          std::max(1, static_cast<int>(std::lround(kTooltipSurfaceHeight * raw->scale))));
    }
    redraw_bar(*raw, Item::All);
  };
  bar->surface->on_closed = [this]() {
    if (output_rebuild_timer_.valid()) output_rebuild_timer_.arm_relative_ms(100, true);
  };
  bar->surface->commit();

  bar->canvas = std::make_unique<Canvas>(display_.shm(), bar->surface->surface());
  bar->canvas->set_on_buffer_free([this, raw]() {
    // Only a pending (un-drawn) update needs a retry here. Redrawing on every
    // buffer release unconditionally makes the bar render as fast as the
    // compositor releases buffers (~800 fps), burning a whole CPU core.
    if (raw->configured && raw->dirty != Item::None) redraw_bar(*raw, raw->dirty);
  });

  bar->tooltip_surface = std::make_unique<LayerSurface>(display_, &output, kLayerOverlay,
                                                        "pillbar-tooltip", false);
  bar->tooltip_surface->set_input_none();
  bar->tooltip_surface->set_anchors(ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
  // The notch reserves the top strip, and Hyprland offsets overlay surfaces by
  // that reserved area, so the tooltip only needs the small gap margin here.
  bar->tooltip_surface->set_margin(static_cast<int>(config_.tooltip_gap_px), 0, 0, 0);
  bar->tooltip_surface->set_size(static_cast<std::uint32_t>(bar->output_w),
                                 static_cast<std::uint32_t>(kTooltipSurfaceHeight));
  bar->tooltip_surface->set_destination(bar->output_w, kTooltipSurfaceHeight);
  bar->tooltip_surface->on_configure = [this, raw](std::uint32_t, std::uint32_t) {
    if (raw->tooltip_canvas == nullptr) return;
    raw->tooltip_surface->set_destination(raw->output_w, kTooltipSurfaceHeight);
    raw->tooltip_canvas->resize(
        std::max(1, static_cast<int>(std::lround(raw->output_w * raw->scale))),
        std::max(1, static_cast<int>(std::lround(kTooltipSurfaceHeight * raw->scale))));
    if (raw->tooltip_visible) update_tooltip(*raw);
  };
  bar->tooltip_surface->commit();
  bar->tooltip_canvas =
      std::make_unique<Canvas>(display_.shm(), bar->tooltip_surface->surface());

  loop_.add(bar->hover_delay.get(), EPOLLIN, [this, raw](std::uint32_t) {
    raw->hover_delay.consume();
    show_tooltip(*raw);
  });
  bar->hover_delay.disarm();
  loop_.add(bar->fade_timer.get(), EPOLLIN, [this, raw](std::uint32_t) {
    raw->fade_timer.consume();
    raw->tooltip_opacity = std::min(1.0, raw->tooltip_opacity + 0.18);
    update_tooltip(*raw);
    if (raw->tooltip_opacity < 1.0 && raw->tooltip_visible) {
      raw->fade_timer.arm_relative_ms(16, true);
    } else {
      raw->fade_timer.disarm();
    }
  });
  bar->fade_timer.disarm();

  loop_.add(bar->anim_timer.get(), EPOLLIN, [this, raw](std::uint32_t) {
    raw->anim_timer.consume();
    on_anim_tick(*raw);
  });
  bar->anim_timer.disarm();

  bars_.push_back(std::move(bar));
}

void App::destroy_bars() {
  for (auto& bar : bars_) {
    if (bar->hover_delay.valid()) loop_.del(bar->hover_delay.get());
    if (bar->fade_timer.valid()) loop_.del(bar->fade_timer.get());
    if (bar->anim_timer.valid()) loop_.del(bar->anim_timer.get());
  }
  bars_.clear();
  tooltip_bar_ = nullptr;
}

void App::reload_config() {
  config_ = Config{};
  config_apply_default_slots(config_);
  renderer_.configure(config_);
  rebuild_bars();
  redraw_all();
}

Bar* App::bar_for_surface(wl_surface* surface) {
  for (auto& bar : bars_) {
    if (bar->surface != nullptr && bar->surface->surface() == surface) return bar.get();
  }
  return nullptr;
}

Item App::hit_test(Bar& bar, double x, double y) {
  bar.hover_ws = -1;
  // Pointer coordinates are surface-local and the surface spans the output, so
  // shift into pill-local space.
  x -= bar.layout.screen_x;
  for (const ItemBox& box : bar.layout.items) {
    if (x < box.rect.x || x > box.rect.x + box.rect.w) continue;
    if (y < box.rect.y || y > box.rect.y + box.rect.h) continue;
    if (box.item == Item::Workspaces) {
      double best = 1e9;
      for (std::size_t i = 0; i < bar.layout.workspace_slots.size(); ++i) {
        const double dx = std::fabs(bar.layout.workspace_slots[i].cx - x);
        if (dx < best) {
          best = dx;
          bar.hover_ws = static_cast<int>(i);
        }
      }
    }
    return box.item;
  }
  return Item::None;
}

void App::on_pointer_enter(wl_surface* surface, double x, double y) {
  on_pointer_motion(surface, x, y);
}

void App::on_pointer_motion(wl_surface* surface, double x, double y) {
  Bar* bar = bar_for_surface(surface);
  if (bar == nullptr) return;
  const Item item = hit_test(*bar, x, y);
  if (item != bar->hover) {
    bar->hover = item;
    hide_tooltip(*bar);
    if (item != Item::None) {
      bar->hover_delay.arm_relative_ms(config_.tooltip_delay_ms, true);
    } else {
      bar->hover_delay.disarm();
    }
  }
}

void App::on_pointer_leave(wl_surface* surface) {
  Bar* bar = bar_for_surface(surface);
  if (bar == nullptr) return;
  bar->hover_delay.disarm();
  bar->hover = Item::None;
  hide_tooltip(*bar);
}

void App::on_pointer_button(wl_surface* surface, std::uint32_t button, double x, double y) {
  Bar* bar = bar_for_surface(surface);
  if (bar == nullptr) return;
  const Item item = hit_test(*bar, x, y);
  if (item == Item::Workspaces && button == 0x110 /* BTN_LEFT */ && bar->hover_ws >= 0 &&
      static_cast<std::size_t>(bar->hover_ws) < bar->layout.workspace_slots.size()) {
    if (hyprland_) {
      const int id = bar->layout.workspace_slots[bar->hover_ws].id;
      LOG_DEBUG("workspace click -> %d", id);
      hyprland_->dispatch_workspace(id);
    }
  } else if (item == Item::Volume && button == 0x112 /* BTN_MIDDLE */) {
    if (audio_) audio_->toggle_mute();
  }
}

void App::on_pointer_scroll(wl_surface* surface, int steps, double x, double y) {
  Bar* bar = bar_for_surface(surface);
  if (bar == nullptr) return;
  const Item item = hit_test(*bar, x, y);
  if (item == Item::Volume) {
    if (audio_) audio_->set_volume_relative(steps * 5);
  } else if (item == Item::Workspaces && hyprland_) {
    hyprland_->dispatch_workspace_relative(steps > 0 ? 1 : -1);
  }
}

void App::on_state_change(Item items) {
  for (auto& bar : bars_) {
    update_target_width(*bar);
    if (!bar->animating) redraw_bar(*bar, items);
  }
  if (tooltip_bar_ != nullptr && tooltip_bar_->tooltip_visible) {
    if (has_item(items, Item::Clock) || has_item(items, Item::Cpu) || has_item(items, Item::Gpu) ||
        has_item(items, tooltip_bar_->hover)) {
      update_tooltip(*tooltip_bar_);
    }
  }
}

void App::redraw_all() {
  for (auto& bar : bars_) {
    redraw_bar(*bar, Item::All);
  }
}

void App::redraw_bar(Bar& bar, Item items) {
  if (!bar.configured || bar.canvas == nullptr) {
    bar.dirty |= items;
    return;
  }
  bar.layout =
      renderer_.compute_layout(config_, state_, bar.pill_w, bar.output_w, bar.output_h);
  renderer_.set_scale(bar.scale);
  cairo_t* cr = bar.canvas->begin();
  if (cr == nullptr) {
    bar.dirty |= items;
    return;
  }
  cairo_scale(cr, bar.scale, bar.scale);
  renderer_.draw_bar(cr, bar.layout, state_);
  // Only the pill accepts pointer input; the rest of the strip is click-through.
  // Set the region before the buffer commit so one surface commit applies both,
  // and only when the geometry actually moved (avoids a second compositor round
  // trip on every animation frame).
  if (bar.layout.screen_x != bar.input_x || bar.layout.bar_w != bar.input_w) {
    bar.surface->set_input_rect(bar.layout.screen_x, 0, bar.layout.bar_w, bar.layout.bar_h);
    bar.input_x = bar.layout.screen_x;
    bar.input_w = bar.layout.bar_w;
  }
  // The pill can change width/position, so redraw the whole (small) surface.
  bar.canvas->damage_all();
  bar.canvas->commit();
  bar.dirty = Item::None;
}

void App::update_target_width(Bar& bar) {
  const double target = renderer_.desired_width(config_, state_, bar.output_w);
  // Titles that animate (spinners, progress) change the measured width by a
  // few pixels every frame; ignore changes below this deadband so the notch
  // does not sit there vibrating.
  if (std::fabs(target - bar.pill_target) < 10.0) return;
  LOG_DEBUG("pill width target %.0f -> %.0f", bar.pill_target, target);
  bar.pill_target = target;
  // Too small to be worth animating: snap to it.
  if (std::fabs(target - bar.pill_w) <= 4.0) {
    bar.pill_w = target;
    bar.animating = false;
    bar.anim_timer.disarm();
    redraw_bar(bar, Item::All);
    return;
  }
  if (!bar.anim_timer.valid()) return;
  bar.anim_from = bar.pill_w;
  bar.anim_start = std::chrono::steady_clock::now();
  if (!bar.animating) {
    bar.animating = true;
    bar.anim_timer.arm_relative_ms(16, false);
  }
}

void App::on_anim_tick(Bar& bar) {
  // Fixed-duration ease-out: the exponential approach used before had a long
  // sub-pixel tail, which made the text look like it was shivering for a few
  // hundred milliseconds after every resize.
  constexpr double kDurationSeconds = 0.16;
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                       bar.anim_start)
                             .count();
  double progress = elapsed / kDurationSeconds;
  if (progress >= 1.0) progress = 1.0;
  const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
  bar.pill_w = bar.anim_from + (bar.pill_target - bar.anim_from) * eased;
  if (progress >= 1.0) {
    bar.pill_w = bar.pill_target;
    bar.animating = false;
    bar.anim_timer.disarm();
  }
  redraw_bar(bar, Item::All);
}

std::vector<std::string> App::tooltip_lines(Item item, Bar& bar) const {
  std::vector<std::string> lines;
  switch (item) {
    case Item::Battery: {
      const BatteryState& b = state_.battery;
      char buffer[160];
      std::snprintf(buffer, sizeof(buffer), "Battery: %d%%", b.percent);
      lines.push_back(buffer);
      lines.push_back(std::string("State: ") +
                      (b.charging ? "charging" : (b.full ? "full" : "discharging")));
      if (b.power_now > 0) {
        std::snprintf(buffer, sizeof(buffer), "Draw: %.1f W",
                      static_cast<double>(b.power_now) / 1000000.0);
        lines.push_back(buffer);
      }
      if (b.charging && b.power_now > 0 && b.energy_full > b.energy_now) {
        const double hours =
            static_cast<double>(b.energy_full - b.energy_now) / static_cast<double>(b.power_now);
        std::snprintf(buffer, sizeof(buffer), "To full: %dh%02dm", static_cast<int>(hours),
                      static_cast<int>((hours - std::floor(hours)) * 60.0));
        lines.push_back(buffer);
      } else if (!b.charging && !b.full && b.power_now > 0) {
        const double hours = static_cast<double>(b.energy_now) / static_cast<double>(b.power_now);
        std::snprintf(buffer, sizeof(buffer), "To empty: %dh%02dm", static_cast<int>(hours),
                      static_cast<int>((hours - std::floor(hours)) * 60.0));
        lines.push_back(buffer);
      }
      if (b.health > 0) {
        std::snprintf(buffer, sizeof(buffer), "Health: %d%%", b.health);
        lines.push_back(buffer);
      }
      lines.push_back(std::string("AC: ") + (b.ac_online ? "online" : "offline"));
      break;
    }
    case Item::Volume: {
      const VolumeState& v = state_.volume;
      char buffer[160];
      std::snprintf(buffer, sizeof(buffer), "Volume: %d%%%s", v.percent,
                    v.muted ? " (muted)" : "");
      lines.push_back(buffer);
      if (!v.sink_desc.empty()) lines.push_back(v.sink_desc);
      if (!v.sink_name.empty()) lines.push_back(v.sink_name);
      break;
    }
    case Item::Wifi:
      lines = wifi_ ? wifi_->detail() : std::vector<std::string>{"Wi-Fi unavailable"};
      break;
    case Item::Bluetooth:
      lines = bluetooth_ ? bluetooth_->detail() : std::vector<std::string>{"Bluetooth unavailable"};
      break;
    case Item::Workspaces: {
      const int state_index =
          bar.hover_ws >= 0 && static_cast<std::size_t>(bar.hover_ws) <
                                   bar.layout.workspace_slots.size()
              ? bar.layout.workspace_slots[static_cast<std::size_t>(bar.hover_ws)].state_index
              : -1;
      if (state_index >= 0 &&
          static_cast<std::size_t>(state_index) < state_.workspaces.list.size()) {
        const WorkspaceState& ws = state_.workspaces.list[static_cast<std::size_t>(state_index)];
        lines.push_back("Workspace " + ws.name + " (id " + std::to_string(ws.id) + ")");
        lines.push_back("Windows: " + std::to_string(ws.windows));
        for (const std::string& cls : ws.classes) lines.push_back("  " + cls);
      } else {
        lines.push_back("Workspaces");
      }
      break;
    }
    case Item::ActiveWindow: {
      const WindowState& w = state_.window;
      lines.push_back(w.title.empty() ? "(untitled)" : w.title);
      lines.push_back("Class: " + (w.cls.empty() ? std::string("unknown") : w.cls));
      lines.push_back("Workspace: " + std::to_string(w.workspace));
      lines.push_back("PID: " + std::to_string(w.pid));
      break;
    }
    case Item::Clock: {
      lines.push_back(format_clock_tooltip(config_.clock_tooltip_format));
      if (config_.system_item_tooltip) {
        if (cpu_) {
          lines.push_back("");
          const std::vector<std::string> cpu_lines = cpu_->detail();
          lines.insert(lines.end(), cpu_lines.begin(), cpu_lines.end());
        }
        if (gpu_) {
          lines.push_back("");
          const std::vector<std::string> gpu_lines = gpu_->detail();
          lines.insert(lines.end(), gpu_lines.begin(), gpu_lines.end());
        }
      }
      break;
    }
    case Item::Cpu:
      lines = cpu_ ? cpu_->detail() : std::vector<std::string>{"CPU"};
      break;
    case Item::Gpu:
      lines = gpu_ ? gpu_->detail() : std::vector<std::string>{"GPU"};
      break;
    default:
      break;
  }
  return lines;
}

void App::show_tooltip(Bar& bar) {
  if (bar.hover == Item::None || !bar.configured) return;
  bar.tooltip_lines = tooltip_lines(bar.hover, bar);
  if (bar.tooltip_lines.empty()) return;
  bar.tooltip_metrics = renderer_.measure_tooltip(bar.tooltip_lines);

  const int logical_w = static_cast<int>(std::ceil(bar.tooltip_metrics.width));
  const Rect* rect = bar.layout.rect_for(bar.hover);
  const double item_center =
      rect != nullptr ? rect->x + rect->w / 2.0 : bar.layout.bar_w / 2.0;
  const int bar_screen_x = bar.layout.screen_x;
  int tx = static_cast<int>(std::lround(bar_screen_x + item_center - logical_w / 2.0));
  tx = std::clamp(tx, 0, std::max(0, bar.output_w - logical_w));
  bar.tooltip_x = tx;
  bar.tooltip_y = 0;

  // The tooltip layer surface is a fixed-size canvas; only its buffer content
  // (positioned at tooltip_x) changes. No layer geometry is re-committed here,
  // which avoids the configure/ack race that kills the connection.
  bar.tooltip_visible = true;
  bar.tooltip_opacity = 0.0;
  tooltip_bar_ = &bar;

  const bool system = bar.hover == Item::Clock || bar.hover == Item::Cpu || bar.hover == Item::Gpu;
  // Wi-Fi re-reads interface counters so the tooltip's up/down rates stay live.
  tooltip_uses_seconds_ = system || bar.hover == Item::Wifi;
  if (tooltip_uses_seconds_ && tooltip_seconds_timer_.valid()) {
    tooltip_seconds_timer_.arm_relative_ms(1000, true);
  }
  if (cpu_) cpu_->set_hovered(bar.hover == Item::Clock || bar.hover == Item::Cpu);
  if (gpu_) gpu_->set_hovered(bar.hover == Item::Clock || bar.hover == Item::Gpu);
  update_tooltip(bar);
  if (bar.fade_timer.valid()) bar.fade_timer.arm_relative_ms(16, true);
}

void App::update_tooltip(Bar& bar) {
  if (!bar.tooltip_visible) return;
  bar.tooltip_lines = tooltip_lines(bar.hover, bar);
  bar.tooltip_metrics = renderer_.measure_tooltip(bar.tooltip_lines);
  renderer_.set_scale(bar.scale);
  cairo_t* cr = bar.tooltip_canvas->begin();
  if (cr == nullptr) return;
  cairo_scale(cr, bar.scale, bar.scale);
  renderer_.draw_tooltip(cr, bar.tooltip_lines, bar.tooltip_opacity, bar.tooltip_metrics,
                         bar.tooltip_x);
  bar.tooltip_canvas->damage_all();
  bar.tooltip_canvas->commit();
}

void App::hide_tooltip(Bar& bar) {
  if (!bar.tooltip_visible) return;
  bar.tooltip_visible = false;
  bar.tooltip_opacity = 0.0;
  bar.fade_timer.disarm();
  // Clear the fixed-size tooltip canvas to fully transparent instead of
  // unmapping it. Remapping later would require another configure/ack cycle;
  // keeping the surface mapped with an empty buffer avoids that entirely.
  if (bar.tooltip_canvas != nullptr) {
    renderer_.set_scale(bar.scale);
    cairo_t* cr = bar.tooltip_canvas->begin();
    if (cr != nullptr) {
      cairo_scale(cr, bar.scale, bar.scale);
      renderer_.draw_tooltip(cr, {}, 0.0, TooltipMetrics{}, bar.tooltip_x);
      bar.tooltip_canvas->damage_all();
      bar.tooltip_canvas->commit();
    }
  }
  if (tooltip_bar_ == &bar) {
    tooltip_bar_ = nullptr;
    tooltip_uses_seconds_ = false;
    tooltip_seconds_timer_.disarm();
    if (cpu_) cpu_->set_hovered(false);
    if (gpu_) gpu_->set_hovered(false);
  }
}

}  // namespace pillbar
