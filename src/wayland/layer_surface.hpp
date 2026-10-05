#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <functional>
#include <string>

#include "wayland/display.hpp"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct zwlr_layer_surface_v1;
struct wp_viewport;
struct wp_fractional_scale_v1;

namespace pillbar {

// A wlr-layer-shell surface with viewport + fractional-scale wiring.
class LayerSurface {
 public:
  LayerSurface(WaylandDisplay& display, Output* output, std::uint32_t layer, std::string ns,
               bool interactive);
  ~LayerSurface();
  LayerSurface(const LayerSurface&) = delete;
  LayerSurface& operator=(const LayerSurface&) = delete;

  wl_surface* surface() const { return surface_; }
  zwlr_layer_surface_v1* layer_surface() const { return layer_; }

  void set_anchors(std::uint32_t anchors);
  void set_size(std::uint32_t width, std::uint32_t height);
  void set_exclusive_zone(std::int32_t zone);
  void set_margin(int top, int right, int bottom, int left);
  void set_input_none();
  // Restricts pointer input to a single surface-local rectangle.
  void set_input_rect(int x, int y, int width, int height);
  void set_destination(int width, int height);
  void commit();

  double preferred_scale() const { return scale_; }
  std::uint32_t configured_width() const { return width_; }
  std::uint32_t configured_height() const { return height_; }

  std::function<void(std::uint32_t, std::uint32_t)> on_configure;
  std::function<void()> on_closed;
  std::function<void(double)> on_scale;

 private:
  static void ev_configure(void* data, zwlr_layer_surface_v1* ls, std::uint32_t serial,
                           std::uint32_t width, std::uint32_t height);
  static void ev_closed(void* data, zwlr_layer_surface_v1* ls);
  static void ev_preferred_scale(void* data, wp_fractional_scale_v1* obj, std::uint32_t scale);

  static const zwlr_layer_surface_v1_listener layer_listener_;
  static const wp_fractional_scale_v1_listener fractional_listener_;

  WaylandDisplay& display_;
  wl_surface* surface_ = nullptr;
  zwlr_layer_surface_v1* layer_ = nullptr;
  wp_viewport* viewport_ = nullptr;
  wp_fractional_scale_v1* frac_ = nullptr;
  double scale_ = 1.0;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool closed_ = false;
};

}  // namespace pillbar
