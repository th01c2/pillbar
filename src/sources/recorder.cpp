#include "sources/recorder.hpp"

#include <dirent.h>
#include <signal.h>

#include <cmath>
#include <cstdlib>

#include "app/logging.hpp"
#include "sources/sysfs.hpp"

namespace pillbar {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kAnimMs = 40;     // ~25 fps while the indicator pulses
constexpr int kPendingMs = 100; // tick while deciding if a session is a recording
constexpr int kScanAfterMs = 300;
// A screencopy session that is still alive after this long is a recording.
// Screenshots (grim and friends) close their session in well under a second.
constexpr int kConfirmMs = 1200;

// Processes we are willing to SIGTERM for the click-to-stop action.
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

// One /proc sweep, only when a recording actually starts. Returns the recorder
// pids (usually exactly one) so the indicator can stop them on click.
std::vector<int> scan_recorder_pids(std::string* first_name) {
  std::vector<int> pids;
  DIR* dir = ::opendir("/proc");
  if (dir == nullptr) return pids;
  while (dirent* entry = ::readdir(dir)) {
    if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
    const int pid = std::atoi(entry->d_name);
    if (pid <= 0) continue;
    const std::string name = read_comm(pid);
    if (!is_recorder_name(name)) continue;
    if (first_name != nullptr && first_name->empty()) *first_name = name;
    pids.push_back(pid);
  }
  ::closedir(dir);
  return pids;
}

}  // namespace

RecorderSource::RecorderSource(AppState& state, NotifyFn notify, int pulse_ms)
    : state_(state), notify_(std::move(notify)), pulse_ms_(pulse_ms > 0 ? pulse_ms : 2000) {}

bool RecorderSource::start(EventLoop& loop) {
  if (!timer_.valid()) return false;
  loop.add(timer_.get(), EPOLLIN, [this](std::uint32_t) {
    timer_.consume();
    on_tick();
  });
  // No polling timer: the Hyprland event stream drives everything. The timer is
  // armed only for the confirmation delay and the pulse animation.
  timer_.disarm();
  return true;
}

void RecorderSource::set_screencast(bool active, const std::string& target) {
  if (active) {
    if (phase_ == Phase::Recording || phase_ == Phase::Pending) return;
    target_ = target;
    pending_ms_ = 0;
    scanned_ = false;
    phase_ = Phase::Pending;
    timer_.arm_relative_ms(kPendingMs, true);
    return;
  }

  if (phase_ == Phase::Pending) {
    // Started and stopped inside the confirmation window: a screenshot, not a
    // recording. Nothing was shown, so just go quiet again.
    LOG_DEBUG("recorder: ignoring short screencopy session (%s)", target_.c_str());
    phase_ = Phase::Idle;
    timer_.disarm();
    return;
  }
  if (phase_ == Phase::Recording) end_recording();
}

void RecorderSource::on_tick() {
  if (phase_ == Phase::Pending) {
    pending_ms_ += kPendingMs;
    // Fast path: a known recorder process means it is really recording.
    if (!scanned_ && pending_ms_ >= kScanAfterMs) {
      scanned_ = true;
      std::string process;
      pids_ = scan_recorder_pids(&process);
      if (!pids_.empty()) {
        begin_recording(process);
        return;
      }
    }
    // Slow path: no known process, so wait out a screenshot's short session.
    if (pending_ms_ >= kConfirmMs) {
      begin_recording(std::string());
      return;
    }
    timer_.arm_relative_ms(kPendingMs, true);
    return;
  }
  if (phase_ != Phase::Recording) return;

  anim_ms_ += kAnimMs;
  RecorderState next = state_.recorder;
  next.pulse = pulse_at(anim_ms_);
  if (!(next == state_.recorder)) {
    state_.recorder = next;
    if (notify_) notify_(Item::Recorder);
  }
  timer_.arm_relative_ms(kAnimMs, true);
}

void RecorderSource::begin_recording(const std::string& process) {
  phase_ = Phase::Recording;
  anim_ms_ = 0;
  LOG_DEBUG("recorder: started (%s%s%s, %zu pid(s))", target_.empty() ? "unknown target" : "target",
            target_.empty() ? "" : " ", target_.c_str(), pids_.size());

  RecorderState next;
  next.active = true;
  next.pulse = pulse_at(0);
  next.target = target_;
  next.process = process;
  next.pid = pids_.empty() ? 0 : pids_.front();
  state_.recorder = next;
  if (notify_) notify_(Item::Recorder);
  timer_.arm_relative_ms(kAnimMs, true);
}

void RecorderSource::end_recording() {
  phase_ = Phase::Idle;
  pids_.clear();
  timer_.disarm();
  LOG_DEBUG("recorder: stopped");
  RecorderState next;  // inactive
  if (!(next == state_.recorder)) {
    state_.recorder = next;
    if (notify_) notify_(Item::Recorder);
  }
}

// 0 at the ends, 1 in the middle: a smooth dark -> bright -> dark breath.
double RecorderSource::pulse_at(int ms) const {
  const double phase =
      std::fmod(static_cast<double>(ms), static_cast<double>(pulse_ms_)) / pulse_ms_;
  return 0.5 - 0.5 * std::cos(2.0 * kPi * phase);
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
    return lines;
  }
  lines.push_back("Recording" + (rec.target.empty() ? std::string() : ": " + rec.target));
  if (!rec.process.empty()) lines.push_back("Process: " + rec.process);
  if (rec.pid > 0) lines.push_back("PID: " + std::to_string(rec.pid));
  lines.push_back(rec.pid > 0 ? "Left click to stop" : "Left click: nothing to stop");
  return lines;
}

}  // namespace pillbar
