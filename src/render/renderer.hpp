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

  double text_width(const std::string& text) const { return text_.measure(text); }
  double icon_width(const std::string& icon) const { return icon_text_.measure(icon); }

  // Natural pill width for the given state: the sum of each item's intrinsic
  // content width (icon + text + padding + gaps), clamped to the output.
  double desired_width(const Config& config, const AppState& state, int output_w) const;
  // Positions the enabled items for a pill of width `pill_w`, centered on
  // `output_w`. Items keep their intrinsic widths, so a wider pill adds
  // breathing room rather than stretching the text.
  BarLayout compute_layout(const Config& config, const AppState& state, double pill_w,
                           int output_w, int output_h) const;

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
