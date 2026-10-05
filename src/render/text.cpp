#include "render/text.hpp"

#include <pango/pangocairo.h>

#include <cmath>
#include <cstring>

namespace pillbar {
namespace {

std::string join_families(const std::vector<std::string>& families) {
  std::string joined;
  for (std::size_t i = 0; i < families.size(); ++i) {
    if (i != 0) joined += ",";
    joined += families[i];
  }
  return joined.empty() ? "monospace" : joined;
}

// Builds a layout with AA disabled and full hinting.
PangoLayout* make_layout(cairo_t* cr, const std::string& family, double size_px,
                         double letter_spacing, bool antialias, PangoContext** ctx_out) {
  PangoContext* ctx = pango_cairo_create_context(cr);
  cairo_font_options_t* options = cairo_font_options_create();
  cairo_font_options_set_antialias(options, antialias ? CAIRO_ANTIALIAS_DEFAULT
                                                      : CAIRO_ANTIALIAS_NONE);
  cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_FULL);
  cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_ON);
  pango_cairo_context_set_font_options(ctx, options);
  cairo_font_options_destroy(options);

  PangoFontDescription* desc = pango_font_description_new();
  pango_font_description_set_family(desc, family.c_str());
  pango_font_description_set_absolute_size(desc, size_px * PANGO_SCALE);
  pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
  PangoLayout* layout = pango_layout_new(ctx);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
  if (letter_spacing != 0.0) {
    PangoAttrList* attrs = pango_attr_list_new();
    const int spacing = static_cast<int>(std::lround(letter_spacing * size_px * PANGO_SCALE));
    pango_attr_list_insert(attrs, pango_attr_letter_spacing_new(spacing));
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
  }
  if (ctx_out != nullptr) {
    *ctx_out = ctx;
  } else {
    g_object_unref(ctx);
  }
  return layout;
}

}  // namespace

void TextRenderer::configure(const std::vector<std::string>& families, double size_px,
                             double letter_spacing, bool antialias) {
  families_ = families;
  if (families_.empty()) families_ = {"monospace"};
  size_px_ = size_px > 0.0 ? size_px : 12.0;
  letter_spacing_ = letter_spacing;
  antialias_ = antialias;
}

double TextRenderer::measure(const std::string& text) const {
  // Use a tiny throwaway draw context: create a 1x1 image surface.
  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t* cr = cairo_create(surface);
  PangoLayout* layout =
      make_layout(cr, join_families(families_), size_px_, letter_spacing_, antialias_, nullptr);
  pango_layout_set_text(layout, text.c_str(), -1);
  PangoRectangle ink{};
  pango_layout_get_pixel_extents(layout, &ink, nullptr);
  const double width = ink.width;
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  return width;
}

double TextRenderer::ink_height(const std::string& text) const {
  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t* cr = cairo_create(surface);
  PangoLayout* layout =
      make_layout(cr, join_families(families_), size_px_, letter_spacing_, antialias_, nullptr);
  pango_layout_set_text(layout, text.c_str(), -1);
  PangoRectangle ink{};
  pango_layout_get_pixel_extents(layout, &ink, nullptr);
  const double height = ink.height;
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  return height;
}

void TextRenderer::draw_left(cairo_t* cr, double x, double center_y, const std::string& text,
                             const Color& color) const {
  if (text.empty()) return;
  PangoContext* ctx = nullptr;
  PangoLayout* layout =
      make_layout(cr, join_families(families_), size_px_, letter_spacing_, antialias_, &ctx);
  pango_layout_set_text(layout, text.c_str(), -1);
  PangoRectangle ink{};
  pango_layout_get_pixel_extents(layout, &ink, nullptr);
  cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
  const double origin_x = x - ink.x;
  const double origin_y = center_y - static_cast<double>(ink.y) - static_cast<double>(ink.height) / 2.0;
  cairo_move_to(cr, origin_x, origin_y);
  pango_cairo_update_layout(cr, layout);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);
  if (ctx != nullptr) g_object_unref(ctx);
}

void TextRenderer::draw_center(cairo_t* cr, double center_x, double center_y,
                               const std::string& text, const Color& color) const {
  if (text.empty()) return;
  PangoContext* ctx = nullptr;
  PangoLayout* layout =
      make_layout(cr, join_families(families_), size_px_, letter_spacing_, antialias_, &ctx);
  pango_layout_set_text(layout, text.c_str(), -1);
  PangoRectangle ink{};
  pango_layout_get_pixel_extents(layout, &ink, nullptr);
  cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
  const double origin_x = center_x - static_cast<double>(ink.width) / 2.0 - static_cast<double>(ink.x);
  const double origin_y = center_y - static_cast<double>(ink.y) - static_cast<double>(ink.height) / 2.0;
  cairo_move_to(cr, origin_x, origin_y);
  pango_cairo_update_layout(cr, layout);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);
  if (ctx != nullptr) g_object_unref(ctx);
}

}  // namespace pillbar
