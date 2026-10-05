#include "app/config.hpp"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "app/logging.hpp"
#include "app/toml.hpp"

namespace pillbar {
namespace {

int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

Color color_from_hex(const std::string& hex, Color fallback) {
  std::string s = hex;
  if (!s.empty() && s.front() == '#') s.erase(s.begin());
  auto parse_channel = [&](std::size_t offset) -> int {
    if (offset + 1 >= s.size()) return -1;
    const int hi = hex_nibble(s[offset]);
    const int lo = hex_nibble(s[offset + 1]);
    if (hi < 0 || lo < 0) return -1;
    return hi * 16 + lo;
  };
  if (s.size() == 6) {
    const int r = parse_channel(0);
    const int g = parse_channel(2);
    const int b = parse_channel(4);
    if (r < 0 || g < 0 || b < 0) return fallback;
    return Color{r / 255.0, g / 255.0, b / 255.0, 1.0};
  }
  if (s.size() == 8) {
    const int r = parse_channel(0);
    const int g = parse_channel(2);
    const int b = parse_channel(4);
    const int a = parse_channel(6);
    if (r < 0 || g < 0 || b < 0 || a < 0) return fallback;
    return Color{r / 255.0, g / 255.0, b / 255.0, a / 255.0};
  }
  return fallback;
}

std::string color_to_hex(const Color& color) {
  auto clamp = [](double v) { return static_cast<int>(v * 255.0 + 0.5); };
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", clamp(color.r), clamp(color.g),
                clamp(color.b));
  return buffer;
}

void config_apply_default_slots(Config& config) {
  config.slots.clear();
  // Fractions chosen so all items (including the added WiFi/Bluetooth cluster)
  // fit while keeping the reference look; see README "Layout assumption".
  config.slots["battery"] = ItemSlot{true, 0.055, 0.155};
  config.slots["volume"] = ItemSlot{true, 0.185, 0.285};
  config.slots["wifi"] = ItemSlot{true, 0.318, 0.340};
  config.slots["bluetooth"] = ItemSlot{true, 0.350, 0.372};
  config.slots["workspaces"] = ItemSlot{true, 0.400, 0.700};
  config.slots["window"] = ItemSlot{true, 0.730, 0.845};
  config.slots["clock"] = ItemSlot{true, 0.875, 0.960};
}

std::string config_default_path() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  std::string base;
  if (xdg != nullptr && *xdg != '\0') {
    base = xdg;
  } else {
    const char* home = std::getenv("HOME");
    base = home != nullptr ? std::string(home) + "/.config" : std::string(".");
  }
  return base + "/pillbar/config.toml";
}

namespace {

const TomlValue* find_value(const TomlTable& table, const std::string& key) {
  const auto it = table.find(key);
  return it == table.end() ? nullptr : &it->second;
}

bool get_bool(const TomlTable& t, const std::string& key, bool fallback) {
  const TomlValue* v = find_value(t, key);
  return v != nullptr ? v->as_bool(fallback) : fallback;
}

double get_double(const TomlTable& t, const std::string& key, double fallback) {
  const TomlValue* v = find_value(t, key);
  return v != nullptr ? v->as_double(fallback) : fallback;
}

int get_int(const TomlTable& t, const std::string& key, int fallback) {
  const TomlValue* v = find_value(t, key);
  return v != nullptr ? static_cast<int>(v->as_int(fallback)) : fallback;
}

std::string get_string(const TomlTable& t, const std::string& key, const std::string& fallback) {
  const TomlValue* v = find_value(t, key);
  return v != nullptr ? v->as_string(fallback) : fallback;
}

Color get_color(const TomlTable& t, const std::string& key, const Color& fallback) {
  const TomlValue* v = find_value(t, key);
  if (v == nullptr || v->type != TomlValue::Type::String) return fallback;
  return color_from_hex(v->string_value, fallback);
}

std::vector<std::string> get_string_array(const TomlTable& t, const std::string& key,
                                          const std::vector<std::string>& fallback) {
  const TomlValue* v = find_value(t, key);
  if (v == nullptr || v->type != TomlValue::Type::Array) return fallback;
  std::vector<std::string> out;
  for (const TomlValue& element : v->array_value) {
    if (element.type == TomlValue::Type::String) out.push_back(element.string_value);
  }
  return out.empty() ? fallback : out;
}

}  // namespace

