#include "sources/clock.hpp"

#include <ctime>

#include "app/logging.hpp"

namespace pillbar {

ClockSource::ClockSource(AppState& state, NotifyFn notify)
    : state_(state), notify_(std::move(notify)) {}

void ClockSource::now(int* hour, int* minute, int* second) {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  if (hour != nullptr) *hour = tm.tm_hour;
  if (minute != nullptr) *minute = tm.tm_min;
  if (second != nullptr) *second = tm.tm_sec;
}

bool ClockSource::start(EventLoop& loop) {
  if (!timer_.valid()) return false;
  loop.add(timer_.get(), EPOLLIN, [this](std::uint32_t) { on_fire(); });
  update();
  arm_next_minute();
  return true;
}

void ClockSource::refresh() {
  update();
  arm_next_minute();
}

void ClockSource::update() {
  ClockState next;
  now(&next.hour, &next.minute, nullptr);
  if (next == state_.clock) return;
  state_.clock = next;
  if (notify_) notify_(Item::Clock);
}

void ClockSource::arm_next_minute() {
  if (!timer_.valid()) return;
  struct timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  // Deadline = start of the next minute (absolute CLOCK_REALTIME ms).
  const long long seconds = static_cast<long long>(ts.tv_sec);
  const long long next_minute = seconds - (seconds % 60) + 60;
  const long long deadline_ms = next_minute * 1000LL;
  timer_.arm_absolute_ms(deadline_ms, true, /*cancel_on_set=*/true);
}

void ClockSource::on_fire() {
  timer_.consume();
  update();
  arm_next_minute();
}

}  // namespace pillbar

