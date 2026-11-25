#pragma once
// ---------------------------------------------------------------------------
// Tiny JSON reader/writer used for presets and settings. No dependencies.
// ---------------------------------------------------------------------------
#include <windows.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace json {

class Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

class Value {
public:
  enum Kind { Null, Bool, Number, String, Arr, Obj };

  Value() : kind_(Null) {}
  Value(bool b) : kind_(Bool), b_(b) {}
  Value(double d) : kind_(Number), num_(d) {}
  Value(int i) : kind_(Number), num_((double)i) {}
  Value(const char* s) : kind_(String), str_(s ? s : "") {}
  Value(std::string s) : kind_(String), str_(std::move(s)) {}

  static Value array() {
    Value v;
    v.kind_ = Arr;
    v.arr_ = std::make_shared<Array>();
    return v;
  }
  static Value object() {
    Value v;
    v.kind_ = Obj;
    v.obj_ = std::make_shared<Object>();
    return v;
  }
  static Value makeObject() { return object(); }

  Kind kind() const { return kind_; }
  bool isObject() const { return kind_ == Obj; }
  bool isArray() const { return kind_ == Arr; }
  bool isString() const { return kind_ == String; }
  bool isNumber() const { return kind_ == Number; }

  // Object access
  Value& operator[](const std::string& key) {
    if (kind_ != Obj) {
      kind_ = Obj;
      obj_ = std::make_shared<Object>();
    }
    return (*obj_)[key];
  }
  const Value* find(const std::string& key) const {
    if (kind_ != Obj || !obj_) return nullptr;
    auto it = obj_->find(key);
    return it == obj_->end() ? nullptr : &it->second;
  }
  const Object& asObject() const {
    static const Object empty;
    return (kind_ == Obj && obj_) ? *obj_ : empty;
  }
  const Array& asArray() const {
    static const Array empty;
    return (kind_ == Arr && arr_) ? *arr_ : empty;
  }
  Array& mutableArray() {
    if (kind_ != Arr) {
      kind_ = Arr;
      arr_ = std::make_shared<Array>();
    }
    return *arr_;
  }
  void push(const Value& v) { mutableArray().push_back(v); }

  double number(double fallback = 0.0) const { return kind_ == Number ? num_ : fallback; }
  float f(float fallback = 0.0f) const { return kind_ == Number ? (float)num_ : fallback; }
  int i(int fallback = 0) const { return kind_ == Number ? (int)std::lround(num_) : fallback; }
  bool boolean(bool fallback = false) const { return kind_ == Bool ? b_ : fallback; }
  std::string string(const std::string& fallback = "") const {
    return kind_ == String ? str_ : fallback;
  }

  // UTF-8 <-> wide helpers for Win32 paths.
  static std::string toUtf8(const std::wstring& w);
  static std::wstring toWide(const std::string& s);

  std::string dump(int indent = 2) const {
    std::ostringstream os;
    write(os, indent, 0);
    return os.str();
  }

  static Value parse(const std::string& text, std::string* error = nullptr);

private:
  void write(std::ostringstream& os, int indent, int depth) const {
    auto pad = [&](int d) {
      if (indent > 0) {
        os << '\n';
        for (int i = 0; i < d * indent; ++i) os << ' ';
      }
    };
    switch (kind_) {
    case Null: os << "null"; break;
    case Bool: os << (b_ ? "true" : "false"); break;
    case Number: {
      if (num_ == std::floor(num_) && std::fabs(num_) < 1e15)
        os << (long long)num_;
      else {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.6g", num_);
        os << buf;
      }
      break;
    }
    case String: writeString(os, str_); break;
    case Arr: {
      os << '[';
      bool first = true;
      for (const auto& v : asArray()) {
        if (!first) os << ',';
        first = false;
        pad(depth + 1);
        v.write(os, indent, depth + 1);
      }
      if (!asArray().empty()) pad(depth);
      os << ']';
      break;
    }
    case Obj: {
      os << '{';
      bool first = true;
      for (const auto& kv : asObject()) {
        if (!first) os << ',';
        first = false;
        pad(depth + 1);
        writeString(os, kv.first);
        os << (indent > 0 ? ": " : ":");
        kv.second.write(os, indent, depth + 1);
      }
      if (!asObject().empty()) pad(depth);
      os << '}';
      break;
    }
    }
  }

