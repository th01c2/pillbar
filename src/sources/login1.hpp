#pragma once

#include <functional>

#include "sources/dbus.hpp"
#include "sources/source.hpp"

namespace pillbar {

// Listens to org.freedesktop.login1 PrepareForSleep and forces a full refresh
// on wake, so sources that missed events during suspend resync immediately.
class Login1Source : public Source {
 public:
  Login1Source(DbusBus& bus, std::function<void()> on_resume);

  const char* name() const override { return "login1"; }
  bool start(EventLoop& loop) override;
  void refresh() override {}

 private:
  static int on_signal(sd_bus_message* message, void* userdata, sd_bus_error* error);

  DbusBus& bus_;
  std::function<void()> on_resume_;
};

}  // namespace pillbar

