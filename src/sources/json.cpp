#include "sources/json.hpp"

#include <cctype>
#include <cstdlib>

namespace pillbar {

const JsonValue* JsonValue::find(const std::string& key) const {
  for (std::size_t i = 0; i < object_keys.size(); ++i) {
    if (object_keys[i] == key) return &object_values[i];
  }
  return nullptr;
}

std::string JsonValue::as_string(const std::string& fallback) const {
  return type == Type::String ? string_value : fallback;
}

double JsonValue::as_number(double fallback) const {
  return type == Type::Number ? number_value : fallback;
}

int JsonValue::as_int(int fallback) const {
  return type == Type::Number ? static_cast<int>(number_value) : fallback;
}

bool JsonValue::as_bool(bool fallback) const {
  return type == Type::Bool ? bool_value : fallback;
}

namespace {

class Parser {
 public:
  Parser(const std::string& text, std::string* error) : text_(text), error_(error) {}

  JsonValue parse() {
    skip_ws();
    JsonValue value = parse_value();
    if (!ok_) return value;
    skip_ws();
    if (pos_ != text_.size()) fail("trailing characters");
    return value;
  }

 private:
  void fail(const std::string& message) {
    if (ok_) {
      ok_ = false;
      if (error_ != nullptr) *error_ = message + " at offset " + std::to_string(pos_);
    }
  }

  void skip_ws() {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) {
      ++pos_;
    }
  }

  bool consume(char c) {
    if (pos_ < text_.size() && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  JsonValue parse_value() {
    skip_ws();
    if (pos_ >= text_.size()) {
      fail("unexpected end");
      return {};
    }
    const char c = text_[pos_];
    switch (c) {
      case '{':
        return parse_object();
      case '[':
        return parse_array();
      case '"':
        return parse_string();
      case 't':
      case 'f':
        return parse_bool();
      case 'n':
        return parse_null();
      default:
        return parse_number();
    }
  }

  JsonValue parse_object() {
    JsonValue value;
    value.type = JsonValue::Type::Object;
    consume('{');
    skip_ws();
    if (consume('}')) return value;
    for (;;) {
      skip_ws();
      if (pos_ >= text_.size() || text_[pos_] != '"') {
        fail("expected key string");
        return value;
      }
      JsonValue key = parse_string();
      skip_ws();
      if (!consume(':')) {
        fail("expected ':'");
        return value;
      }
      JsonValue element = parse_value();
      value.object_keys.push_back(std::move(key.string_value));
      value.object_values.push_back(std::move(element));
      skip_ws();
      if (consume(',')) continue;
      if (consume('}')) break;
      fail("expected ',' or '}'");
      break;
    }
    return value;
  }

  JsonValue parse_array() {
    JsonValue value;
    value.type = JsonValue::Type::Array;
    consume('[');
    skip_ws();
    if (consume(']')) return value;
    for (;;) {
      value.array_value.push_back(parse_value());
      skip_ws();
      if (consume(',')) continue;
      if (consume(']')) break;
      fail("expected ',' or ']'");
      break;
    }
    return value;
  }

  JsonValue parse_string() {
    JsonValue value;
    value.type = JsonValue::Type::String;
    consume('"');
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') return value;
      if (c == '\\') {
        if (pos_ >= text_.size()) break;
        const char esc = text_[pos_++];
        switch (esc) {
          case 'n':
            value.string_value.push_back('\n');
            break;
          case 't':
            value.string_value.push_back('\t');
            break;
          case 'r':
            value.string_value.push_back('\r');
            break;
          case 'b':
            value.string_value.push_back('\b');
            break;
          case 'f':
            value.string_value.push_back('\f');
            break;
          case 'u': {
            if (pos_ + 4 > text_.size()) {
              fail("bad \\u escape");
              return value;
            }
            unsigned code = 0;
            for (int i = 0; i < 4; ++i) {
              const char h = text_[pos_++];
              code <<= 4;
              if (h >= '0' && h <= '9') {
                code |= static_cast<unsigned>(h - '0');
              } else if (h >= 'a' && h <= 'f') {
                code |= static_cast<unsigned>(h - 'a' + 10);
              } else if (h >= 'A' && h <= 'F') {
                code |= static_cast<unsigned>(h - 'A' + 10);
              } else {
                fail("bad hex");
                return value;
              }
            }
            append_utf8(&value.string_value, code);
            break;
          }
          default:
            value.string_value.push_back(esc);
            break;
        }
      } else {
        value.string_value.push_back(c);
      }
    }
    fail("unterminated string");
    return value;
  }

  static void append_utf8(std::string* out, unsigned code) {
    if (code < 0x80) {
      out->push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      out->push_back(static_cast<char>(0xC0 | (code >> 6)));
      out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      out->push_back(static_cast<char>(0xE0 | (code >> 12)));
      out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }

  JsonValue parse_bool() {
    JsonValue value;
    if (text_.compare(pos_, 4, "true") == 0) {
      pos_ += 4;
      value.type = JsonValue::Type::Bool;
      value.bool_value = true;
    } else if (text_.compare(pos_, 5, "false") == 0) {
      pos_ += 5;
      value.type = JsonValue::Type::Bool;
      value.bool_value = false;
    } else {
      fail("bad literal");
    }
    return value;
  }

  JsonValue parse_null() {
    JsonValue value;
    if (text_.compare(pos_, 4, "null") == 0) {
      pos_ += 4;
    } else {
      fail("bad literal");
    }
    return value;
  }

  JsonValue parse_number() {
    const std::size_t start = pos_;
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
        ++pos_;
      } else {
        break;
      }
    }
    JsonValue value;
    value.type = JsonValue::Type::Number;
    value.number_value = std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr);
    if (start == pos_) fail("expected value");
    return value;
  }

  const std::string& text_;
  std::string* error_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

}  // namespace

JsonValue json_parse(const std::string& text, std::string* error) {
  Parser parser(text, error);
  return parser.parse();
}

}  // namespace pillbar
