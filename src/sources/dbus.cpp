#include "sources/dbus.hpp"

#include <poll.h>

#include <cstdarg>
#include <cstring>

#include "app/logging.hpp"

namespace pillbar {

namespace {
constexpr std::uint64_t kNoTimeout = UINT64_MAX;
}

DbusBus::~DbusBus() {
  if (bus_ != nullptr) sd_bus_unref(bus_);
}

int DbusBus::trampoline(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* handler = static_cast<Handler*>(userdata);
  return (*handler)(message);
}

bool DbusBus::open_system(EventLoop& loop) {
  loop_ = &loop;
  const int rc = sd_bus_open_system(&bus_);
  if (rc < 0) {
    LOG_WARN("sd_bus_open_system: %s", std::strerror(-rc));
    bus_ = nullptr;
    return false;
  }
  sd_bus_set_exit_on_disconnect(bus_, 0);
  fd_ = sd_bus_get_fd(bus_);
  if (fd_ < 0) {
    LOG_WARN("sd_bus_get_fd: %s", std::strerror(-static_cast<int>(fd_)));
    sd_bus_unref(bus_);
    bus_ = nullptr;
    return false;
  }
  loop_->add(fd_, EPOLLIN, [this](std::uint32_t) { on_io(); });
  if (timer_.valid()) {
    loop_->add(timer_.get(), EPOLLIN, [this](std::uint32_t) {
      timer_.consume();
      on_io();
    });
  }
  on_io();
  return true;
}

bool DbusBus::add_match(const std::string& match, Handler handler) {
  if (bus_ == nullptr) return false;
  handlers_.push_back(std::make_unique<Handler>(std::move(handler)));
  const int rc = sd_bus_add_match(bus_, nullptr, match.c_str(), &DbusBus::trampoline,
                                  handlers_.back().get());
  if (rc < 0) {
    LOG_WARN("sd_bus_add_match(%s): %s", match.c_str(), std::strerror(-rc));
    handlers_.pop_back();
    return false;
  }
  return true;
}

void DbusBus::on_io() {
  if (bus_ == nullptr) return;
  int rc = 0;
  while ((rc = sd_bus_process(bus_, nullptr)) > 0) {
  }
  if (rc < 0) {
    LOG_WARN("sd_bus_process: %s", std::strerror(-rc));
  }
  update_events();
  arm_timeout();
  sd_bus_flush(bus_);
}

void DbusBus::update_events() {
  if (loop_ == nullptr || fd_ < 0 || bus_ == nullptr) return;
  const int events = sd_bus_get_events(bus_);
  std::uint32_t mask = EPOLLIN;
  if ((events & POLLOUT) != 0) mask |= EPOLLOUT;
  loop_->mod(fd_, mask);
}

void DbusBus::arm_timeout() {
  if (bus_ == nullptr || !timer_.valid()) return;
  std::uint64_t timeout = kNoTimeout;
  if (sd_bus_get_timeout(bus_, &timeout) < 0 || timeout == kNoTimeout) {
    timer_.disarm();
    return;
  }
  timer_.arm_absolute_ms(static_cast<long long>(timeout / 1000ULL), true);
}

bool DbusBus::get_property_string(const char* destination, const char* path, const char* interface,
                                  const char* member, std::string* out) {
  if (bus_ == nullptr) return false;
  sd_bus_error error{};
  sd_bus_message* reply = nullptr;
  const int rc = sd_bus_get_property(bus_, destination, path, interface, member, &error, &reply, "s");
  if (rc < 0) {
    sd_bus_error_free(&error);
    return false;
  }
  const char* value = nullptr;
  if (sd_bus_message_read(reply, "s", &value) >= 0 && value != nullptr) {
    *out = value;
  }
  sd_bus_message_unref(reply);
  sd_bus_error_free(&error);
  return true;
}

bool DbusBus::get_property_object_path(const char* destination, const char* path,
                                       const char* interface, const char* member,
                                       std::string* out) {
  if (bus_ == nullptr) return false;
  sd_bus_error error{};
  sd_bus_message* reply = nullptr;
  const int rc =
      sd_bus_get_property(bus_, destination, path, interface, member, &error, &reply, "o");
  if (rc < 0) {
    sd_bus_error_free(&error);
    return false;
  }
  const char* value = nullptr;
  if (sd_bus_message_read(reply, "o", &value) >= 0 && value != nullptr) {
    *out = value;
  }
  sd_bus_message_unref(reply);
  sd_bus_error_free(&error);
  return true;
}

bool DbusBus::get_property_bool(const char* destination, const char* path, const char* interface,
                                const char* member, bool* out) {
  if (bus_ == nullptr) return false;
  sd_bus_error error{};
  sd_bus_message* reply = nullptr;
  const int rc = sd_bus_get_property(bus_, destination, path, interface, member, &error, &reply, "b");
  if (rc < 0) {
    sd_bus_error_free(&error);
    return false;
  }
  int value = 0;
  if (sd_bus_message_read(reply, "b", &value) >= 0) *out = value != 0;
  sd_bus_message_unref(reply);
  sd_bus_error_free(&error);
  return true;
}

bool DbusBus::get_property_int(const char* destination, const char* path, const char* interface,
                               const char* member, int* out) {
  if (bus_ == nullptr) return false;
  sd_bus_error error{};
  sd_bus_message* reply = nullptr;
  const int rc = sd_bus_get_property(bus_, destination, path, interface, member, &error, &reply, "i");
  if (rc < 0) {
    sd_bus_error_free(&error);
    return false;
  }
  std::int32_t value = 0;
  if (sd_bus_message_read(reply, "i", &value) >= 0) *out = value;
  sd_bus_message_unref(reply);
  sd_bus_error_free(&error);
  return true;
}

bool DbusBus::get_property_uint(const char* destination, const char* path, const char* interface,
                                const char* member, unsigned* out) {
  if (bus_ == nullptr) return false;
  sd_bus_error error{};
  sd_bus_message* reply = nullptr;
  const int rc = sd_bus_get_property(bus_, destination, path, interface, member, &error, &reply, "u");
  if (rc < 0) {
    sd_bus_error_free(&error);
    return false;
  }
  std::uint32_t value = 0;
  if (sd_bus_message_read(reply, "u", &value) >= 0) *out = value;
  sd_bus_message_unref(reply);
  sd_bus_error_free(&error);
  return true;
}

bool DbusBus::get_property_byte(const char* destination, const char* path, const char* interface,
                                const char* member, int* out) {
  if (bus_ == nullptr) return false;
  sd_bus_error error{};
  sd_bus_message* reply = nullptr;
  const int rc = sd_bus_get_property(bus_, destination, path, interface, member, &error, &reply, "y");
  if (rc < 0) {
    sd_bus_error_free(&error);
    return false;
  }
  std::uint8_t value = 0;
  if (sd_bus_message_read(reply, "y", &value) >= 0) *out = value;
  sd_bus_message_unref(reply);
  sd_bus_error_free(&error);
  return true;
}

bool DbusBus::get_property_bytes(const char* destination, const char* path, const char* interface,
                                 const char* member, std::string* out) {
  if (bus_ == nullptr) return false;
  sd_bus_error error{};
  sd_bus_message* reply = nullptr;
  const int rc = sd_bus_get_property(bus_, destination, path, interface, member, &error, &reply, "ay");
  if (rc < 0) {
    sd_bus_error_free(&error);
    return false;
  }
  const void* data = nullptr;
  std::size_t length = 0;
  if (sd_bus_message_read_array(reply, 'y', &data, &length) >= 0) {
    out->assign(static_cast<const char*>(data), length);
  }
  sd_bus_message_unref(reply);
  sd_bus_error_free(&error);
  return true;
}

bool DbusBus::call_method(const char* destination, const char* path, const char* interface,
                          const char* member, sd_bus_message** reply, const char* types, ...) {
  if (bus_ == nullptr) return false;
  va_list args;
  va_start(args, types);
  sd_bus_error error{};
  const int rc = sd_bus_call_methodv(bus_, destination, path, interface, member, &error, reply, types,
                                     args);
  va_end(args);
  sd_bus_error_free(&error);
  return rc >= 0;
}

}  // namespace pillbar
