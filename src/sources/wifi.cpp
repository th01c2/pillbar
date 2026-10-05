#include "sources/wifi.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <cmath>
#include <cstdio>

#include "app/logging.hpp"

namespace pillbar {
namespace {

bool nm_device_type_is_wifi(DbusBus& bus, const std::string& path) {
  unsigned type = 0;
  if (!bus.get_property_uint("org.freedesktop.NetworkManager", path.c_str(),
                             "org.freedesktop.NetworkManager.Device", "DeviceType", &type)) {
    return false;
  }
  return type == 2;  // NM_DEVICE_TYPE_WIFI
}

std::string ipv4_for(const std::string& ifname) {
  if (ifname.empty()) return {};
  std::string result;
  ifaddrs* addrs = nullptr;
  if (::getifaddrs(&addrs) != 0) return {};
  for (ifaddrs* it = addrs; it != nullptr; it = it->ifa_next) {
    if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET) continue;
    if (ifname != it->ifa_name) continue;
    char buffer[INET_ADDRSTRLEN]{};
    const auto* sin = reinterpret_cast<const sockaddr_in*>(it->ifa_addr);
    if (::inet_ntop(AF_INET, &sin->sin_addr, buffer, sizeof(buffer)) != nullptr) {
      result = buffer;
    }
  }
  ::freeifaddrs(addrs);
  return result;
}

std::string band_for(int freq_mhz) {
  if (freq_mhz <= 0) return {};
  if (freq_mhz < 3000) return "2.4GHz";
  if (freq_mhz < 6000) return "5GHz";
  return "6GHz";
}

}  // namespace

WifiSource::WifiSource(AppState& state, NotifyFn notify, DbusBus& bus)
    : state_(state), notify_(std::move(notify)), bus_(bus) {}

int WifiSource::on_signal(sd_bus_message* message, void* userdata, sd_bus_error*) {
  auto* self = static_cast<WifiSource*>(userdata);
  (void)message;
  self->rescan();
  return 0;
}

bool WifiSource::start(EventLoop& loop) {
  (void)loop;
  if (!bus_.valid()) return false;
  bus_.add_match(
      "type='signal',sender='org.freedesktop.NetworkManager',"
      "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
      [this](sd_bus_message* m) { return on_signal(m, this, nullptr); });
  bus_.add_match(
      "type='signal',sender='org.freedesktop.NetworkManager',"
      "interface='org.freedesktop.NetworkManager'",
      [this](sd_bus_message* m) { return on_signal(m, this, nullptr); });
  rescan();
  return state_.wifi.present;
}

void WifiSource::refresh() { rescan(); }

void WifiSource::rescan() {
  if (bus_.bus() == nullptr) return;
  WifiState next;

  bool wireless_enabled = true;
  bus_.get_property_bool("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
                         "org.freedesktop.NetworkManager", "WirelessEnabled", &wireless_enabled);
  next.enabled = wireless_enabled;

  // Find the first wireless device.
  sd_bus_message* reply = nullptr;
  if (!bus_.call_method("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
                        "org.freedesktop.NetworkManager", "GetDevices", &reply, "")) {
    return;
  }
  std::string wifi_path;
  if (sd_bus_message_enter_container(reply, 'a', "o") >= 0) {
    const char* path = nullptr;
    while (sd_bus_message_read(reply, "o", &path) > 0) {
      if (wifi_path.empty() && nm_device_type_is_wifi(bus_, path)) wifi_path = path;
    }
    sd_bus_message_exit_container(reply);
  }
  sd_bus_message_unref(reply);
  if (wifi_path.empty()) {
    if (state_.wifi.present) {
      state_.wifi = next;
      if (notify_) notify_(Item::Wifi);
    }
    return;
  }

  device_path_ = wifi_path;
  next.present = true;
  bus_.get_property_string("org.freedesktop.NetworkManager", wifi_path.c_str(),
                           "org.freedesktop.NetworkManager.Device", "Interface", &next.ifname);
  ifname_ = next.ifname;

  std::string ap_path;
  bus_.get_property_string("org.freedesktop.NetworkManager", wifi_path.c_str(),
                           "org.freedesktop.NetworkManager.Device.Wireless", "ActiveAccessPoint",
                           &ap_path);
  const bool connected = !ap_path.empty() && ap_path != "/";
  next.connected = connected;
  ap_path_ = ap_path;

  if (connected) {
    std::string ssid;
    if (bus_.get_property_bytes("org.freedesktop.NetworkManager", ap_path.c_str(),
                                "org.freedesktop.NetworkManager.AccessPoint", "Ssid", &ssid)) {
      next.ssid = ssid;
    }
    int strength = 0;
    if (bus_.get_property_byte("org.freedesktop.NetworkManager", ap_path.c_str(),
                               "org.freedesktop.NetworkManager.AccessPoint", "Strength", &strength)) {
      next.signal = strength;
    }
    unsigned freq = 0;
    if (bus_.get_property_uint("org.freedesktop.NetworkManager", ap_path.c_str(),
                               "org.freedesktop.NetworkManager.AccessPoint", "Frequency", &freq)) {
      next.freq = static_cast<int>(freq);
    }
    next.band = band_for(next.freq);
    unsigned rate = 0;
    if (bus_.get_property_uint("org.freedesktop.NetworkManager", ap_path.c_str(),
                               "org.freedesktop.NetworkManager.AccessPoint", "MaxBitrate", &rate)) {
      next.rate = static_cast<int>(rate);
    }
  }

  if (!(next == state_.wifi)) {
    state_.wifi = next;
    if (notify_) notify_(Item::Wifi);
  }
}

std::vector<std::string> WifiSource::detail() const {
  std::vector<std::string> lines;
  const WifiState& wifi = state_.wifi;
  if (!wifi.present) {
    lines.push_back("Wi-Fi: no device");
    return lines;
  }
  if (!wifi.connected) {
    lines.push_back("Wi-Fi: disconnected");
    lines.push_back("Interface: " + wifi.ifname);
    return lines;
  }
  lines.push_back("SSID: " + (wifi.ssid.empty() ? std::string("(hidden)") : wifi.ssid));
  char buffer[128];
  const int dbm = static_cast<int>(std::lround(wifi.signal / 2.0 - 100.0));
  std::snprintf(buffer, sizeof(buffer), "Signal: %d%% (%d dBm)", wifi.signal, dbm);
  lines.push_back(buffer);
  std::snprintf(buffer, sizeof(buffer), "Band: %s (%d MHz)", wifi.band.c_str(), wifi.freq);
  lines.push_back(buffer);
  std::snprintf(buffer, sizeof(buffer), "Rate: %.1f Mbit/s", wifi.rate / 1000.0);
  lines.push_back(buffer);
  lines.push_back("Interface: " + wifi.ifname);
  const std::string ip = ipv4_for(wifi.ifname);
  lines.push_back("IP: " + (ip.empty() ? std::string("none") : ip));
  return lines;
}

}  // namespace pillbar

