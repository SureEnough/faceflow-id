// edge-box/src/web/config_manager.cpp
#include "web/config_manager.h"

#include <ctime>
#include <map>

#include "config/mini_json.h"

namespace eb {
namespace web {

void StatusBoard::Update(int64_t deviceId, std::string backend, std::string version,
                         int64_t startedAt, std::vector<CameraStatus> cams, int64_t syncVersion) {
  std::lock_guard<std::mutex> lk(mu_);
  device_id = deviceId;
  this->backend = std::move(backend);
  this->version = std::move(version);
  started_at = startedAt;
  cameras = std::move(cams);
  sync_version = syncVersion;
}

std::string StatusBoard::Version() {
  return "edge_box " EDGE_BOX_VERSION;
}

std::string StatusBoard::ToJson() const {
  std::lock_guard<std::mutex> lk(mu_);
  std::map<std::string, Json> o;
  o["device_id"] = Json::Number(static_cast<double>(device_id));
  o["backend"] = Json::String(backend);
  o["version"] = Json::String(version);
  o["started_at"] = Json::Number(static_cast<double>(started_at));
  o["uptime_s"] = Json::Number(static_cast<double>(std::time(nullptr) - started_at));
  o["sync_version"] = Json::Number(static_cast<double>(sync_version));

  std::vector<Json> cams;
  for (const auto& c : cameras) {
    std::map<std::string, Json> cj;
    cj["camera_id"] = Json::String(c.camera_id);
    cj["opened"] = Json::Bool(c.opened);
    cj["last_frame_ok"] = Json::Bool(c.last_frame_ok);
    cj["frames"] = Json::Number(static_cast<double>(c.frames));
    cj["flow_in"] = Json::Number(static_cast<double>(c.flow_in));
    cj["flow_out"] = Json::Number(static_cast<double>(c.flow_out));
    cams.push_back(Json::Object(std::move(cj)));
  }
  o["cameras"] = Json::Array(std::move(cams));
  return Json::Object(std::move(o)).Dump();
}

Config ConfigManager::Snapshot() const {
  std::lock_guard<std::mutex> lk(mu_);
  return cfg_;
}

bool ConfigManager::UpdateFromJson(const std::string& json, std::string* err) {
  Config next;
  {
    std::lock_guard<std::mutex> lk(mu_);
    next = cfg_;  // 基于当前值：未传字段保持不变（device_psk 掩码不覆盖）
    if (!Config::FromJsonStr(json, next, err)) return false;
  }
  // 原子写盘
  if (!next.SaveFile(path_)) {
    if (err) *err = "failed to write config file: " + path_;
    return false;
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    cfg_ = std::move(next);
  }
  reload_pending_.store(true);
  return true;
}

void ConfigManager::SetConfig(Config cfg) {
  std::lock_guard<std::mutex> lk(mu_);
  cfg_ = std::move(cfg);
}

void ConfigManager::RequestReload() {
  Config fresh;
  if (Config::LoadFile(path_, fresh)) {
    std::lock_guard<std::mutex> lk(mu_);
    cfg_ = std::move(fresh);
  }
  reload_pending_.store(true);
}

bool ConfigManager::ConsumeReload() {
  return reload_pending_.exchange(false);
}

}  // namespace web
}  // namespace eb