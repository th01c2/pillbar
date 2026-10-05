#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/event_loop.hpp"

struct wl_compositor;
struct wl_shm;
struct wl_seat;
struct zwlr_layer_shell_v1;
struct zxdg_output_manager_v1;
struct wp_fractional_scale_manager_v1;
struct wp_viewporter;

namespace pillbar {

class Output;
class Pointer;

// Owns the Wayland connection and the bound globals. The connection fd is
// registered with the event loop; all Wayland events are dispatched from the
// single epoll thread.
class WaylandDisplay {
 public:
  WaylandDisplay();
  ~WaylandDisplay();
  WaylandDisplay(const WaylandDisplay&) = delete;
  WaylandDisplay& operator=(const WaylandDisplay&) = delete;

  bool connect(EventLoop& loop);

  wl_display* display() const { return display_; }
  wl_compositor* compositor() const { return compositor_; }
  wl_shm* shm() const { return shm_; }
  wl_seat* seat() const { return seat_; }
  zwlr_layer_shell_v1* layer_shell() const { return layer_shell_; }
  zxdg_output_manager_v1* xdg_output_manager() const { return xdg_output_manager_; }
  wp_fractional_scale_manager_v1* fractional_scale_manager() const {
    return fractional_scale_manager_;
  }
  wp_viewporter* viewporter() const { return viewporter_; }

  const std::vector<std::unique_ptr<Output>>& outputs() const { return outputs_; }

  Pointer* pointer() const { return pointer_.get(); }

  // Flush queued requests; arms EPOLLOUT when the socket is full.
  bool flush();
  // Called from the event loop when the Wayland fd is readable.
  void dispatch();

  // Fired after the registry is fully populated the first time.
  std::function<void()> on_ready;
  std::function<void(Output&)> on_output_added;
  std::function<void(Output&)> on_output_removed;
  std::function<void()> on_outputs_changed;

  void roundtrip();

 private:
  static void registry_global(void* data, wl_registry* registry, uint32_t name,
                              const char* interface, uint32_t version);
  static void registry_global_remove(void* data, wl_registry* registry, uint32_t name);
  void handle_global(uint32_t name, const char* interface, uint32_t version);
  void handle_global_remove(uint32_t name);

  static const wl_registry_listener registry_listener_;

  EventLoop* loop_ = nullptr;
  wl_display* display_ = nullptr;
  wl_registry* registry_ = nullptr;
  wl_compositor* compositor_ = nullptr;
  wl_shm* shm_ = nullptr;
  wl_seat* seat_ = nullptr;
  zwlr_layer_shell_v1* layer_shell_ = nullptr;
  zxdg_output_manager_v1* xdg_output_manager_ = nullptr;
  wp_fractional_scale_manager_v1* fractional_scale_manager_ = nullptr;
  wp_viewporter* viewporter_ = nullptr;
  std::vector<std::unique_ptr<Output>> outputs_;
  std::unique_ptr<Pointer> pointer_;
  bool ready_ = false;
  bool need_out_ = false;
  bool protocol_error_ = false;
};

}  // namespace pillbar
