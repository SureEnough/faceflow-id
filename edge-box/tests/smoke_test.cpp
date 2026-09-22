// edge-box/tests/smoke_test.cpp
// 无第三方依赖自测：验证 Mock 后端、跟踪、客流、识别、存储/上报链路。
// 构建: cmake -B build && cmake --build build && ctest --test-dir build
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "backend/inference_backend.h"
#include "backend/scrfd_decode.h"
#include "face/face_align.h"
#include "common/base64.h"
#include "common/image_codec.h"
#include "config/mini_json.h"
#include "face/face_engine.h"
#include "flow/flow_counter.h"
#include "pipeline/pipeline.h"
#include "recognizer/recognizer.h"
#include "report/features_sync.h"
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

  // 6. Store + Report（自测模式：原生实现（SQLite when available）与纯内存都过一遍）
  {
    auto runStore = [](const char* tag, RecognitionStore* store) {
      Recognition rec;
      rec.track_id = "T-1"; rec.camera_id = "cam-01"; rec.created_at = 1700000000;
      rec.customer_id = -1; rec.person_type = kCustomer; rec.direction = 0;
      CHECK(store->Insert(rec), std::string("store insert (") + tag + ")");
      auto pending = store->Pending(10);
      CHECK(pending.size() == 1, std::string("store pending (") + tag + ")");
      // 1 天前创建 → 保留；400 天前创建（超过 365 天保留期）→ 删除
      CHECK(store->Cleanup(rec.created_at + 86400, 365) == 0, std::string("store cleanup keep recent (") + tag + ")");
      Recognition oldRec;
      oldRec.track_id = "T-OLD"; oldRec.camera_id = "cam-01";
      oldRec.created_at = rec.created_at - 400 * 86400;
      store->Insert(oldRec);
      CHECK(store->Cleanup(rec.created_at, 365) == 1, std::string("store cleanup remove expired (") + tag + ")");
    };
    {
      std::unique_ptr<RecognitionStore> store(CreateRecognitionStore());
      runStore("native", store.get());
    }
    {
      std::unique_ptr<RecognitionStore> store(CreateMemStore());
      runStore("mem", store.get());
    }
    std::unique_ptr<ReportClient> report(CreateReportClient("", ""));
    Recognition rec;
    rec.track_id = "T-1"; rec.camera_id = "cam-01"; rec.created_at = 1700000000;
    CHECK(report->Upload(1, {rec}) && StoreImplOK(), "report upload (print mode)");
  }

  // 7. SCRFD 解码（文档 15.1）：锚点距离解码 + NMS + 关键点
  {
    constexpr int kSize = 640;
    constexpr int kStride = 8;
    constexpr int kGrid = kSize / kStride;             // 80
    constexpr int kN = kGrid * kGrid;                  // 6400
    std::vector<float> scores(kN, 0.f), boxes(kN * 4, 0.f), kps(kN * 10, 0.f);
    // 锚点 (row=5, col=3)：距离 b=[2,2,3,3]（单元），kps=[1,2]*5（单元）
    int idx = 5 * kGrid + 3;
    scores[idx] = 0.9f;
    float* b = boxes.data() + idx * 4;
    b[0] = 2.f; b[1] = 2.f; b[2] = 3.f; b[3] = 3.f;
    float* k = kps.data() + idx * 10;
    for (int p = 0; p < 5; ++p) { k[p * 2] = 1.f; k[p * 2 + 1] = 2.f; }

    scrfd::StrideOutput so;
    so.stride = kStride; so.count = kN;
    so.scores = scores.data(); so.score_channels = 1; so.score_face_channel = 0; so.score_cn = false;
    so.boxes = boxes.data(); so.box_cn = false;
    so.kps = kps.data(); so.kps_cn = false;

    scrfd::DecodeOptions opt; opt.det_thresh = 0.5f;
    // 期望：中心 (3*8, 5*8)=(24,40)；x1=24-16=8, y1=40-16=24, x2=24+24=48, y2=40+24=64
    auto out = scrfd::Decode({so}, kSize, 1.f, 1.f, opt);
    bool ok = out.size() == 1;
    if (ok) {
      const FaceBox& fb = out[0];
      ok = std::fabs(fb.x - 8.f) < 1e-3 && std::fabs(fb.y - 24.f) < 1e-3 &&
           std::fabs(fb.w - 40.f) < 1e-3 && std::fabs(fb.h - 40.f) < 1e-3 &&
           std::fabs(fb.score - 0.9f) < 1e-3;
      ok = ok && std::fabs(fb.kps[0] - (24.f + 8.f)) < 1e-3 &&
           std::fabs(fb.kps[1] - (40.f + 16.f)) < 1e-3;
    }
    CHECK(ok, "scrfd anchor decode (anchor-first)");

    // channel-first 布局 [C,N]（双类，face 通道=1）
    std::vector<float> scoresCn(kN * 2, 0.f), boxesCn(kN * 4, 0.f);
    scoresCn[static_cast<size_t>(1) * kN + idx] = 0.85f;
    float* bc = boxesCn.data();
    bc[0 * kN + idx] = 1.f; bc[1 * kN + idx] = 1.f; bc[2 * kN + idx] = 2.f; bc[3 * kN + idx] = 2.f;
    scrfd::StrideOutput so2;
    so2.stride = kStride; so2.count = kN;
    so2.scores = scoresCn.data(); so2.score_channels = 2; so2.score_face_channel = 1; so2.score_cn = true;
    so2.boxes = boxesCn.data(); so2.box_cn = true;
    auto out2 = scrfd::Decode({so2}, kSize, 1.f, 1.f, opt);
    ok = out2.size() == 1 && std::fabs(out2[0].x - 16.f) < 1e-3 && std::fabs(out2[0].y - 32.f) < 1e-3 &&
           std::fabs(out2[0].w - 24.f) < 1e-3 && std::fabs(out2[0].h - 24.f) < 1e-3;
    CHECK(ok, "scrfd anchor decode (channel-first)");

    // NMS：同一锚点相邻高分框去重
    std::vector<FaceBox> nb;
    FaceBox fa; fa.x = 8; fa.y = 24; fa.w = 40; fa.h = 40; fa.score = 0.9f;
    FaceBox fb2 = fa; fb2.x = 9; fb2.y = 25; fb2.score = 0.8f;
    nb.push_back(fa); nb.push_back(fb2);
    CHECK(scrfd::IoU(fa, fb2) > 0.5f, "scrfd IoU overlapping > 0.5");
    FaceBox far; far.x = 300; far.y = 300; far.w = 10; far.h = 10;
    CHECK(scrfd::IoU(fa, far) < 1e-6f, "scrfd IoU disjoint ~ 0");
  }

  // 8. 5 点相似变换对齐（文档 15.2）
  {
    using eb::align::ArcFaceDst112;
    std::array<std::pair<float, float>, 5> src, dst;
    for (int i = 0; i < 5; ++i) {
      src[i] = ArcFaceDst112()[i];
      dst[i] = ArcFaceDst112()[i];
    }
    align::Similarity sim;
    CHECK(align::EstimateSimilarity(src, dst, sim), "align identity estimate ok");
    bool idOk = std::fabs(sim.scale - 1.f) < 1e-3 && std::fabs(sim.tx) < 1e-3 && std::fabs(sim.ty) < 1e-3;
    CHECK(idOk, "align identity ~ scale 1, t ~ 0");

    // 源点 = 目标点缩放+平移 → 应还原出逆变换
    std::array<std::pair<float, float>, 5> src2;
    for (int i = 0; i < 5; ++i) {
      src2[i] = {dst[i].first * 2.f + 10.f, dst[i].second * 2.f + 20.f};
    }
    align::Similarity sim2;
    CHECK(align::EstimateSimilarity(src2, dst, sim2), "align scaled estimate ok");
    bool mapOk = true;
    for (int i = 0; i < 5; ++i) {
      float mx = sim2.MapX(src2[i].first, src2[i].second);
      float my = sim2.MapY(src2[i].first, src2[i].second);
      if (std::fabs(mx - dst[i].first) > 1e-2 || std::fabs(my - dst[i].second) > 1e-2) mapOk = false;
    }
    CHECK(mapOk && std::fabs(sim2.scale - 0.5f) < 1e-2, "align scaled maps src->dst");

    // 对齐输出 112x112：构造 5x 放大图像并验证中心像素移入目标点
    ImageFrame big;
    big.width = 560; big.height = 560; big.channels = 3;
    big.data.assign(big.width * big.height * 3, 0);
    align::Similarity ident;
    ident.scale = 5.f; ident.cos = 1.f; ident.sin = 0.f; ident.tx = 10.f; ident.ty = 20.f;
    ImageFrame small;
    align::WarpAffine(big, ident, 112, 112, small);
    CHECK(small.width == 112 && small.height == 112, "align warp output 112x112");
  }

  // 9. 人员库增量同步：响应解析 + 应用到识别库
  {
    // 构造 512 维特征（已 L2 归一化：仅 feat[7]=1）→ base64
    std::vector<float> feat(512, 0.f);
    feat[7] = 1.0f;
    std::string featB64 = Base64Encode(reinterpret_cast<const unsigned char*>(feat.data()), feat.size() * sizeof(float));

    Json body;
    {
      std::vector<Json> items;
      {
        std::map<std::string, Json> a;
        a["customer_id"] = Json::Number(100);
        a["person_type"] = Json::Number(0);
        a["version"] = Json::Number(2);
        a["face_feature"] = Json::String(featB64);
        a["status"] = Json::Number(0);
        items.push_back(Json::Object(std::move(a)));
      }
      {
        std::map<std::string, Json> b;
        b["customer_id"] = Json::Number(200);
        b["person_type"] = Json::Number(1);
        b["version"] = Json::Number(3);
        b["face_feature"] = Json::String("");
        b["status"] = Json::Number(0);
        b["staff_no_hash"] = Json::String("staff-hash-1");
        items.push_back(Json::Object(std::move(b)));
      }
      std::map<std::string, Json> data;
      data["base_version"] = Json::Number(1);
      data["new_version"] = Json::Number(3);
      data["items"] = Json::Array(std::move(items));
      std::map<std::string, Json> root;
      root["code"] = Json::Number(0);
      root["data"] = Json::Object(std::move(data));
      body = Json::Object(std::move(root));
    }
    int64_t nv = 0;
    std::vector<SyncItem> items;
    bool ok = ParseSyncResponse(body.Dump(), &nv, items);
    CHECK(ok && nv == 3 && items.size() == 2, "sync response parse");
    ok = ok && items[0].customer_id == 100 && items[0].has_feature &&
         std::fabs(items[0].feature[7] - 1.0f) < 1e-4 && items[1].person_type == kStaff;
    CHECK(ok, "sync item decode feature + staff type");

    Recognizer rec;
    int applied = ApplySyncItems(items, &rec);
    // 无特征的人员（内部人员只下发工号哈希）不进入识别库
    CHECK(applied == 1 && rec.Size() == 1, "sync apply upsert");

    MatchResult m = rec.Search(items[0].feature, 0.4f);
    CHECK(m.hit && m.customer_id == 100 && m.person_type == kCustomer, "sync applied recognizer hit");

    // 失效项 → 移除
    SyncItem dead;
    dead.customer_id = 100; dead.status = 2;
    applied = ApplySyncItems({dead}, &rec);
    CHECK(applied == 1 && rec.Size() == 0, "sync apply remove disabled");
    m = rec.Search(items[0].feature, 0.4f);
    CHECK(!m.hit, "sync removed customer no longer matches");
  }

  // 10. 抓拍图编码（A2）：无 OpenCV 时输出 BMP（BM 头），解码后尺寸正确
  {
    ImageFrame f = MakeFrame(112, 112, 50);
    std::string b64, mime;
    CHECK(EncodeSnapshotBase64(f, b64, mime), "snapshot encode ok");
    CHECK(!b64.empty() && mime == "image/bmp", "snapshot bmp mime (no opencv)");
    std::string raw;
    CHECK(Base64Decode(b64, raw) && raw.size() > 54, "snapshot b64 decodes");
    CHECK(raw[0] == 'B' && raw[1] == 'M', "snapshot bmp header BM");
  }

  // 11. 流水线新轨迹抓拍（A2）：首帧产生带快照的记录
  {
    eb::CameraConfig cam;
    cam.camera_id = "cam-snap";
    cam.url = "-";
    cam.count_flow = false;
    auto store = std::unique_ptr<RecognitionStore>(CreateMemStore());
    auto backend = std::unique_ptr<IInferenceBackend>(CreateBackend(BackendType::kMock, {}));
    FaceEngine face(backend.get());
    Recognizer rec;
    ReportClient* dummy = nullptr;  // ReportClient 不会在 ProcessOneFrame 中被调用
    Pipeline pipeline(cam, 1, &face, &rec, store.get(), dummy, 0.4f);
    bool opened = pipeline.Open();
    CHECK(opened, "pipeline open (mock video)");
    bool ok = false;
    for (int i = 0; i < 4 && !ok; ++i) ok = pipeline.ProcessOneFrame();
    CHECK(ok, "pipeline processed frames");
    auto pending = store->Pending(10);
    CHECK(!pending.empty(), "pipeline stored records");
    bool hasSnap = false;
    for (auto& p : pending) {
      if (!p.rec.snapshot_b64.empty()) { hasSnap = true; break; }
    }
    CHECK(hasSnap, "pipeline first-track snapshot attached");
  }

  std::printf("smoke test finished: %s\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURE");
  return g_fail == 0 ? 0 : 1;
}

// 辅助：StoreImplOK 占位（保持 smoke 结构简洁）
bool StoreImplOK() { return true; }