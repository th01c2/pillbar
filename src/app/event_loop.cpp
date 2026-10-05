#include "app/event_loop.hpp"

#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>

#include "app/logging.hpp"

namespace pillbar {

EventLoop::EventLoop() {
  epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd_ < 0) {
    LOG_ERR("epoll_create1: %s", std::strerror(errno));
  }
}

EventLoop::~EventLoop() {
  if (epoll_fd_ >= 0) ::close(epoll_fd_);
}

bool EventLoop::add(int fd, std::uint32_t events, Callback callback) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;
  if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) != 0) {
    if (errno != EEXIST) {
      LOG_WARN("epoll add fd=%d: %s", fd, std::strerror(errno));
      return false;
    }
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) != 0) {
      LOG_WARN("epoll mod fd=%d: %s", fd, std::strerror(errno));
      return false;
    }
  }
  callbacks_[fd] = std::move(callback);
  return true;
}

bool EventLoop::mod(int fd, std::uint32_t events) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;
  if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) != 0) {
    LOG_WARN("epoll mod fd=%d: %s", fd, std::strerror(errno));
    return false;
  }
  return true;
}

bool EventLoop::del(int fd) {
  if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) != 0 && errno != ENOENT) {
    LOG_WARN("epoll del fd=%d: %s", fd, std::strerror(errno));
    return false;
  }
  callbacks_.erase(fd);
  return true;
}

int EventLoop::poll_once(int timeout_ms) {
  const int count = ::epoll_wait(epoll_fd_, events_, kMaxEvents, timeout_ms);
  if (count < 0) {
    if (errno == EINTR) return 0;
    LOG_ERR("epoll_wait: %s", std::strerror(errno));
    return -1;
  }
  for (int i = 0; i < count; ++i) {
    const int fd = events_[i].data.fd;
    const auto it = callbacks_.find(fd);
    if (it != callbacks_.end() && it->second) {
      it->second(events_[i].events);
    }
  }
  if (count > 0 && post_dispatch_) post_dispatch_();
  return count;
}

void EventLoop::run() {
  running_ = true;
  while (running_) {
    if (poll_once(-1) < 0) {
      running_ = false;
    }
  }
}

TimerFd::TimerFd(int clock_id) {
  const int fd = ::timerfd_create(clock_id, TFD_NONBLOCK | TFD_CLOEXEC);
  if (fd >= 0) fd_.reset(fd);
  if (fd < 0) LOG_WARN("timerfd_create: %s", std::strerror(errno));
}

void TimerFd::arm_relative_ms(int ms, bool oneshot) {
  if (!fd_.valid()) return;
  itimerspec spec{};
  const long long ns = static_cast<long long>(ms) * 1000000LL;
  spec.it_value.tv_sec = static_cast<time_t>(ns / 1000000000LL);
  spec.it_value.tv_nsec = static_cast<long>(ns % 1000000000LL);
  if (spec.it_value.tv_sec == 0 && spec.it_value.tv_nsec == 0) spec.it_value.tv_nsec = 1;
  if (!oneshot) {
    spec.it_interval.tv_sec = spec.it_value.tv_sec;
    spec.it_interval.tv_nsec = spec.it_value.tv_nsec;
  }
  if (::timerfd_settime(fd_.get(), 0, &spec, nullptr) != 0) {
    LOG_WARN("timerfd_settime: %s", std::strerror(errno));
  }
}

void TimerFd::arm_absolute_ms(long long deadline_ms, bool oneshot, bool cancel_on_set) {
  if (!fd_.valid()) return;
  itimerspec spec{};
  const long long ns = deadline_ms * 1000000LL;
  spec.it_value.tv_sec = static_cast<time_t>(ns / 1000000000LL);
  spec.it_value.tv_nsec = static_cast<long>(ns % 1000000000LL);
  int flags = TFD_TIMER_ABSTIME;
  if (cancel_on_set) flags |= TFD_TIMER_CANCEL_ON_SET;
  if (!oneshot) {
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 0;
  }
  if (::timerfd_settime(fd_.get(), flags, &spec, nullptr) != 0) {
    LOG_WARN("timerfd_settime abs: %s", std::strerror(errno));
  }
}

void TimerFd::disarm() {
  if (!fd_.valid()) return;
  itimerspec spec{};
  if (::timerfd_settime(fd_.get(), 0, &spec, nullptr) != 0) {
    LOG_WARN("timerfd_settime disarm: %s", std::strerror(errno));
  }
}

std::uint64_t TimerFd::consume() {
  if (!fd_.valid()) return 0;
  std::uint64_t expirations = 0;
  const ssize_t n = ::read(fd_.get(), &expirations, sizeof(expirations));
  if (n < 0 && errno != EAGAIN) {
    LOG_WARN("timerfd read: %s", std::strerror(errno));
  }
  return expirations;
}

bool SignalFd::create() {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGPWR);
  sigaddset(&mask, SIGHUP);
  if (::sigprocmask(SIG_BLOCK, &mask, nullptr) != 0) {
    LOG_ERR("sigprocmask: %s", std::strerror(errno));
    return false;
  }
  const int fd = ::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (fd < 0) {
    LOG_ERR("signalfd: %s", std::strerror(errno));
    return false;
  }
  fd_.reset(fd);
  return true;
}

int SignalFd::read_signal() {
  if (!fd_.valid()) return 0;
  signalfd_siginfo info{};
  const ssize_t n = ::read(fd_.get(), &info, sizeof(info));
  if (n != static_cast<ssize_t>(sizeof(info))) {
    if (n < 0 && errno != EAGAIN) LOG_WARN("signalfd read: %s", std::strerror(errno));
    return 0;
  }
  return static_cast<int>(info.ssi_signo);
}

}  // namespace pillbar