  static void writeString(std::ostringstream& os, const std::string& s) {
    os << '"';
    for (char c : s) {
      switch (c) {
      case '"': os << "\\\""; break;
      case '\\': os << "\\\\"; break;
      case '\n': os << "\\n"; break;
      case '\r': os << "\\r"; break;
      case '\t': os << "\\t"; break;
      default: os << c;
      }
    }
    os << '"';
  }

  Kind kind_ = Null;
  bool b_ = false;
  double num_ = 0;
  std::string str_;
  std::shared_ptr<Array> arr_;
  std::shared_ptr<Object> obj_;
};

struct Parser {
  const std::string& s;
  size_t p = 0;
  std::string err;

  explicit Parser(const std::string& in) : s(in) {}

  void ws() {
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) ++p;
  }
  bool eat(char c) {
    ws();
    if (p < s.size() && s[p] == c) {
      ++p;
      return true;
    }
    return false;
  }
  bool fail(const char* m) {
    if (err.empty()) err = std::string(m) + " at offset " + std::to_string(p);
    return false;
  }

  std::string parseString() {
    std::string out;
    if (!eat('"')) return out;
    while (p < s.size() && s[p] != '"') {
      char c = s[p++];
      if (c == '\\' && p < s.size()) {
        char e = s[p++];
        switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          if (p + 4 <= s.size()) {
            unsigned code = (unsigned)strtoul(s.substr(p, 4).c_str(), nullptr, 16);
            p += 4;
            // Minimal UTF-8 encode (BMP only).
            if (code < 0x80) out += (char)code;
            else if (code < 0x800) {
              out += (char)(0xC0 | (code >> 6));
              out += (char)(0x80 | (code & 0x3F));
            } else {
              out += (char)(0xE0 | (code >> 12));
              out += (char)(0x80 | ((code >> 6) & 0x3F));
              out += (char)(0x80 | (code & 0x3F));
            }
          }
          break;
        }
        default: out += e;
        }
      } else {
        out += c;
      }
    }
    ++p; // closing quote
    return out;
  }

  bool parseValue(Value& out, int depth) {
    if (depth > 64) return fail("nesting too deep");
    ws();
    if (p >= s.size()) return fail("unexpected end");
    const char c = s[p];
    if (c == '{') {
      ++p;
      out = Value::object();
      ws();
      if (eat('}')) return true;
      while (true) {
        ws();
        std::string key = parseString();
        if (!eat(':')) return fail("expected ':'");
        Value v;
        if (!parseValue(v, depth + 1)) return false;
        out[key] = v;
        ws();
        if (eat(',')) continue;
        if (eat('}')) return true;
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      ++p;
      out = Value::array();
      ws();
      if (eat(']')) return true;
      while (true) {
        Value v;
        if (!parseValue(v, depth + 1)) return false;
        out.push(v);
        ws();
        if (eat(',')) continue;
        if (eat(']')) return true;
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') {
      out = Value(parseString());
      return true;
    }
    if (s.compare(p, 4, "true") == 0) {
      p += 4;
      out = Value(true);
      return true;
    }
    if (s.compare(p, 5, "false") == 0) {
      p += 5;
      out = Value(false);
      return true;
    }
    if (s.compare(p, 4, "null") == 0) {
      p += 4;
      out = Value();
      return true;
    }
    char* end = nullptr;
    double d = strtod(s.c_str() + p, &end);
    if (end == s.c_str() + p) return fail("bad value");
    p = (size_t)(end - s.c_str());
    out = Value(d);
    return true;
  }

  bool run(Value& out) {
    if (!parseValue(out, 0)) return false;
    ws();
    if (p != s.size()) return fail("trailing characters");
    return true;
  }
};

inline Value Value::parse(const std::string& text, std::string* error) {
  Value v;
  Parser parser(text);
  if (!parser.run(v)) {
    if (error) *error = parser.err;
    return Value();
  }
  if (error) error->clear();
  return v;
}

// UTF-8 / UTF-16 conversion
inline std::string Value::toUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string out((size_t)len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), len, nullptr, nullptr);
  return out;
}
inline std::wstring Value::toWide(const std::string& str) {
  if (str.empty()) return {};
  int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
  std::wstring out((size_t)len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), out.data(), len);
  return out;
}

} // namespace json
