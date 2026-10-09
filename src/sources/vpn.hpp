#pragma once

#include <string>

#include "app/event_loop.hpp"
#include "model/state.hpp"
#include "sources/dbus.hpp"
#include "sources/source.hpp"

namespace pillbar {

// AmneziaWG (WireGuard) tunnel state read from its systemd unit, fully event
// driven: systemd's PropertiesChanged on the unit's ActiveState drives updates,
// and UnitNew re-resolves the unit if it is loaded later. Nothing is polled and
// the item only appears while the tunnel is active.
class VpnSource : public Source {
 public:
  VpnSource(AppState& state, NotifyFn notify, DbusBus& bus, std::string unit);

  const char* name() const override { return "vpn"; }
  bool start(EventLoop& loop) override;
  void refresh() override;

 private:
  int on_properties(sd_bus_message* message);
  int on_unit_new(sd_bus_message* message);
  int on_unit_removed(sd_bus_message* message);
  void resolve_unit();
  void query();

  AppState& state_;
  NotifyFn notify_;
  DbusBus& bus_;
  std::string unit_;
  std::string unit_path_;
  bool exists_ = false;  // the unit file exists (independent of loaded state)
};

}  // namespace pillbar
