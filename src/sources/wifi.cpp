#include "sources/wifi.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <chrono>
#include <cmath>
#include <cstdio>

#include "app/logging.hpp"
#include "sources/sysfs.hpp"

namespace pillbar {
namespace {

// NM_DEVICE_TYPE_WIFI == 2, NM_DEVICE_TYPE_ETHERNET == 1,
// NM_DEVICE_STATE_ACTIVATED == 100.
bool nm_device_type_is_ethernet(unsigned type) { return type == 1; }

bool nm_device_is_activated(DbusBus& bus, const std::string& path) {
  unsigned state = 0;
  if (!bus.get_property_uint("org.freedesktop.NetworkManager", path.c_str(),
                             "org.freedesktop.NetworkManager.Device", "State", &state)) {
    return false;
  }
  return state == 100;
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

long long interface_counter(const std::string& ifname, const char* which) {
  if (ifname.empty()) return 0;
  long value = 0;
  if (!sysfs::read_long("/sys/class/net/" + ifname + "/statistics/" + which, &value)) return 0;
  return value;
}

std::string format_rate(double bits_per_sec) {
  char buffer[48];
  if (bits_per_sec >= 1.0e9) {
    std::snprintf(buffer, sizeof(buffer), "%.1f Gbit/s", bits_per_sec / 1.0e9);
  } else if (bits_per_sec >= 1.0e6) {
    std::snprintf(buffer, sizeof(buffer), "%.1f Mbit/s", bits_per_sec / 1.0e6);
  } else if (bits_per_sec >= 1.0e3) {
    std::snprintf(buffer, sizeof(buffer), "%.0f kbit/s", bits_per_sec / 1.0e3);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.0f bit/s", bits_per_sec);
  }
  return buffer;
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

  // Find the first wireless device and whether any ethernet device is active.
  sd_bus_message* reply = nullptr;
  if (!bus_.call_method("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
                        "org.freedesktop.NetworkManager", "GetDevices", &reply, "")) {
    return;
  }
  std::string wifi_path;
  std::string wired_path;
  if (sd_bus_message_enter_container(reply, 'a', "o") >= 0) {
    const char* path = nullptr;
    while (sd_bus_message_read(reply, "o", &path) > 0) {
      unsigned type = 0;
      if (bus_.get_property_uint("org.freedesktop.NetworkManager", path,
                                 "org.freedesktop.NetworkManager.Device", "DeviceType", &type)) {
        if (type == 2 && wifi_path.empty()) {
          wifi_path = path;
        } else if (nm_device_type_is_ethernet(type) && wired_path.empty() &&
                   nm_device_is_activated(bus_, path)) {
          wired_path = path;
        }
      }
    }
    sd_bus_message_exit_container(reply);
  }
  sd_bus_message_unref(reply);

  if (!wired_path.empty()) {
    next.wired = true;
    bus_.get_property_string("org.freedesktop.NetworkManager", wired_path.c_str(),
                             "org.freedesktop.NetworkManager.Device", "Interface",
                             &next.wired_ifname);
  }

  if (!wifi_path.empty()) {
    device_path_ = wifi_path;
    next.present = true;
    bus_.get_property_string("org.freedesktop.NetworkManager", wifi_path.c_str(),
                             "org.freedesktop.NetworkManager.Device", "Interface", &next.ifname);
    ifname_ = next.ifname;

    std::string ap_path;
    bus_.get_property_object_path("org.freedesktop.NetworkManager", wifi_path.c_str(),
                                  "org.freedesktop.NetworkManager.Device.Wireless",
                                  "ActiveAccessPoint", &ap_path);
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
                                 "org.freedesktop.NetworkManager.AccessPoint", "Strength",
                                 &strength)) {
        next.signal = strength;
      }
      unsigned freq = 0;
      if (bus_.get_property_uint("org.freedesktop.NetworkManager", ap_path.c_str(),
                                 "org.freedesktop.NetworkManager.AccessPoint", "Frequency",
                                 &freq)) {
        next.freq = static_cast<int>(freq);
      }
      next.band = band_for(next.freq);
      unsigned rate = 0;
      if (bus_.get_property_uint("org.freedesktop.NetworkManager", ap_path.c_str(),
                                 "org.freedesktop.NetworkManager.AccessPoint", "MaxBitrate",
                                 &rate)) {
        next.rate = static_cast<int>(rate);
      }
    }
  }

  // Ethernet takes display priority over wifi; the wifi device can stay
  // connected while a dock is plugged in.
  if (next.wired) next.present = true;

  LOG_DEBUG("wifi: present=%d connected=%d wired=%d iface=%s signal=%d", next.present ? 1 : 0,
            next.connected ? 1 : 0, next.wired ? 1 : 0, next.ifname.c_str(), next.signal);

  if (!(next == state_.wifi)) {
    state_.wifi = next;
    if (notify_) notify_(Item::Wifi);
  }
}

std::vector<std::string> WifiSource::detail() const {
  sample_rates();
  std::vector<std::string> lines;
  const WifiState& wifi = state_.wifi;
  if (wifi.wired) {
    lines.push_back("Ethernet: " +
                    (wifi.wired_ifname.empty() ? std::string("connected") : wifi.wired_ifname));
  }
  if (!wifi.present) {
    if (!wifi.wired) lines.push_back("Wi-Fi: no device");
    lines.push_back("Down: " + format_rate(rx_rate_bps_));
    lines.push_back("Up: " + format_rate(tx_rate_bps_));
    return lines;
  }
  if (!wifi.connected) {
    lines.push_back("Wi-Fi: disconnected");
    lines.push_back("Interface: " + wifi.ifname);
    lines.push_back("Down: " + format_rate(rx_rate_bps_));
    lines.push_back("Up: " + format_rate(tx_rate_bps_));
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
  lines.push_back("Down: " + format_rate(rx_rate_bps_));
  lines.push_back("Up: " + format_rate(tx_rate_bps_));
  return lines;
}

void WifiSource::sample_rates() const {
  const WifiState& wifi = state_.wifi;
  const std::string& ifname = wifi.wired ? wifi.wired_ifname : wifi.ifname;
  const long long rx = interface_counter(ifname, "rx_bytes");
  const long long tx = interface_counter(ifname, "tx_bytes");
  const auto now = std::chrono::steady_clock::now();
  if (last_rx_bytes_ >= 0 && rx >= last_rx_bytes_ && tx >= last_tx_bytes_) {
    const double dt = std::chrono::duration<double>(now - last_rate_time_).count();
    if (dt > 0.05) {
      rx_rate_bps_ = static_cast<double>(rx - last_rx_bytes_) * 8.0 / dt;
      tx_rate_bps_ = static_cast<double>(tx - last_tx_bytes_) * 8.0 / dt;
    }
  }
  last_rx_bytes_ = rx;
  last_tx_bytes_ = tx;
  last_rate_time_ = now;
}

}  // namespace pillbar
