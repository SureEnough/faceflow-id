// edge-box/src/common/base64.h
// Base64 编解码（标准 RFC 4648，带 padding）。仅标准库。
#pragma once

#include <string>

namespace eb {

// 编码二进制 → base64 字符串
std::string Base64Encode(const unsigned char* data, size_t len);
inline std::string Base64Encode(const std::string& s) {
  return Base64Encode(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

// 解码 base64 → 二进制；失败返回 false
bool Base64Decode(const std::string& in, std::string& out);

}  // namespace eb