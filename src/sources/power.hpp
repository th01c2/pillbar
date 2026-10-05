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

  const char* name() const override { return "power"; }
  bool start(EventLoop& loop) override;
  void refresh() override;

 private:
  void on_uevent();
  void rescan();
  void arm_fallback();

  AppState& state_;
  NotifyFn notify_;
  EventLoop* loop_ = nullptr;
  udev* udev_ = nullptr;
  udev_monitor* monitor_ = nullptr;
  int monitor_fd_ = -1;
  TimerFd fallback_;
};

}  // namespace pillbar

