#include "app/inotify_watcher.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sys/stat.h>

#include "app/logging.hpp"

namespace pillbar {

InotifyWatcher::InotifyWatcher(EventLoop& loop, std::string path, Callback callback,
                               int debounce_ms)
    : loop_(loop), path_(std::move(path)), callback_(std::move(callback)), buffer_(4096) {
  const std::size_t slash = path_.find_last_of('/');
  if (slash == std::string::npos) {
    directory_ = ".";
    basename_ = path_;
  } else {
    directory_ = path_.substr(0, slash);
    basename_ = path_.substr(slash + 1);
  }
  // If the config directory does not exist yet, watch its parent so we notice
  // when it is created (e.g. the first `cp config.toml ~/.config/pillbar/`).
  struct stat info{};
  watch_target_ = directory_;
  if (::stat(watch_target_.c_str(), &info) != 0) {
    const std::size_t up = watch_target_.find_last_of('/');
    if (up != std::string::npos && up > 0) watch_target_ = watch_target_.substr(0, up);
  }
  const int fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (fd < 0) {
    LOG_WARN("inotify_init1: %s", std::strerror(errno));
    return;
  }
  fd_.reset(fd);
  rearm();
  loop_.add(fd_.get(), EPOLLIN, [this](std::uint32_t) { on_events(); });
  loop_.add(debounce_.get(), EPOLLIN, [this](std::uint32_t) {
    debounce_.consume();
    flush();
  });
  debounce_ms_ = debounce_ms == 0 ? 120 : debounce_ms;
  // Timer is disarmed until an event arrives.
  debounce_.disarm();
}

InotifyWatcher::~InotifyWatcher() {
  if (fd_.valid()) loop_.del(fd_.get());
  if (debounce_.valid()) loop_.del(debounce_.get());
}

void InotifyWatcher::rearm() {
  if (!fd_.valid()) return;
  if (watch_dir_ >= 0) {
    ::inotify_rm_watch(fd_.get(), watch_dir_);
    watch_dir_ = -1;
  }
  if (watch_file_ >= 0) {
    ::inotify_rm_watch(fd_.get(), watch_file_);
    watch_file_ = -1;
  }
  watch_dir_ = ::inotify_add_watch(
      fd_.get(), watch_target_.c_str(),
      IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_ATTRIB);
  if (watch_dir_ < 0)
    LOG_WARN("inotify add dir %s: %s", watch_target_.c_str(), std::strerror(errno));
  watch_file_ = ::inotify_add_watch(fd_.get(), path_.c_str(), IN_CLOSE_WRITE | IN_ATTRIB);
  if (watch_file_ < 0) {
    // File may not exist yet; the directory watch will catch creation.
    LOG_DEBUG("inotify add file %s: %s", path_.c_str(), std::strerror(errno));
  }
}

void InotifyWatcher::on_events() {
  drain();
  debounce_.arm_relative_ms(debounce_ms_, true);
}

void InotifyWatcher::drain() {
  for (;;) {
    const ssize_t n = ::read(fd_.get(), buffer_.data(), buffer_.size());
    if (n <= 0) {
      if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_WARN("inotify read: %s", std::strerror(errno));
      }
      return;
    }
    // Only our basename matters, but any event in the directory triggers a
    // debounced reload; the reload tolerates the file being absent.
  }
}

void InotifyWatcher::flush() {
  rearm();
  if (callback_) callback_();
}

}  // namespace pillbar
