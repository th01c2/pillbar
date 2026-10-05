#include "sources/login1.hpp"

#include "app/logging.hpp"

namespace pillbar {

Login1Source::Login1Source(DbusBus& bus, std::function<void()> on_resume)
    : bus_(bus), on_resume_(std::move(on_resume)) {}

int Login1Source::on_signal(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* self = static_cast<Login1Source*>(userdata);
  int sleeping = 0;
  if (sd_bus_message_read(message, "b", &sleeping) >= 0 && sleeping == 0) {
    // Woke up.
    if (self->on_resume_) self->on_resume_();
  }
  return 0;
}

bool Login1Source::start(EventLoop& loop) {
  (void)loop;
  if (!bus_.valid()) return false;
  return bus_.add_match(
      "type='signal',interface='org.freedesktop.login1.Manager',member='PrepareForSleep'",
      [this](sd_bus_message* m) { return on_signal(m, this, nullptr); });
}

}  // namespace pillbar

