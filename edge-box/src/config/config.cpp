#include "config/config.h"

#include <fstream>
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
}  // namespace

bool Config::LoadFile(const std::string& path, Config& out) {
  std::string text = ReadFile(path);
  if (text.empty()) return false;

  Json root;
  if (!Json::Parse(text, root) || !root.IsObject()) return false;

  if (const Json* v = root.Get("det_thresh")) out.det_thresh = static_cast<float>(v->AsNumber(0.5));
  if (const Json* v = root.Get("recog_thresh")) out.recog_thresh = static_cast<float>(v->AsNumber(0.40));
  if (const Json* v = root.Get("verify_thresh")) out.verify_thresh = static_cast<float>(v->AsNumber(0.50));
  if (const Json* v = root.Get("liveness_enabled")) out.liveness_enabled = v->AsBool(true);
  if (const Json* v = root.Get("staff_enabled")) out.staff_enabled = v->AsBool(true);
  if (const Json* v = root.Get("retention_days")) out.retention_days = static_cast<int>(v->AsInt(365));
  if (const Json* v = root.Get("report_interval_s")) out.report_interval_s = static_cast<int>(v->AsInt(60));
  if (const Json* v = root.Get("report_endpoint")) out.report_endpoint = v->AsString();
  if (const Json* v = root.Get("device_psk")) out.device_psk = v->AsString();
  if (const Json* v = root.Get("device_id")) out.device_id = v->AsInt(0);
  if (const Json* v = root.Get("max_frames")) out.max_frames = static_cast<int>(v->AsInt(0));

  if (const Json* arr = root.Get("cameras"); arr && arr->IsArray()) {
    out.cameras.clear();
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
      out.cameras.push_back(std::move(cam));
    }
  }
  return true;
}

}  // namespace eb