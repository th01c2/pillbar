#include "wayland/output.hpp"

#include "app/logging.hpp"
#include "xdg-output-unstable-v1-client-protocol.h"

namespace pillbar {

Output::Output() = default;

Output::~Output() {
  if (xdg_ != nullptr) zxdg_output_v1_destroy(xdg_);
  if (wl_ != nullptr) wl_output_destroy(wl_);
}

void Output::bind(wl_output* output) {
  wl_ = output;
  wl_output_add_listener(wl_, &output_listener_, this);
}

void Output::setup(zxdg_output_manager_v1* manager) {
  if (manager == nullptr || wl_ == nullptr) {
    xdg_done_ = true;
    return;
  }
  xdg_ = zxdg_output_manager_v1_get_xdg_output(manager, wl_);
  zxdg_output_v1_add_listener(xdg_, &xdg_listener_, this);
}

int Output::fallback_width() const { return scale_ > 0 ? phys_w_ / scale_ : phys_w_; }
int Output::fallback_height() const { return scale_ > 0 ? phys_h_ / scale_ : phys_h_; }

void Output::ev_geometry(void* data, wl_output*, int32_t x, int32_t y, int32_t physical_width,
                        int32_t physical_height, int32_t, const char*, const char*,
                        int32_t transform) {
  auto* self = static_cast<Output*>(data);
  self->logical_x_ = x;
  self->logical_y_ = y;
  self->phys_w_ = physical_width;
  self->phys_h_ = physical_height;
  self->transform_ = transform;
  self->have_geometry_ = true;
  if (self->on_changed) self->on_changed();
}

void Output::ev_mode(void* data, wl_output*, uint32_t, int32_t width, int32_t height, int32_t) {
  auto* self = static_cast<Output*>(data);
  self->phys_w_ = width;
  self->phys_h_ = height;
  self->have_mode_ = true;
  if (self->on_changed) self->on_changed();
}

void Output::ev_scale(void* data, wl_output*, int32_t factor) {
  auto* self = static_cast<Output*>(data);
  self->scale_ = factor > 0 ? factor : 1;
  self->have_scale_ = true;
  if (self->on_changed) self->on_changed();
}

void Output::ev_name(void* data, wl_output*, const char* name) {
  auto* self = static_cast<Output*>(data);
  self->name_ = name != nullptr ? name : "";
}

void Output::ev_description(void* data, wl_output*, const char* description) {
  auto* self = static_cast<Output*>(data);
  self->description_ = description != nullptr ? description : "";
}

void Output::ev_done(void* data, wl_output*) {
  auto* self = static_cast<Output*>(data);
  self->have_done_ = true;
  if (self->ready() && self->on_done) self->on_done();
}

void Output::ev_xdg_logical_position(void* data, zxdg_output_v1*, int32_t x, int32_t y) {
  auto* self = static_cast<Output*>(data);
  self->logical_x_ = x;
  self->logical_y_ = y;
  if (self->on_changed) self->on_changed();
}

void Output::ev_xdg_logical_size(void* data, zxdg_output_v1*, int32_t width, int32_t height) {
  auto* self = static_cast<Output*>(data);
  self->logical_w_ = width;
  self->logical_h_ = height;
  if (self->on_changed) self->on_changed();
}

void Output::ev_xdg_done(void* data, zxdg_output_v1*) {
  auto* self = static_cast<Output*>(data);
  self->xdg_done_ = true;
  if (self->ready() && self->on_done) self->on_done();
}

void Output::ev_xdg_name(void* data, zxdg_output_v1*, const char* name) {
  auto* self = static_cast<Output*>(data);
  if (name != nullptr && *name != '\0') self->name_ = name;
}

void Output::ev_xdg_description(void* data, zxdg_output_v1*, const char* description) {
  auto* self = static_cast<Output*>(data);
  if (description != nullptr && *description != '\0') self->description_ = description;
}

const wl_output_listener Output::output_listener_ = {
    &Output::ev_geometry,     &Output::ev_mode, &Output::ev_done,
    &Output::ev_scale,        &Output::ev_name, &Output::ev_description,
};

const zxdg_output_v1_listener Output::xdg_listener_ = {
    &Output::ev_xdg_logical_position,
    &Output::ev_xdg_logical_size,
    &Output::ev_xdg_done,
    &Output::ev_xdg_name,
    &Output::ev_xdg_description,
};

}  // namespace pillbar
