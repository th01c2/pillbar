#pragma once

#include <curl/curl.h>

#include <chrono>
#include <string>
#include <unordered_set>
#include <vector>

#include "app/event_loop.hpp"
#include "sources/source.hpp"

namespace pillbar {

struct PublicIpInfo {
  std::string address;
  std::string city;
  std::string country;
  bool operator==(const PublicIpInfo&) const = default;
};

class PublicIpLookup {
 public:
  explicit PublicIpLookup(NotifyFn notify);
  ~PublicIpLookup();
  PublicIpLookup(const PublicIpLookup&) = delete;
  PublicIpLookup& operator=(const PublicIpLookup&) = delete;

  bool start(EventLoop& loop);
  void request();
  void invalidate();
  std::vector<std::string> detail() const;

 private:
  static size_t write_callback(char* data, size_t size, size_t count, void* userdata);
  static int socket_callback(CURL* easy, curl_socket_t socket, int action, void* userdata,
                             void* socketp);
  static int timer_callback(CURLM* multi, long timeout_ms, void* userdata);

  void begin_request();
  void cancel_request();
  void finish_request(CURLcode result);
  void read_completed();
  void on_socket_event(int fd, std::uint32_t events);
  void on_timer();
  bool parse_response();

  NotifyFn notify_;
  EventLoop* loop_ = nullptr;
  CURLM* multi_ = nullptr;
  CURL* easy_ = nullptr;
  std::string response_;
  PublicIpInfo info_;
  std::unordered_set<int> sockets_;
  TimerFd timer_;
  bool loading_ = false;
  bool failed_ = false;
  std::chrono::steady_clock::time_point last_finished_;
};

} // namespace pillbar
