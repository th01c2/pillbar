#pragma once

#include <functional>
#include <string>

#include "app/event_loop.hpp"
#include "model/state.hpp"

namespace pillbar {

// A Source owns one or more fds, registers them with the central epoll loop,
// and pushes typed updates into the shared AppState. It must only signal a
// change after comparing the new value with the previous one.
class Source {
 public:
  virtual ~Source() = default;
  virtual const char* name() const = 0;
  // Returns false when the backing service is unavailable (degrade gracefully).
  virtual bool start(EventLoop& loop) = 0;
  // Force a re-query of full state (used after suspend/resume and reconnect).
  virtual void refresh() {}
  virtual bool active() const { return true; }
};

using NotifyFn = std::function<void(Item)>;

}  // namespace pillbar

