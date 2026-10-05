#pragma once

#include <sys/inotify.h>

#include <functional>
#include <string>
#include <vector>

#include "app/event_loop.hpp"

namespace pillbar {

// Watches a single file (and, for atomic-save editors, its directory) for
// changes and fires a debounced callback. Used for live config reload.
class InotifyWatcher {
 public:
  using Callback = std::function<void()>;

  InotifyWatcher(EventLoop& loop, std::string path, Callback callback, int debounce_ms = 120);
  ~InotifyWatcher();
  InotifyWatcher(const InotifyWatcher&) = delete;
  InotifyWatcher& operator=(const InotifyWatcher&) = delete;

  bool valid() const { return fd_.valid(); }
  // Re-arms watches after a reload (the inode may have changed).
  void rearm();
  // Called by the event loop when the debounce timer fires.
  void flush();

 private:
  void on_events();
  void drain();

  EventLoop& loop_;
  std::string path_;
  std::string directory_;
  std::string watch_target_;
  std::string basename_;
  Callback callback_;
  Fd fd_;
  TimerFd debounce_;
  int debounce_ms_ = 120;
  int watch_dir_ = -1;
  int watch_file_ = -1;
  std::vector<char> buffer_;
};

}  // namespace pillbar
