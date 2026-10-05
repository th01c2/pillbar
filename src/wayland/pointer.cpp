#include "wayland/pointer.hpp"

#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>
#include <wayland-cursor.h>

#include <algorithm>
#include <cmath>

#include "app/logging.hpp"

namespace pillbar {

void Pointer::attach(wl_seat* seat, EventLoop* /*loop*/) {
  seat_ = seat;
  if (seat_ == nullptr) return;
  wl_seat_add_listener(seat_, &seat_listener_, this);
}

Pointer::~Pointer() {
  if (cursor_theme_ != nullptr) wl_cursor_theme_destroy(cursor_theme_);
  if (cursor_surface_ != nullptr) wl_surface_destroy(cursor_surface_);
  if (pointer_ != nullptr) {
    wl_pointer_destroy(pointer_);
  }
}

void Pointer::configure(wl_compositor* compositor, wl_shm* shm, int size_px) {
  compositor_ = compositor;
  shm_ = shm;
  cursor_base_px_ = size_px > 0 ? size_px : 24;
  if (compositor_ == nullptr || cursor_surface_ != nullptr) return;
  cursor_surface_ = wl_compositor_create_surface(compositor_);
}

void Pointer::set_cursor(bool hand, double scale) {
  if (pointer_ == nullptr || focus_ == nullptr || cursor_surface_ == nullptr || shm_ == nullptr) {
    return;
  }
  // Skip redundant work while the pointer stays on the same surface, but always
  // re-apply on a fresh enter so the compositor cannot keep a stale cursor.
  if (hand == hand_active_ && focus_ == cursor_focus_ && cursor_theme_ != nullptr) return;
  hand_active_ = hand;
  cursor_focus_ = focus_;
  LOG_DEBUG("cursor -> %s (theme %s, size %d)", hand ? "pointer" : "default",
            cursor_theme_ != nullptr ? "loaded" : "missing", cursor_size_);

  const int scale_int = std::max(1, static_cast<int>(std::lround(scale)));
  // The theme is loaded in physical pixels and the cursor surface is marked
  // with the matching buffer scale, so it lands at 24 logical px on HiDPI too.
  const int want_size = std::max(8, cursor_base_px_ * scale_int);
  if (cursor_theme_ == nullptr || want_size != cursor_size_) {
    if (cursor_theme_ != nullptr) wl_cursor_theme_destroy(cursor_theme_);
    cursor_theme_ = wl_cursor_theme_load(nullptr, want_size, shm_);
    cursor_size_ = want_size;
    cursor_arrow_ = cursor_theme_ != nullptr ? wl_cursor_theme_get_cursor(cursor_theme_, "default")
                                             : nullptr;
    cursor_hand_ =
        cursor_theme_ != nullptr ? wl_cursor_theme_get_cursor(cursor_theme_, "pointer") : nullptr;
  }

  wl_cursor* cursor = hand ? cursor_hand_ : cursor_arrow_;
  if (cursor == nullptr || cursor->image_count == 0) {
    // No theme available: fall back to the compositor's own cursor.
    wl_pointer_set_cursor(pointer_, enter_serial_, nullptr, 0, 0);
    return;
  }
  wl_cursor_image* image = cursor->images[0];
  // Derive the buffer scale from the image actually loaded: some themes ship
  // only one size, in which case wl_cursor_theme_load() hands back the nominal
  // image instead of a scaled one.
  const int image_scale =
      std::max(1, static_cast<int>(std::lround(static_cast<double>(image->width) /
                                               static_cast<double>(cursor_base_px_))));
  if (image_scale != cursor_scale_) {
    wl_surface_set_buffer_scale(cursor_surface_, image_scale);
    cursor_scale_ = image_scale;
  }
  wl_surface_attach(cursor_surface_, wl_cursor_image_get_buffer(image), 0, 0);
  wl_surface_damage(cursor_surface_, 0, 0, static_cast<std::int32_t>(image->width),
                    static_cast<std::int32_t>(image->height));
  wl_surface_commit(cursor_surface_);
  // The hotspot is in surface-local (logical) coordinates, while the theme
  // reports it in image pixels of the scaled buffer. Without dividing by the
  // buffer scale the pointer image is drawn offset from the real position.
  wl_pointer_set_cursor(pointer_, enter_serial_, cursor_surface_,
                        static_cast<std::int32_t>(image->hotspot_x) / image_scale,
                        static_cast<std::int32_t>(image->hotspot_y) / image_scale);
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

void Pointer::ev_enter(void* data, wl_pointer*, std::uint32_t serial, wl_surface* surface,
                       wl_fixed_t sx, wl_fixed_t sy) {
  auto* self = static_cast<Pointer*>(data);
  self->enter_serial_ = serial;
  self->focus_ = surface;
  self->x_ = wl_fixed_to_double(sx);
  self->y_ = wl_fixed_to_double(sy);
  if (self->on_enter) self->on_enter(surface, self->x_, self->y_);
}

void Pointer::ev_leave(void* data, wl_pointer*, std::uint32_t, wl_surface* surface) {
  auto* self = static_cast<Pointer*>(data);
  self->focus_ = nullptr;
  self->cursor_focus_ = nullptr;
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
