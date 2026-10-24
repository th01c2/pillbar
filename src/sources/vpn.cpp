#include "sources/vpn.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "app/logging.hpp"

namespace pillbar {
namespace {

constexpr const char* kSystemd = "org.freedesktop.systemd1";
constexpr const char* kManagerPath = "/org/freedesktop/systemd1";
constexpr const char* kManagerInterface = "org.freedesktop.systemd1.Manager";
constexpr const char* kUnitInterface = "org.freedesktop.systemd1.Unit";

}  // namespace

VpnSource::VpnSource(AppState& state, NotifyFn notify, DbusBus& bus, std::string unit)
    : state_(state), notify_(std::move(notify)), bus_(bus), unit_(std::move(unit)) {}

bool VpnSource::start(EventLoop& loop) {
  if (!bus_.valid()) {
    LOG_WARN("vpn: system bus unavailable; item hidden");
    return false;
  }
  loop.add(anim_timer_.get(), EPOLLIN, [this](std::uint32_t) { on_anim(); });
  anim_timer_.disarm();
  bus_.add_match(
      "type='signal',sender='org.freedesktop.systemd1',"
      "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',"
      "arg0='org.freedesktop.systemd1.Unit'",
      [this](sd_bus_message* m) { return on_properties(m); });
  bus_.add_match(
      "type='signal',sender='org.freedesktop.systemd1',path='/org/freedesktop/systemd1',"
      "interface='org.freedesktop.systemd1.Manager',member='UnitNew'",
      [this](sd_bus_message* m) { return on_unit_new(m); });
  bus_.add_match(
      "type='signal',sender='org.freedesktop.systemd1',path='/org/freedesktop/systemd1',"
      "interface='org.freedesktop.systemd1.Manager',member='UnitRemoved'",
      [this](sd_bus_message* m) { return on_unit_removed(m); });
  resolve_unit();
  return true;
}

void VpnSource::refresh() { resolve_unit(); }

void VpnSource::resolve_unit() {
  unit_path_.clear();
  exists_ = false;
  if (bus_.valid()) {
    sd_bus_message* reply = nullptr;
    // GetUnitFileState works whether or not the unit is currently loaded, so a
    // stopped tunnel still counts as "present" and shows red instead of hiding.
    if (bus_.call_method(kSystemd, kManagerPath, kManagerInterface, "GetUnitFileState", &reply,
                         "s", unit_.c_str())) {
      const char* file_state = nullptr;
      if (sd_bus_message_read(reply, "s", &file_state) > 0 && file_state != nullptr &&
          file_state[0] != '\0') {
        exists_ = true;
      }
      sd_bus_message_unref(reply);
    }
    reply = nullptr;
    if (bus_.call_method(kSystemd, kManagerPath, kManagerInterface, "GetUnit", &reply, "s",
                         unit_.c_str())) {
      const char* path = nullptr;
      if (sd_bus_message_read(reply, "o", &path) > 0 && path != nullptr) unit_path_ = path;
      sd_bus_message_unref(reply);
    }
  }
  query();
}

void VpnSource::query() {
  VpnState next;
  next.unit = unit_;
  next.present = exists_;
  if (!unit_path_.empty()) {
    std::string active;
    std::string sub;
    if (bus_.get_property_string(kSystemd, unit_path_.c_str(), kUnitInterface, "ActiveState",
                                 &active) &&
        bus_.get_property_string(kSystemd, unit_path_.c_str(), kUnitInterface, "SubState",
                                 &sub)) {
      next.up = active == "active";
      next.state = sub.empty() ? active : active + " (" + sub + ")";
    }
  }
  if (!have_state_ || !state_.vpn.present || !next.present) {
    next.mix = next.up ? 1.0 : 0.0;
    next.pulse = 0.0;
  } else if (state_.vpn.up != next.up) {
    from_mix_ = state_.vpn.mix;
    next.mix = state_.vpn.mix;
    next.pulse = 0.0;
    anim_ms_ = 0;
    if (anim_timer_.valid()) anim_timer_.arm_relative_ms(16, false);
  } else {
    next.mix = state_.vpn.mix;
    next.pulse = state_.vpn.pulse;
  }
  have_state_ = true;
  if (!(next == state_.vpn)) {
    state_.vpn = next;
    if (notify_) notify_(Item::Vpn);
  }
}

int VpnSource::on_properties(sd_bus_message* message) {
  const char* path = sd_bus_message_get_path(message);
  if (path != nullptr && unit_path_ == path) query();
  return 0;
}

int VpnSource::on_unit_new(sd_bus_message* message) {
  const char* name = nullptr;
  const char* path = nullptr;
  if (sd_bus_message_read(message, "so", &name, &path) < 0 || name == nullptr) return 0;
  if (unit_ == name) {
    resolve_unit();
  }
  return 0;
}

int VpnSource::on_unit_removed(sd_bus_message* message) {
  const char* name = nullptr;
  const char* path = nullptr;
  if (sd_bus_message_read(message, "so", &name, &path) < 0 || name == nullptr) return 0;
  if (unit_ == name) {
    unit_path_.clear();
    query();  // unit file still exists -> present, but down -> red
  }
  return 0;
}

void VpnSource::on_anim() {
  anim_timer_.consume();
  anim_ms_ += 16;
  const double target = state_.vpn.up ? 1.0 : 0.0;
  const double progress = std::clamp(static_cast<double>(anim_ms_) / 200.0, 0.0, 1.0);
  const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
  VpnState next = state_.vpn;
  next.mix = from_mix_ + (target - from_mix_) * eased;
  next.pulse = std::sin(3.14159265358979323846 *
                        std::clamp(static_cast<double>(anim_ms_) / 300.0, 0.0, 1.0));
  if (progress >= 1.0) next.mix = target;
  if (anim_ms_ >= 300) {
    next.mix = target;
    next.pulse = 0.0;
  }
  if (!(next == state_.vpn)) {
    state_.vpn = next;
    if (notify_) notify_(Item::Vpn);
  }
  if (anim_ms_ < 300) {
    anim_timer_.arm_relative_ms(16, true);
  } else {
    anim_timer_.disarm();
  }
}

}  // namespace pillbar
