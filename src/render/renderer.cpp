#include "render/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "app/logging.hpp"
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

// Rectangle with only the bottom corners rounded, so it sits flush against the
// screen edge (notch style).
void notch_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  const double radius = std::min(r, std::min(w / 2.0, h));
  cairo_new_sub_path(cr);
  cairo_move_to(cr, x, y);
  cairo_line_to(cr, x + w, y);
  cairo_line_to(cr, x + w, y + h - radius);
  cairo_arc(cr, x + w - radius, y + h - radius, radius, 0.0, kPi / 2.0);
  cairo_line_to(cr, x + radius, y + h);
  cairo_arc(cr, x + radius, y + h - radius, radius, kPi / 2.0, kPi);
  cairo_close_path(cr);
}

void set_color(cairo_t* cr, const Color& c, double alpha_mul = 1.0) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a * alpha_mul);
}

// Length in bytes of the UTF-8 character starting at `index` (at least 1).
std::size_t utf8_char_width(const std::string& input, std::size_t index) {
  const unsigned char lead = static_cast<unsigned char>(input[index]);
  std::size_t width = 1;
  if ((lead & 0x80u) == 0u) {
    width = 1;
  } else if ((lead & 0xE0u) == 0xC0u) {
    width = 2;
  } else if ((lead & 0xF0u) == 0xE0u) {
    width = 3;
  } else if ((lead & 0xF8u) == 0xF0u) {
    width = 4;
  }
  return std::min(width, input.size() - index);
}

// Truncates on UTF-8 boundaries; when cut, the last character becomes an
// ellipsis so the result is still `max_chars` characters at most.
std::string truncate_chars(const std::string& input, int max_chars) {
  if (max_chars <= 0) return {};
  std::size_t index = 0;
  int count = 0;
  while (index < input.size()) {
    const std::size_t start = index;
    index += utf8_char_width(input, index);
    ++count;
    if (count == max_chars && index < input.size()) {
      return input.substr(0, start) + "\u2026";
    }
  }
  return input;
}

std::string format_percent(int value) { return std::to_string(value) + "%"; }

std::string format_clock(const ClockState& clock) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d", clock.hour, clock.minute);
  return buffer;
}

// 10%..100% maps to index 0..9; anything below 10% shows the 10% icon.
int battery_icon_index(int percent) {
  return std::clamp((percent + 5) / 10, 1, 10) - 1;
}

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
  // Wired wins when both are up.
  if (wifi.wired) return config.wifi_wired;
  if (!wifi.connected) return config.wifi_off;
  if (config.wifi_levels.empty()) return config.wifi_off;
  const int level = std::clamp((wifi.signal + 12) / 25, 1,
                               static_cast<int>(config.wifi_levels.size()));
  return config.wifi_levels[static_cast<std::size_t>(level) - 1];
}

// Layout metrics (logical pixels).
constexpr double kPillPad = 14.0;      // left/right padding inside the pill
constexpr double kItemGap = 12.0;      // space between items
constexpr double kIconGap = 6.0;       // icon -> label inside an item
constexpr double kNameTitleGap = 8.0;  // app name -> window title
constexpr int kAppNameMaxChars = 20;

Item item_of(const std::string& name) {
  if (name == "battery") return Item::Battery;
  if (name == "volume") return Item::Volume;
  if (name == "wifi") return Item::Wifi;
  if (name == "bluetooth") return Item::Bluetooth;
  if (name == "workspaces") return Item::Workspaces;
  if (name == "window") return Item::ActiveWindow;
  if (name == "recorder") return Item::Recorder;
  if (name == "clock") return Item::Clock;
  if (name == "cpu") return Item::Cpu;
  if (name == "gpu") return Item::Gpu;
  if (name == "vpn") return Item::Vpn;
  return Item::None;
}

// One rendered workspace numeral: its id plus the index into
// AppState::workspaces.list (-1 when nothing is known about it).
struct WorkspaceDisplay {
  int id = 0;
  int state_index = -1;
};

// The numerals to draw: ids 1..min_workspaces always (5 by default), plus any
// higher-numbered workspace that actually exists or is focused. Opening
// workspace 7 therefore adds "7" without dragging 6 along, and the gaps stay
// hidden.
std::vector<WorkspaceDisplay> display_workspaces(const Config& config, const AppState& state) {
  const int base = config.min_workspaces > 0 ? config.min_workspaces : 5;
  std::vector<WorkspaceDisplay> out;
  out.reserve(static_cast<std::size_t>(base) + state.workspaces.list.size());
  for (int id = 1; id <= base; ++id) {
    int index = -1;
    for (std::size_t i = 0; i < state.workspaces.list.size(); ++i) {
      if (state.workspaces.list[i].id == id) {
        index = static_cast<int>(i);
        break;
      }
    }
    out.push_back(WorkspaceDisplay{id, index});
  }
  for (std::size_t i = 0; i < state.workspaces.list.size(); ++i) {
    const WorkspaceState& ws = state.workspaces.list[i];
    if (ws.id > base && (ws.occupied || ws.focused)) {
      out.push_back(WorkspaceDisplay{ws.id, static_cast<int>(i)});
    }
  }
  std::sort(out.begin(), out.end(),
            [](const WorkspaceDisplay& a, const WorkspaceDisplay& b) { return a.id < b.id; });
  return out;
}

