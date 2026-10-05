#include "sources/sysfs.hpp"

#include <dirent.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace pillbar {
namespace sysfs {

namespace {
std::string read_all(const std::string& path, bool* ok) {
  std::ifstream file(path);
  if (!file) {
    if (ok != nullptr) *ok = false;
    return {};
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (ok != nullptr) *ok = true;
  return buffer.str();
}
}  // namespace

bool read_string(const std::string& path, std::string* out) {
  bool ok = false;
  std::string value = read_all(path, &ok);
  if (!ok) return false;
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
    value.pop_back();
  }
  *out = value;
  return true;
}

bool read_long(const std::string& path, long* out) {
  std::string value;
  if (!read_string(path, &value)) return false;
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str()) return false;
  *out = parsed;
  return true;
}

bool read_int(const std::string& path, int* out) {
  long value = 0;
  if (!read_long(path, &value)) return false;
  *out = static_cast<int>(value);
  return true;
}

bool read_double(const std::string& path, double* out) {
  std::string value;
  if (!read_string(path, &value)) return false;
  char* end = nullptr;
  const double parsed = std::strtod(value.c_str(), &end);
  if (end == value.c_str()) return false;
  *out = parsed;
  return true;
}

bool exists(const std::string& path) {
  std::ifstream file(path);
  return static_cast<bool>(file);
}

std::vector<std::string> list_directory(const std::string& path) {
  std::vector<std::string> entries;
  DIR* dir = ::opendir(path.c_str());
  if (dir == nullptr) return entries;
  while (dirent* entry = ::readdir(dir)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    entries.push_back(name);
  }
  ::closedir(dir);
  return entries;
}

}  // namespace sysfs
}  // namespace pillbar

