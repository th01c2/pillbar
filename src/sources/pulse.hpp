#pragma once

#include <pulse/pulseaudio.h>

#include <sys/time.h>

#include <vector>

#include "app/event_loop.hpp"

namespace pillbar {

// A pa_mainloop_api implementation backed by the central epoll loop. This lets
// libpulse run inside our single-threaded reactor: every fd libpulse wants is
// registered with the event loop, so there is no polling and no extra thread.
class PulseLoop {
 public:
  explicit PulseLoop(EventLoop& loop);
  ~PulseLoop();
  PulseLoop(const PulseLoop&) = delete;
  PulseLoop& operator=(const PulseLoop&) = delete;

  pa_mainloop_api* api() { return &api_; }

 private:
  struct IoEvent {
    PulseLoop* self = nullptr;
    int fd = -1;
    pa_io_event_flags_t events = PA_IO_EVENT_NULL;
    pa_io_event_cb_t cb = nullptr;
    void* userdata = nullptr;
    pa_io_event_destroy_cb_t destroy = nullptr;
    bool registered = false;
  };
  struct TimeEvent {
    PulseLoop* self = nullptr;
    pa_time_event_cb_t cb = nullptr;
    void* userdata = nullptr;
    pa_time_event_destroy_cb_t destroy = nullptr;
    struct timeval tv {};
    TimerFd timer{CLOCK_REALTIME};
  };
  struct DeferEvent {
    PulseLoop* self = nullptr;
    pa_defer_event_cb_t cb = nullptr;
    void* userdata = nullptr;
    pa_defer_event_destroy_cb_t destroy = nullptr;
    bool enabled = false;
  };

  static pa_io_event* io_new(pa_mainloop_api* api, int fd, pa_io_event_flags_t events,
                             pa_io_event_cb_t cb, void* userdata);
  static void io_enable(pa_io_event* event, pa_io_event_flags_t events);
  static void io_free(pa_io_event* event);
  static void io_set_destroy(pa_io_event* event, pa_io_event_destroy_cb_t cb);
  static void io_dispatch(IoEvent* event, std::uint32_t epoll_events);

  static pa_time_event* time_new(pa_mainloop_api* api, const struct timeval* tv,
                                 pa_time_event_cb_t cb, void* userdata);
  static void time_restart(pa_time_event* event, const struct timeval* tv);
  static void time_free(pa_time_event* event);
  static void time_set_destroy(pa_time_event* event, pa_time_event_destroy_cb_t cb);

  static pa_defer_event* defer_new(pa_mainloop_api* api, pa_defer_event_cb_t cb, void* userdata);
  static void defer_enable(pa_defer_event* event, int enable);
  static void defer_free(pa_defer_event* event);
  static void defer_set_destroy(pa_defer_event* event, pa_defer_event_destroy_cb_t cb);

  static void quit(pa_mainloop_api* api, int retval);

  void run_deferred();

  pa_mainloop_api api_{};
  EventLoop& loop_;
  TimerFd defer_timer_{CLOCK_MONOTONIC};
  std::vector<DeferEvent*> pending_defers_;
};

}  // namespace pillbar

