#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <functional>
#include <string>

#include "xdg-output-unstable-v1-client-protocol.h"

struct zxdg_output_manager_v1;
struct zxdg_output_v1;

namespace pillbar {

// One wl_output plus its xdg-output companion. Reports logical geometry used
// for placement and the fractional/integer scale used for rendering.
class Output {
 public:
  Output();
  ~Output();
  Output(const Output&) = delete;
  Output& operator=(const Output&) = delete;

  void bind(wl_output* output);
  void set_registry_name(std::uint32_t name) { registry_name_ = name; }
  void setup(zxdg_output_manager_v1* manager);

  std::uint32_t registry_name() const { return registry_name_; }
  wl_output* wl() const { return wl_; }

  int logical_width() const { return logical_w_ > 0 ? logical_w_ : fallback_width(); }
  int logical_height() const { return logical_h_ > 0 ? logical_h_ : fallback_height(); }
  int logical_x() const { return logical_x_; }
  int logical_y() const { return logical_y_; }
  double scale() const { return static_cast<double>(scale_ > 0 ? scale_ : 1); }
  const std::string& name() const { return name_; }
  const std::string& description() const { return description_; }
  // Readiness is synced on wl_output.done. xdg-output's own done event is
  // deprecated as of v3 and is not sent by all compositors (e.g. Hyprland),
  // so it must not gate readiness.
  bool ready() const { return have_geometry_ && have_mode_ && have_scale_ && have_done_; }

  std::function<void()> on_changed;
  std::function<void()> on_done;

 private:
  static void ev_geometry(void* data, wl_output* output, int32_t x, int32_t y,
                          int32_t physical_width, int32_t physical_height, int32_t subpixel,
                          const char* make, const char* model, int32_t transform);
  static void ev_mode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height,
                      int32_t refresh);
  static void ev_scale(void* data, wl_output* output, int32_t factor);
  static void ev_name(void* data, wl_output* output, const char* name);
  static void ev_description(void* data, wl_output* output, const char* description);
  static void ev_done(void* data, wl_output* output);
  static void ev_xdg_logical_position(void* data, zxdg_output_v1* obj, int32_t x, int32_t y);
  static void ev_xdg_logical_size(void* data, zxdg_output_v1* obj, int32_t width, int32_t height);
  static void ev_xdg_done(void* data, zxdg_output_v1* obj);
  static void ev_xdg_name(void* data, zxdg_output_v1* obj, const char* name);
  static void ev_xdg_description(void* data, zxdg_output_v1* obj, const char* description);

  int fallback_width() const;
  int fallback_height() const;

  static const wl_output_listener output_listener_;
  static const zxdg_output_v1_listener xdg_listener_;

  wl_output* wl_ = nullptr;
  zxdg_output_v1* xdg_ = nullptr;
  std::uint32_t registry_name_ = 0;

  int phys_w_ = 0;
  int phys_h_ = 0;
  int logical_x_ = 0;
  int logical_y_ = 0;
  int logical_w_ = 0;
  int logical_h_ = 0;
  int transform_ = 0;
  int scale_ = 1;

  bool have_geometry_ = false;
  bool have_mode_ = false;
  bool have_scale_ = false;
  bool have_done_ = false;
  bool xdg_done_ = false;

  std::string name_;
  std::string description_;
};

}  // namespace pillbar
