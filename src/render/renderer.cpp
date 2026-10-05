#include "render/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "render/glyphs.hpp"

namespace pillbar {
namespace {

constexpr double kPi = 3.14159265358979323846;

void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  const double radius = std::min(r, std::min(w, h) / 2.0);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - radius, y + radius, radius, -kPi / 2.0, 0.0);
  cairo_arc(cr, x + w - radius, y + h - radius, radius, 0.0, kPi / 2.0);
  cairo_arc(cr, x + radius, y + h - radius, radius, kPi / 2.0, kPi);
  cairo_arc(cr, x + radius, y + radius, radius, kPi, 3.0 * kPi / 2.0);
  cairo_close_path(cr);
}

void set_color(cairo_t* cr, const Color& c, double alpha_mul = 1.0) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a * alpha_mul);
}

std::string uppercase_ascii(const std::string& input) {
  std::string out = input;
  for (char& ch : out) {
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
  }
  return out;
}

std::string format_percent(int value) { return std::to_string(value) + "%"; }

std::string format_clock(const ClockState& clock) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d", clock.hour, clock.minute);
  return buffer;
}

int battery_icon_index(int percent) { return std::clamp((percent + 5) / 10, 0, 10); }

std::string battery_icon(const Config& config, const BatteryState& battery) {
  const bool charging = battery.charging || battery.full;
  const std::vector<std::string>& set =
      charging ? config.battery_charging : config.battery_levels;
  if (set.empty()) return {};
  const std::size_t index =
      std::min<std::size_t>(static_cast<std::size_t>(battery_icon_index(battery.percent)),
                            set.size() - 1);
  return set[index];
}

std::string volume_icon(const Config& config, const VolumeState& volume) {
  if (volume.muted) return config.volume_muted;
  if (volume.percent < 34) return config.volume_low;
  if (volume.percent < 67) return config.volume_medium;
  return config.volume_high;
}

std::string wifi_icon(const Config& config, const WifiState& wifi) {
  if (!wifi.connected) return config.wifi_off;
  if (config.wifi_levels.empty()) return config.wifi_off;
  const int level = std::clamp((wifi.signal + 12) / 25, 1,
                               static_cast<int>(config.wifi_levels.size()));
  return config.wifi_levels[static_cast<std::size_t>(level) - 1];
}

}  // namespace

void Renderer::configure(const Config& config) {
  config_ = &config;
  text_.configure(config.fonts, config.font_size_px, config.letter_spacing);
  icon_text_.configure(config.icon_fonts,
                       std::max(1.0, config.height_px * config.icon_size_frac), 0.0,
                       /*antialias=*/true);
}

std::string Renderer::ellipsize(const std::string& input, double max_width) const {
  if (max_width <= 0.0) return {};
  if (text_.measure(input) <= max_width) return input;
  const std::string dots = "...";
  std::string best;
  for (std::size_t len = 1; len <= input.size(); ++len) {
    const std::string candidate = input.substr(0, len) + dots;
    if (text_.measure(candidate) > max_width) break;
    best = candidate;
  }
  return best.empty() ? dots : best;
}

TooltipMetrics Renderer::measure_tooltip(const std::vector<std::string>& lines) const {
  TooltipMetrics metrics;
  metrics.line_height = config_->tooltip_font_size_px * 1.4;
  double widest = 0.0;
  for (const std::string& line : lines) {
    const double measured = text_.measure(line);
    const double scaled = measured * (config_->tooltip_font_size_px / text_.size_px());
    widest = std::max(widest, scaled);
  }
  metrics.width = widest + config_->tooltip_pad_px * 2.0;
  metrics.height = static_cast<double>(lines.size()) * metrics.line_height +
                   config_->tooltip_pad_px * 2.0;
  return metrics;
}

