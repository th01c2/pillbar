#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <functional>

#include "app/event_loop.hpp"

namespace pillbar {

// wl_pointer wrapper. Translates enter/motion/leave/button/axis into
// surface-space callbacks used for hover, click and scroll handling.
class Pointer {
 public:
  Pointer() = default;
  ~Pointer();
  Pointer(const Pointer&) = delete;
  Pointer& operator=(const Pointer&) = delete;

  void attach(wl_seat* seat, EventLoop* loop);

  std::function<void(wl_surface*, double, double)> on_enter;
  std::function<void(wl_surface*, double, double)> on_motion;
  std::function<void(wl_surface*)> on_leave;
  std::function<void(wl_surface*, std::uint32_t button, double, double)> on_button;
  std::function<void(wl_surface*, int steps, double, double)> on_scroll;

  wl_surface* focus() const { return focus_; }

 private:
  static void ev_capabilities(void* data, wl_seat* seat, std::uint32_t capabilities);
  static void ev_name(void* data, wl_seat* seat, const char* name);
  static void ev_enter(void* data, wl_pointer* pointer, std::uint32_t serial, wl_surface* surface,
                       wl_fixed_t sx, wl_fixed_t sy);
  static void ev_leave(void* data, wl_pointer* pointer, std::uint32_t serial, wl_surface* surface);
  static void ev_motion(void* data, wl_pointer* pointer, std::uint32_t time, wl_fixed_t sx,
                        wl_fixed_t sy);
  static void ev_button(void* data, wl_pointer* pointer, std::uint32_t serial, std::uint32_t time,
                        std::uint32_t button, std::uint32_t state);
  static void ev_axis(void* data, wl_pointer* pointer, std::uint32_t time, std::uint32_t axis,
                      wl_fixed_t value);
  static void ev_frame(void* data, wl_pointer* pointer);
  static void ev_axis_source(void* data, wl_pointer* pointer, std::uint32_t source);
  static void ev_axis_stop(void* data, wl_pointer* pointer, std::uint32_t time, std::uint32_t axis);
  static void ev_axis_discrete(void* data, wl_pointer* pointer, std::uint32_t axis, int32_t value);
  static void ev_axis_value120(void* data, wl_pointer* pointer, std::uint32_t axis, int32_t value);
  static void ev_axis_relative_direction(void* data, wl_pointer* pointer, std::uint32_t axis,
                                         std::uint32_t direction);
  static void ev_warp(void* data, wl_pointer* pointer, wl_fixed_t sx, wl_fixed_t sy);

  static const wl_seat_listener seat_listener_;
  static const wl_pointer_listener pointer_listener_;

  wl_seat* seat_ = nullptr;
  wl_pointer* pointer_ = nullptr;
  wl_surface* focus_ = nullptr;
  double x_ = 0.0;
  double y_ = 0.0;
};

}  // namespace pillbar

