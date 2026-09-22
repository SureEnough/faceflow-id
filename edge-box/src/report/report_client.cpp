#include "report/report_client.h"

#include <ctime>
#include <map>
#include <sstream>

#include "config/mini_json.h"
#include "report/api_client.h"

namespace eb {

namespace {

// Unix 秒 → RFC3339（UTC，Z 后缀；后台用 time.RFC3339 解析）
std::string FormatRfc3339(int64_t unix_sec) {
  std::time_t t = static_cast<std::time_t>(unix_sec);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

// 组装批量上报 JSON（与后台 recogRecord/batchRecognitionReq 契约一致）
std::string BuildBatchJson(int64_t device_id, const std::vector<Recognition>& records) {
  std::vector<Json> arr;
  arr.reserve(records.size());
  for (const auto& rec : records) {
    std::map<std::string, Json> r;
    r["track_id"] = Json::String(rec.track_id);
    // 后台 customer_id 为 *uint64：匿名（-1）必须发 null
    if (rec.customer_id >= 0) {
      r["customer_id"] = Json::Number(static_cast<double>(rec.customer_id));
    } else {
      r["customer_id"] = Json::Null();
    }
    r["person_type"] = Json::Number(static_cast<double>(rec.person_type));
    r["face_feature"] = Json::String("");
    r["snapshot"] = Json::String(rec.snapshot_b64);
    r["snapshot_mime"] = Json::String(rec.snapshot_mime);
    r["similarity"] = Json::Number(static_cast<double>(rec.similarity));
    r["direction"] = Json::Number(static_cast<double>(rec.direction));
    r["camera_id"] = Json::String(rec.camera_id);
    r["created_at"] = Json::String(FormatRfc3339(rec.created_at));
    arr.push_back(Json::Object(std::move(r)));
  }
  std::map<std::string, Json> root;
  root["device_id"] = Json::Number(static_cast<double>(device_id));
  root["records"] = Json::Array(std::move(arr));
  return Json::Object(std::move(root)).Dump();
}

}  // namespace

// 打印实现：无 HTTP 依赖时的自测/CI 模式
class PrintReportClient : public ReportClient {
 public:
  bool Upload(int64_t device_id, const std::vector<Recognition>& records) override {
    std::ostringstream ss;
    ss << "report device=" << device_id << " records=" << records.size();
    if (!records.empty()) {
      ss << " first_track=" << records.front().track_id;
    }
    LOG_INFO("%s", ss.str().c_str());
    return true;  // 打印模式视为成功
  }
};

#if defined(HAVE_CPPHTTPLIB)
// HTTP 实现：设备登录 → POST /records/recognition/batch
class HttpReportClient : public ReportClient {
 public:
  HttpReportClient(std::string endpoint, std::string psk)
      : api_(std::move(endpoint), std::move(psk)) {}

  bool Upload(int64_t device_id, const std::vector<Recognition>& records) override {
    if (records.empty()) return true;
    if (!api_.httpAvailable()) {
      LOG_WARN("HttpReportClient: http unavailable, skip upload");
      return false;
    }
    const std::string payload = BuildBatchJson(device_id, records);
    std::string body;
    int status = 0;

    // 幂等键：(device_id, track_id, camera_id, created_at)；后端唯一索引兜底
    if (!api_.EnsureToken(device_id)) return false;
    if (!api_.Post("/records/recognition/batch", payload, body, status)) return false;
    if (status == 401) {
      // token 失效：重登后重试一次
      if (!api_.EnsureToken(device_id)) return false;
      if (!api_.Post("/records/recognition/batch", payload, body, status)) return false;
    }
    if (status != 200) {
      LOG_WARN("report batch http status=%d body=%.200s", status, body.c_str());
      return false;
    }
    return true;
  }

 private:
  ApiClient api_;
};
#endif  // HAVE_CPPHTTPLIB

ReportClient* CreateReportClient(const std::string& endpoint, const std::string& psk) {
#if defined(HAVE_CPPHTTPLIB)
  if (endpoint.empty()) return new PrintReportClient();  // 自测/CI 模式
  return new HttpReportClient(endpoint, psk);
#else
  (void)endpoint; (void)psk;
  return new PrintReportClient();
#endif
}

}  // namespace eb