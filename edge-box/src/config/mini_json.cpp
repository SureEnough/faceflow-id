#include "config/mini_json.h"

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <utility>

namespace eb {

namespace {
bool SkipWs(const std::string& s, size_t& p) {
  while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
  return p < s.size();
}
}  // namespace

bool Json::Has(const std::string& k) const { return o_.count(k) > 0; }

const Json* Json::Get(const std::string& k) const {
  auto it = o_.find(k);
  return it == o_.end() ? nullptr : &it->second;
}

bool Json::Parse(const std::string& text, Json& out, size_t& pos) {
  if (!SkipWs(text, pos)) return false;
  char c = text[pos];
  switch (c) {
    case '{': {
      ++pos;
      std::map<std::string, Json> obj;
      if (!SkipWs(text, pos)) return false;
      if (text[pos] == '}') { ++pos; out = Json::Object(std::move(obj)); return true; }
      while (true) {
        if (!SkipWs(text, pos) || text[pos] != '"') return false;
        ++pos;
        std::string key;
        while (pos < text.size() && text[pos] != '"') {
          if (text[pos] == '\\' && pos + 1 < text.size()) { key += text[pos + 1]; pos += 2; }
          else key += text[pos++];
        }
        if (pos >= text.size()) return false;
        ++pos;  // 跳过结束引号
        if (!SkipWs(text, pos) || text[pos] != ':') return false;
        ++pos;
        Json val;
        if (!Parse(text, val, pos)) return false;
        obj.emplace(std::move(key), std::move(val));
        if (!SkipWs(text, pos)) return false;
        if (text[pos] == ',') { ++pos; continue; }
        if (text[pos] == '}') { ++pos; break; }
        return false;
      }
      out = Json::Object(std::move(obj));
      return true;
    }
    case '[': {
      ++pos;
      std::vector<Json> arr;
      if (!SkipWs(text, pos)) return false;
      if (text[pos] == ']') { ++pos; out = Json::Array(std::move(arr)); return true; }
      while (true) {
        Json val;
        if (!Parse(text, val, pos)) return false;
        arr.push_back(std::move(val));
        if (!SkipWs(text, pos)) return false;
        if (text[pos] == ',') { ++pos; continue; }
        if (text[pos] == ']') { ++pos; break; }
        return false;
      }
      out = Json::Array(std::move(arr));
      return true;
    }
    case '"': {
      ++pos;
      std::string s;
      while (pos < text.size() && text[pos] != '"') {
        if (text[pos] == '\\' && pos + 1 < text.size()) { s += text[pos + 1]; pos += 2; }
        else s += text[pos++];
      }
      if (pos >= text.size()) return false;
      ++pos;
      out = Json::String(std::move(s));
      return true;
    }
    case 't': {
      if (text.compare(pos, 4, "true") == 0) { pos += 4; out = Json::Bool(true); return true; }
      return false;
    }
    case 'f': {
      if (text.compare(pos, 5, "false") == 0) { pos += 5; out = Json::Bool(false); return true; }
      return false;
    }
    case 'n': {
      if (text.compare(pos, 4, "null") == 0) { pos += 4; out = Json::Null(); return true; }
      return false;
    }
    default: {
      // number
      size_t start = pos;
      if (text[pos] == '-') ++pos;
      while (pos < text.size() && (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.' || text[pos] == 'e' || text[pos] == 'E' || text[pos] == '+' || text[pos] == '-')) ++pos;
      if (pos == start) return false;
      out = Json::Number(std::atof(text.substr(start, pos - start).c_str()));
      return true;
    }
  }
}

std::string Json::Dump() const {
  std::ostringstream ss;
  switch (type_) {
    case Type::kNull: ss << "null"; break;
    case Type::kBool: ss << (b_ ? "true" : "false"); break;
    case Type::kNumber: {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.6g", n_);
      ss << buf;
      break;
    }
    case Type::kString: ss << '"' << s_ << '"'; break;
    case Type::kArray: {
      ss << '[';
      for (size_t i = 0; i < a_.size(); ++i) { if (i) ss << ','; ss << a_[i].Dump(); }
      ss << ']';
      break;
    }
    case Type::kObject: {
      ss << '{';
      bool first = true;
      for (const auto& kv : o_) { if (!first) ss << ','; first = false; ss << '"' << kv.first << "\":" << kv.second.Dump(); }
      ss << '}';
      break;
    }
  }
  return ss.str();
}

}  // namespace eb