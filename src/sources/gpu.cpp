#include "sources/gpu.hpp"

#include <dlfcn.h>

#include <cmath>
#include <cstdio>

#include "app/logging.hpp"
#include "sources/sysfs.hpp"

namespace pillbar {
namespace {

// NVML ABI-compatible structures / function signatures (libnvidia-ml may be
// absent; everything is resolved with dlopen and skipped if missing).
struct NvmlUtilization {
  unsigned gpu = 0;
  unsigned memory = 0;
};

struct NvmlMemory {
  unsigned long long total = 0;
  unsigned long long free = 0;
  unsigned long long used = 0;
};

using NvmlInitFn = int (*)();
using NvmlShutdownFn = int (*)();
using NvmlHandleByIndexFn = int (*)(unsigned, void**);
using NvmlTempFn = int (*)(void*, int, unsigned*);
using NvmlUtilFn = int (*)(void*, NvmlUtilization*);
using NvmlMemoryFn = int (*)(void*, NvmlMemory*);
using NvmlNameFn = int (*)(void*, char*, unsigned);

constexpr int kNvmlSuccess = 0;
constexpr int kNvmlTempGpu = 0;

std::string first_existing(const std::vector<std::string>& candidates) {
  for (const std::string& path : candidates) {
    if (sysfs::exists(path)) return path;
  }
  return {};
}

}  // namespace

GpuSource::GpuSource(AppState& state, NotifyFn notify)
    : state_(state), notify_(std::move(notify)) {}

GpuSource::~GpuSource() {
  if (nvml_ != nullptr) {
    auto shutdown = reinterpret_cast<NvmlShutdownFn>(::dlsym(nvml_, "nvmlShutdown"));
    if (shutdown != nullptr) shutdown();
    ::dlclose(nvml_);
  }
}

bool GpuSource::start(EventLoop& loop) {
  detect();
  if (timer_.valid()) {
    loop.add(timer_.get(), EPOLLIN, [this](std::uint32_t) {
      timer_.consume();
      sample();
    });
    timer_.disarm();
  }
  if (state_.gpu.present) sample();
  return state_.gpu.present;
}

void GpuSource::refresh() {
  detect();
  if (hovered_) sample();
}

void GpuSource::set_hovered(bool hovered) {
  hovered_ = hovered;
  if (!timer_.valid()) return;
  if (hovered) {
    sample();
    timer_.arm_relative_ms(1000, true);
  } else {
    timer_.disarm();
  }
}

void GpuSource::detect() {
  vendor_.clear();
  temp_path_.clear();
  busy_path_.clear();
  vram_used_path_.clear();
  vram_total_path_.clear();

  for (const std::string& hwmon : sysfs::list_directory("/sys/class/hwmon")) {
    const std::string dir = "/sys/class/hwmon/" + hwmon;
    std::string name;
    if (!sysfs::read_string(dir + "/name", &name)) continue;
    if (name != "amdgpu" && name != "nvidia" && name != "i915" && name != "xe") continue;
    vendor_ = name;
    for (const std::string& entry : sysfs::list_directory(dir)) {
      if (entry.size() > 6 && entry.compare(0, 4, "temp") == 0 &&
          entry.compare(entry.size() - 6, 6, "_input") == 0) {
        temp_path_ = dir + "/" + entry;
        break;
      }
    }
    break;
  }

  if (vendor_ == "amdgpu" || vendor_.empty()) {
    busy_path_ = first_existing({"/sys/class/drm/card0/device/gpu_busy_percent",
                                 "/sys/class/drm/card1/device/gpu_busy_percent"});
    vram_used_path_ = first_existing({"/sys/class/drm/card0/device/mem_info_vram_used",
                                      "/sys/class/drm/card1/device/mem_info_vram_used"});
    vram_total_path_ = first_existing({"/sys/class/drm/card0/device/mem_info_vram_total",
                                       "/sys/class/drm/card1/device/mem_info_vram_total"});
    if (!busy_path_.empty() && vendor_.empty()) vendor_ = "amdgpu";
  }

  if (vendor_ == "nvidia" || vendor_.empty()) {
    nvml_ = ::dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
    if (nvml_ != nullptr) {
      auto init = reinterpret_cast<NvmlInitFn>(::dlsym(nvml_, "nvmlInit_v2"));
      auto handle = reinterpret_cast<NvmlHandleByIndexFn>(::dlsym(nvml_, "nvmlDeviceGetHandleByIndex_v2"));
      if (init != nullptr && handle != nullptr && init() == kNvmlSuccess) {
        if (handle(0, &nvml_device_) == kNvmlSuccess) {
          vendor_ = "nvidia";
        }
      }
    } else if (vendor_.empty()) {
      nvml_ = nullptr;
    }
  }

  const bool present = !vendor_.empty();
  state_.gpu.present = present;
  state_.gpu.vendor = vendor_;
}

void GpuSource::sample_nvml() {
  if (nvml_ == nullptr || nvml_device_ == nullptr) return;
  auto temp = reinterpret_cast<NvmlTempFn>(::dlsym(nvml_, "nvmlDeviceGetTemperature"));
  auto util = reinterpret_cast<NvmlUtilFn>(::dlsym(nvml_, "nvmlDeviceGetUtilizationRates"));
  auto memory = reinterpret_cast<NvmlMemoryFn>(::dlsym(nvml_, "nvmlDeviceGetMemoryInfo"));
  unsigned celsius = 0;
  NvmlUtilization utilization{};
  NvmlMemory mem{};
  if (temp != nullptr && temp(nvml_device_, kNvmlTempGpu, &celsius) == kNvmlSuccess) {
    state_.gpu.temperature = celsius;
  }
  if (util != nullptr && util(nvml_device_, &utilization) == kNvmlSuccess) {
    state_.gpu.utilization = utilization.gpu;
  }
  if (memory != nullptr && memory(nvml_device_, &mem) == kNvmlSuccess) {
    state_.gpu.vram_used = static_cast<long>(mem.used);
    state_.gpu.vram_total = static_cast<long>(mem.total);
  }
}

void GpuSource::sample() {
  GpuState next = state_.gpu;
  next.present = state_.gpu.present;
  next.vendor = state_.gpu.vendor;

  if (vendor_ == "nvidia") {
    sample_nvml();
    next.temperature = state_.gpu.temperature;
    next.utilization = state_.gpu.utilization;
    next.vram_used = state_.gpu.vram_used;
    next.vram_total = state_.gpu.vram_total;
  } else {
    if (!temp_path_.empty()) {
      long millidegrees = 0;
      if (sysfs::read_long(temp_path_, &millidegrees)) {
        next.temperature = static_cast<double>(millidegrees) / 1000.0;
      }
    }
    long busy = 0;
    if (!busy_path_.empty() && sysfs::read_long(busy_path_, &busy)) {
      next.utilization = static_cast<double>(busy);
    }
    long vram_used = 0;
    long vram_total = 0;
    if (!vram_used_path_.empty() && sysfs::read_long(vram_used_path_, &vram_used)) {
      next.vram_used = vram_used;
    }
    if (!vram_total_path_.empty() && sysfs::read_long(vram_total_path_, &vram_total)) {
      next.vram_total = vram_total;
    }
  }

  if (!(next == state_.gpu)) {
    state_.gpu = next;
    if (notify_) notify_(Item::Gpu);
  }
}

std::vector<std::string> GpuSource::detail() const {
  std::vector<std::string> lines;
  const GpuState& gpu = state_.gpu;
  if (!gpu.present) {
    lines.push_back("GPU: not detected");
    return lines;
  }
  lines.push_back("GPU: " + gpu.vendor);
  char buffer[160];
  if (gpu.temperature > 0.0) {
    std::snprintf(buffer, sizeof(buffer), "Temp: %.0f C", gpu.temperature);
    lines.push_back(buffer);
  }
  std::snprintf(buffer, sizeof(buffer), "Util: %.0f%%", gpu.utilization);
  lines.push_back(buffer);
  if (gpu.vram_total > 0) {
    std::snprintf(buffer, sizeof(buffer), "VRAM: %.1f / %.1f GiB",
                  static_cast<double>(gpu.vram_used) / 1073741824.0,
                  static_cast<double>(gpu.vram_total) / 1073741824.0);
    lines.push_back(buffer);
  }
  return lines;
}

}  // namespace pillbar
