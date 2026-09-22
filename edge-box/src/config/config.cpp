#include "config/config.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include "config/mini_json.h"

namespace eb {

namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

double ClampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 摄像头对象 → Json
Json CameraToJson(const CameraConfig& cam) {
  std::map<std::string, Json> o;
  o["camera_id"] = Json::String(cam.camera_id);
  o["url"] = Json::String(cam.url);
  o["role"] = Json::String(cam.role);
  o["count_flow"] = Json::Bool(cam.count_flow);
  o["direction"] = Json::String(cam.direction);
  std::map<std::string, Json> line;
  line["x1"] = Json::Number(cam.line_x1);
  line["y1"] = Json::Number(cam.line_y1);
  line["x2"] = Json::Number(cam.line_x2);
  line["y2"] = Json::Number(cam.line_y2);
  o["virtual_line"] = Json::Object(std::move(line));
  return Json::Object(std::move(o));
}

}  // namespace

bool Config::LoadFile(const std::string& path, Config& out) {
  std::string text = ReadFile(path);
  if (text.empty()) return false;
  return FromJsonStr(text, out, nullptr);
}

bool Config::FromJsonStr(const std::string& json, Config& out, std::string* err) {
  Json root;
  if (!Json::Parse(json, root) || !root.IsObject()) {
    if (err) *err = "invalid json: not an object";
    return false;
  }

  Config tmp = out;  // 保留原值，失败的字段不破坏
  if (const Json* v = root.Get("det_thresh")) tmp.det_thresh = static_cast<float>(v->AsNumber(0.5));
  if (const Json* v = root.Get("recog_thresh")) tmp.recog_thresh = static_cast<float>(v->AsNumber(0.40));
  if (const Json* v = root.Get("verify_thresh")) tmp.verify_thresh = static_cast<float>(v->AsNumber(0.50));
  if (const Json* v = root.Get("liveness_enabled")) tmp.liveness_enabled = v->AsBool(true);
  if (const Json* v = root.Get("staff_enabled")) tmp.staff_enabled = v->AsBool(true);
  if (const Json* v = root.Get("retention_days")) tmp.retention_days = static_cast<int>(v->AsInt(365));
  if (const Json* v = root.Get("report_interval_s")) tmp.report_interval_s = static_cast<int>(v->AsInt(60));
  if (const Json* v = root.Get("report_endpoint")) tmp.report_endpoint = v->AsString();
  if (const Json* v = root.Get("device_psk"); v && !v->AsString().empty()) tmp.device_psk = v->AsString();
  if (const Json* v = root.Get("device_id")) tmp.device_id = v->AsInt(0);
  if (const Json* v = root.Get("max_frames")) tmp.max_frames = static_cast<int>(v->AsInt(0));
  if (const Json* v = root.Get("store_id")) tmp.store_id = v->AsInt(0);
  if (const Json* v = root.Get("device_name")) tmp.device_name = v->AsString();
  if (const Json* v = root.Get("heartbeat_interval_s")) tmp.heartbeat_interval_s = static_cast<int>(v->AsInt(30));
  if (const Json* v = root.Get("config_poll_s")) tmp.config_poll_s = static_cast<int>(v->AsInt(300));

  if (const Json* v = root.Get("web_enabled")) tmp.web_enabled = v->AsBool(false);
  if (const Json* v = root.Get("web_port")) tmp.web_port = static_cast<int>(v->AsInt(8180));
  if (const Json* v = root.Get("web_username")) tmp.web_username = v->AsString();
  if (const Json* v = root.Get("web_password"); v && !v->AsString().empty()) tmp.web_password = v->AsString();
  if (const Json* v = root.Get("web_static_dir")) tmp.web_static_dir = v->AsString();

  if (const Json* arr = root.Get("cameras"); arr && arr->IsArray()) {
    tmp.cameras.clear();
    for (const auto& c : arr->AsArray()) {
      if (!c.IsObject()) continue;
      CameraConfig cam;
      if (const Json* v = c.Get("camera_id")) cam.camera_id = v->AsString();
      if (const Json* v = c.Get("url")) cam.url = v->AsString();
      if (const Json* v = c.Get("role")) cam.role = v->AsString();
      if (const Json* v = c.Get("count_flow")) cam.count_flow = v->AsBool(true);
      if (const Json* v = c.Get("direction")) cam.direction = v->AsString();
      if (const Json* line = c.Get("virtual_line"); line && line->IsObject()) {
        if (const Json* v = line->Get("x1")) cam.line_x1 = static_cast<float>(v->AsNumber(0));
        if (const Json* v = line->Get("y1")) cam.line_y1 = static_cast<float>(v->AsNumber(0));
        if (const Json* v = line->Get("x2")) cam.line_x2 = static_cast<float>(v->AsNumber(0));
        if (const Json* v = line->Get("y2")) cam.line_y2 = static_cast<float>(v->AsNumber(0));
      }
      tmp.cameras.push_back(std::move(cam));
    }
  }

  std::string verr;
  if (!tmp.Validate(&verr)) {
    if (err) *err = verr;
    return false;
  }
  out = std::move(tmp);
  return true;
}

