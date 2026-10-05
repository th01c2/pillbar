#include "sources/stats.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "sources/sysfs.hpp"

namespace pillbar {

CpuSource::CpuSource(AppState& state, NotifyFn notify)
    : state_(state), notify_(std::move(notify)) {}

bool CpuSource::start(EventLoop& loop) {
  detect_hwmon();
  if (timer_.valid()) {
    loop.add(timer_.get(), EPOLLIN, [this](std::uint32_t) {
      timer_.consume();
      sample();
    });
    timer_.disarm();
  }
  // One priming read so the item is populated without polling.
  state_.cpu.present = true;
  read_proc_stat(&previous_);
  return true;
}

void CpuSource::refresh() {
  detect_hwmon();
  if (hovered_) sample();
}

void CpuSource::set_hovered(bool hovered) {
  hovered_ = hovered;
  if (!timer_.valid()) return;
  if (hovered) {
    sample();
    timer_.arm_relative_ms(1000, true);
  } else {
    timer_.disarm();
  }
}

bool CpuSource::read_proc_stat(std::vector<CoreTimes>* out) {
  std::ifstream file("/proc/stat");
  if (!file) return false;
  out->clear();
  std::string line;
  while (std::getline(file, line)) {
    if (line.compare(0, 3, "cpu") != 0) break;
    std::istringstream stream(line);
    std::string label;
    stream >> label;
    unsigned long long values[10] = {0};
    int count = 0;
    while (count < 10 && (stream >> values[count])) ++count;
    if (count < 4) continue;
    unsigned long long total = 0;
    for (int i = 0; i < count; ++i) total += values[i];
    const unsigned long long idle = values[3] + (count > 4 ? values[4] : 0);
    out->push_back(CoreTimes{total, idle});
  }
  return !out->empty();
}

void CpuSource::detect_hwmon() {
  temp_path_.clear();
  static const char* kNames[] = {"k10temp", "coretemp", "zenpower"};
  for (const std::string& hwmon : sysfs::list_directory("/sys/class/hwmon")) {
    const std::string dir = "/sys/class/hwmon/" + hwmon;
    std::string name;
    if (!sysfs::read_string(dir + "/name", &name)) continue;
    bool match = false;
    for (const char* candidate : kNames) {
      if (name == candidate) match = true;
    }
    if (!match) continue;
    for (const std::string& entry : sysfs::list_directory(dir)) {
      if (entry.size() > 6 && entry.compare(0, 4, "temp") == 0 &&
          entry.compare(entry.size() - 6, 6, "_input") == 0) {
        temp_path_ = dir + "/" + entry;
        return;
      }
    }
  }
}

double CpuSource::read_temperature() const {
  if (temp_path_.empty()) return 0.0;
  long millidegrees = 0;
  if (!sysfs::read_long(temp_path_, &millidegrees)) return 0.0;
  return static_cast<double>(millidegrees) / 1000.0;
}

void CpuSource::sample() {
  std::vector<CoreTimes> current;
  if (!read_proc_stat(&current)) return;

  CpuState next;
  next.present = true;
  if (previous_.size() == current.size()) {
    // cpu0..cpuN: index 0 is the aggregate line.
    for (std::size_t i = 1; i < current.size(); ++i) {
      const unsigned long long dt = current[i].total - previous_[i].total;
      const unsigned long long di = current[i].idle - previous_[i].idle;
      const double usage = dt > 0 ? 100.0 * static_cast<double>(dt - di) / static_cast<double>(dt)
                                  : 0.0;
      next.cores.push_back(CpuCoreState{usage});
    }
  }
  if (!current.empty()) {
    const unsigned long long dt = current[0].total - previous_[0].total;
    const unsigned long long di = current[0].idle - previous_[0].idle;
    next.total = dt > 0 ? 100.0 * static_cast<double>(dt - di) / static_cast<double>(dt) : 0.0;
  }
  previous_ = current;

  // Frequency: average scaling_cur_freq over present CPUs.
  double freq_sum = 0.0;
  int freq_count = 0;
  for (const std::string& cpu : sysfs::list_directory("/sys/devices/system/cpu")) {
    if (cpu.compare(0, 3, "cpu") != 0 || cpu.size() < 4) continue;
    if (cpu[3] < '0' || cpu[3] > '9') continue;
    long khz = 0;
    if (sysfs::read_long("/sys/devices/system/cpu/" + cpu + "/cpufreq/scaling_cur_freq", &khz) &&
        khz > 0) {
      freq_sum += static_cast<double>(khz) / 1000.0;
      ++freq_count;
    }
  }
  if (freq_count > 0) next.frequency = freq_sum / freq_count;
  next.temperature = read_temperature();

  if (!(next == state_.cpu)) {
    state_.cpu = next;
    if (notify_) notify_(Item::Cpu);
  }
}

std::vector<std::string> CpuSource::detail() const {
  std::vector<std::string> lines;
  const CpuState& cpu = state_.cpu;
  char buffer[160];
  std::snprintf(buffer, sizeof(buffer), "CPU: %.0f%%", cpu.total);
  lines.push_back(buffer);
  std::string cores;
  for (std::size_t i = 0; i < cpu.cores.size(); ++i) {
    char core[32];
    std::snprintf(core, sizeof(core), "%s%d:%.0f%%", i == 0 ? "" : " ", static_cast<int>(i),
                  cpu.cores[i].usage);
    cores += core;
    if ((i + 1) % 6 == 0) {
      lines.push_back(cores);
      cores.clear();
    }
  }
  if (!cores.empty()) lines.push_back(cores);
  if (cpu.temperature > 0.0) {
    std::snprintf(buffer, sizeof(buffer), "Temp: %.0f C", cpu.temperature);
    lines.push_back(buffer);
  }
  if (cpu.frequency > 0.0) {
    std::snprintf(buffer, sizeof(buffer), "Freq: %.0f MHz", cpu.frequency);
    lines.push_back(buffer);
  }
  return lines;
}

}  // namespace pillbar
