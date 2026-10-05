#include "app/logging.hpp"

#include <atomic>
#include <string>

namespace pillbar {
namespace {

std::atomic<int> g_level{static_cast<int>(LogLevel::Warn)};
bool g_initialized = false;
std::atomic<bool> g_in_log{false};

const char* level_name(LogLevel level) {
  switch (level) {
    case LogLevel::Error:
      return "ERROR";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Debug:
      return "DEBUG";
  }
  return "?";
}

LogLevel parse_level(const char* raw) {
  if (raw == nullptr || *raw == '\0') return LogLevel::Warn;
  if (std::strcmp(raw, "error") == 0) return LogLevel::Error;
  if (std::strcmp(raw, "warn") == 0) return LogLevel::Warn;
  if (std::strcmp(raw, "info") == 0) return LogLevel::Info;
  if (std::strcmp(raw, "debug") == 0) return LogLevel::Debug;
  return LogLevel::Warn;
}

}  // namespace

LogLevel log_level() { return static_cast<LogLevel>(g_level.load(std::memory_order_relaxed)); }

void log_set_level(LogLevel level) {
  g_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

void log_emit(LogLevel level, const char* file, int line, const char* fmt, ...) {
  if (!g_initialized) {
    g_initialized = true;
    log_set_level(parse_level(std::getenv("PILLBAR_LOG")));
  }
  if (static_cast<int>(level) > g_level.load(std::memory_order_relaxed)) return;
  if (g_in_log.exchange(true)) return;  // guard against reentrant logging

  const char* base = std::strrchr(file, '/');
  base = base != nullptr ? base + 1 : file;

  std::fprintf(stderr, "[pillbar %s %s:%d] ", level_name(level), base, line);
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);
  std::fputc('\n', stderr);
  std::fflush(stderr);

  g_in_log.store(false, std::memory_order_relaxed);
}

}  // namespace pillbar

