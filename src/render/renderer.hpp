#pragma once

#include <cairo.h>

#include <string>
#include <vector>

#include "app/config.hpp"
#include "model/state.hpp"
#include "render/layout.hpp"
#include "render/text.hpp"

namespace pillbar {

struct TooltipMetrics {
  double width = 0.0;
  double height = 0.0;
  double line_height = 0.0;
};

// Draws the bar and tooltip in logical coordinates. The caller scales the
// Cairo context by the surface scale before calling these methods.
class Renderer {
 public:
  void configure(const Config& config);
  TextRenderer& text() { return text_; }

  void draw_bar(cairo_t* cr, const BarLayout& layout, const AppState& state);
  void draw_tooltip(cairo_t* cr, const std::vector<std::string>& lines, double opacity,
                    const TooltipMetrics& metrics, double x_offset = 0.0);

  TooltipMetrics measure_tooltip(const std::vector<std::string>& lines) const;

  void set_scale(double scale) { scale_ = scale > 0.0 ? scale : 1.0; }
  double scale() const { return scale_; }

  // Ellipsizes `text` so it fits within `max_width` logical pixels.
  std::string ellipsize(const std::string& text, double max_width) const;

 private:
  const Config* config_ = nullptr;
  TextRenderer text_;
  TextRenderer icon_text_;
  double scale_ = 1.0;
};

}  // namespace pillbar
