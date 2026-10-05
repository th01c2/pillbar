#pragma once

#include <array>
#include <string>
#include <vector>

#include "app/event_loop.hpp"
#include "model/state.hpp"
#include "sources/source.hpp"

namespace pillbar {

// CPU statistics. /proc/stat and hwmon are read ONLY while the CPU/system
// tooltip is open: set_hovered(true) arms a 1s timerfd, set_hovered(false)
// disarms it. Nothing is sampled while idle.
class CpuSource : public Source {
 public:
  CpuSource(AppState& state, NotifyFn notify);

  const char* name() const override { return "cpu"; }
  bool start(EventLoop& loop) override;
  void refresh() override;

  void set_hovered(bool hovered);
  std::vector<std::string> detail() const;

 private:
  struct CoreTimes {
    unsigned long long total = 0;
    unsigned long long idle = 0;
  };

  void sample();
  bool read_proc_stat(std::vector<CoreTimes>* out);
  void detect_hwmon();
  double read_temperature() const;

  AppState& state_;
  NotifyFn notify_;
  TimerFd timer_;
  std::vector<CoreTimes> previous_;
  std::string temp_path_;
  bool hovered_ = false;
};

}  // namespace pillbar
