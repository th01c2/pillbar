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

const PixelGlyph& bluetooth();
const PixelGlyph& cpu();

// Draws a glyph with top-left at (x, y), each bitmap pixel being `px` logical
// pixels (nearest-neighbour, integer-aligned rects).
void draw(cairo_t* cr, const PixelGlyph& glyph, double x, double y, double px, const Color& color);

double glyph_aspect(const PixelGlyph& glyph);

}  // namespace glyphs
}  // namespace pillbar
