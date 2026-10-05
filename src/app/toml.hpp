#pragma once

#include <map>
#include <string>
#include <vector>

namespace pillbar {

// Minimal TOML subset: tables ([section]), key = value where value is a
// string, boolean, integer, float or homogeneous array. Enough for the
// pillbar config; not a general TOML implementation.
class TomlValue {
 public:
  enum class Type { Null, Bool, Int, Float, String, Array };
  using Array = std::vector<TomlValue>;

  Type type = Type::Null;
  bool bool_value = false;
  long long int_value = 0;
  double float_value = 0.0;
  std::string string_value;
  Array array_value;

  bool is_null() const { return type == Type::Null; }
  bool is_array() const { return type == Type::Array; }

  bool as_bool(bool fallback) const;
  long long as_int(long long fallback) const;
  double as_double(double fallback) const;
  std::string as_string(std::string fallback) const;
};

using TomlTable = std::map<std::string, TomlValue>;

TomlTable toml_parse(const std::string& text, std::string* error);
std::string toml_read_file(const std::string& path, bool* ok);

}  // namespace pillbar

