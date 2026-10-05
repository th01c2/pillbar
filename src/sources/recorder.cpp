#include "sources/recorder.hpp"

#include <dirent.h>
#include <signal.h>

#include <cstdlib>
#include <cmath>
#include <cstring>

#include "app/logging.hpp"
#include "sources/sysfs.hpp"

namespace pillbar {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Process names (as seen in /proc/<pid>/comm) that mean "screen recording is
// running". obs is deliberately absent: its process exists without recording.
const char* const kRecorders[] = {
    "wl-screenrec", "wf-recorder", "gpu-screen-recorder", "kooha",
    "wl-recorder",  "simplescreenrecorder",
};

bool is_recorder_name(const std::string& name) {
  for (const char* candidate : kRecorders) {
    if (name == candidate) return true;
  }
  return false;
}

std::string read_comm(int pid) {
  std::string text;
  if (!sysfs::read_string("/proc/" + std::to_string(pid) + "/comm", &text)) return {};
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return text;
}

}  // namespace

// 0 at the ends, 1 in the middle: a smooth dark -> bright -> dark breath.
double RecorderSource::pulse_at(int ms) const {
  const double phase =
      std::fmod(static_cast<double>(ms), static_cast<double>(pulse_ms_)) / pulse_ms_;
  return 0.5 - 0.5 * std::cos(2.0 * kPi * phase);
}

RecorderSource::RecorderSource(AppState& state, NotifyFn notify, int pulse_ms)
    : state_(state), notify_(std::move(notify)), pulse_ms_(pulse_ms > 0 ? pulse_ms : 1500) {}

bool RecorderSource::start(EventLoop& loop) {
  if (!timer_.valid()) return false;
  loop.add(timer_.get(), EPOLLIN, [this](std::uint32_t) {
    timer_.consume();
    on_tick();
  });
  rescan();
  timer_.arm_relative_ms(kDetectMs, true);
  return true;
}

void RecorderSource::refresh() { rescan(); }

// Drives both the animation and the process detection. While idle it wakes
// twice a second just to notice a recorder starting; while recording it runs at
// ~25 fps to animate the icon and re-checks processes less often.
void RecorderSource::on_tick() {
  ++tick_;
  const bool recording = state_.recorder.active;
  if (!recording) {
    rescan();
    timer_.arm_relative_ms(kDetectMs, true);
    return;
  }

  anim_ms_ += kAnimMs;
  if (tick_ % kDetectEveryTicks == 0) {
    rescan();
  } else {
    RecorderState next = state_.recorder;
    next.pulse = pulse_at(anim_ms_);
    if (!(next == state_.recorder)) {
      state_.recorder = next;
      if (notify_) notify_(Item::Recorder);
    }
  }
  timer_.arm_relative_ms(kAnimMs, true);
}

void RecorderSource::rescan() {
  RecorderState next;
  std::vector<int> pids;

  DIR* dir = ::opendir("/proc");
  if (dir == nullptr) return;
  while (dirent* entry = ::readdir(dir)) {
    if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
    const int pid = std::atoi(entry->d_name);
    if (pid <= 0) continue;
    const std::string name = read_comm(pid);
    if (!is_recorder_name(name)) continue;
    if (next.process.empty()) {
      next.process = name;
      next.pid = pid;
    }
    pids.push_back(pid);
  }
  ::closedir(dir);
  next.active = !pids.empty();
  next.pulse = next.active ? pulse_at(anim_ms_) : 0.0;
  if (!next.active) {
    anim_ms_ = 0;
    tick_ = 0;
  }
  pids_ = std::move(pids);

  if (!(next == state_.recorder)) {
    LOG_DEBUG("recorder: %s (pid %d pulse %.2f)", next.active ? next.process.c_str() : "none",
              next.pid, next.pulse);
    state_.recorder = next;
    if (notify_) notify_(Item::Recorder);
  }
}

bool RecorderSource::stop() {
  bool stopped = false;
  for (int pid : pids_) {
    if (::kill(pid, SIGTERM) == 0) stopped = true;
  }
  return stopped;
}

std::vector<std::string> RecorderSource::detail() const {
  std::vector<std::string> lines;
  const RecorderState& rec = state_.recorder;
  if (!rec.active) {
    lines.push_back("Not recording");
    lines.push_back("Left click to stop when recording");
    return lines;
  }
  lines.push_back("Recording: " + rec.process);
  lines.push_back("PID: " + std::to_string(rec.pid));
  lines.push_back("Left click to stop");
  return lines;
}

}  // namespace pillbar
