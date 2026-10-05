#pragma once

#include <string>

#include "app/event_loop.hpp"
#include "model/state.hpp"
#include "sources/source.hpp"

namespace pillbar {

// Hyprland integration. Subscribes to the event stream on .socket2 (line
// parsed, non-blocking, reconnects with backoff) and uses .socket.sock only
// for initial state queries and `dispatch` commands. hyprctl is never polled.
class HyprlandSource : public Source {
 public:
  HyprlandSource(AppState& state, NotifyFn notify, int min_workspaces, int max_workspaces);
  ~HyprlandSource() override;

  const char* name() const override { return "hyprland"; }
  bool start(EventLoop& loop) override;
  void refresh() override;

  // Sends "dispatch <command>" over the command socket (workspace switch, etc).
  void dispatch_command(const std::string& command);

 private:
  bool connect_event();
  void close_event();
  void on_event();
  void handle_line(const std::string& line);
  void requery();
  std::string request(const std::string& command);
  void schedule_reconnect();

  AppState& state_;
  NotifyFn notify_;
  int min_workspaces_ = 5;
  int max_workspaces_ = 10;
  EventLoop* loop_ = nullptr;
  std::string event_path_;
  std::string command_path_;
  Fd event_fd_;
  std::string buffer_;
  TimerFd reconnect_;
  int backoff_ms_ = 1000;
  bool have_workspaces_ = false;
  bool have_window_ = false;
};

}  // namespace pillbar

