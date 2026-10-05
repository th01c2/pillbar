#include "sources/pulse.hpp"

#include <sys/epoll.h>

#include <cstring>

#include "app/logging.hpp"

namespace pillbar {

namespace {

pa_io_event_flags_t to_pa_flags(std::uint32_t epoll_events) {
  pa_io_event_flags_t flags = PA_IO_EVENT_NULL;
  if ((epoll_events & EPOLLIN) != 0) flags = static_cast<pa_io_event_flags_t>(flags | PA_IO_EVENT_INPUT);
  if ((epoll_events & EPOLLOUT) != 0)
    flags = static_cast<pa_io_event_flags_t>(flags | PA_IO_EVENT_OUTPUT);
  if ((epoll_events & EPOLLHUP) != 0)
    flags = static_cast<pa_io_event_flags_t>(flags | PA_IO_EVENT_HANGUP);
  if ((epoll_events & EPOLLERR) != 0)
    flags = static_cast<pa_io_event_flags_t>(flags | PA_IO_EVENT_ERROR);
  if (flags == PA_IO_EVENT_NULL) flags = PA_IO_EVENT_INPUT;
  return flags;
}

std::uint32_t to_epoll_flags(pa_io_event_flags_t flags) {
  std::uint32_t mask = 0;
  if ((flags & PA_IO_EVENT_INPUT) != 0) mask |= EPOLLIN;
  if ((flags & PA_IO_EVENT_OUTPUT) != 0) mask |= EPOLLOUT;
  return mask;
}

}  // namespace

PulseLoop::PulseLoop(EventLoop& loop) : loop_(loop) {
  api_.userdata = this;
  api_.io_new = &PulseLoop::io_new;
  api_.io_enable = &PulseLoop::io_enable;
  api_.io_free = &PulseLoop::io_free;
  api_.io_set_destroy = &PulseLoop::io_set_destroy;
  api_.time_new = &PulseLoop::time_new;
  api_.time_restart = &PulseLoop::time_restart;
  api_.time_free = &PulseLoop::time_free;
  api_.time_set_destroy = &PulseLoop::time_set_destroy;
  api_.defer_new = &PulseLoop::defer_new;
  api_.defer_enable = &PulseLoop::defer_enable;
  api_.defer_free = &PulseLoop::defer_free;
  api_.defer_set_destroy = &PulseLoop::defer_set_destroy;
  api_.quit = &PulseLoop::quit;
  if (defer_timer_.valid()) {
    loop_.add(defer_timer_.get(), EPOLLIN, [this](std::uint32_t) {
      defer_timer_.consume();
      run_deferred();
    });
    defer_timer_.disarm();
  }
}

PulseLoop::~PulseLoop() = default;

pa_io_event* PulseLoop::io_new(pa_mainloop_api* api, int fd, pa_io_event_flags_t events,
                               pa_io_event_cb_t cb, void* userdata) {
  auto* self = static_cast<PulseLoop*>(api->userdata);
  auto* event = new IoEvent();
  event->self = self;
  event->fd = fd;
  event->events = events;
  event->cb = cb;
  event->userdata = userdata;
  if (events != PA_IO_EVENT_NULL) {
    self->loop_.add(fd, to_epoll_flags(events),
                    [event](std::uint32_t ev) { io_dispatch(event, ev); });
    event->registered = true;
  }
  return reinterpret_cast<pa_io_event*>(event);
}

void PulseLoop::io_enable(pa_io_event* event, pa_io_event_flags_t events) {
  auto* e = reinterpret_cast<IoEvent*>(event);
  e->events = events;
  const std::uint32_t mask = to_epoll_flags(events);
  if (mask == 0) {
    if (e->registered) {
      e->self->loop_.del(e->fd);
      e->registered = false;
    }
    return;
  }
  if (e->registered) {
    e->self->loop_.mod(e->fd, mask);
  } else {
    e->self->loop_.add(e->fd, mask, [e](std::uint32_t ev) { io_dispatch(e, ev); });
    e->registered = true;
  }
}

void PulseLoop::io_free(pa_io_event* event) {
  auto* e = reinterpret_cast<IoEvent*>(event);
  if (e->registered) e->self->loop_.del(e->fd);
  if (e->destroy != nullptr) e->destroy(e->self->api(), event, e->userdata);
  delete e;
}

void PulseLoop::io_set_destroy(pa_io_event* event, pa_io_event_destroy_cb_t cb) {
  reinterpret_cast<IoEvent*>(event)->destroy = cb;
}

void PulseLoop::io_dispatch(IoEvent* event, std::uint32_t epoll_events) {
  if (event->cb == nullptr) return;
  event->cb(event->self->api(), reinterpret_cast<pa_io_event*>(event), event->fd,
            to_pa_flags(epoll_events), event->userdata);
}

pa_time_event* PulseLoop::time_new(pa_mainloop_api* api, const struct timeval* tv,
                                   pa_time_event_cb_t cb, void* userdata) {
  auto* self = static_cast<PulseLoop*>(api->userdata);
  auto* event = new TimeEvent();
  event->self = self;
  event->cb = cb;
  event->userdata = userdata;
  if (tv != nullptr) event->tv = *tv;
  self->loop_.add(event->timer.get(), EPOLLIN, [event](std::uint32_t) {
    event->timer.consume();
    if (event->cb != nullptr) {
      event->cb(event->self->api(), reinterpret_cast<pa_time_event*>(event), &event->tv,
                event->userdata);
    }
  });
  if (tv != nullptr) time_restart(reinterpret_cast<pa_time_event*>(event), tv);
  return reinterpret_cast<pa_time_event*>(event);
}

void PulseLoop::time_restart(pa_time_event* event, const struct timeval* tv) {
  auto* e = reinterpret_cast<TimeEvent*>(event);
  if (tv == nullptr) {
    e->timer.disarm();
    return;
  }
  e->tv = *tv;
  const long long deadline_ms =
      static_cast<long long>(tv->tv_sec) * 1000LL + static_cast<long long>(tv->tv_usec) / 1000LL;
  e->timer.arm_absolute_ms(deadline_ms, true);
}

void PulseLoop::time_free(pa_time_event* event) {
  auto* e = reinterpret_cast<TimeEvent*>(event);
  e->self->loop_.del(e->timer.get());
  if (e->destroy != nullptr) e->destroy(e->self->api(), event, e->userdata);
  delete e;
}

void PulseLoop::time_set_destroy(pa_time_event* event, pa_time_event_destroy_cb_t cb) {
  reinterpret_cast<TimeEvent*>(event)->destroy = cb;
}

pa_defer_event* PulseLoop::defer_new(pa_mainloop_api* api, pa_defer_event_cb_t cb, void* userdata) {
  auto* self = static_cast<PulseLoop*>(api->userdata);
  auto* event = new DeferEvent();
  event->self = self;
  event->cb = cb;
  event->userdata = userdata;
  defer_enable(reinterpret_cast<pa_defer_event*>(event), 1);
  return reinterpret_cast<pa_defer_event*>(event);
}

void PulseLoop::defer_enable(pa_defer_event* event, int enable) {
  auto* e = reinterpret_cast<DeferEvent*>(event);
  e->enabled = enable != 0;
  if (!e->enabled) return;
  e->self->pending_defers_.push_back(e);
  if (e->self->defer_timer_.valid()) e->self->defer_timer_.arm_relative_ms(1, true);
}

void PulseLoop::defer_free(pa_defer_event* event) {
  auto* e = reinterpret_cast<DeferEvent*>(event);
  auto& pending = e->self->pending_defers_;
  for (auto it = pending.begin(); it != pending.end();) {
    if (*it == e) {
      it = pending.erase(it);
    } else {
      ++it;
    }
  }
  if (e->destroy != nullptr) e->destroy(e->self->api(), event, e->userdata);
  delete e;
}

void PulseLoop::defer_set_destroy(pa_defer_event* event, pa_defer_event_destroy_cb_t cb) {
  reinterpret_cast<DeferEvent*>(event)->destroy = cb;
}

void PulseLoop::quit(pa_mainloop_api* api, int) {
  auto* self = static_cast<PulseLoop*>(api->userdata);
  self->loop_.stop();
}

void PulseLoop::run_deferred() {
  std::vector<DeferEvent*> pending;
  pending.swap(pending_defers_);
  for (DeferEvent* event : pending) {
    if (event->enabled && event->cb != nullptr) {
      event->cb(api(), reinterpret_cast<pa_defer_event*>(event), event->userdata);
    }
  }
}

}  // namespace pillbar
