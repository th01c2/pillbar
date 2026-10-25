#include "sources/vpn.hpp"

#include <utility>

#include "app/logging.hpp"

namespace pillbar {
namespace {

constexpr const char* kSystemd = "org.freedesktop.systemd1";
constexpr const char* kManagerPath = "/org/freedesktop/systemd1";
constexpr const char* kManagerInterface = "org.freedesktop.systemd1.Manager";
constexpr const char* kUnitInterface = "org.freedesktop.systemd1.Unit";

} // namespace

VpnSource::VpnSource(AppState& state, NotifyFn notify, DbusBus& bus, std::string unit)
    : state_(state), notify_(std::move(notify)), bus_(bus), unit_(std::move(unit)) {}

bool VpnSource::start(EventLoop& loop) {
  (void)loop;
  if (!bus_.valid()) {
    LOG_WARN("vpn: system bus unavailable");
    return false;
  }
  bus_.add_match("type='signal',sender='org.freedesktop.systemd1',"
                 "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',"
                 "arg0='org.freedesktop.systemd1.Unit'",
                 [this](sd_bus_message* message) { return on_properties(message); });
  bus_.add_match("type='signal',sender='org.freedesktop.systemd1',path='/org/freedesktop/systemd1',"
                 "interface='org.freedesktop.systemd1.Manager',member='UnitNew'",
                 [this](sd_bus_message* message) { return on_unit_new(message); });
  bus_.add_match("type='signal',sender='org.freedesktop.systemd1',path='/org/freedesktop/systemd1',"
                 "interface='org.freedesktop.systemd1.Manager',member='UnitRemoved'",
                 [this](sd_bus_message* message) { return on_unit_removed(message); });
  resolve_unit();
  return true;
}

void VpnSource::refresh() {
  resolve_unit();
}

void VpnSource::resolve_unit() {
  unit_path_.clear();
  if (bus_.valid()) {
    sd_bus_message* reply = nullptr;
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
  bool up = false;
  if (!unit_path_.empty()) {
    std::string active_state;
    if (bus_.get_property_string(kSystemd, unit_path_.c_str(), kUnitInterface, "ActiveState",
                                 &active_state)) {
      up = active_state == "active";
    }
  }
  if (state_.vpn_up != up) {
    state_.vpn_up = up;
    if (notify_) notify_(Item::Wifi);
  }
}

int VpnSource::on_properties(sd_bus_message* message) {
  const char* path = sd_bus_message_get_path(message);
  if (path != nullptr && unit_path_ == path) query();
  return 0;
}

int VpnSource::on_unit_new(sd_bus_message* message) {
  const char* name = nullptr;
  if (sd_bus_message_read(message, "s", &name) > 0 && name != nullptr && unit_ == name) {
    resolve_unit();
  }
  return 0;
}

int VpnSource::on_unit_removed(sd_bus_message* message) {
  const char* name = nullptr;
  if (sd_bus_message_read(message, "s", &name) > 0 && name != nullptr && unit_ == name) {
    unit_path_.clear();
    query();
  }
  return 0;
}

} // namespace pillbar
