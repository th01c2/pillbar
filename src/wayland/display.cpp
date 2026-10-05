#include "wayland/display.hpp"

#include <wayland-client-protocol.h>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstring>

#include "app/logging.hpp"
#include "wayland/output.hpp"
#include "wayland/pointer.hpp"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

namespace pillbar {

namespace {

void wayland_log_handler(const char* fmt, va_list args) {
  char buffer[512];
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  LOG_ERR("wayland: %s", buffer);
}

}  // namespace

WaylandDisplay::WaylandDisplay() = default;

WaylandDisplay::~WaylandDisplay() {
  outputs_.clear();
  pointer_.reset();
  if (viewporter_ != nullptr) wp_viewporter_destroy(viewporter_);
  if (fractional_scale_manager_ != nullptr)
    wp_fractional_scale_manager_v1_destroy(fractional_scale_manager_);
  if (xdg_output_manager_ != nullptr) zxdg_output_manager_v1_destroy(xdg_output_manager_);
  if (layer_shell_ != nullptr) zwlr_layer_shell_v1_destroy(layer_shell_);
  if (seat_ != nullptr) wl_seat_destroy(seat_);
  if (shm_ != nullptr) wl_shm_destroy(shm_);
  if (compositor_ != nullptr) wl_compositor_destroy(compositor_);
  if (registry_ != nullptr) wl_registry_destroy(registry_);
  if (display_ != nullptr) wl_display_disconnect(display_);
}

void WaylandDisplay::registry_global(void* data, wl_registry* registry, uint32_t name,
                                     const char* interface, uint32_t version) {
  (void)registry;
  static_cast<WaylandDisplay*>(data)->handle_global(name, interface, version);
}

void WaylandDisplay::registry_global_remove(void* data, wl_registry* registry, uint32_t name) {
  (void)registry;
  static_cast<WaylandDisplay*>(data)->handle_global_remove(name);
}

void WaylandDisplay::handle_global(uint32_t name, const char* interface, uint32_t version) {
  const std::string iface = interface;
  if (iface == wl_compositor_interface.name) {
    compositor_ = static_cast<wl_compositor*>(
        wl_registry_bind(registry_, name, &wl_compositor_interface, std::min(version, 4u)));
  } else if (iface == wl_shm_interface.name) {
    shm_ = static_cast<wl_shm*>(wl_registry_bind(registry_, name, &wl_shm_interface, 1));
  } else if (iface == wl_seat_interface.name) {
    seat_ = static_cast<wl_seat*>(wl_registry_bind(registry_, name, &wl_seat_interface,
                                                   std::min(version, 7u)));
    if (pointer_ == nullptr) {
      pointer_ = std::make_unique<Pointer>();
      pointer_->attach(seat_, loop_);
    }
  } else if (iface == zwlr_layer_shell_v1_interface.name) {
    layer_shell_ = static_cast<zwlr_layer_shell_v1*>(
        wl_registry_bind(registry_, name, &zwlr_layer_shell_v1_interface, 4));
  } else if (iface == zxdg_output_manager_v1_interface.name) {
    xdg_output_manager_ = static_cast<zxdg_output_manager_v1*>(
        wl_registry_bind(registry_, name, &zxdg_output_manager_v1_interface,
                         std::min(version, 3u)));
  } else if (iface == wp_fractional_scale_manager_v1_interface.name) {
    fractional_scale_manager_ = static_cast<wp_fractional_scale_manager_v1*>(wl_registry_bind(
        registry_, name, &wp_fractional_scale_manager_v1_interface, 1));
  } else if (iface == wp_viewporter_interface.name) {
    viewporter_ = static_cast<wp_viewporter*>(
        wl_registry_bind(registry_, name, &wp_viewporter_interface, 1));
  } else if (iface == wl_output_interface.name) {
    auto output = std::make_unique<Output>();
    output->bind(static_cast<wl_output*>(
        wl_registry_bind(registry_, name, &wl_output_interface, std::min(version, 4u))));
    output->set_registry_name(name);
    output->setup(xdg_output_manager_);
    outputs_.push_back(std::move(output));
  }
}

void WaylandDisplay::handle_global_remove(uint32_t name) {
  for (auto it = outputs_.begin(); it != outputs_.end(); ++it) {
    if ((*it)->registry_name() == name) {
      Output& removed = **it;
      if (on_output_removed) on_output_removed(removed);
      outputs_.erase(it);
      if (on_outputs_changed) on_outputs_changed();
      return;
    }
  }
}

bool WaylandDisplay::connect(EventLoop& loop) {
  loop_ = &loop;
  wl_log_set_handler_client(&wayland_log_handler);
  display_ = wl_display_connect(nullptr);
  if (display_ == nullptr) {
    LOG_ERR("wl_display_connect failed (is WAYLAND_DISPLAY set?)");
    return false;
  }
  registry_ = wl_display_get_registry(display_);
  wl_registry_add_listener(registry_, &registry_listener_, this);
  roundtrip();
  roundtrip();
  if (compositor_ == nullptr || shm_ == nullptr || layer_shell_ == nullptr) {
    LOG_ERR("missing required globals (compositor/shm/layer-shell)");
    return false;
  }
  if (fractional_scale_manager_ == nullptr) {
    LOG_WARN("wp_fractional_scale_v1 unavailable; falling back to integer output scale");
  }
  if (viewporter_ == nullptr) {
    LOG_WARN("wp_viewporter unavailable; fractional scaling disabled");
  }
  if (pointer_ == nullptr) {
    pointer_ = std::make_unique<Pointer>();
    pointer_->attach(seat_, loop_);
  }
  loop_->add(wl_display_get_fd(display_), EPOLLIN, [this](std::uint32_t events) {
    if ((events & EPOLLIN) != 0) {
      dispatch();
      return;
    }
    if ((events & EPOLLOUT) != 0) {
      need_out_ = false;
      loop_->mod(wl_display_get_fd(display_), EPOLLIN);
      flush();
    }
  });
  ready_ = true;
  if (on_ready) on_ready();
  return true;
}

void WaylandDisplay::dispatch() {
  if (display_ == nullptr) return;
  wl_display_dispatch_pending(display_);
  if (wl_display_prepare_read(display_) == 0) {
    if (::wl_display_flush(display_) < 0 && errno != EAGAIN) {
      LOG_WARN("wl_display_flush(before read): %s", std::strerror(errno));
    }
    if (wl_display_read_events(display_) < 0) {
      LOG_ERR("wl_display_read_events: %s", std::strerror(errno));
    }
  }
  wl_display_dispatch_pending(display_);
  flush();
}

bool WaylandDisplay::flush() {
  if (display_ == nullptr) return false;
  if (::wl_display_flush(display_) < 0) {
    if (errno == EAGAIN) {
      if (!need_out_) {
        need_out_ = true;
        loop_->mod(wl_display_get_fd(display_), EPOLLIN | EPOLLOUT);
      }
      return true;
    }
    if (errno == EPROTO || errno == EPIPE) {
      if (!protocol_error_) {
        protocol_error_ = true;
        LOG_ERR("Wayland connection lost (protocol error); stopping");
        if (loop_ != nullptr) loop_->stop();
      }
      return false;
    }
    LOG_WARN("wl_display_flush: %s", std::strerror(errno));
    return false;
  }
  return true;
}

void WaylandDisplay::roundtrip() {
  if (display_ == nullptr) return;
  wl_display_roundtrip(display_);
}

const wl_registry_listener WaylandDisplay::registry_listener_ = {
    &WaylandDisplay::registry_global,
    &WaylandDisplay::registry_global_remove,
};

}  // namespace pillbar
