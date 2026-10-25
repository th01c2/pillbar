#pragma once

#include <libudev.h>

#include "app/event_loop.hpp"
#include "model/state.hpp"
#include "sources/source.hpp"

namespace pillbar {

// Battery / AC derived from libudev uevents on the "power_supply" subsystem.
// Because some firmware emits sparse capacity uevents, a SINGLE adaptive
// fallback timerfd is armed only while charging/discharging and not full:
//   60s normally, 30s below 20%. It is disarmed on full/idle/AC, so the
// process makes no wakeups at all when the battery is not changing.
class PowerSource : public Source {
 public:
  PowerSource(AppState& state, NotifyFn notify);
  ~PowerSource() override;

  bool start(EventLoop& loop) override;
  void refresh() override;

 private:
  void on_uevent();
  void rescan();
  void arm_fallback();
  void arm_shimmer();
  void on_shimmer();
  void on_charge_tick();

  AppState& state_;
  NotifyFn notify_;
  udev* udev_ = nullptr;
  udev_monitor* monitor_ = nullptr;
  int monitor_fd_ = -1;
  TimerFd fallback_;
  TimerFd shimmer_timer_;
  int shimmer_ms_ = 0;
  TimerFd charge_timer_;
  int charge_ms_ = 0;
  double charge_from_ = 0.0;
};

} // namespace pillbar
