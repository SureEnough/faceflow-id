// edge-box/src/report/features_sync.h
// 人员库增量同步（roadmap #2c）：轮询 GET /customers/features/sync?since_version=N，
// 解析后台返回的特征并增量更新本地 1:N 识别库；内部人员仅下发工号哈希。
#pragma once

#include <cstdint>
#include <vector>

#include "common/common.h"
#include "recognizer/recognizer.h"
#include "report/api_client.h"
#include "storage/identity.h"

namespace eb {

// 同步响应中的单条人员
struct SyncItem {
  int64_t customer_id = -1;
  PersonType person_type = kCustomer;
  int64_t version = 0;
  int8_t status = 0;
  std::string staff_no_hash;
  Feature feature{};       // 已解码（base64 → 512×float32）
  bool has_feature = false;
};

// 解析后台同步响应体（纯函数，可单测）；成功返回 true，new_version 为最新版本号
bool ParseSyncResponse(const std::string& body, int64_t* new_version, std::vector<SyncItem>& items);

// 将同步项应用到识别库（纯逻辑：status!=0 → 移除；有效 → Upsert）。可单测。
int ApplySyncItems(const std::vector<SyncItem>& items, Recognizer* recognizer);

// 增量同步客户端：SyncOnce 拉取 since_version 之后的变化并应用到 Recognizer
class FeaturesSyncClient {
 public:
  FeaturesSyncClient(ApiClient* api, Recognizer* recognizer)
      : api_(api), recognizer_(recognizer) {}

  // 返回同步后的新版本号；失败返回 since_version（不变）
  int64_t SyncOnce(int64_t device_id, int64_t since_version);

 private:
  ApiClient* api_;
  Recognizer* recognizer_;
};

}  // namespace eb