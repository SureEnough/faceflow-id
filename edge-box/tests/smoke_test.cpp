// edge-box/tests/smoke_test.cpp
// 无第三方依赖自测：验证 Mock 后端、跟踪、客流、识别、存储/上报链路。
// 构建: cmake -B build && cmake --build build && ctest --test-dir build
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>

#include "backend/inference_backend.h"
#include "config/mini_json.h"
#include "face/face_engine.h"
#include "flow/flow_counter.h"
#include "pipeline/pipeline.h"
#include "recognizer/recognizer.h"
#include "report/report_client.h"
#include "store/recognition_store.h"
#include "tracker/iou_tracker.h"
#include "video/video_source.h"

using namespace eb;

static int g_fail = 0;
bool StoreImplOK();
#define CHECK(cond, msg)                                        \
  do {                                                          \
    if (!(cond)) {                                              \
      std::printf("[FAIL] %s\n", msg);                          \
      ++g_fail;                                                 \
    } else {                                                    \
      std::printf("[ OK ] %s\n", msg);                          \
    }                                                           \
  } while (0)

// Mock 视频合成帧
static ImageFrame MakeFrame(int w, int h, int block_x) {
  ImageFrame f;
  f.width = w; f.height = h; f.channels = 3;
  f.data.assign(static_cast<size_t>(w) * h * 3, 128);
  int b = 40;
  int x0 = std::max(0, block_x);
  int x1 = std::min(w, block_x + b);
  for (int y = h / 2 - b / 2; y < h / 2 + b / 2; ++y) {
    for (int x = x0; x < x1; ++x) {
      uint8_t* p = f.Ptr(y, x);
      p[0] = 200; p[1] = 120; p[2] = 60;
    }
  }
  return f;
}

int main() {
  // 1. MiniJson 解析
  {
    Json j;
    bool ok = Json::Parse(R"({"cameras":[{"camera_id":"cam-01","count_flow":true}],"det_thresh":0.5})", j);
    CHECK(ok && j.IsObject() && j.Has("cameras") && j.Has("det_thresh"), "MiniJson parse object");
    const Json* arr = j.Get("cameras");
    CHECK(arr && arr->IsArray() && arr->AsArray().size() == 1, "MiniJson array access");
    const Json* cam = arr->AsArray()[0].Get("camera_id");
    CHECK(cam && cam->AsString() == "cam-01", "MiniJson nested string");
  }

  // 2. Mock 后端：检测 + 特征
  {
    std::unique_ptr<IInferenceBackend> backend(CreateBackend(BackendType::kMock, {}));
    ImageFrame f = MakeFrame(320, 240, 140);
    std::vector<FaceBox> boxes;
    CHECK(backend->Detect(f, 0.5f, boxes) && !boxes.empty(), "Mock detect returns one box");
    Feature feat;
    ImageFrame aligned;
    FaceEngine engine(backend.get());
    FaceSample sample;
    CHECK(engine.AlignCrop(f, boxes[0], aligned) && aligned.width == 112, "AlignCrop 112x112");
    CHECK(engine.Sample(f, 0.5f, sample) && sample.has_feature, "Sample extract feature");
  }

  // 3. IOU 跟踪：相同位置跨帧关联为同一 id
  {
    IOUTracker tracker;
    std::vector<FaceBox> b1, b2;
    FaceBox fb{10, 10, 40, 40, 0.9f, {}};
    b1.push_back(fb); b2.push_back(fb);
    std::vector<Track> t1, t2;
    tracker.Update(b1, t1);
    tracker.Update(b2, t2);
    CHECK(t1.size() == 1 && t2.size() == 1 && t1[0].id == t2[0].id, "IOU tracker keeps id");
  }

  // 4. FlowCounter：质心从线上方到下方应计 1 次 IN
  {
    FlowCounter fc(0, 120, 320, 120);
    CHECK(fc.Update("t1", 160, 100) == -1, "flow no cross yet");
    int dir = fc.Update("t1", 160, 140);
    CHECK(dir == 0 && fc.stats().in == 1, "flow crosses line => IN");
    CHECK(fc.Update("t1", 160, 160) == -1 && fc.stats().in == 1, "flow same track counted once");
  }

  // 5. Recognizer：同一特征命中，不同特征不命中
  {
    std::unique_ptr<IInferenceBackend> backend(CreateBackend(BackendType::kMock, {}));
    ImageFrame f = MakeFrame(320, 240, 140);
    FaceEngine engine(backend.get());
    FaceSample s1;
    CHECK(engine.Sample(f, 0.5f, s1), "sample1 for recognizer");

    Recognizer rec;
    Identity id;
    id.customer_id = 100;
    id.person_type = kCustomer;
    id.feature = s1.feature;
    rec.Upsert(id);

    MatchResult m = rec.Search(s1.feature, 0.4f);
    CHECK(m.hit && m.customer_id == 100 && m.similarity > 0.99f, "recognizer same-feature hit");

    ImageFrame f2 = MakeFrame(320, 240, 10);  // 不同位置 → 不同特征
    FaceSample s2;
    engine.Sample(f2, 0.5f, s2);
    MatchResult m2 = rec.Search(s2.feature, 0.9f);
    CHECK(!m2.hit, "recognizer different-feature miss @0.9");
  }

  // 6. Store + Report（自测模式）
  {
    std::unique_ptr<RecognitionStore> store(CreateRecognitionStore());
    std::unique_ptr<ReportClient> report(CreateReportClient("", ""));
    Recognition rec;
    rec.track_id = "T-1"; rec.camera_id = "cam-01"; rec.created_at = 1700000000;
    rec.customer_id = -1; rec.person_type = kCustomer; rec.direction = 0;
    CHECK(store->Insert(rec), "store insert");
    auto pending = store->Pending(10);
    CHECK(pending.size() == 1, "store pending");
    CHECK(report->Upload(1, {rec}) && StoreImplOK(), "report upload (print mode)");
    CHECK(store->Cleanup(1700000000 + 99999999, 365) == 0, "store cleanup keep recent");
  }

  std::printf("smoke test finished: %s\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURE");
  return g_fail == 0 ? 0 : 1;
}

// 辅助：StoreImplOK 占位（保持 smoke 结构简洁）
bool StoreImplOK() { return true; }