std::string window_name_text(const WindowState& window) {
  if (!window.cls.empty()) return truncate_chars(window.cls, kAppNameMaxChars);
  return {};
}

std::string window_title_text(const Config& config, const WindowState& window) {
  const std::string& raw = window.title.empty() ? window.cls : window.title;
  return truncate_chars(raw, config.window_title_max_chars);
}

std::string volume_label(const VolumeState& volume) {
  return volume.muted ? std::string("muted") : format_percent(volume.percent);
}

// Intrinsic content width of one item, or a negative value when the item has
// nothing to show and should be skipped.
double item_width(const Renderer& renderer, const Config& config, const AppState& state,
                  Item item) {
  switch (item) {
    case Item::Battery:
      if (!state.battery.present) return -1.0;
      return renderer.icon_width(battery_icon(config, state.battery)) + kIconGap +
             renderer.text_width(format_percent(state.battery.percent));
    case Item::Volume:
      if (!state.volume.available) return -1.0;
      return renderer.icon_width(volume_icon(config, state.volume)) + kIconGap +
             renderer.text_width(volume_label(state.volume));
    case Item::Wifi:
      if (!state.wifi.present) return -1.0;
      return renderer.icon_width(wifi_icon(config, state.wifi));
    case Item::Bluetooth:
      if (!state.bluetooth.present) return -1.0;
      return config.height_px * 0.5;
    case Item::Workspaces: {
      const std::size_t count = display_workspaces(config, state).size();
      if (count == 0) return -1.0;
      const double diameter = std::min(20.0, config.height_px * 0.62);
      return static_cast<double>(count) * diameter * 1.35;
    }
    case Item::ActiveWindow: {
      if (!state.window.present) return -1.0;
      double width = renderer.icon_width(config.window_glyph) + kIconGap;
      const std::string name = window_name_text(state.window);
      if (!name.empty()) width += renderer.text_width(name) + kNameTitleGap;
      width += renderer.text_width(window_title_text(config, state.window));
      return width;
    }
    case Item::Clock:
      return renderer.text_width(format_clock(state.clock));
    case Item::Recorder:
      if (!state.recorder.active) return -1.0;
      return renderer.icon_width(config.recorder_glyph);
    case Item::Vpn:
      if (!state.vpn.present || config.vpn_glyph.empty()) return -1.0;
      return renderer.icon_width(config.vpn_glyph);
    default:
      return -1.0;
  }
}

}  // namespace

void Renderer::configure(const Config& config) {
  config_ = &config;
  text_.configure(config.fonts, config.font_size_px, config.letter_spacing,
                  config.smooth_text);
  icon_text_.configure(config.icon_fonts,
                       std::max(1.0, config.height_px * config.icon_size_frac), 0.0,
                       /*antialias=*/true);
}

double Renderer::desired_width(const Config& config, const AppState& state, int output_w) const {
  double total = kPillPad * 2.0;
  int shown = 0;
  for (const std::string& name : config.order) {
    const auto it = config.slots.find(name);
    if (it == config.slots.end() || !it->second.enabled) continue;
    const double width = item_width(*this, config, state, item_of(name));
    if (width < 0.0) continue;
    if (shown > 0) total += kItemGap;
    total += width;
    ++shown;
  }
  const double max_width = static_cast<double>(output_w) * 0.96;
  return std::min(std::max(total, 120.0), max_width);
}

