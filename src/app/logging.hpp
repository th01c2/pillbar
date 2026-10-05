#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pillbar {

enum class LogLevel : int { Error = 0, Warn = 1, Info = 2, Debug = 3 };

LogLevel log_level();
void log_set_level(LogLevel level);

void log_emit(LogLevel level, const char* file, int line, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

}  // namespace pillbar

#define PILLBAR_LOG_AT(lvl, ...) \
  ::pillbar::log_emit((lvl), __FILE__, __LINE__, __VA_ARGS__)

#define LOG_ERR(...) PILLBAR_LOG_AT(::pillbar::LogLevel::Error, __VA_ARGS__)
#define LOG_WARN(...) PILLBAR_LOG_AT(::pillbar::LogLevel::Warn, __VA_ARGS__)
#define LOG_INFO(...) PILLBAR_LOG_AT(::pillbar::LogLevel::Info, __VA_ARGS__)
#define LOG_DEBUG(...) PILLBAR_LOG_AT(::pillbar::LogLevel::Debug, __VA_ARGS__)

