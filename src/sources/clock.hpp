#pragma once

#include "app/event_loop.hpp"
#include "model/state.hpp"
#include "sources/source.hpp"

namespace pillbar {

// Wall clock driven by a CLOCK_REALTIME timerfd aligned to the minute boundary
// with TFD_TIMER_CANCEL_ON_SET, so a manual/NTP clock change re-syncs it.
class ClockSource : public Source {
 public:
  ClockSource(AppState& state, NotifyFn notify);

  const char* name() const override { return "clock"; }
  bool start(EventLoop& loop) override;
  void refresh() override;

  static void now(int* hour, int* minute, int* second);

 private:
  void on_fire();
  void arm_next_minute();
  void update();

  AppState& state_;
  NotifyFn notify_;
  TimerFd timer_{CLOCK_REALTIME};
};

}  // namespace pillbar

