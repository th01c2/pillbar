#pragma once

#include <sys/epoll.h>
#include <unistd.h>

#include <cstdint>
#include <ctime>
#include <functional>
#include <unordered_map>

namespace pillbar {

// Move-only owning file descriptor. Closes on destruction.
class Fd {
 public:
  Fd() = default;
  explicit Fd(int fd) : fd_(fd) {}
  ~Fd() { reset(); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  Fd(Fd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  Fd& operator=(Fd&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }
  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }
  int release() {
    const int fd = fd_;
    fd_ = -1;
    return fd;
  }
  void reset(int fd = -1) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = fd;
  }

 private:
  int fd_ = -1;
};

// Single-threaded epoll reactor. All fds (Wayland, sockets, netlink, D-Bus,
// inotify, timerfd, signalfd) are registered here. epoll_wait blocks
// indefinitely: when nothing is pending the process sleeps with zero wakeups.
class EventLoop {
 public:
  using Callback = std::function<void(std::uint32_t events)>;

  EventLoop();
  ~EventLoop();
  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;

  bool add(int fd, std::uint32_t events, Callback callback);
  bool mod(int fd, std::uint32_t events);
  bool del(int fd);

  void run();
  void stop() { running_ = false; }
  bool running() const { return running_; }

  // Called after each non-empty batch of dispatched events. Used to flush the
  // Wayland connection after any requests generated while handling events.
  void set_post_dispatch(std::function<void()> callback) {
    post_dispatch_ = std::move(callback);
  }

  // Run one non-blocking pass; used by tests and by `--once` smoke checks.
  int poll_once(int timeout_ms);

 private:
  int epoll_fd_ = -1;
  bool running_ = false;
  std::unordered_map<int, Callback> callbacks_;
  std::function<void()> post_dispatch_;
  static constexpr int kMaxEvents = 64;
  epoll_event events_[kMaxEvents]{};
};

// timerfd wrapper supporting relative and absolute (CLOCK_MONOTONIC or REALTIME)
// arming plus TFD_TIMER_CANCEL_ON_SET for wall-clock jumps.
class TimerFd {
 public:
  explicit TimerFd(int clock_id = CLOCK_MONOTONIC);
  bool valid() const { return fd_.valid(); }
  int get() const { return fd_.get(); }

  void arm_relative_ms(int ms, bool oneshot = true);
  // Deadline is milliseconds on the chosen clock. `cancel_on_set` requests
  // TFD_TIMER_CANCEL_ON_SET (only meaningful for CLOCK_REALTIME).
  void arm_absolute_ms(long long deadline_ms, bool oneshot = true, bool cancel_on_set = false);
  void disarm();
  // Reads and clears expiration count.
  std::uint64_t consume();

 private:
  Fd fd_;
};

// signalfd collecting SIGTERM/SIGINT/SIGPWR for graceful shutdown and resume.
class SignalFd {
 public:
  bool create();
  int get() const { return fd_.get(); }
  // Returns the raw siginfo si_signo, or 0 when drained.
  int read_signal();

 private:
  Fd fd_;
};

}  // namespace pillbar