Config config_load(const std::string& path, bool* used_defaults) {
  Config config;
  config_apply_default_slots(config);

  bool ok = false;
  const std::string text = toml_read_file(path, &ok);
  if (!ok) {
    if (used_defaults != nullptr) *used_defaults = true;
    return config;
  }

  std::string error;
  const TomlTable table = toml_parse(text, &error);
  if (!error.empty()) {
    LOG_WARN("config %s: %s (using defaults for missing keys)", path.c_str(), error.c_str());
  }
  if (used_defaults != nullptr) *used_defaults = false;

  config.width_frac = get_double(table, "bar.width_frac", config.width_frac);
  config.height_px = get_double(table, "bar.height_px", config.height_px);
  config.margin_top_frac = get_double(table, "bar.margin_top_frac", config.margin_top_frac);
  config.layer = get_string(table, "bar.layer", config.layer);
  config.namespace_name = get_string(table, "bar.namespace", config.namespace_name);
  config.monitors = get_string(table, "bar.monitors", config.monitors);
  config.bar_color = get_color(table, "bar.color", config.bar_color);
  config.shadow = get_bool(table, "bar.shadow", config.shadow);
  config.shadow_color = get_color(table, "bar.shadow_color", config.shadow_color);
  config.shadow_offset_px = get_double(table, "bar.shadow_offset_px", config.shadow_offset_px);
  config.shadow_blur_px = get_double(table, "bar.shadow_blur_px", config.shadow_blur_px);

  config.fonts = get_string_array(table, "font.families", config.fonts);
  config.icon_fonts = get_string_array(table, "font.icon_families", config.icon_fonts);
  config.icon_size_frac = get_double(table, "font.icon_size_frac", config.icon_size_frac);
  config.font_size_px = get_double(table, "font.size_px", config.font_size_px);
  config.letter_spacing = get_double(table, "font.letter_spacing", config.letter_spacing);

  config.battery_levels = get_string_array(table, "icons.battery", config.battery_levels);
  config.battery_charging =
      get_string_array(table, "icons.battery_charging", config.battery_charging);
  config.volume_muted = get_string(table, "icons.volume_muted", config.volume_muted);
  config.volume_low = get_string(table, "icons.volume_low", config.volume_low);
  config.volume_medium = get_string(table, "icons.volume_medium", config.volume_medium);
  config.volume_high = get_string(table, "icons.volume_high", config.volume_high);
  config.wifi_levels = get_string_array(table, "icons.wifi", config.wifi_levels);
  config.wifi_off = get_string(table, "icons.wifi_off", config.wifi_off);
  config.wifi_wired = get_string(table, "icons.wifi_wired", config.wifi_wired);

  config.text = get_color(table, "colors.text", config.text);
  config.dim = get_color(table, "colors.dim", config.dim);
  config.ws_occupied = get_color(table, "colors.workspace_occupied", config.ws_occupied);
  config.ws_focused = get_color(table, "colors.workspace_focused", config.ws_focused);
  config.ws_focused_text =
      get_color(table, "colors.workspace_focused_text", config.ws_focused_text);
  config.ws_focus_ring_color =
      get_color(table, "colors.workspace_focus_ring", config.ws_focus_ring_color);
  config.battery_green = get_color(table, "colors.battery_green", config.battery_green);
  config.battery_low = get_color(table, "colors.battery_low", config.battery_low);
  config.window_icon = get_color(table, "colors.window_icon", config.window_icon);
  config.speaker = get_color(table, "colors.speaker", config.speaker);
  config.bar_color = get_color(table, "colors.bar", config.bar_color);

  config.order = get_string_array(table, "items.order", config.order);

  // Per-item slots: [items.battery] x0/1, [items.volume] x0/1, ...
  for (const std::string& name : config.order) {
    ItemSlot slot = config.slots.count(name) != 0 ? config.slots[name] : ItemSlot{};
    slot.x0 = get_double(table, "items." + name + ".x0", slot.x0);
    slot.x1 = get_double(table, "items." + name + ".x1", slot.x1);
    slot.enabled = get_bool(table, "items." + name + ".enabled", slot.enabled);
    config.slots[name] = slot;
  }

  config.tooltip_delay_ms = get_int(table, "tooltip.delay_ms", config.tooltip_delay_ms);
  config.tooltip_radius_px = get_double(table, "tooltip.radius_px", config.tooltip_radius_px);
  config.tooltip_gap_px = get_double(table, "tooltip.gap_px", config.tooltip_gap_px);
  config.tooltip_pad_px = get_double(table, "tooltip.pad_px", config.tooltip_pad_px);
  config.tooltip_font_size_px =
      get_double(table, "tooltip.font_size_px", config.tooltip_font_size_px);

  config.system_item_tooltip =
      get_bool(table, "behavior.system_item_tooltip", config.system_item_tooltip);
  config.min_workspaces = get_int(table, "behavior.min_workspaces", config.min_workspaces);
  config.ws_focus_ring =
      get_bool(table, "behavior.workspace_focus_ring", config.ws_focus_ring);
  config.clock_tooltip_format =
      get_string(table, "behavior.clock_tooltip_format", config.clock_tooltip_format);

  if (config.fonts.empty()) config.fonts = {"monospace"};
  if (config.order.empty()) config.order = {"battery", "clock"};
  return config;
}

}  // namespace pillbar
