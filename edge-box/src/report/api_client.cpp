// edge-box/src/report/api_client.cpp
#include "report/api_client.h"

#include <ctime>

#include "common/common.h"

#include "config/mini_json.h"

#if defined(HAVE_CPPHTTPLIB)
#include <httplib.h>
#endif

namespace eb {

ApiClient::ApiClient(std::string endpoint, std::string psk)
    : endpoint_(std::move(endpoint)), psk_(std::move(psk)) {
#if defined(HAVE_CPPHTTPLIB)
  // 拆分 scheme://host[:port] 与路径前缀（如 /api/v1）
  std::string prefix;
  size_t scheme = endpoint_.find("://");
  size_t slash = endpoint_.find('/', scheme == std::string::npos ? 0 : scheme + 3);
  if (slash == std::string::npos) {
    base_ = endpoint_;
  } else {
    base_ = endpoint_.substr(0, slash);
    prefix = endpoint_.substr(slash);
  }
  while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
  prefix_ = prefix;
  auto* cli = new httplib::Client(base_.c_str());
  cli->set_connection_timeout(10);
  cli->set_read_timeout(15);
  cli->set_write_timeout(15);
  client_ = cli;
#else
  http_available_ = false;
#endif
}

ApiClient::~ApiClient() {
#if defined(HAVE_CPPHTTPLIB)
  delete static_cast<httplib::Client*>(client_);
#endif
}

bool ApiClient::Login(int64_t device_id) {
#if !defined(HAVE_CPPHTTPLIB)
  (void)device_id;
  return false;
#else
  if (!client_) return false;
  std::map<std::string, Json> obj;
  obj["device_id"] = Json::Number(static_cast<double>(device_id));
  obj["psk"] = Json::String(psk_);
  std::string body = Json::Object(std::move(obj)).Dump();

  httplib::Result res = static_cast<httplib::Client*>(client_)
                            ->Post((prefix_ + "/auth/device/login").c_str(), body, "application/json");
  if (res && res->status == 200) {
    Json j;
    if (Json::Parse(res->body, j)) {
      const Json* data = j.Get("data");
      const Json* tok = data ? data->Get("token") : nullptr;
      const Json* exp = data ? data->Get("expires_in") : nullptr;
      if (tok) {
        token_ = tok->AsString();
        int64_t ttl = exp ? exp->AsInt(86400) : 86400;
        token_exp_ = std::time(nullptr) + ttl;
        return true;
      }
    }
    LOG_WARN("device login parse failed: %s", res->body.c_str());
  } else {
    int status = res ? res->status : -1;
    LOG_WARN("device login http failed: status=%d", status);
  }
  return false;
#endif
}

bool ApiClient::EnsureToken(int64_t device_id) {
#if defined(HAVE_CPPHTTPLIB)
  if (!token_.empty() && token_exp_ > std::time(nullptr) + 30) return true;
  return Login(device_id);
#else
  (void)device_id;
  return false;
#endif
}

bool ApiClient::Post(const std::string& path, const std::string& body, std::string& out_body,
                     int& out_status) {
  return Request("POST", path, body, out_body, out_status);
}

bool ApiClient::Get(const std::string& path, std::string& out_body, int& out_status) {
  return Request("GET", path, "", out_body, out_status);
}

bool ApiClient::Request(const char* method, const std::string& path, const std::string& body,
                        std::string& out_body, int& out_status) {
#if defined(HAVE_CPPHTTPLIB)
  if (!client_) return false;
  auto* cli = static_cast<httplib::Client*>(client_);
  httplib::Headers headers;
  if (!token_.empty()) headers.emplace("Authorization", "Bearer " + token_);

  auto doReq = [&]() -> httplib::Result {
    std::string full = prefix_ + path;
    if (std::string(method) == "GET") return cli->Get(full.c_str(), headers);
    return cli->Post(full.c_str(), headers, body, "application/json");
  };

  httplib::Result res = doReq();
  if (!res) {
    out_status = -1;
    out_body.clear();
    return false;
  }
  out_status = res->status;
  out_body = res->body;

  // 401：token 失效 → 重登一次重试（需外部传入 device_id，因此这里仅提示；
  // 上层可调用 EnsureToken 后再次请求）
  if (res->status == 401) {
    token_.clear();
    token_exp_ = 0;
  }
  return true;
#else
  (void)path; (void)body; (void)out_body; (void)out_status;
  return false;
#endif
}

}  // namespace eb