#include "sources/audio.hpp"

#include <algorithm>
#include <cmath>

#include "app/logging.hpp"

namespace pillbar {

AudioSource::AudioSource(AppState& state, NotifyFn notify)
    : state_(state), notify_(std::move(notify)) {}

AudioSource::~AudioSource() {
  if (context_ != nullptr) {
    pa_context_disconnect(context_);
    pa_context_unref(context_);
    context_ = nullptr;
  }
}

bool AudioSource::start(EventLoop& loop) {
  loop_ = std::make_unique<PulseLoop>(loop);
  context_ = pa_context_new(loop_->api(), "pillbar");
  if (context_ == nullptr) {
    LOG_WARN("pa_context_new failed");
    return false;
  }
  pa_context_set_state_callback(context_, &AudioSource::ctx_state_cb, this);
  if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
    LOG_WARN("pa_context_connect: %s", pa_strerror(pa_context_errno(context_)));
    return false;
  }
  state_.volume.available = false;
  return true;
}

void AudioSource::refresh() {
  if (context_ != nullptr && pa_context_get_state(context_) == PA_CONTEXT_READY) {
    request_default_sink();
  }
}

void AudioSource::ctx_state_cb(pa_context* context, void* userdata) {
  auto* self = static_cast<AudioSource*>(userdata);
  switch (pa_context_get_state(context)) {
    case PA_CONTEXT_READY: {
      pa_context_set_subscribe_callback(context, &AudioSource::subscribe_cb, self);
      pa_context_subscribe(context,
                           static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK |
                                                               PA_SUBSCRIPTION_MASK_SERVER),
                           nullptr, nullptr);
      pa_context_get_server_info(context, &AudioSource::server_info_cb, self);
      break;
    }
    case PA_CONTEXT_FAILED:
      LOG_WARN("pulse context %s", pa_strerror(pa_context_errno(context)));
      self->state_.volume.available = false;
      break;
    case PA_CONTEXT_TERMINATED:
      self->state_.volume.available = false;
      break;
    default:
      break;
  }
}

void AudioSource::server_info_cb(pa_context* context, const pa_server_info* info, void* userdata) {
  auto* self = static_cast<AudioSource*>(userdata);
  if (info != nullptr && info->default_sink_name != nullptr) {
    self->default_sink_ = info->default_sink_name;
  }
  if (context != nullptr) {
    pa_context_get_sink_info_by_name(context, self->default_sink_.c_str(),
                                     &AudioSource::sink_info_cb, self);
  }
}

void AudioSource::sink_info_cb(pa_context*, const pa_sink_info* info, int eol, void* userdata) {
  auto* self = static_cast<AudioSource*>(userdata);
  if (eol > 0 || info == nullptr) return;
  self->apply_sink(info);
}

void AudioSource::apply_sink(const pa_sink_info* info) {
  VolumeState next;
  next.available = true;
  next.muted = info->mute != 0;
  next.sink_name = info->name != nullptr ? info->name : "";
  next.sink_desc = info->description != nullptr ? info->description : "";
  const pa_volume_t avg = pa_cvolume_avg(&info->volume);
  next.percent = static_cast<int>(std::lround(static_cast<double>(avg) * 100.0 /
                                              static_cast<double>(PA_VOLUME_NORM)));
  next.percent = std::clamp(next.percent, 0, 100);
  sink_index_ = static_cast<int>(info->index);
  muted_ = next.muted;
  volume_percent_ = next.percent;
  volume_template_ = info->volume;
  if (next == state_.volume) return;
  state_.volume = next;
  if (notify_) notify_(Item::Volume);
}

void AudioSource::subscribe_cb(pa_context* context, pa_subscription_event_type_t type,
                               uint32_t index, void* userdata) {
  auto* self = static_cast<AudioSource*>(userdata);
  const auto facility = static_cast<pa_subscription_event_type_t>(type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK);
  if (facility == PA_SUBSCRIPTION_EVENT_SERVER) {
    pa_context_get_server_info(context, &AudioSource::server_info_cb, self);
  } else if (facility == PA_SUBSCRIPTION_EVENT_SINK) {
    pa_context_get_sink_info_by_index(context, index, &AudioSource::sink_info_cb, self);
  }
}

void AudioSource::success_cb(pa_context*, int success, void*) {
  if (success == 0) LOG_WARN("pulse operation failed");
}

void AudioSource::request_default_sink() {
  if (context_ == nullptr) return;
  if (!default_sink_.empty()) {
    pa_context_get_sink_info_by_name(context_, default_sink_.c_str(), &AudioSource::sink_info_cb,
                                     this);
  } else {
    pa_context_get_server_info(context_, &AudioSource::server_info_cb, this);
  }
}

void AudioSource::set_volume_relative(int delta_percent) {
  if (context_ == nullptr || sink_index_ < 0) return;
  const int target = std::clamp(volume_percent_ + delta_percent, 0, 100);
  pa_cvolume volume = volume_template_;
  const unsigned channels = volume.channels > 0 ? volume.channels : 1;
  pa_cvolume_set(&volume, channels,
                 static_cast<pa_volume_t>(static_cast<long long>(PA_VOLUME_NORM) * target / 100));
  pa_context_set_sink_volume_by_index(context_, static_cast<uint32_t>(sink_index_), &volume,
                                      &AudioSource::success_cb, this);
}

void AudioSource::toggle_mute() {
  if (context_ == nullptr || sink_index_ < 0) return;
  pa_context_set_sink_mute_by_index(context_, static_cast<uint32_t>(sink_index_), muted_ ? 0 : 1,
                                    &AudioSource::success_cb, this);
}

}  // namespace pillbar
