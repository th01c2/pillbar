#pragma once

#include <string>
#include <vector>

#include "model/state.hpp"
#include "sources/dbus.hpp"
#include "sources/source.hpp"

namespace pillbar {

// Bluetooth via BlueZ D-Bus. Subscribes to PropertiesChanged and
// InterfacesAdded/Removed; only those signals trigger a re-enumeration.
class BluetoothSource : public Source {
 public:
  BluetoothSource(AppState& state, NotifyFn notify, DbusBus& bus);

  const char* name() const override { return "bluetooth"; }
  bool start(EventLoop& loop) override;
  void refresh() override;

  std::vector<std::string> detail() const;

 private:
  static int on_signal(sd_bus_message* message, void* userdata, sd_bus_error* error);
  void rescan();

  AppState& state_;
  NotifyFn notify_;
  DbusBus& bus_;
};

}  // namespace pillbar

