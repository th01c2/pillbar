#pragma once

#include <string>
#include <vector>

#include "app/event_loop.hpp"
#include "model/state.hpp"
#include "sources/source.hpp"

namespace pillbar {

// Screen-recorder watchdog. Instead of spawning `pgrep` on a timer (what the
// waybar snippet does), it scans /proc directly for known recorder processes.
// Cheap, no subprocesses, and it also records the pid so the icon can stop the
// recording on click.
class RecorderSource : public Source {
 public:
  // `pulse_ms` is the dark-red -> bright-red -> dark-red cycle length.
  RecorderSource(AppState& state, NotifyFn notify, int pulse_ms);

  const char* name() const override { return "recorder"; }
  bool start(EventLoop& loop) override;
  void refresh() override;

  std::vector<std::string> detail() const;
  // SIGTERM the detected recorder(s); returns false when nothing was running.
  bool stop();

 private:
  void on_tick();
  void rescan();
  double pulse_at(int ms) const;

  static constexpr int kDetectMs = 500;         // idle wake-up period
  static constexpr int kAnimMs = 40;            // ~25 fps while recording
  static constexpr int kDetectEveryTicks = 12;  // re-check processes ~0.5s

  AppState& state_;
  NotifyFn notify_;
  TimerFd timer_;
  std::vector<int> pids_;
  int pulse_ms_ = 1500;
  int anim_ms_ = 0;
  int tick_ = 0;
};

}  // namespace pillbar