std::string Config::ToJson(bool mask_psk) const {
  std::map<std::string, Json> o;
  o["det_thresh"] = Json::Number(det_thresh);
  o["recog_thresh"] = Json::Number(recog_thresh);
  o["verify_thresh"] = Json::Number(verify_thresh);
  o["liveness_enabled"] = Json::Bool(liveness_enabled);
  o["staff_enabled"] = Json::Bool(staff_enabled);
  o["retention_days"] = Json::Number(static_cast<double>(retention_days));
  o["report_interval_s"] = Json::Number(static_cast<double>(report_interval_s));
  o["report_endpoint"] = Json::String(report_endpoint);
  o["device_psk"] = Json::String(mask_psk && !device_psk.empty() ? "********" : device_psk);
  o["device_id"] = Json::Number(static_cast<double>(device_id));
  o["max_frames"] = Json::Number(static_cast<double>(max_frames));
  o["store_id"] = Json::Number(static_cast<double>(store_id));
  o["device_name"] = Json::String(device_name);
  o["heartbeat_interval_s"] = Json::Number(static_cast<double>(heartbeat_interval_s));
  o["config_poll_s"] = Json::Number(static_cast<double>(config_poll_s));

  o["web_enabled"] = Json::Bool(web_enabled);
  o["web_port"] = Json::Number(static_cast<double>(web_port));
  o["web_username"] = Json::String(web_username);
  o["web_password"] = Json::String(web_password.empty() ? "" : "********");
  o["web_static_dir"] = Json::String(web_static_dir);

  std::vector<Json> cams;
  cams.reserve(cameras.size());
  for (const auto& c : cameras) cams.push_back(CameraToJson(c));
  o["cameras"] = Json::Array(std::move(cams));

  return Json::Object(std::move(o)).Dump();
}

bool Config::SaveFile(const std::string& path) const {
  std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << ToJson(false);
    out.flush();
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

bool Config::Validate(std::string* err) const {
  auto fail = [err](const char* m) {
    if (err) *err = m;
    return false;
  };
  if (det_thresh < 0.f || det_thresh > 1.f) return fail("det_thresh must be in [0,1]");
  if (recog_thresh < 0.f || recog_thresh > 1.f) return fail("recog_thresh must be in [0,1]");
  if (verify_thresh < 0.f || verify_thresh > 1.f) return fail("verify_thresh must be in [0,1]");
  if (retention_days <= 0) return fail("retention_days must be > 0");
  if (report_interval_s <= 0) return fail("report_interval_s must be > 0");
  if (web_port <= 0 || web_port > 65535) return fail("web_port must be in 1..65535");
  if (!report_endpoint.empty() && report_endpoint.rfind("http://", 0) != 0 &&
      report_endpoint.rfind("https://", 0) != 0) {
    return fail("report_endpoint must start with http:// or https://");
  }
  if (!web_password.empty() && web_username.empty()) return fail("web_username required when web enabled");
  return true;
}

}  // namespace eb