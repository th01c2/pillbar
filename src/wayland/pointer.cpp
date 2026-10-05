#include "wayland/pointer.hpp"

#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>

#include <cmath>

#include "app/logging.hpp"

namespace pillbar {

void Pointer::attach(wl_seat* seat, EventLoop* /*loop*/) {
  seat_ = seat;
  if (seat_ == nullptr) return;
  wl_seat_add_listener(seat_, &seat_listener_, this);
}

Pointer::~Pointer() {
  if (pointer_ != nullptr) {
    wl_pointer_destroy(pointer_);
  }
}

void Pointer::ev_capabilities(void* data, wl_seat* seat, std::uint32_t capabilities) {
  auto* self = static_cast<Pointer*>(data);
  const bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
  if (has_pointer && self->pointer_ == nullptr) {
    self->pointer_ = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(self->pointer_, &pointer_listener_, self);
  } else if (!has_pointer && self->pointer_ != nullptr) {
    wl_pointer_destroy(self->pointer_);
    self->pointer_ = nullptr;
  }
}

void Pointer::ev_name(void*, wl_seat*, const char*) {}

void Pointer::ev_enter(void* data, wl_pointer*, std::uint32_t, wl_surface* surface, wl_fixed_t sx,
                       wl_fixed_t sy) {
  auto* self = static_cast<Pointer*>(data);
  self->focus_ = surface;
  self->x_ = wl_fixed_to_double(sx);
  self->y_ = wl_fixed_to_double(sy);
  if (self->on_enter) self->on_enter(surface, self->x_, self->y_);
}

void Pointer::ev_leave(void* data, wl_pointer*, std::uint32_t, wl_surface* surface) {
  auto* self = static_cast<Pointer*>(data);
  self->focus_ = nullptr;
  if (self->on_leave) self->on_leave(surface);
}

void Pointer::ev_motion(void* data, wl_pointer*, std::uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
  auto* self = static_cast<Pointer*>(data);
  self->x_ = wl_fixed_to_double(sx);
  self->y_ = wl_fixed_to_double(sy);
  if (self->on_motion && self->focus_ != nullptr) {
    self->on_motion(self->focus_, self->x_, self->y_);
  }
}

void Pointer::ev_button(void* data, wl_pointer*, std::uint32_t, std::uint32_t, std::uint32_t button,
                        std::uint32_t state) {
  auto* self = static_cast<Pointer*>(data);
  if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;
  if (self->on_button && self->focus_ != nullptr) {
    self->on_button(self->focus_, button, self->x_, self->y_);
  }
}

void Pointer::ev_axis(void* data, wl_pointer*, std::uint32_t, std::uint32_t axis,
                      wl_fixed_t value) {
  auto* self = static_cast<Pointer*>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  const double delta = wl_fixed_to_double(value);
  if (delta == 0.0) return;
  if (self->on_scroll && self->focus_ != nullptr) {
    const int steps = delta > 0.0 ? 1 : -1;
    self->on_scroll(self->focus_, steps, self->x_, self->y_);
  }
}

void Pointer::ev_frame(void*, wl_pointer*) {}
void Pointer::ev_axis_source(void*, wl_pointer*, std::uint32_t) {}
void Pointer::ev_axis_stop(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}

void Pointer::ev_axis_discrete(void* data, wl_pointer*, std::uint32_t axis, int32_t value) {
  auto* self = static_cast<Pointer*>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  if (value == 0) return;
  if (self->on_scroll && self->focus_ != nullptr) {
    const int steps = value > 0 ? std::abs(value) : -std::abs(value);
    self->on_scroll(self->focus_, steps, self->x_, self->y_);
  }
}

void Pointer::ev_axis_value120(void* data, wl_pointer*, std::uint32_t axis, int32_t value) {
  auto* self = static_cast<Pointer*>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  if (value == 0) return;
  const int steps = static_cast<int>(std::lround(static_cast<double>(value) / 120.0));
  if (steps == 0) return;
  if (self->on_scroll && self->focus_ != nullptr) {
    self->on_scroll(self->focus_, steps, self->x_, self->y_);
  }
}

void Pointer::ev_axis_relative_direction(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}
void Pointer::ev_warp(void*, wl_pointer*, wl_fixed_t, wl_fixed_t) {}

const wl_seat_listener Pointer::seat_listener_ = {
    &Pointer::ev_capabilities,
    &Pointer::ev_name,
};

const wl_pointer_listener Pointer::pointer_listener_ = {
    &Pointer::ev_enter,
    &Pointer::ev_leave,
    &Pointer::ev_motion,
    &Pointer::ev_button,
    &Pointer::ev_axis,
    &Pointer::ev_frame,
    &Pointer::ev_axis_source,
    &Pointer::ev_axis_stop,
    &Pointer::ev_axis_discrete,
    &Pointer::ev_axis_value120,
    &Pointer::ev_axis_relative_direction,
    &Pointer::ev_warp,
};

}  // namespace pillbar
