#pragma once

#include <string>
#include <utility>
#include <vector>

namespace pillbar {

// Minimal JSON value + parser. Sufficient for parsing Hyprland `j/...`
// responses and BlueZ/NM D-Bus string payloads. Not a general-purpose library.
class JsonValue {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool bool_value = false;
  double number_value = 0.0;
  std::string string_value;
  std::vector<JsonValue> array_value;
  // Object members are stored as parallel vectors to avoid instantiating
  // std::pair<...> with the still-incomplete JsonValue type.
  std::vector<std::string> object_keys;
  std::vector<JsonValue> object_values;

  bool is_null() const { return type == Type::Null; }
  bool is_object() const { return type == Type::Object; }
  bool is_array() const { return type == Type::Array; }

  const JsonValue* find(const std::string& key) const;
  std::string as_string(const std::string& fallback = "") const;
  double as_number(double fallback = 0.0) const;
  int as_int(int fallback = 0) const;
  bool as_bool(bool fallback = false) const;
};

JsonValue json_parse(const std::string& text, std::string* error);

}  // namespace pillbar