BarLayout Renderer::compute_layout(const Config& config, const AppState& state, double pill_w,
                                   int output_w, int output_h) const {
  BarLayout layout;
  layout.bar_w = std::max(1, static_cast<int>(std::lround(pill_w)));
  layout.bar_h = std::max(1, static_cast<int>(std::lround(config.height_px)));
  layout.screen_x = (output_w - layout.bar_w) / 2;
  layout.screen_y =
      static_cast<int>(std::lround(static_cast<double>(output_h) * config.margin_top_frac));

  struct Entry {
    Item item = Item::None;
    double width = 0.0;
  };
  std::vector<Entry> entries;
  double content = 0.0;
  for (const std::string& name : config.order) {
    const auto it = config.slots.find(name);
    if (it == config.slots.end() || !it->second.enabled) continue;
    const Item item = item_of(name);
    const double width = item_width(*this, config, state, item);
    if (width < 0.0) continue;
    if (!entries.empty()) content += kItemGap;
    content += width;
    entries.push_back(Entry{item, width});
  }

  // If the pill is not wide enough yet (e.g. mid-animation), take the deficit
  // out of the window title, which is the only elastic item.
  const double available = static_cast<double>(layout.bar_w) - kPillPad * 2.0;
  if (content > available) {
    double deficit = content - available;
    for (Entry& entry : entries) {
      if (entry.item != Item::ActiveWindow) continue;
      const double take = std::min(deficit, entry.width);
      entry.width -= take;
      content -= take;
      deficit -= take;
      break;
    }
  }

  // Anchor the content in *screen* space, not pill space. Rounding the
  // pill-local start and the pill origin separately makes the text flip by one
  // pixel every frame while the pill width animates (visible as shivering);
  // rounding the screen position once keeps the text perfectly still and lets
  // the notch edges animate around it.
  double cursor_screen = std::lround((static_cast<double>(output_w) - content) / 2.0);
  const double min_screen = static_cast<double>(layout.screen_x) + kPillPad;
  if (cursor_screen < min_screen) cursor_screen = min_screen;
  const double center_y = static_cast<double>(layout.bar_h) / 2.0;
  for (const Entry& entry : entries) {
    const Rect rect{static_cast<int>(std::lround(cursor_screen)) - layout.screen_x, 0,
                    std::max(1, static_cast<int>(std::lround(entry.width))), layout.bar_h};
    layout.items.push_back(ItemBox{entry.item, rect});
    cursor_screen += entry.width + kItemGap;
  }

  std::size_t index = 0;
  for (const ItemBox& box : layout.items) {
    if (box.item != Item::Workspaces) continue;
    const std::vector<WorkspaceDisplay> slots = display_workspaces(config, state);
    const int count = static_cast<int>(slots.size());
    if (count <= 0) continue;
    const double diameter =
        std::min(20.0, std::min(static_cast<double>(box.rect.w) / count * 1.1,
                                config.height_px * 0.62));
    for (int i = 0; i < count; ++i) {
      WorkspaceSlot slot;
      slot.cx = static_cast<double>(box.rect.x) +
                (static_cast<double>(i) + 0.5) * static_cast<double>(box.rect.w) / count;
      slot.cy = center_y;
      slot.diameter = std::max(1.0, diameter);
      slot.id = slots[static_cast<std::size_t>(i)].id;
      slot.state_index = slots[static_cast<std::size_t>(i)].state_index;
      if (slot.state_index >= 0 &&
          state.workspaces.list[static_cast<std::size_t>(slot.state_index)].focused) {
        layout.workspace_focused_index = i;
      }
      layout.workspace_slots.push_back(slot);
    }
    index = layout.workspace_slots.size();
  }
  return layout;
}

