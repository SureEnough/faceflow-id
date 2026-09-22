// edge-box/src/report/api_client.h
// HTTP API 客户端（cpp-httplib）：统一管理设备 token 与 GET/POST（401 自动清 token）。
// 无 cpp-httplib 时提供不可用实现（逻辑回退打印模式）。
#pragma once

#include <cstdint>
#include <string>

namespace eb {

class ApiClient {
 public:
  ApiClient(std::string endpoint, std::string psk);
  ~ApiClient();

  // 确保有可用 token（无/过期则 POST /auth/device/login）
  bool EnsureToken(int64_t device_id);

  // 带鉴权 POST/GET；成功返回 true（out_status 为状态码）。
  // 收到 401 会自动清除 token；上层可 EnsureToken 后重试一次。
  bool Post(const std::string& path, const std::string& body, std::string& out_body, int& out_status);
  bool Get(const std::string& path, std::string& out_body, int& out_status);

  bool httpAvailable() const { return http_available_; }

 private:
  bool Login(int64_t device_id);
  bool Request(const char* method, const std::string& path, const std::string& body,
               std::string& out_body, int& out_status);

  std::string base_;
  std::string prefix_;
  std::string endpoint_;
  std::string psk_;
  std::string token_;
  int64_t token_exp_ = 0;  // Unix 秒
  bool http_available_ = true;
  void* client_ = nullptr;  // HAVE_CPPHTTPLIB 时持有 httplib::Client*
};

}  // namespace eb