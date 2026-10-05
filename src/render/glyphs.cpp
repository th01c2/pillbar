#include "render/glyphs.hpp"

#include <algorithm>

namespace pillbar {
namespace glyphs {
namespace {

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

}  // namespace glyphs
}  // namespace pillbar
