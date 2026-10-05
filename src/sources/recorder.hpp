#pragma once

#include <string>
#include <vector>

#include "app/event_loop.hpp"
#include "model/state.hpp"
#include "sources/source.hpp"

namespace pillbar {

// Screen-recorder indicator.
//
// Detection is event-driven, not polled: Hyprland broadcasts `screencast` /
// `screencastv2` on its event socket whenever a screencopy session starts or
// stops, so this source sleeps until that happens. A short confirmation delay
// filters out one-shot screenshots (grim and friends use the same protocol for
// a few milliseconds), and the only /proc access is a single lookup when a real
// recording starts, to learn the pid for the click-to-stop action.
//
// The timer exists purely for the pulsing indicator animation while recording.
class RecorderSource : public Source {
 public:
  RecorderSource(AppState& state, NotifyFn notify, int pulse_ms);

  const char* name() const override { return "recorder"; }
  bool start(EventLoop& loop) override;
  void refresh() override {}  // nothing to poll

  std::vector<std::string> detail() const;
  // SIGTERM the recorder process; false when it could not be identified.
  bool stop();

  // Fed by the Hyprland event stream: `target` is the monitor name or the
  // captured window, as reported by screencastv2.
  void set_screencast(bool active, const std::string& target);

 private:
  enum class Phase { Idle, Pending, Recording };

  void on_tick();
  void begin_recording(const std::string& process);
  void end_recording();
  double pulse_at(int ms) const;

  AppState& state_;
  NotifyFn notify_;
  TimerFd timer_;
  int pulse_ms_ = 2000;
  int anim_ms_ = 0;
  int pending_ms_ = 0;
  Phase phase_ = Phase::Idle;
  bool scanned_ = false;
  std::string target_;
  std::vector<int> pids_;
};

}  // namespace pillbar
