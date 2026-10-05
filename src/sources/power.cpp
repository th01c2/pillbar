#include "sources/power.hpp"

#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>

#include "app/logging.hpp"
#include "sources/sysfs.hpp"

namespace pillbar {

PowerSource::PowerSource(AppState& state, NotifyFn notify)
    : state_(state), notify_(std::move(notify)) {}

PowerSource::~PowerSource() {
  if (monitor_ != nullptr) udev_monitor_unref(monitor_);
  if (udev_ != nullptr) udev_unref(udev_);
}

bool PowerSource::start(EventLoop& loop) {
  loop_ = &loop;
  udev_ = udev_new();
  if (udev_ == nullptr) {
    LOG_WARN("udev_new failed");
    return false;
  }
  monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (monitor_ == nullptr) {
    LOG_WARN("udev_monitor_new_from_netlink failed");
    return false;
  }
  udev_monitor_filter_add_match_subsystem_devtype(monitor_, "power_supply", nullptr);
  udev_monitor_enable_receiving(monitor_);
  monitor_fd_ = udev_monitor_get_fd(monitor_);
  if (monitor_fd_ < 0) {
    LOG_WARN("udev_monitor_get_fd failed");
    return false;
  }
  loop.add(monitor_fd_, EPOLLIN, [this](std::uint32_t) { on_uevent(); });
  if (fallback_.valid()) {
    loop.add(fallback_.get(), EPOLLIN, [this](std::uint32_t) {
      fallback_.consume();
      rescan();
    });
    fallback_.disarm();
  }
  rescan();
  return true;
}

void PowerSource::refresh() { rescan(); }

void PowerSource::on_uevent() {
  // Drain all queued uevents; one rescan covers them all.
  for (;;) {
    udev_device* device = udev_monitor_receive_device(monitor_);
    if (device == nullptr) break;
    udev_device_unref(device);
  }
  rescan();
}

void PowerSource::rescan() {
  const std::string base = "/sys/class/power_supply";
  BatteryState battery;
  bool ac_online = false;
  bool found_battery = false;

  for (const std::string& entry : sysfs::list_directory(base)) {
    const std::string dir = base + "/" + entry;
    std::string type;
    if (!sysfs::read_string(dir + "/type", &type)) continue;
    if (type == "Mains") {
      int online = 0;
      if (sysfs::read_int(dir + "/online", &online) && online != 0) ac_online = true;
      continue;
    }
    if (type != "Battery" || found_battery) continue;

    found_battery = true;
    battery.present = true;
    battery.ac_online = false;

    int capacity = -1;
    if (sysfs::read_int(dir + "/capacity", &capacity) && capacity >= 0) {
      battery.percent = std::clamp(capacity, 0, 100);
    }
    std::string status;
    if (sysfs::read_string(dir + "/status", &status)) {
      battery.charging = status == "Charging";
      battery.full = status == "Full";
    }
    long energy_now = 0;
    long energy_full = 0;
    long power_now = 0;
    if (sysfs::read_long(dir + "/energy_now", &energy_now)) battery.energy_now = energy_now;
    if (sysfs::read_long(dir + "/energy_full", &energy_full)) battery.energy_full = energy_full;
    if (sysfs::read_long(dir + "/power_now", &power_now)) battery.power_now = power_now;
    if (battery.energy_now == 0 || battery.energy_full == 0) {
      long charge_now = 0;
      long charge_full = 0;
      if (sysfs::read_long(dir + "/charge_now", &charge_now)) battery.energy_now = charge_now;
      if (sysfs::read_long(dir + "/charge_full", &charge_full)) battery.energy_full = charge_full;
    }
    if (battery.power_now == 0) {
      long current_now = 0;
      long voltage_now = 0;
      if (sysfs::read_long(dir + "/current_now", &current_now) &&
          sysfs::read_long(dir + "/voltage_now", &voltage_now)) {
        battery.power_now = current_now * voltage_now / 1000000;  // uA * uV -> uW
      }
    }
    if (capacity < 0 && battery.energy_full > 0) {
      battery.percent = static_cast<int>(battery.energy_now * 100 / battery.energy_full);
    }
    long full_design = 0;
    if (sysfs::read_long(dir + "/energy_full_design", &full_design) && full_design > 0) {
      battery.health = static_cast<int>(battery.energy_full * 100 / full_design);
    } else if (sysfs::read_long(dir + "/charge_full_design", &full_design) && full_design > 0) {
      battery.health = static_cast<int>(battery.energy_full * 100 / full_design);
    }
    battery.ac_online = ac_online;
  }

  if (!found_battery) battery.present = false;
  battery.ac_online = ac_online;

  if (!(battery == state_.battery)) {
    state_.battery = battery;
    if (notify_) notify_(Item::Battery);
  }
  arm_fallback();
}

void PowerSource::arm_fallback() {
  if (!fallback_.valid()) return;
  const BatteryState& battery = state_.battery;
  const bool active = battery.present && !battery.full && (battery.charging || !battery.ac_online);
  if (!active) {
    fallback_.disarm();
    return;
  }
  const int interval_ms = battery.percent < 20 ? 30000 : 60000;
  fallback_.arm_relative_ms(interval_ms, true);
}

}  // namespace pillbar

