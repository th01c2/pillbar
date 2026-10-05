#pragma once

#include <map>
#include <string>
#include <vector>

namespace pillbar {

struct Color {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  double a = 1.0;
};

struct ItemSlot {
  bool enabled = true;
  double x0 = 0.0;  // fraction of bar width
  double x1 = 1.0;  // fraction of bar width
};

struct Config {
  // --- bar geometry ---
  double width_frac = 0.30;         // fraction of output width
  double height_px = 30.0;          // logical pixels
  double margin_top_frac = 0.012;   // fraction of output height (~1.2%)
  std::string layer = "top";        // background | bottom | top | overlay
  std::string namespace_name = "pillbar";
  std::string monitors = "all";     // all | primary

  Color bar_color{0.1255, 0.1255, 0.1255, 1.0};  // #202020
  bool shadow = false;
  Color shadow_color{0.0, 0.0, 0.0, 0.35};
  double shadow_offset_px = 2.0;
  double shadow_blur_px = 6.0;

  // --- typography ---
  std::vector<std::string> fonts{"Departure Mono", "Terminus", "Cozette", "monospace"};
  double font_size_px = 12.0;
  double letter_spacing = 0.05;  // +5%

  // --- palette ---
  Color text{0.902, 0.902, 0.902, 1.0};      // #e6e6e6
  Color dim{0.722, 0.722, 0.722, 1.0};       // #b8b8b8
  Color ws_occupied{0.290, 0.290, 0.306, 1.0};  // #4a4a4e
  // Active workspace: a clearly distinct, soft periwinkle accent so it is easy
  // to spot (the reference #55555a was nearly identical to the occupied fill).
  Color ws_focused{0.435, 0.498, 0.847, 1.0};    // #6f7fd8
  Color ws_focused_text{1.0, 1.0, 1.0, 1.0};     // #ffffff
  bool ws_focus_ring = false;                    // draw a ring around the active circle
  Color ws_focus_ring_color{1.0, 1.0, 1.0, 1.0}; // #ffffff
  Color battery_green{0.239, 0.863, 0.353, 1.0};  // #3ddc5a
  Color battery_low{1.0, 0.333, 0.333, 1.0};      // #ff5555
  Color window_icon{0.435, 0.498, 0.847, 1.0};    // #6f7fd8
  Color speaker{0.902, 0.902, 0.902, 1.0};        // #e6e6e6

  // --- items ---
  std::vector<std::string> order{"battery", "volume", "wifi", "bluetooth",
                                 "workspaces", "window", "clock"};
  std::map<std::string, ItemSlot> slots;

  // --- tooltip ---
  int tooltip_delay_ms = 250;
  double tooltip_radius_px = 8.0;
  double tooltip_gap_px = 6.0;
  double tooltip_pad_px = 8.0;
  double tooltip_font_size_px = 11.0;

  // --- behavior ---
  bool system_item_tooltip = true;  // CPU/GPU exposed via the clock/system tooltip
  int min_workspaces = 5;
  std::string clock_tooltip_format = "%A, %d %B %Y  %H:%M:%S";
};

// Fills default item slots matching the reference screenshot.
void config_apply_default_slots(Config& config);

// Returns the default config path: $XDG_CONFIG_HOME/pillbar/config.toml
std::string config_default_path();

// Loads config from `path`; on failure returns defaults and sets *used_defaults.
Config config_load(const std::string& path, bool* used_defaults);

Color color_from_hex(const std::string& hex, Color fallback);
std::string color_to_hex(const Color& color);

}  // namespace pillbar
