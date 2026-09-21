// edge-box/src/report/report_client.h
// 上报客户端：识别记录批量上报到管理后台（断网重试由上层调度）。
#pragma once

#include <string>
#include <vector>

#include "common/common.h"

namespace eb {

class ReportClient {
 public:
  virtual ~ReportClient() = default;

  // 批量上报识别记录；返回 true 表示成功（调用方置 sync_status=1）
  virtual bool Upload(int64_t device_id, const std::vector<Recognition>& records) = 0;
};

// 创建上报客户端：有 cpp-httplib 时走 HTTP，否则打印（自测模式）
ReportClient* CreateReportClient(const std::string& endpoint, const std::string& psk);

}  // namespace eb