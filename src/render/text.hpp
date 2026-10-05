#pragma once

#include <cairo.h>

#include <string>
#include <vector>

#include "app/config.hpp"

namespace pillbar {

// Pango/Cairo text with antialiasing disabled and full hinting, so bitmap and
// pixel fonts stay crisp. All coordinates are logical pixels.
class TextRenderer {
 public:
  void configure(const std::vector<std::string>& families, double size_px, double letter_spacing);

  double size_px() const { return size_px_; }
  // Width of the inked text in logical pixels.
  double measure(const std::string& text) const;
  double ink_height(const std::string& text) const;

  // Draws `text` with its ink-box left edge at x and vertically centered on
  // center_y.
  void draw_left(cairo_t* cr, double x, double center_y, const std::string& text,
                 const Color& color) const;
  // Draws `text` horizontally centered on center_x.
  void draw_center(cairo_t* cr, double center_x, double center_y, const std::string& text,
                   const Color& color) const;

 private:
  mutable std::vector<std::string> families_;
  double size_px_ = 12.0;
  double letter_spacing_ = 0.05;
};

}  // namespace pillbar

