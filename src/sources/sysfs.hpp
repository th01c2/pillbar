#pragma once

#include <string>
#include <vector>

namespace pillbar {
namespace sysfs {

bool read_string(const std::string& path, std::string* out);
bool read_long(const std::string& path, long* out);
bool read_int(const std::string& path, int* out);
bool read_double(const std::string& path, double* out);
bool exists(const std::string& path);
// Lists immediate entry names (no path prefix) of a directory.
std::vector<std::string> list_directory(const std::string& path);

}  // namespace sysfs
}  // namespace pillbar

