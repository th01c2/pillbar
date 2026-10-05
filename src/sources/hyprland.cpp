#include "sources/hyprland.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <fcntl.h>

#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/logging.hpp"
#include "sources/json.hpp"

namespace pillbar {
namespace {

std::string instance_dir() {
  const char* runtime = std::getenv("XDG_RUNTIME_DIR");
  const char* signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (runtime == nullptr || signature == nullptr) return {};
  return std::string(runtime) + "/hypr/" + signature;
}

int connect_unix(const std::string& path, bool nonblocking) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return -1;
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return -1;
  }
  if (nonblocking) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
  return fd;
}

}  // namespace

HyprlandSource::HyprlandSource(AppState& state, NotifyFn notify, int min_workspaces,
                               int max_workspaces)
    : state_(state), notify_(std::move(notify)), min_workspaces_(min_workspaces),
      max_workspaces_(max_workspaces) {}

HyprlandSource::~HyprlandSource() { close_event(); }

bool HyprlandSource::start(EventLoop& loop) {
  loop_ = &loop;
  const std::string dir = instance_dir();
  if (dir.empty()) {
    LOG_WARN("HYPRLAND_INSTANCE_SIGNATURE/XDG_RUNTIME_DIR unset; Hyprland items hidden");
    return false;
  }
  event_path_ = dir + "/.socket2.sock";
  command_path_ = dir + "/.socket.sock";
  if (reconnect_.valid()) {
    loop_->add(reconnect_.get(), EPOLLIN, [this](std::uint32_t) {
      reconnect_.consume();
      if (!connect_event()) schedule_reconnect();
    });
  }
  if (!connect_event()) {
    schedule_reconnect();
    return false;
  }
  requery();
  return true;
}

void HyprlandSource::refresh() { requery(); }

bool HyprlandSource::connect_event() {
  close_event();
  const int fd = connect_unix(event_path_, true);
  if (fd < 0) {
    LOG_DEBUG("hyprland event socket unavailable: %s", std::strerror(errno));
    return false;
  }
  event_fd_.reset(fd);
  loop_->add(fd, EPOLLIN, [this](std::uint32_t events) {
    if ((events & (EPOLLHUP | EPOLLERR)) != 0) {
      close_event();
      schedule_reconnect();
      return;
    }
    on_event();
  });
  backoff_ms_ = 1000;
  buffer_.clear();
  LOG_INFO("connected to Hyprland event socket");
  return true;
}

void HyprlandSource::close_event() {
  if (event_fd_.valid()) {
    if (loop_ != nullptr) loop_->del(event_fd_.get());
    event_fd_.reset();
  }
}

void HyprlandSource::schedule_reconnect() {
  if (!reconnect_.valid() || loop_ == nullptr) return;
  reconnect_.arm_relative_ms(backoff_ms_, true);
  backoff_ms_ = backoff_ms_ < 15000 ? backoff_ms_ * 2 : 15000;
}