std::string Renderer::ellipsize(const std::string& input, double max_width) const {
  if (max_width <= 0.0) return {};
  if (text_.measure(input) <= max_width) return input;
  const std::string dots = "\u2026";
  std::string best;
  // Walk character boundaries only: measuring or drawing a string cut in the
  // middle of a UTF-8 sequence is invalid and makes Pango warn.
  for (std::size_t len = 1; len <= input.size();) {
    const std::string candidate = input.substr(0, len) + dots;
    if (text_.measure(candidate) > max_width) break;
    best = candidate;
    len += utf8_char_width(input, len);
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
  tooltip_text.configure(config_->fonts, config_->tooltip_font_size_px,
                         config_->letter_spacing, config_->smooth_text);
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

  // Item rects are pill-local; the pill is centered inside the (wide) surface.
  cairo_save(cr);
  cairo_translate(cr, layout.screen_x, 0.0);

  const double radius = layout.bar_h / 2.0;
  if (config_->shadow) {
    for (int i = 6; i >= 1; --i) {
      const double spread = static_cast<double>(i) * (config_->shadow_blur_px / 6.0);
      set_color(cr, config_->shadow_color, 1.0 / static_cast<double>(i + 2));
      if (config_->notch) {
        notch_rect(cr, spread * 0.5, spread * 0.5 + config_->shadow_offset_px,
                   layout.bar_w - spread, layout.bar_h - spread, config_->notch_radius_px);
      } else {
        rounded_rect(cr, spread * 0.5, spread * 0.5 + config_->shadow_offset_px,
                     layout.bar_w - spread, layout.bar_h - spread, radius);
      }
      cairo_fill(cr);
    }
  }
  set_color(cr, config_->bar_color);
  if (config_->notch) {
    notch_rect(cr, 0, 0, layout.bar_w, layout.bar_h, config_->notch_radius_px);
  } else {
    rounded_rect(cr, 0, 0, layout.bar_w, layout.bar_h, radius);
  }
  cairo_fill(cr);

  // Item shapes (the workspace circles) are anti-aliased; text and icons set
  // their own font options, so this only affects the vector drawing.
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
  const double center_y = layout.bar_h / 2.0;

  for (const ItemBox& box : layout.items) {
    const Rect& r = box.rect;
    switch (box.item) {
      case Item::Battery: {
        if (!state.battery.present) break;
        Color color = config_->text;
        if (state.battery.charging) {
          color = config_->battery_green;
        } else if (state.battery.percent <= config_->battery_low_percent) {
          color = config_->battery_low;
        }
        const std::string glyph = battery_icon(*config_, state.battery);
        double glyph_w = 0.0;
        if (!glyph.empty()) {
          glyph_w = icon_text_.measure(glyph);
          icon_text_.draw_center(cr, r.x + glyph_w / 2.0, center_y, glyph, color);
        }
        const double text_x = r.x + glyph_w + kIconGap;
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
        const double text_x = r.x + glyph_w + kIconGap;
        const std::string label = volume_label(state.volume);
        text_.draw_left(cr, text_x, center_y, label,
                        state.volume.muted ? config_->dim : config_->text);
        break;
      }
      case Item::Wifi: {
        if (!state.wifi.present) break;
        const std::string glyph = wifi_icon(*config_, state.wifi);
        const Color icon_color =
            (state.wifi.wired || state.wifi.connected) ? config_->text : config_->dim;
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
        for (const WorkspaceSlot& slot : layout.workspace_slots) {
          const bool valid = slot.state_index >= 0 &&
                             static_cast<std::size_t>(slot.state_index) <
                                 state.workspaces.list.size();
          const WorkspaceState* ws =
              valid ? &state.workspaces.list[static_cast<std::size_t>(slot.state_index)] : nullptr;
          const int id = slot.id;
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
            set_color(cr, config_->ws_empty);
            cairo_arc(cr, slot.cx, slot.cy, slot.diameter / 2.0, 0, 2.0 * kPi);
            cairo_fill(cr);
            text_.draw_center(cr, slot.cx, slot.cy, std::to_string(id), config_->dim);
          }
        }
        break;
      }
      case Item::ActiveWindow: {
        if (!state.window.present) break;
        double glyph_w = 0.0;
        if (!config_->window_glyph.empty()) {
          glyph_w = icon_text_.measure(config_->window_glyph);
          icon_text_.draw_center(cr, r.x + glyph_w / 2.0, center_y, config_->window_glyph,
                                 config_->window_icon);
        }
        double text_x = r.x + glyph_w + kIconGap;
        const double max_x = static_cast<double>(r.x + r.w);
        // App name first (dim), then the window title capped to a fixed count.
        const std::string name = window_name_text(state.window);
        if (!name.empty() && text_x < max_x) {
          const double name_w = text_.measure(name);
          text_.draw_left(cr, text_x, center_y, ellipsize(name, max_x - text_x), config_->dim);
          text_x += name_w + kNameTitleGap;
        }
        if (text_x < max_x) {
          const std::string title = window_title_text(*config_, state.window);
          text_.draw_left(cr, text_x, center_y, ellipsize(title, max_x - text_x), config_->text);
        }
        break;
      }
      case Item::Clock: {
        const double center_x = r.x + r.w / 2.0;
        text_.draw_center(cr, center_x, center_y, format_clock(state.clock), config_->text);
        break;
      }
      case Item::Recorder: {
        if (!state.recorder.active || config_->recorder_glyph.empty()) break;
        // Pulse between dark red and bright red.
        const double p = std::clamp(state.recorder.pulse, 0.0, 1.0);
        const Color& dim = config_->recorder_color_dim;
        const Color& bright = config_->recorder_color;
        const Color color{dim.r + (bright.r - dim.r) * p, dim.g + (bright.g - dim.g) * p,
                          dim.b + (bright.b - dim.b) * p, 1.0};
        icon_text_.draw_center(cr, r.x + r.w / 2.0, center_y, config_->recorder_glyph, color);
        break;
      }
      case Item::Vpn: {
        if (!state.vpn.present || config_->vpn_glyph.empty()) break;
        const Color color = state.vpn.up ? config_->vpn_color : config_->vpn_color_down;
        icon_text_.draw_center(cr, r.x + r.w / 2.0, center_y, config_->vpn_glyph,
                               color);
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
  cairo_restore(cr);
}

}  // namespace pillbar