void Renderer::draw_tooltip(cairo_t* cr, const std::vector<std::string>& lines, double opacity,
                            const TooltipMetrics& metrics, double x_offset) {
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

  cairo_save(cr);
  cairo_translate(cr, x_offset, 0.0);
  set_color(cr, config_->bar_color, opacity);
  rounded_rect(cr, 0, 0, metrics.width, metrics.height, config_->tooltip_radius_px);
  cairo_fill(cr);

  TextRenderer tooltip_text;
  tooltip_text.configure(config_->fonts, config_->tooltip_font_size_px, config_->letter_spacing);
  double y = config_->tooltip_pad_px + metrics.line_height / 2.0;
  for (const std::string& line : lines) {
    Color color = config_->text;
    color.a *= opacity;
    tooltip_text.draw_left(cr, config_->tooltip_pad_px, y, line, color);
    y += metrics.line_height;
  }
  cairo_restore(cr);
}

void Renderer::draw_bar(cairo_t* cr, const BarLayout& layout, const AppState& state) {
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  const double radius = layout.bar_h / 2.0;
  if (config_->shadow) {
    for (int i = 6; i >= 1; --i) {
      const double spread = static_cast<double>(i) * (config_->shadow_blur_px / 6.0);
      set_color(cr, config_->shadow_color, 1.0 / static_cast<double>(i + 2));
      rounded_rect(cr, spread * 0.5, spread * 0.5 + config_->shadow_offset_px,
                   layout.bar_w - spread, layout.bar_h - spread, radius);
      cairo_fill(cr);
    }
  }
  set_color(cr, config_->bar_color);
  rounded_rect(cr, 0, 0, layout.bar_w, layout.bar_h, radius);
  cairo_fill(cr);

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
  const double center_y = layout.bar_h / 2.0;

  for (const ItemBox& box : layout.items) {
    const Rect& r = box.rect;
    switch (box.item) {
      case Item::Battery: {
        if (!state.battery.present) break;
        Color color = config_->text;
        if (state.battery.charging) {
          color = config_->battery_green;
        } else if (state.battery.percent <= 15) {
          color = config_->battery_low;
        }
        const std::string glyph = battery_icon(*config_, state.battery);
        double glyph_w = 0.0;
        if (!glyph.empty()) {
          glyph_w = icon_text_.measure(glyph);
          icon_text_.draw_center(cr, r.x + glyph_w / 2.0, center_y, glyph, color);
        }
        const double text_x = r.x + glyph_w + layout.bar_w * 0.010;
        text_.draw_left(cr, text_x, center_y, format_percent(state.battery.percent), color);
        break;
      }
      case Item::Volume: {
        if (!state.volume.available) break;
        const Color icon_color = state.volume.muted ? config_->dim : config_->speaker;
        const std::string glyph = volume_icon(*config_, state.volume);
        double glyph_w = 0.0;
        if (!glyph.empty()) {
          glyph_w = icon_text_.measure(glyph);
          icon_text_.draw_center(cr, r.x + glyph_w / 2.0, center_y, glyph, icon_color);
        }
        const double text_x = r.x + glyph_w + layout.bar_w * 0.010;
        const std::string label =
            state.volume.muted ? "muted" : format_percent(state.volume.percent);
        text_.draw_left(cr, text_x, center_y, label,
                        state.volume.muted ? config_->dim : config_->text);
        break;
      }
      case Item::Wifi: {
        if (!state.wifi.present) break;
        const std::string glyph = wifi_icon(*config_, state.wifi);
        const Color icon_color = state.wifi.connected ? config_->text : config_->dim;
        icon_text_.draw_center(cr, r.x + r.w / 2.0, center_y, glyph, icon_color);
        break;
      }
      case Item::Bluetooth: {
        if (!state.bluetooth.present) break;
        const double glyph_px = std::max(1.0, std::round((r.w * 0.020 / 5.0) * scale_) / scale_);
        const double glyph_h = 7.0 * glyph_px;
        const double gy = center_y - glyph_h / 2.0;
        bool any_connected = false;
        for (const BluetoothDevice& device : state.bluetooth.devices) {
          if (device.connected) any_connected = true;
        }
        const Color color = any_connected
                                ? config_->window_icon
                                : (state.bluetooth.powered ? config_->text : config_->dim);
        glyphs::draw(cr, glyphs::bluetooth(), r.x, gy, glyph_px, color);
        break;
      }
      case Item::Workspaces: {
        for (std::size_t i = 0; i < layout.workspace_slots.size(); ++i) {
          const WorkspaceSlot& slot = layout.workspace_slots[i];
          const WorkspaceState* ws = i < state.workspaces.list.size() ? &state.workspaces.list[i]
                                                                     : nullptr;
          const int id = ws != nullptr ? ws->id : slot.id;
          const bool focused = ws != nullptr && ws->focused;
          const bool occupied = ws != nullptr && ws->occupied;
          // The focused workspace keeps its purple fill even when it holds no
          // windows, so the active workspace is always visible.
          if (focused || occupied) {
            set_color(cr, focused ? config_->ws_focused : config_->ws_occupied);
            cairo_arc(cr, slot.cx, slot.cy, slot.diameter / 2.0, 0, 2.0 * kPi);
            cairo_fill(cr);
            if (focused && config_->ws_focus_ring) {
              const double ring_w = std::max(
                  1.0 / scale_,
                  std::round(slot.diameter * 0.16 * scale_) / scale_);
              set_color(cr, config_->ws_focus_ring_color);
              cairo_set_line_width(cr, ring_w);
              cairo_arc(cr, slot.cx, slot.cy, slot.diameter / 2.0 - ring_w / 2.0, 0, 2.0 * kPi);
              cairo_stroke(cr);
            }
            text_.draw_center(cr, slot.cx, slot.cy, std::to_string(id),
                              focused ? config_->ws_focused_text : config_->text);
          } else {
            text_.draw_center(cr, slot.cx, slot.cy, std::to_string(id), config_->dim);
          }
        }
        break;
      }
      case Item::ActiveWindow: {
        if (!state.window.present) break;
        const double glyph_px = std::max(1.0, std::round((r.w * 0.022 / 9.0) * scale_) / scale_);
        const double glyph_w = 9.0 * glyph_px;
        const double glyph_h = 9.0 * glyph_px;
        const double gy = center_y - glyph_h / 2.0;
        glyphs::draw(cr, glyphs::gem(), r.x, gy, glyph_px, config_->window_icon);
        const double text_x = r.x + glyph_w + layout.bar_w * 0.010;
        const double max_w = static_cast<double>(r.x + r.w) - text_x;
        const std::string title =
            uppercase_ascii(state.window.cls.empty() ? state.window.title : state.window.cls);
        text_.draw_left(cr, text_x, center_y, ellipsize(title, max_w), config_->text);
        break;
      }
      case Item::Clock: {
        const double center_x = r.x + r.w / 2.0;
        text_.draw_center(cr, center_x, center_y, format_clock(state.clock), config_->text);
        break;
      }
      case Item::Cpu: {
        if (!state.cpu.present) break;
        const double glyph_px = std::max(1.0, std::round((r.w * 0.020 / 9.0) * scale_) / scale_);
        const double glyph_w = 9.0 * glyph_px;
        const double glyph_h = 9.0 * glyph_px;
        glyphs::draw(cr, glyphs::cpu(), r.x, center_y - glyph_h / 2.0, glyph_px, config_->text);
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(state.cpu.total + 0.5));
        text_.draw_left(cr, r.x + glyph_w + layout.bar_w * 0.010, center_y, buffer, config_->text);
        break;
      }
      case Item::Gpu: {
        if (!state.gpu.present) break;
        const double glyph_px = std::max(1.0, std::round((r.w * 0.020 / 9.0) * scale_) / scale_);
        const double glyph_w = 9.0 * glyph_px;
        const double glyph_h = 9.0 * glyph_px;
        glyphs::draw(cr, glyphs::cpu(), r.x, center_y - glyph_h / 2.0, glyph_px,
                     config_->window_icon);
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%d%%",
                      static_cast<int>(state.gpu.utilization + 0.5));
        text_.draw_left(cr, r.x + glyph_w + layout.bar_w * 0.010, center_y, buffer, config_->text);
        break;
      }
      default:
        break;
    }
  }
}

}  // namespace pillbar
