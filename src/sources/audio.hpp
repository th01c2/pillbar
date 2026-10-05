#pragma once

#include <pulse/pulseaudio.h>

#include <memory>
#include <string>

#include "app/event_loop.hpp"
#include "model/state.hpp"
#include "sources/pulse.hpp"
#include "sources/source.hpp"

namespace pillbar {

// Volume via PulseAudio, fully event-driven: pa_context_subscribe delivers
// sink/server changes and the pulse sockets live in the central epoll loop.
class AudioSource : public Source {
 public:
  AudioSource(AppState& state, NotifyFn notify);
  ~AudioSource() override;

  const char* name() const override { return "audio"; }
  bool start(EventLoop& loop) override;
  void refresh() override;

  void set_volume_relative(int delta_percent);
  void toggle_mute();

 private:
  static void ctx_state_cb(pa_context* context, void* userdata);
  static void server_info_cb(pa_context* context, const pa_server_info* info, void* userdata);
  static void sink_info_cb(pa_context* context, const pa_sink_info* info, int eol, void* userdata);
  static void subscribe_cb(pa_context* context, pa_subscription_event_type_t type, uint32_t index,
                           void* userdata);
  static void success_cb(pa_context*, int success, void*);

  void request_default_sink();
  void apply_sink(const pa_sink_info* info);

  AppState& state_;
  NotifyFn notify_;
  std::unique_ptr<PulseLoop> loop_;
  pa_context* context_ = nullptr;
  int sink_index_ = -1;
  bool muted_ = false;
  int volume_percent_ = 0;
  pa_cvolume volume_template_{};
  std::string default_sink_;
};

}  // namespace pillbar
