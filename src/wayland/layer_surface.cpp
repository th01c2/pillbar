#include "wayland/layer_surface.hpp"

#include "app/logging.hpp"
#include "wayland/output.hpp"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace pillbar {

LayerSurface::LayerSurface(WaylandDisplay& display, Output* output, std::uint32_t layer,
                           std::string ns, bool /*interactive*/)
    : display_(display) {
  surface_ = wl_compositor_create_surface(display.compositor());
  layer_ = zwlr_layer_shell_v1_get_layer_surface(display.layer_shell(), surface_,
                                                 output != nullptr ? output->wl() : nullptr, layer,
                                                 ns.c_str());
  zwlr_layer_surface_v1_add_listener(layer_, &layer_listener_, this);
  zwlr_layer_surface_v1_set_keyboard_interactivity(layer_,
                                                   ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
  if (display.viewporter() != nullptr) {
    viewport_ = wp_viewporter_get_viewport(display.viewporter(), surface_);
  }
  if (display.fractional_scale_manager() != nullptr) {
    frac_ = wp_fractional_scale_manager_v1_get_fractional_scale(display.fractional_scale_manager(),
                                                                surface_);
    wp_fractional_scale_v1_add_listener(frac_, &fractional_listener_, this);
  }
}

const zwlr_layer_surface_v1_listener LayerSurface::layer_listener_ = {
    &LayerSurface::ev_configure,
    &LayerSurface::ev_closed,
};

const wp_fractional_scale_v1_listener LayerSurface::fractional_listener_ = {
    &LayerSurface::ev_preferred_scale,
};

LayerSurface::~LayerSurface() {
  if (frac_ != nullptr) wp_fractional_scale_v1_destroy(frac_);
  if (viewport_ != nullptr) wp_viewport_destroy(viewport_);
  if (layer_ != nullptr) zwlr_layer_surface_v1_destroy(layer_);
  if (surface_ != nullptr) wl_surface_destroy(surface_);
}

void LayerSurface::set_anchors(std::uint32_t anchors) {
  zwlr_layer_surface_v1_set_anchor(layer_, anchors);
}

void LayerSurface::set_size(std::uint32_t width, std::uint32_t height) {
  zwlr_layer_surface_v1_set_size(layer_, width, height);
}

void LayerSurface::set_exclusive_zone(std::int32_t zone) {
  zwlr_layer_surface_v1_set_exclusive_zone(layer_, zone);
}

void LayerSurface::set_margin(int top, int right, int bottom, int left) {
  zwlr_layer_surface_v1_set_margin(layer_, top, right, bottom, left);
}

void LayerSurface::set_input_none() {
  wl_region* region = wl_compositor_create_region(display_.compositor());
  wl_surface_set_input_region(surface_, region);
  wl_region_destroy(region);
}

void LayerSurface::set_input_rect(int x, int y, int width, int height) {
  wl_region* region = wl_compositor_create_region(display_.compositor());
  wl_region_add(region, x, y, width, height);
  wl_surface_set_input_region(surface_, region);
  wl_region_destroy(region);
}

void LayerSurface::set_destination(int width, int height) {
  if (viewport_ == nullptr) return;
  wp_viewport_set_destination(viewport_, width, height);
}

void LayerSurface::commit() { wl_surface_commit(surface_); }

void LayerSurface::ev_configure(void* data, zwlr_layer_surface_v1* ls, std::uint32_t serial,
                                std::uint32_t width, std::uint32_t height) {
  auto* self = static_cast<LayerSurface*>(data);
  self->width_ = width;
  self->height_ = height;
  zwlr_layer_surface_v1_ack_configure(ls, serial);
  if (self->on_configure) self->on_configure(width, height);
}

void LayerSurface::ev_closed(void* data, zwlr_layer_surface_v1*) {
  auto* self = static_cast<LayerSurface*>(data);
  self->closed_ = true;
  if (self->on_closed) self->on_closed();
}

void LayerSurface::ev_preferred_scale(void* data, wp_fractional_scale_v1*, std::uint32_t scale) {
  auto* self = static_cast<LayerSurface*>(data);
  const double value = static_cast<double>(scale) / 120.0;
  self->scale_ = value > 0.0 ? value : 1.0;
  if (self->on_scale) self->on_scale(self->scale_);
}

}  // namespace pillbar
