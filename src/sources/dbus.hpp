#pragma once

#include <systemd/sd-bus.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/event_loop.hpp"

namespace pillbar {

// Shared sd-bus connection driven from the epoll loop. Multiple sources add
// match rules to the same connection; all dispatching happens on one fd.
class DbusBus {
 public:
  using Handler = std::function<int(sd_bus_message*)>;

  DbusBus() = default;
  ~DbusBus();
  DbusBus(const DbusBus&) = delete;
  DbusBus& operator=(const DbusBus&) = delete;

  bool open_system(EventLoop& loop);
  sd_bus* bus() const { return bus_; }
  bool valid() const { return bus_ != nullptr; }

  // Adds a signal match rule; returns false on failure.
  bool add_match(const std::string& match, Handler handler);

  // Property getters (synchronous, cached by sd-bus).
  bool get_property_string(const char* destination, const char* path, const char* interface,
                           const char* member, std::string* out);
  bool get_property_bool(const char* destination, const char* path, const char* interface,
                         const char* member, bool* out);
  bool get_property_int(const char* destination, const char* path, const char* interface,
                        const char* member, int* out);
  bool get_property_uint(const char* destination, const char* path, const char* interface,
                         const char* member, unsigned* out);
  bool get_property_byte(const char* destination, const char* path, const char* interface,
                         const char* member, int* out);
  bool get_property_bytes(const char* destination, const char* path, const char* interface,
                          const char* member, std::string* out);

  // Method call helper; returns the reply message in *reply (caller unrefs).
  bool call_method(const char* destination, const char* path, const char* interface,
                   const char* member, sd_bus_message** reply, const char* types, ...);

 private:
  static int trampoline(sd_bus_message* message, void* userdata, sd_bus_error* error);
  void on_io();
  void update_events();
  void arm_timeout();

  sd_bus* bus_ = nullptr;
  EventLoop* loop_ = nullptr;
  TimerFd timer_;
  int fd_ = -1;
  std::vector<std::unique_ptr<Handler>> handlers_;
};

}  // namespace pillbar
