#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "model/state.hpp"
#include "sources/dbus.hpp"
#include "sources/source.hpp"

namespace pillbar {

// Wi-Fi via NetworkManager over D-Bus (the D-Bus path chosen over raw nl80211).
// PropertiesChanged signals on the NM daemon, device and access point drive
// updates; signal strength is only read again after such an event. IP address
// is read with getifaddrs only when the tooltip opens.
class WifiSource : public Source {
 public:
  WifiSource(AppState& state, NotifyFn notify, DbusBus& bus);

  const char* name() const override { return "wifi"; }
  bool start(EventLoop& loop) override;
  void refresh() override;

  // Lines for the tooltip; reads the IP address on demand.
  std::vector<std::string> detail() const;

 private:
  static int on_signal(sd_bus_message* message, void* userdata, sd_bus_error* error);
  void rescan();
  void sample_rates() const;

  AppState& state_;
  NotifyFn notify_;
  DbusBus& bus_;
  std::string device_path_;
  std::string ap_path_;
  std::string ifname_;
  mutable long long last_rx_bytes_ = -1;
  mutable long long last_tx_bytes_ = -1;
  mutable std::chrono::steady_clock::time_point last_rate_time_;
  mutable double rx_rate_bps_ = 0.0;
  mutable double tx_rate_bps_ = 0.0;
};

}  // namespace pillbar
