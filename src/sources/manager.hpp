#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "app/event_loop.hpp"
#include "sources/source.hpp"

namespace pillbar {

// Owns all sources and coalesces bursts of change notifications with a single
// ~16ms debounce timerfd. The timer is disarmed while idle, so no wakeups
// happen when nothing changes.
class SourceManager {
 public:
  SourceManager(EventLoop& loop, std::function<void(Item)> deliver);

  // The debounced notify function to hand to sources.
  NotifyFn notify_fn();
  // Non-owning: the App keeps the std::unique_ptr for each source so it can
  // call control methods (volume, dispatch) on them.
  void add(Source* source);

  void start_all();
  // Re-query every source (resume, output change, config reload).
  void refresh_all();
  // Immediately dispatch any pending change (initial draw).
  void flush();

 private:
  void notify(Item item);
  void on_timer();

  EventLoop& loop_;
  std::function<void(Item)> deliver_;
  std::vector<Source*> sources_;
  TimerFd debounce_;
  Item pending_ = Item::None;
  static constexpr int kDebounceMs = 16;
};

}  // namespace pillbar
