#pragma once

#include <string>
#include <vector>

#include "app/event_loop.hpp"
#include "model/state.hpp"
#include "sources/source.hpp"

namespace pillbar {

// GPU statistics. Detects the vendor by reading hwmon `name` files at startup
// (amdgpu / nvidia / i915 / xe), never by hardcoded hwmonN paths, and
// re-detects on udev hwmon add/remove. AMD is the primary target; NVIDIA uses
// NVML via dlopen when available; Intel is best-effort. Sampling is hover-gated
// exactly like CPU stats.
class GpuSource : public Source {
 public:
  GpuSource(AppState& state, NotifyFn notify);
  ~GpuSource() override;

  const char* name() const override { return "gpu"; }
  bool start(EventLoop& loop) override;
  void refresh() override;

  void set_hovered(bool hovered);
  std::vector<std::string> detail() const;

 private:
  void detect();
  void sample();
  void sample_nvml();

  AppState& state_;
  NotifyFn notify_;
  TimerFd timer_;
  bool hovered_ = false;
  std::string vendor_;
  std::string temp_path_;
  std::string busy_path_;
  std::string vram_used_path_;
  std::string vram_total_path_;

  void* nvml_ = nullptr;
  void* nvml_device_ = nullptr;
};

}  // namespace pillbar
