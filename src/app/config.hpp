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

// 0xRRGGBB (+ optional alpha) -> Color, so palette entries can be written as
// the hex you see in a colour picker.
constexpr Color rgb(unsigned int hex, double alpha = 1.0) {
  return Color{static_cast<double>((hex >> 16) & 0xFFu) / 255.0,
               static_cast<double>((hex >> 8) & 0xFFu) / 255.0,
               static_cast<double>(hex & 0xFFu) / 255.0, alpha};
}

struct ItemSlot {
  bool enabled = true;
  double x0 = 0.0;  // fraction of bar width
  double x1 = 1.0;  // fraction of bar width
};

struct Config {
  // --- bar geometry ---
  double width_frac = 0.50;         // fraction of output width
  double height_px = 30.0;          // logical pixels
  double margin_top_frac = 0.0;     // fraction of output height
  // Notch shape: flush with the screen edge, rounded only on the bottom.
  bool notch = true;
  double notch_radius_px = 12.0;
  std::string layer = "top";        // background | bottom | top | overlay
  std::string namespace_name = "pillbar";
  std::string monitors = "all";     // all | primary

  Color bar_color = rgb(0x181818);  // notch fill
  bool shadow = false;
  Color shadow_color = rgb(0x000000, 0.35);
  double shadow_offset_px = 2.0;
  double shadow_blur_px = 6.0;

  // --- typography ---
  std::vector<std::string> fonts{"Departure Mono", "Terminus", "Cozette", "monospace"};
  // Anti-alias the text. The intended bitmap/pixel fonts want this off, but the
  // fallback families are vector fonts, where AA-off looks ragged at 12px.
  bool smooth_text = true;
  // Separate family list for Nerd Font icon glyphs, since the text families
  // above are bitmap/pixel fonts without Nerd Font symbols.
  std::vector<std::string> icon_fonts{"Symbols Nerd Font", "JetBrainsMono Nerd Font",
                                      "FiraCode Nerd Font", "Hack Nerd Font", "monospace"};
  // Icon glyph size as a fraction of the bar height.
  double icon_size_frac = 0.58;
  double font_size_px = 12.0;
  double letter_spacing = 0.05;  // +5%

  // --- nerd font icons ---
  // Battery icons for 10%,20%,...,100% (index 0..9).
  std::vector<std::string> battery_levels{
      "\U000F007A", "\U000F007B", "\U000F007C", "\U000F007D", "\U000F007E",
      "\U000F007F", "\U000F0080", "\U000F0081", "\U000F0082", "\U000F0079"};
  // Charging battery icons for 10%,20%,...,100% (index 0..9).
  std::vector<std::string> battery_charging{
      "\U000F089C", "\U000F0086", "\U000F0087", "\U000F0088", "\U000F089D",
      "\U000F0089", "\U000F089E", "\U000F008A", "\U000F008B", "\U000F0085"};
  std::string volume_muted = "\uEEE8";
  std::string volume_low = "\uF027";
  std::string volume_medium = "\uEFCF";
  std::string volume_high = "\uF028";
  // Wi-Fi signal strength for levels 1..4, plus off/disconnected and wired.
  std::vector<std::string> wifi_levels{"\U000F091F", "\U000F0922", "\U000F0925",
                                       "\U000F0928"};
  std::string wifi_off = "\U000F092F";
  std::string wifi_wired = "\U000F0200";
  std::string window_glyph = "\uEA85";
  // Screen-recorder indicator (nf-cod-record U+EBA7; U+EBFA and U+F044A also
  // exist). While recording it pulses from dark red to bright red and back.
  std::string recorder_glyph = "\uEBA7";
  int recorder_pulse_ms = 2000;  // one dark -> bright -> dark cycle
  // VPN indicator (nf-md-vpn): accent while the tunnel is up, red while down.
  // Hidden only when the unit does not exist on the system.
  std::string vpn_glyph = "\U000F0306";
  // systemd unit that brings the tunnel up (AmneziaWG).
  std::string vpn_unit = "awg-wg0.service";
  // Cursor size in logical pixels for the hand cursor over clickable items
  // (multiplied by the output scale internally, so it stays crisp on HiDPI).
  // Tune this to match the compositor's cursor: 24 is small, 48 is large.
  int cursor_size_px = 32;
  // Window title is truncated to this many characters before fitting.
  int window_title_max_chars = 20;

  // --- palette ---
  Color text = rgb(0xe6e6e6);
  Color dim = rgb(0xb8b8b8);
  Color ws_occupied = rgb(0x4a4a4e);
  // Empty workspaces: the same grey as occupied but faded, so they read as
  // "available" without competing with the ones holding windows. Drop the alpha
  // further for a subtler look, raise it towards 1.0 to make them louder.
  Color ws_empty = rgb(0x4a4a4e, 0.45);
  // Active workspace: a clearly distinct, soft periwinkle accent so it is easy
  // to spot (the reference #55555a was nearly identical to the occupied fill).
  Color ws_focused = rgb(0x6f7fd8);
  Color ws_focused_text = rgb(0xffffff);
  bool ws_focus_ring = false;                    // draw a ring around the active circle
  Color ws_focus_ring_color = rgb(0xffffff);
  Color battery_green = rgb(0x3ddc5a);
  Color battery_low = rgb(0xff5555);
  // At or below this charge the battery glyph and percent turn red.
  int battery_low_percent = 20;
  Color window_icon = rgb(0x6f7fd8);
  Color speaker = rgb(0xe6e6e6);
  Color recorder_color = rgb(0xff3b30);
  Color recorder_color_dim = rgb(0x7a0d08);
  // Matches the focused-workspace accent, so a healthy tunnel is calm rather
  // than a bright green that pulls the eye.
  Color vpn_color = rgb(0x6f7fd8);
  Color vpn_color_down = rgb(0xff5555);

  // --- items ---
  // Window item omitted on purpose: the app name + title took the most width
  // for the least information. Add "window" back here to restore it.
  std::vector<std::string> order{"battery", "volume", "wifi",  "bluetooth", "workspaces",
                                 "recorder", "vpn", "clock"};
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

}  // namespace pillbar
