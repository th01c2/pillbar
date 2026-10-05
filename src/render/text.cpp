#include "render/text.hpp"

#include <pango/pangocairo.h>

#include <cmath>
#include <cstring>

namespace pillbar {
namespace {

std::size_t utf8_sequence_length(unsigned char lead) {
  if ((lead & 0x80u) == 0u) return 1;
  if ((lead & 0xE0u) == 0xC0u) return 2;
  if ((lead & 0xF0u) == 0xE0u) return 3;
  if ((lead & 0xF8u) == 0xF0u) return 4;
  return 0;  // invalid lead byte
}

// Replaces malformed UTF-8 sequences with U+FFFD so Pango never gets invalid
// input (window titles are arbitrary bytes from other processes).
std::string sanitize_utf8(const std::string& input) {
  bool needs_fix = false;
  const std::size_t length = input.size();
  for (std::size_t i = 0; i < length;) {
    const unsigned char lead = static_cast<unsigned char>(input[i]);
    const std::size_t width = utf8_sequence_length(lead);
    if (width == 0 || i + width > length) {
      needs_fix = true;
      break;
    }
    bool continuation_ok = true;
    for (std::size_t k = 1; k < width; ++k) {
      if ((static_cast<unsigned char>(input[i + k]) & 0xC0u) != 0x80u) {
        continuation_ok = false;
        break;
      }
    }
    if (!continuation_ok) {
      needs_fix = true;
      break;
    }
    i += width;
  }
  if (!needs_fix) return input;

  std::string out;
  out.reserve(input.size());
  for (std::size_t i = 0; i < length;) {
    const unsigned char lead = static_cast<unsigned char>(input[i]);
    const std::size_t width = utf8_sequence_length(lead);
    bool ok = width != 0 && i + width <= length;
    for (std::size_t k = 1; ok && k < width; ++k) {
      if ((static_cast<unsigned char>(input[i + k]) & 0xC0u) != 0x80u) ok = false;
    }
    if (ok) {
      out.append(input, i, width);
      i += width;
    } else {
      out += "\uFFFD";
      ++i;
    }
  }
  return out;
}

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
  cairo_font_options_set_antialias(options,
                                   antialias ? CAIRO_ANTIALIAS_GRAY : CAIRO_ANTIALIAS_NONE);
  cairo_font_options_set_hint_style(options, antialias ? CAIRO_HINT_STYLE_SLIGHT
                                                       : CAIRO_HINT_STYLE_FULL);
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
  const std::string safe = sanitize_utf8(text);
  pango_layout_set_text(layout, safe.c_str(), -1);
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
  const std::string safe = sanitize_utf8(text);
  pango_layout_set_text(layout, safe.c_str(), -1);
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
  const std::string safe = sanitize_utf8(text);
  pango_layout_set_text(layout, safe.c_str(), -1);
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
  const std::string safe = sanitize_utf8(text);
  pango_layout_set_text(layout, safe.c_str(), -1);
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
