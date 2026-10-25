#include "sources/public_ip.hpp"

#include <sys/epoll.h>

#include <utility>

#include "app/logging.hpp"
#include "sources/json.hpp"

namespace pillbar {
namespace {

constexpr const char* kLookupUrl = "https://ipwho.is/";
constexpr std::size_t kMaxResponseBytes = 16384;
constexpr auto kCacheLifetime = std::chrono::minutes(10);
constexpr auto kFailureRetry = std::chrono::seconds(30);

bool curl_ready() {
  static const bool ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
  return ready;
}

} // namespace

PublicIpLookup::PublicIpLookup(NotifyFn notify) : notify_(std::move(notify)) {}

PublicIpLookup::~PublicIpLookup() {
  cancel_request();
  if (multi_ != nullptr) curl_multi_cleanup(multi_);
  if (loop_ != nullptr) {
    for (const int fd : sockets_)
      loop_->del(fd);
  }
}

bool PublicIpLookup::start(EventLoop& loop) {
  if (!curl_ready()) return false;
  multi_ = curl_multi_init();
  if (multi_ == nullptr) return false;
  loop_ = &loop;
  curl_multi_setopt(multi_, CURLMOPT_SOCKETFUNCTION, &PublicIpLookup::socket_callback);
  curl_multi_setopt(multi_, CURLMOPT_SOCKETDATA, this);
  curl_multi_setopt(multi_, CURLMOPT_TIMERFUNCTION, &PublicIpLookup::timer_callback);
  curl_multi_setopt(multi_, CURLMOPT_TIMERDATA, this);
  curl_multi_setopt(multi_, CURLMOPT_MAX_TOTAL_CONNECTIONS, 1L);
  loop_->add(timer_.get(), EPOLLIN, [this](std::uint32_t) { on_timer(); });
  timer_.disarm();
  return true;
}

void PublicIpLookup::request() {
  if (loop_ == nullptr || multi_ == nullptr || loading_) return;
  const auto now = std::chrono::steady_clock::now();
  if (!info_.address.empty() && now - last_finished_ < kCacheLifetime) return;
  if (failed_ && now - last_finished_ < kFailureRetry) return;
  begin_request();
}

void PublicIpLookup::invalidate() {
  info_ = {};
  failed_ = false;
  last_finished_ = {};
  cancel_request();
}

std::vector<std::string> PublicIpLookup::detail() const {
  if (!info_.address.empty()) {
    std::vector<std::string> lines{"Public IP: " + info_.address};
    std::string location = info_.city;
    if (!location.empty() && !info_.country.empty()) location += ", ";
    location += info_.country;
    if (!location.empty()) lines.push_back("Location: " + location);
    return lines;
  }
  if (loading_) return {"Public IP: looking up..."};
  return {"Public IP: unavailable"};
}

size_t PublicIpLookup::write_callback(char* data, size_t size, size_t count, void* userdata) {
  auto* self = static_cast<PublicIpLookup*>(userdata);
  const std::size_t bytes = size * count;
  if (self->response_.size() + bytes > kMaxResponseBytes) return 0;
  self->response_.append(data, bytes);
  return bytes;
}

int PublicIpLookup::socket_callback(CURL*, curl_socket_t socket, int action, void* userdata,
                                    void*) {
  auto* self = static_cast<PublicIpLookup*>(userdata);
  const int fd = static_cast<int>(socket);
  if (action == CURL_POLL_REMOVE) {
    if (self->loop_ != nullptr && self->sockets_.erase(fd) > 0) self->loop_->del(fd);
    return 0;
  }
  std::uint32_t events = 0;
  if ((action & CURL_POLL_IN) != 0) events |= EPOLLIN;
  if ((action & CURL_POLL_OUT) != 0) events |= EPOLLOUT;
  if (events == 0) return 0;
  if (self->loop_ == nullptr) return -1;
  self->sockets_.insert(fd);
  self->loop_->add(fd, events,
                   [self, fd](std::uint32_t ready) { self->on_socket_event(fd, ready); });
  return 0;
}

int PublicIpLookup::timer_callback(CURLM*, long timeout_ms, void* userdata) {
  auto* self = static_cast<PublicIpLookup*>(userdata);
  if (timeout_ms < 0) {
    self->timer_.disarm();
  } else {
    self->timer_.arm_relative_ms(static_cast<int>(timeout_ms), true);
  }
  return 0;
}

void PublicIpLookup::begin_request() {
  easy_ = curl_easy_init();
  if (easy_ == nullptr) {
    failed_ = true;
    last_finished_ = std::chrono::steady_clock::now();
    if (notify_) notify_(Item::Wifi);
    return;
  }
  response_.clear();
  loading_ = true;
  failed_ = false;
  curl_easy_setopt(easy_, CURLOPT_URL, kLookupUrl);
  curl_easy_setopt(easy_, CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(easy_, CURLOPT_REDIR_PROTOCOLS_STR, "https");
  curl_easy_setopt(easy_, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
  curl_easy_setopt(easy_, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(easy_, CURLOPT_MAXREDIRS, 2L);
  curl_easy_setopt(easy_, CURLOPT_CONNECTTIMEOUT_MS, 4000L);
  curl_easy_setopt(easy_, CURLOPT_TIMEOUT_MS, 7000L);
  curl_easy_setopt(easy_, CURLOPT_USERAGENT, "pillbar/0.1");
  curl_easy_setopt(easy_, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(easy_, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(easy_, CURLOPT_WRITEFUNCTION, &PublicIpLookup::write_callback);
  curl_easy_setopt(easy_, CURLOPT_WRITEDATA, this);
  const CURLMcode added = curl_multi_add_handle(multi_, easy_);
  if (added != CURLM_OK) {
    finish_request(CURLE_FAILED_INIT);
    return;
  }
  int running = 0;
  curl_multi_socket_action(multi_, CURL_SOCKET_TIMEOUT, 0, &running);
  read_completed();
}

void PublicIpLookup::read_completed() {
  int queued = 0;
  while (CURLMsg* message = curl_multi_info_read(multi_, &queued)) {
    if (message->msg != CURLMSG_DONE || message->easy_handle != easy_) continue;
    finish_request(message->data.result);
    break;
  }
}

void PublicIpLookup::cancel_request() {
  if (easy_ != nullptr && multi_ != nullptr) curl_multi_remove_handle(multi_, easy_);
  if (easy_ != nullptr) curl_easy_cleanup(easy_);
  easy_ = nullptr;
  loading_ = false;
  response_.clear();
  timer_.disarm();
}

void PublicIpLookup::finish_request(CURLcode result) {
  long status = 0;
  if (easy_ != nullptr) curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &status);
  const bool http_ok = result == CURLE_OK && status >= 200 && status < 300;
  PublicIpInfo next;
  if (http_ok && parse_response()) next = info_;
  const bool ok = http_ok && !next.address.empty();
  info_ = ok ? next : PublicIpInfo{};
  failed_ = !ok;
  if (ok) {
    LOG_DEBUG("public ip: %s (%s, %s)", info_.address.c_str(), info_.city.c_str(),
              info_.country.c_str());
  } else {
    LOG_DEBUG("public ip lookup failed: %s (HTTP %ld)", curl_easy_strerror(result), status);
  }
  loading_ = false;
  last_finished_ = std::chrono::steady_clock::now();
  cancel_request();
  if (notify_) notify_(Item::Wifi);
}

void PublicIpLookup::on_socket_event(int fd, std::uint32_t events) {
  int action = 0;
  if ((events & EPOLLIN) != 0) action |= CURL_CSELECT_IN;
  if ((events & EPOLLOUT) != 0) action |= CURL_CSELECT_OUT;
  if ((events & (EPOLLERR | EPOLLHUP)) != 0) action |= CURL_CSELECT_ERR;
  if (multi_ == nullptr) return;
  int running = 0;
  curl_multi_socket_action(multi_, fd, action, &running);
  read_completed();
}

void PublicIpLookup::on_timer() {
  timer_.consume();
  if (multi_ == nullptr) return;
  int running = 0;
  curl_multi_socket_action(multi_, CURL_SOCKET_TIMEOUT, 0, &running);
  read_completed();
}

bool PublicIpLookup::parse_response() {
  PublicIpInfo next;
  std::string error;
  const JsonValue root = json_parse(response_, &error);
  if (!root.is_object()) return false;
  const JsonValue* success = root.find("success");
  if (success != nullptr && success->type == JsonValue::Type::Bool && !success->bool_value) {
    return false;
  }
  const JsonValue* address = root.find("ip");
  const JsonValue* city = root.find("city");
  const JsonValue* region = root.find("region");
  const JsonValue* country = root.find("country");
  if (address != nullptr) next.address = address->as_string();
  if (city != nullptr) next.city = city->as_string();
  if (next.city.empty() && region != nullptr) next.city = region->as_string();
  if (country != nullptr) next.country = country->as_string();
  if (next.address.empty()) return false;
  info_ = std::move(next);
  return true;
}

} // namespace pillbar
