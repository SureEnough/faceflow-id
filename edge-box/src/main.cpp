// edge-box/src/main.cpp
// 边缘盒子入口：加载配置 → 后端（缺依赖 fallback Mock）→ 每路相机独立线程流水线。
// 支持 Web 配置界面（web_enabled/web_port/web_password）与热重载重建。
// 主线程负责任命周期：状态板、人员库同步、心跳、配置下发、批量上报、退出清理。
// 用法: edge_box -c edge_box.json [-backend mock|onnx]
#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <thread>
#include <vector>

#include "backend/inference_backend.h"
#include "common/common.h"
#include "config/config.h"
#include "face/face_engine.h"
#include "pipeline/pipeline.h"
#include "recognizer/recognizer.h"
#include "report/api_client.h"
#include "report/device_ops.h"
#include "report/features_sync.h"
#include "report/report_client.h"
#include "store/recognition_store.h"
#include "web/config_manager.h"
#include "web/preview_store.h"
#include "web/web_server.h"

namespace {
volatile std::sig_atomic_t g_stop = 0;
std::atomic<bool> g_web_stop{false};
void OnSignal(int) { g_stop = 1; }

// 构建/重建 流水线 + 上报/同步客户端；返回打开的流水线数
int RebuildRuntime(const eb::Config& cfg, eb::FaceEngine* face, eb::Recognizer* recognizer,
                   eb::RecognitionStore* store, std::unique_ptr<eb::ReportClient>* report,
                   std::unique_ptr<eb::ApiClient>* api,
                   std::unique_ptr<eb::FeaturesSyncClient>* sync,
                   std::vector<std::unique_ptr<eb::Pipeline>>* pipelines) {
  report->reset(eb::CreateReportClient(cfg.report_endpoint, cfg.device_psk));
  *api = std::make_unique<eb::ApiClient>(cfg.report_endpoint, cfg.device_psk);
  *sync = std::make_unique<eb::FeaturesSyncClient>(api->get(), recognizer);

  pipelines->clear();
  for (const auto& cam : cfg.cameras) {
    auto p = std::make_unique<eb::Pipeline>(cam, cfg.device_id, face, recognizer, store,
                                            report->get(), cfg.recog_thresh);
    if (!p->Open()) {
      LOG_ERROR("[%s] pipeline open failed", cam.camera_id.c_str());
      continue;
    }
    pipelines->push_back(std::move(p));
  }
  return static_cast<int>(pipelines->size());
}

// 采集各相机运行状态（供状态板/心跳）
std::vector<eb::web::CameraStatus> CollectStatus(const eb::Config& cfg,
                                                 const std::vector<std::unique_ptr<eb::Pipeline>>& pipelines) {
  std::vector<eb::web::CameraStatus> cams;
  cams.reserve(cfg.cameras.size());
  for (const auto& cam : cfg.cameras) {
    eb::web::CameraStatus cs;
    cs.camera_id = cam.camera_id;
    cs.opened = false;
    cs.last_frame_ok = true;
    for (const auto& p : pipelines) {
      if (p->camera_id() == cam.camera_id) {
        cs.opened = true;
        cs.last_frame_ok = p->LastFrameOk();
        cs.frames = p->Frames();
        auto st = p->flow();
        cs.flow_in = st.in;
        cs.flow_out = st.out;
        break;
      }
    }
    cams.push_back(std::move(cs));
  }
  return cams;
}
}  // namespace

