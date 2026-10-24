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
};

const PixelGlyph& bluetooth();
const PixelGlyph& cpu();

// Draws a glyph with top-left at (x, y), each bitmap pixel being `px` logical
// pixels (nearest-neighbour, integer-aligned rects).
void draw(cairo_t* cr, const PixelGlyph& glyph, double x, double y, double px, const Color& color);

}  // namespace glyphs
}  // namespace pillbar
