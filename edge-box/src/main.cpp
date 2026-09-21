// edge-box/src/main.cpp
// 边缘盒子入口：加载配置 → 创建推理后端（缺依赖时 fallback Mock）→ 每路相机一条流水线。
// 用法: edge_box -c config.json [-backend mock|onnx]
#include <csignal>
#include <memory>
#include <vector>

#include "backend/inference_backend.h"
#include "common/common.h"
#include "config/config.h"
#include "face/face_engine.h"
#include "pipeline/pipeline.h"
#include "recognizer/recognizer.h"
#include "report/report_client.h"
#include "store/recognition_store.h"

namespace {
volatile std::sig_atomic_t g_stop = 0;
void OnSignal(int) { g_stop = 1; }
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
  if (!eb::Config::LoadFile(cfg_path, cfg) || cfg.cameras.empty()) {
    eb::LOG_ERROR("load config failed or no cameras: %s", cfg_path.c_str());
    return 1;
  }
  eb::LOG_INFO("config loaded: %zu camera(s), device=%lld", cfg.cameras.size(),
               static_cast<long long>(cfg.device_id));

  // 1. 推理后端（ONNX 不可用时 fallback Mock）
  std::unique_ptr<eb::IInferenceBackend> backend(eb::CreateBackend(backend_type, {}));
  if (!backend) {
    eb::LOG_WARN("backend unavailable (%d), fallback to MOCK", static_cast<int>(backend_type));
    backend.reset(eb::CreateBackend(eb::BackendType::kMock, {}));
  }
  backend->WarmUp();
  eb::FaceEngine face(backend.get());

  // 2. 共享组件：特征库 / 存储 / 上报
  eb::Recognizer recognizer;
  // TODO: 从后台 /customers/features/sync 拉取人员库后 recognizer.LoadLibrary(...)
  std::unique_ptr<eb::RecognitionStore> store(eb::CreateRecognitionStore());
  std::unique_ptr<eb::ReportClient> report(
      eb::CreateReportClient(cfg.report_endpoint, cfg.device_psk));

  // 3. 每路相机一条流水线（真实多路可用线程池，见文档 4.1.6）
  std::vector<std::unique_ptr<eb::Pipeline>> pipelines;
  for (const auto& cam : cfg.cameras) {
    auto p = std::make_unique<eb::Pipeline>(cam, cfg.device_id, &face, &recognizer,
                                            store.get(), report.get(), cfg.recog_thresh);
    if (!p->Open()) continue;
    pipelines.push_back(std::move(p));
  }
  if (pipelines.empty()) {
    eb::LOG_ERROR("no pipeline opened");
    return 1;
  }

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);
  eb::LOG_INFO("edge-box running... (Ctrl+C to stop)");

  // 简化：mock 场景单线程轮询所有路（真实实现为每路一线程）
  int frames = 0;
  while (!g_stop && (cfg.max_frames <= 0 || frames < cfg.max_frames)) {
    for (auto& p : pipelines) {
      if (p->ProcessOneFrame()) {
        ++frames;
      }
    }
    if (frames % 30 == 0) {
      for (auto& p : pipelines) {
        eb::LOG_INFO("[%s] flow in=%lld out=%lld", p->camera_id().c_str(),
                     static_cast<long long>(p->flow().in),
                     static_cast<long long>(p->flow().out));
      }
    }
  }

  eb::LOG_INFO("edge-box stopped; frames=%d", frames);
  // 退出前补齐上报
  auto pending = store->Pending(200);
  if (!pending.empty()) {
    std::vector<eb::Recognition> recs;
    for (auto& p : pending) { recs.push_back(p.rec); store->MarkConfirmed(p.rec); }
    report->Upload(cfg.device_id, recs);
  }
  return 0;
}