void HyprlandSource::on_event() {
  char chunk[4096];
  for (;;) {
    const ssize_t n = ::recv(event_fd_.get(), chunk, sizeof(chunk), 0);
    if (n > 0) {
      buffer_.append(chunk, static_cast<std::size_t>(n));
      continue;
    }
    if (n == 0) {
      close_event();
      schedule_reconnect();
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    if (errno == EINTR) continue;
    close_event();
    schedule_reconnect();
    return;
  }
  std::size_t pos = 0;
  for (;;) {
    const std::size_t newline = buffer_.find('\n', pos);
    if (newline == std::string::npos) break;
    handle_line(buffer_.substr(pos, newline - pos));
    pos = newline + 1;
  }
  buffer_.erase(0, pos);
  if (buffer_.size() > 1u << 20) buffer_.clear();  // guard against unbounded growth
}

void HyprlandSource::handle_line(const std::string& line) {
  const std::size_t sep = line.find(">>");
  const std::string event = sep == std::string::npos ? line : line.substr(0, sep);
  // Events that only change the active window / its title: re-query just
  // j/activewindow. A terminal that rewrites its title ten times a second
  // otherwise triggers four socket queries per update.
  static const char* kWindowOnly[] = {"activewindow", "activewindowv2", "fullscreen",
                                      "changefloatingmode", "urgent"};
  for (const char* name : kWindowOnly) {
    if (event == name) {
      requery_window();
      return;
    }
  }
  static const char* kRelevant[] = {
      "workspace",         "workspacev2",    "focusedmon",        "openwindow",
      "closewindow",       "movewindow",     "createworkspace",   "destroyworkspace",
      "monitoradded",      "monitorremoved"};
  for (const char* name : kRelevant) {
    if (event == name) {
      requery();
      return;
    }
  }
}

std::string HyprlandSource::request(const std::string& command) {
  const int fd = connect_unix(command_path_, false);
  if (fd < 0) return {};
  struct timeval timeout{};
  timeout.tv_sec = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  const ssize_t written = ::write(fd, command.data(), command.size());
  std::string response;
  if (written == static_cast<ssize_t>(command.size())) {
    char chunk[4096];
    for (;;) {
      const ssize_t n = ::read(fd, chunk, sizeof(chunk));
      if (n > 0) {
        response.append(chunk, static_cast<std::size_t>(n));
      } else {
        break;
      }
    }
  }
  ::close(fd);
  return response;
}

void HyprlandSource::dispatch_command(const std::string& command) {
  request("dispatch " + command);
}

namespace {
// Returns true when the compositor replied "ok" (legacy replies are empty on
// success in some versions, so only an explicit error counts as failure).
bool dispatch_ok(const std::string& response) {
  return response.rfind("error", 0) != 0;
}
}  // namespace

void HyprlandSource::dispatch_workspace(int id) {
  const std::string name = std::to_string(id);
  if (dispatch_ok(request("dispatch hl.dsp.focus({workspace=\"" + name + "\"})"))) return;
  request("dispatch workspace " + name);
}

void HyprlandSource::dispatch_workspace_relative(int delta) {
  const std::string selector = delta > 0 ? "e+1" : "e-1";
  if (dispatch_ok(request("dispatch hl.dsp.focus({workspace=\"" + selector + "\"})"))) return;
  request("dispatch workspace " + selector);
}

void HyprlandSource::requery() {
  if (command_path_.empty()) return;
  std::string error;

  // Workspaces + clients.
  const JsonValue workspaces = json_parse(request("j/workspaces"), &error);
  const JsonValue clients = json_parse(request("j/clients"), &error);
  const JsonValue active = json_parse(request("j/activeworkspace"), &error);

  int active_id = -1;
  if (active.is_object()) {
    const JsonValue* id = active.find("id");
    if (id != nullptr) active_id = id->as_int(-1);
  }

  WorkspacesState next;
  int max_id = min_workspaces_ > 0 ? min_workspaces_ : 5;
  if (workspaces.is_array()) {
    for (const JsonValue& ws : workspaces.array_value) {
      const JsonValue* id = ws.find("id");
      if (id != nullptr) max_id = std::max(max_id, id->as_int(0));
    }
  }
  max_id = std::min(max_id, max_workspaces_ > 0 ? max_workspaces_ : max_id);

  for (int id = 1; id <= max_id; ++id) {
    WorkspaceState ws;
    ws.id = id;
    ws.name = std::to_string(id);
    ws.focused = id == active_id;
    if (workspaces.is_array()) {
      for (const JsonValue& entry : workspaces.array_value) {
        const JsonValue* entry_id = entry.find("id");
        if (entry_id == nullptr || entry_id->as_int(-1) != id) continue;
        const JsonValue* windows = entry.find("windows");
        if (windows != nullptr) ws.windows = windows->as_int(0);
        const JsonValue* name = entry.find("name");
        if (name != nullptr && !name->as_string().empty()) ws.name = name->as_string();
        ws.occupied = ws.windows > 0;
      }
    }
    if (clients.is_array()) {
      for (const JsonValue& client : clients.array_value) {
        const JsonValue* workspace = client.find("workspace");
        if (workspace == nullptr) continue;
        const JsonValue* ws_id = workspace->find("id");
        if (ws_id == nullptr || ws_id->as_int(-1) != id) continue;
        const JsonValue* cls = client.find("class");
        if (cls != nullptr && !cls->as_string().empty()) {
          ws.classes.push_back(cls->as_string());
        }
        ws.occupied = true;
      }
    }
    next.list.push_back(std::move(ws));
  }

  if (!have_workspaces_ || !(next == state_.workspaces)) {
    state_.workspaces = next;
    have_workspaces_ = true;
    if (notify_) notify_(Item::Workspaces);
  }

  update_window();
}

// Query only j/activewindow; used for events that cannot change workspaces.
void HyprlandSource::requery_window() {
  if (command_path_.empty()) return;
  update_window();
}

void HyprlandSource::update_window() {
  std::string error;
  const JsonValue window = json_parse(request("j/activewindow"), &error);
  WindowState win;
  if (window.is_object()) {
    const JsonValue* cls = window.find("class");
    const JsonValue* title = window.find("title");
    const JsonValue* pid = window.find("pid");
    const JsonValue* workspace = window.find("workspace");
    if (cls != nullptr && !cls->as_string().empty()) {
      win.present = true;
      win.cls = cls->as_string();
    } else if (title != nullptr && !title->as_string().empty()) {
      win.present = true;
    }
    if (title != nullptr) win.title = title->as_string();
    if (pid != nullptr) win.pid = pid->as_int(0);
    if (workspace != nullptr) {
      const JsonValue* ws_id = workspace->find("id");
      if (ws_id != nullptr) win.workspace = ws_id->as_int(0);
    }
  }
  if (!have_window_ || !(win == state_.window)) {
    state_.window = win;
    have_window_ = true;
    if (notify_) notify_(Item::ActiveWindow);
  }
}

}  // namespace pillbar