int main(int argc, char** argv) {
  std::string cfg_path = "edge_box.json";
  eb::BackendType backend_type = eb::BackendType::kMock;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-c" && i + 1 < argc) cfg_path = argv[++i];
    else if (arg == "-backend" && i + 1 < argc) {
      std::string v = argv[++i];
      if (v == "onnx") backend_type = eb::BackendType::kOnnx;
      else if (v == "mock") backend_type = eb::BackendType::kMock;
    } else if (arg == "-h" || arg == "--help") {
      std::printf("Usage: %s -c edge_box.json [-backend mock|onnx]\n", argv[0]);
      return 0;
    }
  }

  eb::Config cfg;
  if (!eb::Config::LoadFile(cfg_path, cfg)) {
    LOG_ERROR("load config failed: %s", cfg_path.c_str());
    return 1;
  }
  LOG_INFO("config loaded: %zu camera(s), device=%lld", cfg.cameras.size(),
           static_cast<long long>(cfg.device_id));

  eb::web::ConfigManager cm(cfg_path, cfg);
  eb::web::StatusBoard board;

  std::unique_ptr<eb::IInferenceBackend> backend(eb::CreateBackend(backend_type, {}));
  if (!backend) {
    LOG_WARN("backend unavailable (%d), fallback to MOCK", static_cast<int>(backend_type));
    backend.reset(eb::CreateBackend(eb::BackendType::kMock, {}));
  }
  backend->WarmUp();
  eb::FaceEngine face(backend.get());
  const std::string backend_name = (backend_type == eb::BackendType::kOnnx) ? "onnx" : "mock";

  eb::Recognizer recognizer;
  std::unique_ptr<eb::RecognitionStore> store(eb::CreateRecognitionStore());
  std::unique_ptr<eb::ReportClient> report;
  std::unique_ptr<eb::ApiClient> api;
  std::unique_ptr<eb::FeaturesSyncClient> sync;
  std::vector<std::unique_ptr<eb::Pipeline>> pipelines;
  int64_t sync_version = 0;
  const std::time_t started_at = std::time(nullptr);

  auto applyConfig = [&](const eb::Config& c) {
    int opened = RebuildRuntime(c, &face, &recognizer, store.get(), &report, &api, &sync, &pipelines);
    LOG_INFO("runtime rebuilt: %d pipeline(s) opened", opened);
  };
  applyConfig(cfg);
  if (pipelines.empty()) {
    LOG_ERROR("no pipeline opened");
    return 1;
  }

  // Web 配置界面（后台线程）
  std::unique_ptr<eb::web::PreviewStore> preview = std::make_unique<eb::web::PreviewStore>();
  std::unique_ptr<eb::web::WebServer> web;
  if (cfg.web_enabled && !cfg.web_password.empty()) {
    web = std::make_unique<eb::web::WebServer>(&cm, &board, store.get(), preview.get());
    if (web->Start(cfg.web_port, cfg.web_username, cfg.web_password, cfg.web_static_dir,
                   g_web_stop)) {
      LOG_INFO("web UI started: http://0.0.0.0:%d (login %s)", cfg.web_port,
               cfg.web_username.c_str());
    } else {
      web.reset();
    }
  } else {
    LOG_WARN("web UI disabled (set web_enabled=true and web_password)");
  }

  // 设备注册（自注册主设备 + 代注册摄像头）
  int64_t device_id = cfg.device_id;
  if (api->httpAvailable() && !cfg.report_endpoint.empty()) {
    device_id = eb::EnsureDeviceRegistered(cfg, api.get(), device_id);
    if (device_id != cfg.device_id) {
      // 回写配置，保证重启后稳定
      eb::Config next = cm.Snapshot();
      next.device_id = device_id;
      cm.SetConfig(next);
      next.SaveFile(cm.path());
      cfg = next;
      applyConfig(cfg);  // 流水线使用新 device_id
    }
  }

  // 每路相机一个工作线程（文档 4.1.6）
  std::atomic<bool> workers_stop{false};
  std::atomic<int64_t> total_frames{0};
  std::vector<std::thread> workers;
  auto startWorkers = [&](const eb::Config& c) {
    const int max_frames = c.max_frames;
    for (auto& p : pipelines) {
      workers.emplace_back([p = p.get(), preview = preview.get(), &workers_stop, &total_frames,
                             max_frames]() {
        const auto interval = std::chrono::milliseconds(1000 / eb::kTargetFps);
        while (!workers_stop.load() && !g_stop && !g_web_stop.load() &&
               (max_frames <= 0 || total_frames.load() < max_frames)) {
          if (p->ProcessOneFrame()) total_frames.fetch_add(1);
          // 预览缓存（节流编码，Web /api/preview 用）
          preview->Capture(p->camera_id(), p->FrameSnapshot());
          std::this_thread::sleep_for(interval);  // 每路限速 kTargetFps
        }
      });
    }
  };
  auto stopWorkers = [&]() {
    workers_stop.store(true);
    for (auto& t : workers) t.join();
    workers.clear();
    workers_stop.store(false);
  };
  startWorkers(cfg);

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);
  LOG_INFO("edge-box running... (%zu camera threads)", workers.size());

  // 启动即同步一次人员库
  std::time_t last_sync = 0;
  std::time_t last_heartbeat = 0;
  std::time_t last_report = 0;
  std::time_t last_cleanup = 0;
  std::time_t last_config_poll = 0;
  if (api->httpAvailable() && !cfg.report_endpoint.empty() && device_id > 0) {
    sync_version = sync->SyncOnce(device_id, sync_version);
    last_sync = std::time(nullptr);
  }

  // 生命周期主循环（每秒维护一次）
  while (!g_stop && !g_web_stop.load() &&
         (cfg.max_frames <= 0 || total_frames.load() < cfg.max_frames)) {
    if (cm.reload_pending()) {
      cm.ConsumeReload();
      stopWorkers();
      cfg = cm.Snapshot();
      applyConfig(cfg);
      if (pipelines.empty()) {
        LOG_ERROR("reload produced no open pipelines");
        break;
      }
      startWorkers(cfg);
      LOG_INFO("config reloaded: %zu camera(s)", cfg.cameras.size());
    }

    std::time_t now = std::time(nullptr);
    if (now - last_report >= std::max(5, cfg.report_interval_s)) {
      last_report = now;
      auto pending = store->Pending(200);
      if (!pending.empty()) {
        std::vector<eb::Recognition> recs;
        for (auto& p : pending) {
          recs.push_back(p.rec);
        }
        if (report->Upload(cfg.device_id, recs)) {
          for (auto& p : pending) store->MarkConfirmed(p.rec);
        }
      }
    }

    // 每秒：状态板 + 心跳/同步/配置下发调度
    const int64_t seconds = now - started_at;
    (void)seconds;
    if (api->httpAvailable() && !cfg.report_endpoint.empty() && device_id > 0) {
      if (now - last_sync >= std::max(5, cfg.report_interval_s)) {
        sync_version = sync->SyncOnce(device_id, sync_version);
        last_sync = now;
      }
      if (now - last_heartbeat >= std::max(5, cfg.heartbeat_interval_s)) {
        auto cams = CollectStatus(cfg, pipelines);
        bool ok = eb::SendHeartbeat(device_id, api.get(), cams);
        if (ok) last_heartbeat = now;
        (void)ok;
      }
      if (now - last_config_poll >= std::max(5, cfg.config_poll_s)) {
        std::string remote;
        if (eb::PullRemoteConfig(device_id, api.get(), remote)) {
          eb::Config parsed;
          if (eb::Config::FromJsonStr(remote, parsed, nullptr)) {
            // 设备身份与 Web 安全字段以本地为准（远程配置不改这些）
            parsed.device_id = cfg.device_id;
            parsed.device_psk = cfg.device_psk;
            parsed.web_enabled = cfg.web_enabled;
            parsed.web_port = cfg.web_port;
            parsed.web_username = cfg.web_username;
            parsed.web_password = cfg.web_password;
            parsed.web_static_dir = cfg.web_static_dir;
            // 规范化比较：双方 mask psk 后比较，避免 PSK 差异导致循环重载
            if (parsed.ToJson(/*mask_psk=*/true) != cfg.ToJson(/*mask_psk=*/true)) {
              LOG_INFO("remote config differs, applying...");
              cm.UpdateFromJson(parsed.ToJson(/*mask_psk=*/false), nullptr);  // 写盘 + 置重载
            }
          }
        }
        last_config_poll = now;
      }
    }

    {
      auto cams = CollectStatus(cfg, pipelines);
      board.Update(cfg.device_id, backend_name, eb::web::StatusBoard::Version(),
                   started_at, std::move(cams), sync_version);
    }

    if (now - last_cleanup >= 3600) {
      last_cleanup = now;
      int64_t removed = store->Cleanup(now, cfg.retention_days);
      if (removed > 0) LOG_INFO("store cleanup: removed %lld records", static_cast<long long>(removed));
    }

    // 周期打印客流（仅少量路数，避免刷屏）
    static int log_tick = 0;
    if ((++log_tick % 30) == 0 && pipelines.size() <= 4) {
      for (auto& p : pipelines) {
        auto st = p->flow();
        LOG_INFO("[%s] frames=%lld flow in=%lld out=%lld", p->camera_id().c_str(),
                 static_cast<long long>(p->Frames()), static_cast<long long>(st.in),
                 static_cast<long long>(st.out));
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  LOG_INFO("edge-box stopping...");
  stopWorkers();
  if (web) web->Stop();

  // 退出前补齐上报
  auto pending = store->Pending(200);
  if (!pending.empty()) {
    std::vector<eb::Recognition> recs;
    for (auto& p : pending) {
      recs.push_back(p.rec);
    }
    if (report->Upload(cfg.device_id, recs)) {
      for (auto& p : pending) store->MarkConfirmed(p.rec);
    }
  }
  LOG_INFO("edge-box stopped; frames=%lld", static_cast<long long>(total_frames.load()));
  return 0;
}