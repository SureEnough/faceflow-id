// edge-box/src/report/device_ops.cpp
#include "report/device_ops.h"

#include <map>

#include "config/mini_json.h"

namespace eb {

namespace {

// 简化 JSON 组装（MiniJson Dump；远程注册只需少量字段）
std::string RegisterBody(int device_type, int64_t parent_id, const std::string& device_key,
                         const std::string& name, int64_t store_id, const std::string& psk,
                         bool has_parent) {
  std::map<std::string, Json> o;
  o["device_type"] = Json::Number(static_cast<double>(device_type));
  if (has_parent) o["parent_id"] = Json::Number(static_cast<double>(parent_id));
  if (!device_key.empty()) o["device_key"] = Json::String(device_key);
  o["name"] = Json::String(name);
  o["store_id"] = Json::Number(static_cast<double>(store_id));
  o["psk"] = Json::String(psk);
  return Json::Object(std::move(o)).Dump();
}

bool PostOk(ApiClient* api, const std::string& path, const std::string& body) {
  std::string resp;
  int status = 0;
  if (!api->Post(path, body, resp, status)) return false;
  if (status == 401 && api->httpAvailable()) return false;  // token 场景由上层重登
  return status == 200 || status == 201;
}

}  // namespace

int64_t EnsureDeviceRegistered(const Config& cfg, ApiClient* api, int64_t device_id,
                               std::string* err) {
  if (!api || !api->httpAvailable()) return device_id;

  // 1. 主设备注册（配置里 device_id<=0 且给出 store_id 时才自注册）
  if (device_id <= 0) {
    if (cfg.store_id <= 0) {
      if (err) *err = "device_id<=0 but store_id not set; skip auto register";
      return device_id;
    }
    const std::string name =
        cfg.device_name.empty() ? "edge-box-" + std::to_string(cfg.store_id) : cfg.device_name;
    std::string body = RegisterBody(1, 0, "", name, cfg.store_id, cfg.device_psk, false);
    std::string resp;
    int status = 0;
    if (!api->Post("/devices/register", body, resp, status) || status != 200) {
      if (err) *err = "register main device failed status=" + std::to_string(status);
      return device_id;
    }
    Json j;
    if (Json::Parse(resp, j)) {
      const Json* d = j.Get("data");
      const Json* id = d ? d->Get("device_id") : nullptr;
      if (id) device_id = id->AsInt(device_id);
    }
    LOG_INFO("device auto-registered: id=%lld", static_cast<long long>(device_id));
  }

  // 2. 摄像头代注册为子设备（幂等 upsert by parent+type+key）
  {
    std::string resp;
    int status = 0;
    for (const auto& cam : cfg.cameras) {
      if (cam.camera_id.empty()) continue;
      std::string body =
          RegisterBody(3, device_id, cam.camera_id, cam.camera_id, cfg.store_id, cfg.device_psk, true);
      if (api->Post("/devices/register", body, resp, status)) {
        if (status == 200) LOG_INFO("camera registered: %s", cam.camera_id.c_str());
      } else {
        LOG_WARN("camera register failed: %s", cam.camera_id.c_str());
      }
    }
  }
  return device_id;
}

bool SendHeartbeat(int64_t device_id, ApiClient* api, const std::vector<web::CameraStatus>& cams) {
  if (!api || !api->httpAvailable() || device_id <= 0) return false;
  std::map<std::string, Json> o;
  o["status"] = Json::Number(1);
  std::vector<Json> subs;
  for (const auto& c : cams) {
    std::map<std::string, Json> sd;
    sd["device_key"] = Json::String(c.camera_id);
    sd["type"] = Json::Number(3);
    sd["online"] = Json::Bool(c.opened);
    subs.push_back(Json::Object(std::move(sd)));
  }
  o["sub_devices"] = Json::Array(std::move(subs));

  std::string resp;
  int status = 0;
  const std::string path = "/devices/" + std::to_string(device_id) + "/heartbeat";
  if (!api->Post(path, Json::Object(std::move(o)).Dump(), resp, status)) return false;
  return status == 200;
}

bool PullRemoteConfig(int64_t device_id, ApiClient* api, std::string& out_json) {
  if (!api || !api->httpAvailable() || device_id <= 0) return false;
  std::string resp;
  int status = 0;
  const std::string path = "/devices/" + std::to_string(device_id) + "/config";
  if (!api->EnsureToken(device_id)) return false;
  if (!api->Get(path, resp, status)) return false;
  if (status == 401) {
    if (!api->EnsureToken(device_id)) return false;
    if (!api->Get(path, resp, status)) return false;
  }
  if (status != 200) return false;
  Json root;
  if (!Json::Parse(resp, root)) return false;
  const Json* data = root.Get("data");
  const Json* cfg_json = data ? data->Get("config") : nullptr;
  if (!cfg_json || cfg_json->IsNull()) return false;
  if (!cfg_json->IsObject()) return false;
  out_json = cfg_json->Dump();
  return true;
}

}  // namespace eb