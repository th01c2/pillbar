#pragma once

#include <cairo.h>

#include <string>
#include <vector>

#include "app/config.hpp"

namespace pillbar {
namespace glyphs {

// A crisp pixel-grid glyph. '#' is an opaque pixel, '.' is transparent.
struct PixelGlyph {
  std::vector<std::string> rows;
  int width() const { return rows.empty() ? 0 : static_cast<int>(rows[0].size()); }
  int height() const { return static_cast<int>(rows.size()); }
};

const PixelGlyph& battery();
const PixelGlyph& bolt();
const PixelGlyph& speaker();
const PixelGlyph& speaker_muted();
const PixelGlyph& wifi();
const PixelGlyph& bluetooth();
const PixelGlyph& gem();
const PixelGlyph& cpu();

// Draws a glyph with top-left at (x, y), each bitmap pixel being `px` logical
// pixels (nearest-neighbour, integer-aligned rects).
void draw(cairo_t* cr, const PixelGlyph& glyph, double x, double y, double px, const Color& color);

// Draws Wi-Fi signal as up to 4 stacked bars. `bars` is 0..4.
void draw_wifi(cairo_t* cr, double x, double y, double box, int bars, bool connected,
               const Color& on, const Color& off);

double glyph_aspect(const PixelGlyph& glyph);

}  // namespace glyphs
}  // namespace pillbar

