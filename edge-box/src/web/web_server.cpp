// edge-box/src/web/web_server.cpp
#include "web/web_server.h"

#if defined(HAVE_CPPHTTPLIB)
#include <httplib.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <thread>

#include "common/base64.h"
#include "common/common.h"
#include "common/image_codec.h"
#include "config/mini_json.h"
#include "face/face_engine.h"
#include "store/recognition_store.h"
#include "web/preview_store.h"

namespace eb {
namespace web {

namespace {

#if defined(HAVE_CPPHTTPLIB)
void OkResp(httplib::Response& res, const std::string& jsonPayload) {
  res.status = 200;
  res.set_content("{\"code\":0,\"message\":\"ok\",\"data\":" + jsonPayload + "}",
                  "application/json; charset=utf-8");
}
void FailResp(httplib::Response& res, int code, const std::string& msg) {
  std::map<std::string, Json> o;
  o["code"] = Json::Number(static_cast<double>(code));
  o["message"] = Json::String(msg);
  res.status = (code == 401) ? 401 : (code == 404 ? 404 : 400);
  res.set_content(Json::Object(std::move(o)).Dump(), "application/json; charset=utf-8");
}

// Basic Auth 校验：Authorization: Basic base64(user:pass)
bool CheckAuth(const httplib::Request& req, const std::string& user, const std::string& pass) {
  if (user.empty() || pass.empty()) return false;
  const std::string auth = req.get_header_value("Authorization");
  if (auth.rfind("Basic ", 0) != 0) return false;
  std::string decoded;
  if (!Base64Decode(auth.substr(6), decoded)) return false;
  return decoded == user + ":" + pass;
}

bool StaticDirReady(const std::string& dir) {
  std::ifstream f(dir + "/index.html");
  return f.good();
}

std::string BuildHintPage(const std::string& static_dir) {
  std::string h =
      "<html><head><meta charset=\"utf-8\"><title>FaceFlow 边缘盒</title></head>\n"
      "<body style=\"font-family:sans-serif;padding:40px\">\n"
      "<h2>FaceFlow 边缘盒 Web 配置</h2>\n"
      "<p>未找到前端构建产物（<code>%DIR%/index.html</code>）。请在 edge-box/web 下构建：</p>\n"
      "<pre>cd edge-box/web && npm install && npm run build</pre>\n"
      "<p>API 服务正常：<a href=\"/api/status\">/api/status</a>（需 Basic Auth）</p>\n"
      "</body></html>";
  std::string::size_type p = h.find("%DIR%");
  if (p != std::string::npos) h.replace(p, 5, static_dir);
  return h;
}
#endif

}  // namespace

WebServer::~WebServer() { Stop(); }

bool WebServer::Start(int port, const std::string& username, const std::string& password,
                      const std::string& static_dir, const std::atomic<bool>& stop_flag) {
#if !defined(HAVE_CPPHTTPLIB)
  (void)port; (void)username; (void)password; (void)static_dir; (void)stop_flag;
  LOG_WARN("web server requires cpp-httplib (HAVE_CPPHTTPLIB)");
  return false;
#else
  if (username.empty() || password.empty()) {
    LOG_WARN("web server disabled: web_username/web_password not set");
    return false;
  }
  auto* svr = new httplib::Server();
  auto* th = new std::thread([this, svr, port, username, password, static_dir, &stop_flag]() {
    // 全部 API 先鉴权
    auto guard = [&](const httplib::Request& req, httplib::Response& res) -> bool {
      if (!CheckAuth(req, username, password)) {
        res.status = 401;
        res.set_header("WWW-Authenticate", "Basic realm=\"edge-box\"");
        res.set_content(R"({"code":401,"message":"unauthorized"})", "application/json");
        return false;
      }
      return true;
    };

    // 静态前端（edge-box/web/dist）
    if (StaticDirReady(static_dir)) {
      svr->set_mount_point("/", static_dir.c_str());
      LOG_INFO("web static mounted: %s", static_dir.c_str());
    } else {
      static const std::string hint = BuildHintPage(static_dir);
      svr->Get("/", [hint](const httplib::Request&, httplib::Response& res) {
        res.set_content(hint, "text/html; charset=utf-8");
      });
      LOG_WARN("web static dir not found: %s (run: cd edge-box/web && npm run build)",
               static_dir.c_str());
    }

    svr->Get("/api/status", [&](const httplib::Request& req, httplib::Response& res) {
      if (!guard(req, res)) return;
      OkResp(res, status_->ToJson());
    });

    svr->Get("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
      if (!guard(req, res)) return;
      OkResp(res, cm_->Snapshot().ToJson(/*mask_psk=*/true));
    });

    svr->Put("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
      if (!guard(req, res)) return;
      std::string err;
      if (!cm_->UpdateFromJson(req.body, &err)) {
        FailResp(res, 400, err.empty() ? "invalid config" : err);
        return;
      }
      OkResp(res, "{}");
      LOG_INFO("web config updated");
    });

    svr->Get("/api/snapshots", [&](const httplib::Request& req, httplib::Response& res) {
      if (!guard(req, res)) return;
      if (!store_) {
        OkResp(res, "[]");
        return;
      }
      // 筛选参数（均为可选；空=不限制）
      RecognitionQuery q;
      if (req.has_param("limit")) {
        int v = std::atoi(req.get_param_value("limit").c_str());
        if (v > 0 && v <= 50) q.limit = v;
      } else {
        q.limit = 10;
      }
      if (req.has_param("camera_id")) q.camera_id = req.get_param_value("camera_id");
      if (req.has_param("identified")) q.identified = std::atoi(req.get_param_value("identified").c_str());
      if (req.has_param("person_type")) q.person_type = std::atoi(req.get_param_value("person_type").c_str());
      if (req.has_param("direction")) q.direction = std::atoi(req.get_param_value("direction").c_str());
      if (req.has_param("min_similarity")) q.min_similarity = std::atof(req.get_param_value("min_similarity").c_str());
      if (req.has_param("start_at")) q.start_at = std::atoll(req.get_param_value("start_at").c_str());
      if (req.has_param("end_at")) q.end_at = std::atoll(req.get_param_value("end_at").c_str());
      auto recs = store_->RecentFiltered(q);
      std::vector<Json> arr;
      arr.reserve(recs.size());
      for (const auto& it : recs) {
        std::map<std::string, Json> o;
        o["track_id"] = Json::String(it.rec.track_id);
        o["camera_id"] = Json::String(it.rec.camera_id);
        o["created_at"] = Json::Number(static_cast<double>(it.rec.created_at));
        if (it.rec.customer_id >= 0) {
          o["customer_id"] = Json::Number(static_cast<double>(it.rec.customer_id));
        } else {
          o["customer_id"] = Json::Null();
        }
        o["person_type"] = Json::Number(static_cast<double>(it.rec.person_type));
        o["similarity"] = Json::Number(static_cast<double>(it.rec.similarity));
        o["direction"] = Json::Number(static_cast<double>(it.rec.direction));
        o["snapshot_mime"] = Json::String(it.rec.snapshot_mime);
        o["snapshot"] = Json::String(it.rec.snapshot_b64);  // base64 图片
        arr.push_back(Json::Object(std::move(o)));
      }
      OkResp(res, Json::Array(std::move(arr)).Dump());
      LOG_INFO("web snapshots: limit=%d hits=%zu", q.limit, recs.size());
    });

    svr->Get("/api/preview", [&](const httplib::Request& req, httplib::Response& res) {
      if (!guard(req, res)) return;
      if (!preview_ || !req.has_param("camera_id")) {
        FailResp(res, 400, "preview unavailable or camera_id required");
        return;
      }
      std::string mime, b64;
      int srcW = 0, srcH = 0;
      if (!preview_->Get(req.get_param_value("camera_id"), mime, b64, srcW, srcH)) {
        FailResp(res, 404, "no preview frame for camera");
        return;
      }
      // mime 固定值 + base64 为 URL 安全字符，无需 JSON 转义；width/height 为原始分辨率
      OkResp(res, "{\"mime\":\"" + mime + "\",\"b64\":\"" + b64 +
                  "\",\"width\":" + std::to_string(srcW) + ",\"height\":" + std::to_string(srcH) + "}");
    });

    // 人脸特征提取：与 face-service 契约一致（POST /api/face/extract）。
    // 鉴权：复用 Basic Auth（web_username/web_password）；管理后台系统配置的
    // 人脸识别服务地址指向本接口时，密钥填 "user:password" 即走 Basic 认证。
    svr->Post("/api/face/extract", [&](const httplib::Request& req, httplib::Response& res) {
      if (!guard(req, res)) return;
      if (!face_) {
        FailResp(res, 400, "face engine unavailable");
        return;
      }
      // 1. 取图：multipart file(image) 或 JSON image_b64
      std::string img;
      if (req.is_multipart_form_data() && req.form.has_file("image")) {
        img = req.form.get_file("image").content;
      } else {
        Json root;
        if (!Json::Parse(req.body, root)) {
          FailResp(res, 400, "invalid json");
          return;
        }
        const Json* v = root.Get("image_b64");
        if (!v || v->AsString().empty()) {
          FailResp(res, 400, "multipart image or image_b64 required");
          return;
        }
        if (!Base64Decode(v->AsString(), img)) {
          FailResp(res, 400, "image_b64 invalid");
          return;
        }
      }
      if (img.empty()) {
        FailResp(res, 400, "empty image");
        return;
      }
      // 2. 解码 + 人脸采样（检测 → 对齐 → 特征 + 活体）
      ImageFrame frame;
      if (!DecodeImageBytes(img, frame)) {
        FailResp(res, 400, "image decode failed");
        return;
      }
      FaceSample sample;
      const float det_thresh = cm_->Snapshot().det_thresh;
      if (!face_->Sample(frame, det_thresh, sample) || !sample.has_feature) {
        FailResp(res, 404, "no face detected or feature extract failed");
        return;
      }
      // 3. 特征 → base64（512×float32，与后台/face-service 契约一致）
      const Feature& feat = sample.feature;
      const std::string raw(reinterpret_cast<const char*>(feat.data()), feat.size() * sizeof(float));
      std::map<std::string, Json> o;
      o["feature_b64"] = Json::String(Base64Encode(reinterpret_cast<const unsigned char*>(raw.data()), raw.size()));
      o["dim"] = Json::Number(static_cast<double>(kFeatureDim));
      o["faces"] = Json::Number(1.0);
      o["engine"] = Json::String("edge-box");
      OkResp(res, Json::Object(std::move(o)).Dump());
      LOG_INFO("face extract ok: image=%zuB", img.size());
    });

    svr->Post("/api/reload", [&](const httplib::Request& req, httplib::Response& res) {
      if (!guard(req, res)) return;
      cm_->RequestReload();
      OkResp(res, "{}");
      LOG_INFO("web reload requested");
    });

    svr->Post("/api/restart", [&](const httplib::Request& req, httplib::Response& res) {
      if (!guard(req, res)) return;
      OkResp(res, "{}");
      LOG_INFO("web restart requested, exiting...");
      std::this_thread::sleep_for(std::chrono::milliseconds(200));  // 先回包再退出
      const_cast<std::atomic<bool>&>(stop_flag).store(true);
    });

    LOG_INFO("web UI listening on 0.0.0.0:%d", port);
    if (!svr->listen("0.0.0.0", port)) {
      LOG_ERROR("web server listen failed on port %d", port);
    }
  });

  server_ = svr;
  thread_ = th;
  return true;
#endif
}

void WebServer::Stop() {
#if defined(HAVE_CPPHTTPLIB)
  if (server_) {
    static_cast<httplib::Server*>(server_)->stop();
  }
  if (thread_ && static_cast<std::thread*>(thread_)->joinable()) {
    static_cast<std::thread*>(thread_)->join();
  }
  delete static_cast<std::thread*>(thread_);
  delete static_cast<httplib::Server*>(server_);
  thread_ = nullptr;
  server_ = nullptr;
#endif
}

}  // namespace web
}  // namespace eb