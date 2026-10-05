#include "app/toml.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace pillbar {

bool TomlValue::as_bool(bool fallback) const {
  return type == Type::Bool ? bool_value : fallback;
}

long long TomlValue::as_int(long long fallback) const {
  switch (type) {
    case Type::Int:
      return int_value;
    case Type::Float:
      return static_cast<long long>(float_value);
    case Type::Bool:
      return bool_value ? 1 : 0;
    default:
      return fallback;
  }
}

double TomlValue::as_double(double fallback) const {
  switch (type) {
    case Type::Int:
      return static_cast<double>(int_value);
    case Type::Float:
      return float_value;
    default:
      return fallback;
  }
}

std::string TomlValue::as_string(std::string fallback) const {
  return type == Type::String ? string_value : fallback;
}

namespace {

std::string trim(const std::string& in) {
  std::size_t b = 0;
  std::size_t e = in.size();
  while (b < e && std::isspace(static_cast<unsigned char>(in[b])) != 0) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1])) != 0) --e;
  return in.substr(b, e - b);
}

std::string strip_comment(const std::string& line) {
  bool in_string = false;
  char quote = '\0';
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (in_string) {
      if (c == '\\' && quote == '"') {
        ++i;
        continue;
      }
      if (c == quote) in_string = false;
      continue;
    }
    if (c == '"' || c == '\'') {
      in_string = true;
      quote = c;
      continue;
    }
    if (c == '#') return line.substr(0, i);
  }
  return line;
}

bool parse_value(const std::string& raw, TomlValue* out, std::string* error);

bool parse_array(const std::string& body, TomlValue* out, std::string* error) {
  out->type = TomlValue::Type::Array;
  std::string current;
  bool in_string = false;
  char quote = '\0';
  int depth = 0;
  auto flush = [&]() -> bool {
    const std::string piece = trim(current);
    current.clear();
    if (piece.empty()) return true;
    TomlValue element;
    if (!parse_value(piece, &element, error)) return false;
    out->array_value.push_back(std::move(element));
    return true;
  };
  for (std::size_t i = 0; i < body.size(); ++i) {
    const char c = body[i];
    if (in_string) {
      current.push_back(c);
      if (c == '\\' && quote == '"' && i + 1 < body.size()) {
        current.push_back(body[++i]);
        continue;
      }
      if (c == quote) in_string = false;
      continue;
    }
    if (c == '"' || c == '\'') {
      in_string = true;
      quote = c;
      current.push_back(c);
      continue;
    }
    if (c == '[') {
      ++depth;
      current.push_back(c);
      continue;
    }
    if (c == ']') {
      if (depth == 0) {
        if (!flush()) return false;
        return true;
      }
      --depth;
      current.push_back(c);
      continue;
    }
    if (c == ',' && depth == 0) {
      if (!flush()) return false;
      continue;
    }
    current.push_back(c);
  }
  if (!flush()) return false;
  return true;
}

std::string unescape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char n = s[++i];
      switch (n) {
        case 'n':
          out.push_back('\n');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '"':
          out.push_back('"');
          break;
        default:
          out.push_back(n);
          break;
      }
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

bool parse_value(const std::string& raw, TomlValue* out, std::string* error) {
  const std::string value = trim(raw);
  if (value.empty()) {
    out->type = TomlValue::Type::Null;
    return true;
  }
  const char first = value.front();
  if (first == '"' || first == '\'') {
    if (value.size() < 2 || value.back() != first) {
      *error = "unterminated string";
      return false;
    }
    out->type = TomlValue::Type::String;
    const std::string inner = value.substr(1, value.size() - 2);
    out->string_value = first == '"' ? unescape(inner) : inner;
    return true;
  }
  if (first == '[') {
    if (value.back() != ']') {
      *error = "unterminated array";
      return false;
    }
    return parse_array(value.substr(1, value.size() - 2), out, error);
  }
  if (value == "true" || value == "false") {
    out->type = TomlValue::Type::Bool;
    out->bool_value = value == "true";
    return true;
  }
  // Numeric (integer or float), tolerating underscores.
  std::string numeric;
  numeric.reserve(value.size());
  for (char c : value) {
    if (c != '_') numeric.push_back(c);
  }
  char* end = nullptr;
  const bool looks_float =
      numeric.find('.') != std::string::npos || numeric.find('e') != std::string::npos ||
      numeric.find('E') != std::string::npos;
  if (looks_float) {
    const double d = std::strtod(numeric.c_str(), &end);
    if (end != numeric.c_str() && *end == '\0') {
      out->type = TomlValue::Type::Float;
      out->float_value = d;
      return true;
    }
  } else {
    const long long i = std::strtoll(numeric.c_str(), &end, 0);
    if (end != numeric.c_str() && *end == '\0') {
      out->type = TomlValue::Type::Int;
      out->int_value = i;
      return true;
    }
  }
  *error = "unrecognized value: " + value;
  return false;
}

}  // namespace

TomlTable toml_parse(const std::string& text, std::string* error) {
  TomlTable table;
  std::string prefix;
  std::istringstream stream(text);
  std::string line;
  int lineno = 0;
  while (std::getline(stream, line)) {
    ++lineno;
    std::string cleaned = trim(strip_comment(line));
    if (cleaned.empty()) continue;
    if (cleaned.front() == '[') {
      if (cleaned.back() != ']') {
        if (error) *error = "line " + std::to_string(lineno) + ": bad table header";
        return table;
      }
      prefix = trim(cleaned.substr(1, cleaned.size() - 2));
      continue;
    }
    const std::size_t eq = cleaned.find('=');
    if (eq == std::string::npos) {
      if (error) *error = "line " + std::to_string(lineno) + ": expected key = value";
      return table;
    }
    const std::string key = trim(cleaned.substr(0, eq));
    const std::string raw_value = cleaned.substr(eq + 1);
    TomlValue value;
    std::string value_error;
    if (!parse_value(raw_value, &value, &value_error)) {
      if (error) *error = "line " + std::to_string(lineno) + ": " + value_error;
      return table;
    }
    const std::string full_key = prefix.empty() ? key : prefix + "." + key;
    table[full_key] = std::move(value);
  }
  return table;
}

std::string toml_read_file(const std::string& path, bool* ok) {
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

}  // namespace pillbar

