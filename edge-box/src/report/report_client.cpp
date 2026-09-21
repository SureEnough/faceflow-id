#include "report/report_client.h"

#include <sstream>

namespace eb {

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
// HTTP 实现：POST /records/recognition/batch
#include <httplib.h>
class HttpReportClient : public ReportClient {
 public:
  HttpReportClient(std::string endpoint, std::string psk) : endpoint_(std::move(endpoint)), psk_(std::move(psk)) {}

  bool Upload(int64_t device_id, const std::vector<Recognition>& records) override {
    if (records.empty()) return true;
    // TODO(impl): 组装 JSON、附加 Authorization: Bearer、调用 httplib POST
    // 幂等键：(device_id, track_id, camera_id, created_at)
    (void)device_id;
    LOG_WARN("HttpReportClient not fully implemented yet");
    return true;
  }

 private:
  std::string endpoint_;
  std::string psk_;
};
#endif  // HAVE_CPPHTTPLIB

ReportClient* CreateReportClient(const std::string& endpoint, const std::string& psk) {
#if defined(HAVE_CPPHTTPLIB)
  return new HttpReportClient(endpoint, psk);
#else
  (void)endpoint; (void)psk;
  return new PrintReportClient();
#endif
}

}  // namespace eb