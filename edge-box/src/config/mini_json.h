// edge-box/src/config/mini_json.h
// 极简 JSON 解析器（仅支持配置所需子集），无第三方依赖。
// 类型层次：Value = 基础类型之一；支持 object/array/string/number/bool。
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eb {

class Json {
 public:
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  Json() : type_(Type::kNull) {}
  static Json Null() { return Json(); }
  static Json Bool(bool b) { Json j; j.type_ = Type::kBool; j.b_ = b; return j; }
  static Json Number(double n) { Json j; j.type_ = Type::kNumber; j.n_ = n; return j; }
  static Json String(std::string s) { Json j; j.type_ = Type::kString; j.s_ = std::move(s); return j; }
  static Json Array(std::vector<Json> a) { Json j; j.type_ = Type::kArray; j.a_ = std::move(a); return j; }
  static Json Object(std::map<std::string, Json> o) { Json j; j.type_ = Type::kObject; j.o_ = std::move(o); return j; }

  Type type() const { return type_; }
  bool IsNull() const { return type_ == Type::kNull; }
  bool IsBool() const { return type_ == Type::kBool; }
  bool IsNumber() const { return type_ == Type::kNumber; }
  bool IsString() const { return type_ == Type::kString; }
  bool IsArray() const { return type_ == Type::kArray; }
  bool IsObject() const { return type_ == Type::kObject; }

  bool AsBool(bool def = false) const { return type_ == Type::kBool ? b_ : def; }
  double AsNumber(double def = 0) const { return type_ == Type::kNumber ? n_ : def; }
  int64_t AsInt(int64_t def = 0) const { return type_ == Type::kNumber ? static_cast<int64_t>(n_) : def; }
  const std::string& AsString() const { return s_; }
  const std::vector<Json>& AsArray() const { return a_; }
  const std::map<std::string, Json>& AsObject() const { return o_; }

  // object 访问
  bool Has(const std::string& k) const;
  const Json* Get(const std::string& k) const;

  // 解析（成功返回 true，pos 推进到解析结束位置）
  static bool Parse(const std::string& text, Json& out, size_t& pos);
  static bool Parse(const std::string& text, Json& out) { size_t p = 0; return Parse(text, out, p); }
  std::string Dump() const;

 private:
  Type type_ = Type::kNull;
  bool b_ = false;
  double n_ = 0;
  std::string s_;
  std::vector<Json> a_;
  std::map<std::string, Json> o_;
};

}  // namespace eb