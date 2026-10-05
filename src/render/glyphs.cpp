#include "render/glyphs.hpp"

#include <algorithm>

namespace pillbar {
namespace glyphs {
namespace {

const PixelGlyph kBattery = {
    {"#########..", "#.......#..", "#.......#.#", "#.......#.#",
     "#.......#.#", "#.......#..", "#########.."}};

const PixelGlyph kBolt = {
    {"....#..", "...##..", "..##...", ".#####.", "...##..", "..##...", ".##...."}};

const PixelGlyph kSpeaker = {
    {"...#...", "..##...", ".###.#.", "#####.#", ".###.#.", "..##...", "...#..."}};

const PixelGlyph kSpeakerMuted = {
    {"...#...", "..##.#.", ".###..#", "#####..", ".###..#", "..##.#.", "...#..."}};

const PixelGlyph kWifi = {
    {"#......#", "#.....##", "#.....##", "#.....##", "#.....##", "#.....##", "########"}};

const PixelGlyph kBluetooth = {
    {"..#..", "..##.", ".#.#.", "#..##", ".#.#.", "..##.", "..#.."}};

const PixelGlyph kGem = {
    {"....#....", "...#.#...", "..#.#.#..", ".#.....#.", "#...#...#",
     ".#.....#.", "..#...#..", "...#.#...", "....#...."}};

const PixelGlyph kCpu = {
    {"#.#.#.#.#", ".........", "..#####..", "#.#...#.#", "#.#...#.#",
     "#.#...#.#", "..#####..", ".........", "#.#.#.#.#"}};

void fill_pixel(cairo_t* cr, double x, double y, double px, const Color& color) {
  cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
  cairo_rectangle(cr, x, y, px, px);
  cairo_fill(cr);
}

}  // namespace

const PixelGlyph& battery() { return kBattery; }
const PixelGlyph& bolt() { return kBolt; }
const PixelGlyph& speaker() { return kSpeaker; }
const PixelGlyph& speaker_muted() { return kSpeakerMuted; }
const PixelGlyph& wifi() { return kWifi; }
const PixelGlyph& bluetooth() { return kBluetooth; }
const PixelGlyph& gem() { return kGem; }
const PixelGlyph& cpu() { return kCpu; }

double glyph_aspect(const PixelGlyph& glyph) {
  const int h = glyph.height();
  return h > 0 ? static_cast<double>(glyph.width()) / static_cast<double>(h) : 1.0;
}

void draw(cairo_t* cr, const PixelGlyph& glyph, double x, double y, double px, const Color& color) {
  if (px <= 0.0) return;
  for (std::size_t row = 0; row < glyph.rows.size(); ++row) {
    const std::string& line = glyph.rows[row];
    for (std::size_t col = 0; col < line.size(); ++col) {
      if (line[col] != '#') continue;
      fill_pixel(cr, x + static_cast<double>(col) * px, y + static_cast<double>(row) * px, px,
                 color);
    }
  }
}

void draw_wifi(cairo_t* cr, double x, double y, double box, int bars, bool connected,
               const Color& on, const Color& off) {
  const int count = std::clamp(bars, 0, 4);
  const double bar_w = box / 7.0;  // 4 bars of width bar_w separated by bar_w
  const double base_y = y + box;
  for (int i = 0; i < 4; ++i) {
    const double h = bar_w * static_cast<double>(i + 1);
    const double bx = x + static_cast<double>(i) * (bar_w * 1.6) + bar_w * 0.3;
    const bool lit = connected && i < count;
    const Color& color = lit ? on : off;
    cairo_set_source_rgba(cr, color.r, color.g, color.b, lit ? 1.0 : 0.45);
    cairo_rectangle(cr, bx, base_y - h, bar_w, h);
    cairo_fill(cr);
  }
  if (!connected) {
    cairo_set_source_rgba(cr, on.r, on.g, on.b, 0.9);
    cairo_set_line_width(cr, std::max(1.0, box / 10.0));
    cairo_move_to(cr, x, y + box);
    cairo_line_to(cr, x + box, y);
    cairo_stroke(cr);
  }
}

}  // namespace glyphs
}  // namespace pillbar
