// edge-box/src/common/base64.cpp
#include "common/base64.h"

#include <cstdint>

namespace eb {

namespace {
const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int DecodeChar(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
}  // namespace

std::string Base64Encode(const unsigned char* data, size_t len) {
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 3 <= len; i += 3) {
    uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                 (static_cast<uint32_t>(data[i + 1]) << 8) |
                 static_cast<uint32_t>(data[i + 2]);
    out.push_back(kTable[(v >> 18) & 63]);
    out.push_back(kTable[(v >> 12) & 63]);
    out.push_back(kTable[(v >> 6) & 63]);
    out.push_back(kTable[v & 63]);
  }
  size_t rem = len - i;
  if (rem == 1) {
    uint32_t v = static_cast<uint32_t>(data[i]) << 16;
    out.push_back(kTable[(v >> 18) & 63]);
    out.push_back(kTable[(v >> 12) & 63]);
    out.push_back('=');
    out.push_back('=');
  } else if (rem == 2) {
    uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                 (static_cast<uint32_t>(data[i + 1]) << 8);
    out.push_back(kTable[(v >> 18) & 63]);
    out.push_back(kTable[(v >> 12) & 63]);
    out.push_back(kTable[(v >> 6) & 63]);
    out.push_back('=');
  }
  return out;
}

bool Base64Decode(const std::string& in, std::string& out) {
  out.clear();
  std::string clean;
  clean.reserve(in.size());
  for (char c : in) {
    if (c == '\r' || c == '\n' || c == ' ') continue;
    if (c == '=') break;  // padding 之后忽略
    clean.push_back(c);
  }
  if (clean.size() % 4 == 1) return false;
  int buf = 0, bits = 0;
  for (char c : clean) {
    int v = DecodeChar(c);
    if (v < 0) return false;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buf >> bits) & 0xFF));
    }
  }
  return true;
}

}  // namespace eb