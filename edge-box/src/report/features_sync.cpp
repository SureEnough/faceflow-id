// edge-box/src/report/features_sync.cpp
#include "report/features_sync.h"

#include <cmath>
#include <cstring>

#include "common/base64.h"
#include "config/mini_json.h"

namespace eb {

namespace {

// 特征 base64 → Feature（512×float32）；失败返回 false
bool DecodeFeature(const std::string& b64, Feature& out) {
  std::string raw;
  if (!Base64Decode(b64, raw) || raw.size() != kFeatureDim * sizeof(float)) return false;
  std::memcpy(out.data(), raw.data(), kFeatureDim * sizeof(float));
  // 防御性 L2 归一化（后台已归一化，双保险）
  double norm = 0;
  for (float v : out) norm += static_cast<double>(v) * v;
  norm = std::sqrt(norm);
  if (norm < 1e-9) return false;
  for (float& v : out) v = static_cast<float>(v / norm);
  return true;
}

}  // namespace

bool ParseSyncResponse(const std::string& body, int64_t* new_version, std::vector<SyncItem>& items) {
  Json root;
  if (!Json::Parse(body, root)) return false;
  const Json* data = root.Get("data");
  if (!data) return false;
  const Json* nv = data->Get("new_version");
  if (!nv) return false;
  if (new_version) *new_version = nv->AsInt(0);
  const Json* arr = data->Get("items");
  if (!arr || !arr->IsArray()) return true;  // 无增量也视为成功

  items.clear();
  for (const Json& it : arr->AsArray()) {
    SyncItem si;
    si.customer_id = it.Get("customer_id")->AsInt(-1);
    int64_t pt = it.Get("person_type") ? it.Get("person_type")->AsInt(0) : 0;
    si.person_type = static_cast<PersonType>(pt == 1 ? kStaff : kCustomer);
    si.version = it.Get("version") ? it.Get("version")->AsInt(0) : 0;
    si.status = static_cast<int8_t>(it.Get("status") ? it.Get("status")->AsInt(0) : 0);
    const Json* staff = it.Get("staff_no_hash");
    if (staff) si.staff_no_hash = staff->AsString();
    const Json* feat = it.Get("face_feature");
    if (feat && !feat->AsString().empty()) {
      si.has_feature = DecodeFeature(feat->AsString(), si.feature);
    }
    items.push_back(std::move(si));
  }
  return true;
}

int ApplySyncItems(const std::vector<SyncItem>& items, Recognizer* recognizer) {
  if (!recognizer) return 0;
  int applied = 0;
  for (const SyncItem& it : items) {
    if (it.status != 0) {
      // 失效（黑名单/注销/离职）→ 移出本地库；期间不再匹配
      recognizer->Remove(it.customer_id);
      ++applied;
      continue;
    }
    if (!it.has_feature) continue;
    Identity id;
    id.customer_id = it.customer_id;
    id.person_type = it.person_type;
    id.feature = it.feature;
    id.version = it.version;
    id.status = 0;
    id.staff_no_hash = it.staff_no_hash;
    recognizer->Upsert(id);
    ++applied;
  }
  return applied;
}

int64_t FeaturesSyncClient::SyncOnce(int64_t device_id, int64_t since_version) {
  if (!api_ || !api_->httpAvailable() || !recognizer_) return since_version;

  const std::string path =
      "/customers/features/sync?since_version=" + std::to_string(since_version);
  std::string body;
  int status = 0;

  if (!api_->EnsureToken(device_id)) return since_version;
  if (!api_->Get(path, body, status)) return since_version;
  if (status == 401) {
    if (!api_->EnsureToken(device_id)) return since_version;
    if (!api_->Get(path, body, status)) return since_version;
  }
  if (status != 200) {
    LOG_WARN("features sync http status=%d", status);
    return since_version;
  }

  int64_t new_version = since_version;
  std::vector<SyncItem> items;
  if (!ParseSyncResponse(body, &new_version, items)) {
    LOG_WARN("features sync parse failed");
    return since_version;
  }

  int applied = ApplySyncItems(items, recognizer_);
  LOG_INFO("features sync: since=%lld -> new=%lld items=%zu applied=%d", 
           static_cast<long long>(since_version), static_cast<long long>(new_version),
           items.size(), applied);
  return std::max(new_version, since_version);
}

}  // namespace eb