// edge-box/src/backend/onnx_backend.cpp
// ONNXRuntime 后端（HAVE_ONNXRUNTIME 条件编译）：SCRFD 检测 + ArcFace 特征 + RGB 活体
// 需要：onnxruntime_cxx_api.h / onnxruntime(.so|.dll)
// 说明：模型输入输出名通过 Session 动态读取，兼容多数导出版本；
//       SCRFD 输出按 insightface scrfd.py 原版 anchor 解码（文档 15.1）：
//       - 布局 A：每个 stride 一个输出（score_1/bbox_1/kps_1 等，标准导出）
//       - 布局 B：拼接单输出（锚点按 stride 8→16→32 顺序，anchor-first 排布）
//       - 回退：无法识别时按绝对坐标框解码（旧布局 / yolo 类导出）
//       模型训练/导出约定见 README（Detect 输入 640x640，Extract 输入 112x112）。
#include "backend/inference_backend.h"

#if defined(HAVE_ONNXRUNTIME)

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

#include "backend/scrfd_decode.h"

namespace eb {

namespace {

// ---------- 工具 ----------
float ClampF(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

void NormalizeL2(std::vector<float>& v) {
  double norm = 0;
  for (float x : v) norm += static_cast<double>(x) * x;
  norm = std::sqrt(norm);
  if (norm > 1e-9) {
    for (float& x : v) x = static_cast<float>(x / norm);
  }
}

// 双线性缩放采样（BGR frame → [3,H,W] float，channel-first，可选 RGB 交换）
void BgrToPlanar(const ImageFrame& frame, int dstW, int dstH, bool swapRgb, bool norm01,
                 std::vector<float>& out) {
  out.assign(static_cast<size_t>(3) * dstW * dstH, 0.f);
  const int ch = frame.channels;
  for (int y = 0; y < dstH; ++y) {
    int sy = std::min(frame.height - 1, (y * frame.height) / dstH);
    for (int x = 0; x < dstW; ++x) {
      int sx = std::min(frame.width - 1, (x * frame.width) / dstW);
      const uint8_t* p = frame.Ptr(sy, sx);
      for (int c = 0; c < 3; ++c) {
        int dstC = swapRgb ? (2 - c) : c;  // BGR(0,1,2) → RGB(2,1,0)
        uint8_t v = (ch >= 3) ? p[c] : p[0];
        float f = norm01 ? v / 255.0f : (static_cast<float>(v) - 127.5f) / 128.0f;
        out[static_cast<size_t>(dstC) * dstW * dstH + y * dstW + x] = f;
      }
    }
  }
}

// ---------- Session 包装 ----------
std::vector<std::string> GetInputNames(Ort::Session& s, Ort::AllocatorWithDefaultOptions& a) {
  std::vector<std::string> names;
  for (size_t i = 0; i < s.GetInputCount(); ++i) {
    auto name = s.GetInputNameAllocated(i, a);
    names.emplace_back(name.get());
  }
  return names;
}
std::vector<std::string> GetOutputNames(Ort::Session& s, Ort::AllocatorWithDefaultOptions& a) {
  std::vector<std::string> names;
  for (size_t i = 0; i < s.GetOutputCount(); ++i) {
    auto name = s.GetOutputNameAllocated(i, a);
    names.emplace_back(name.get());
  }
  return names;
}

bool NameHas(const std::string& n, const char* key) { return n.find(key) != std::string::npos; }

struct OrtTensor {
  const float* data = nullptr;
  std::vector<int64_t> shape;
  int64_t count = 0;
};

// 分数张量布局：[1,C,N] / [1,N,C] / [1,N]
struct ScoreLayout {
  int channels = 1;
  int64_t n = 0;
  bool cn = true;  // true: [1,C,N]，false: [1,N,C]
  bool ok = false;
};
ScoreLayout ParseScoreLayout(const OrtTensor& t) {
  ScoreLayout out;
  const auto& s = t.shape;
  if (s.size() == 3) {
    int64_t d1 = s[1], d2 = s[2];
    if (d1 <= 4 && d2 > d1) { out.channels = static_cast<int>(d1); out.n = d2; out.cn = true; out.ok = true; }
    else if (d2 <= 4 && d1 > d2) { out.channels = static_cast<int>(d2); out.n = d1; out.cn = false; out.ok = true; }
    else if (d1 == 1 && d2 == 1) { out.channels = 1; out.n = 1; out.cn = true; out.ok = true; }
    return out;
  }
  if (s.size() == 2) { out.channels = 1; out.n = s[1]; out.cn = true; out.ok = true; }
  return out;
}

// 框/关键点张量布局：[1,k,N] / [1,N,k]（k=4 或 10）
struct BoxLayout {
  int64_t n = 0;
  int k = 4;
  bool cn = true;  // true: [1,k,N]，false: [1,N,k]
  bool ok = false;
};
BoxLayout ParseBoxLayout(const OrtTensor& t, int expectK) {
  BoxLayout out;
  out.k = expectK;
  const auto& s = t.shape;
  if (s.size() == 3) {
    int64_t d1 = s[1], d2 = s[2];
    if (d1 == expectK) { out.n = d2; out.cn = true; out.ok = true; }
    else if (d2 == expectK) { out.n = d1; out.cn = false; out.ok = true; }
    return out;
  }
  if (s.size() == 2 && s[1] == expectK) { out.n = 1; out.cn = false; out.ok = true; }  // [N,k] 缺 batch
  return out;
}

// 常见步长网格：把 N 拆成 (input/stride)^2 的拼接（stride 8→16→32 顺序）
bool SplitConcat(int64_t n, int input_size, std::vector<int>& strides, std::vector<int64_t>& offsets) {
  static const int kCandidates[] = {8, 16, 32};
  strides.clear(); offsets.clear();
  int64_t off = 0;
  for (int s : kCandidates) {
    int64_t c = static_cast<int64_t>(input_size / s);
    c *= c;
    if (off + c > n) break;
    strides.push_back(s);
    offsets.push_back(off);
    off += c;
  }
  if (strides.empty() || off != n) { strides.clear(); offsets.clear(); return false; }
  offsets.push_back(off);  // 末尾哨兵
  return true;
}

}  // namespace

class OnnxBackend : public IInferenceBackend {
 public:
  explicit OnnxBackend(const ModelPaths& paths) : env_(ORT_LOGGING_LEVEL_WARNING), alloc_() {
    if (!paths.det.empty()) det_ = std::make_unique<Ort::Session>(env_, paths.det.c_str(), sess_);
    if (!paths.rec.empty()) rec_ = std::make_unique<Ort::Session>(env_, paths.rec.c_str(), sess_);
    if (!paths.liveness.empty()) liv_ = std::make_unique<Ort::Session>(env_, paths.liveness.c_str(), sess_);
  }

  bool WarmUp() override {
    if (det_) {
      try {
        ImageFrame dummy;
        dummy.width = 64; dummy.height = 64; dummy.channels = 3;
        dummy.data.assign(64 * 64 * 3, 0);
        std::vector<FaceBox> boxes;
        Detect(dummy, 0.5f, boxes);
      } catch (...) { /* warm-up 失败不致命 */ }
    }
    return det_ || rec_;
  }

  bool Detect(const ImageFrame& frame, float det_thresh, std::vector<FaceBox>& out) override {
    if (!det_) return false;
    out.clear();
    constexpr int kDetSize = 640;  // SCRFD 常用输入尺寸
    std::vector<std::string> inNames = GetInputNames(*det_, alloc_);
    std::vector<std::string> outNames = GetOutputNames(*det_, alloc_);
    if (inNames.empty() || outNames.empty()) return false;

    std::vector<float> input;
    BgrToPlanar(frame, kDetSize, kDetSize, /*swapRgb=*/false, /*norm01=*/true, input);
    std::vector<int64_t> shape{1, 3, kDetSize, kDetSize};

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inTensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());

    std::vector<const char*> inNamePtrs{inNames[0].c_str()};
    std::vector<const char*> outNamePtrs;
    for (auto& n : outNames) outNamePtrs.push_back(n.c_str());

    auto outputs = det_->Run(Ort::RunOptions{nullptr}, inNamePtrs.data(), &inTensor, 1,
                             outNamePtrs.data(), outNamePtrs.size());

    // 按类型分类输出（SCRFD 命名：score/bbox/kps，部分导出带 stride 后缀）
    std::vector<OrtTensor> scoreTs, boxTs, kpsTs;
    for (size_t i = 0; i < outputs.size(); ++i) {
      if (!outputs[i].IsTensor()) continue;
      OrtTensor t;
      t.data = outputs[i].GetTensorData<float>();
      t.shape = outputs[i].GetTensorTypeAndShapeInfo().GetShape();
      t.count = outputs[i].GetTensorTypeAndShapeInfo().GetElementCount();
      const std::string& nm = outNames[i];
      if (NameHas(nm, "score")) scoreTs.push_back(t);
      else if (NameHas(nm, "bbox") || NameHas(nm, "box")) boxTs.push_back(t);
      else if (NameHas(nm, "kps") || NameHas(nm, "landmark")) kpsTs.push_back(t);
    }
    if (boxTs.empty() || scoreTs.empty()) return false;

    const float sx = static_cast<float>(frame.width) / kDetSize;
    const float sy = static_cast<float>(frame.height) / kDetSize;
    scrfd::DecodeOptions dopt;
    dopt.det_thresh = det_thresh;

    std::vector<scrfd::StrideOutput> strides;

    // ---------- 布局 A：每 stride 一个输出（score_1/bbox_1 数量一致） ----------
    if (boxTs.size() >= 2 && boxTs.size() == scoreTs.size()) {
      for (size_t i = 0; i < boxTs.size(); ++i) {
        BoxLayout bl = ParseBoxLayout(boxTs[i], 4);
        ScoreLayout sl = ParseScoreLayout(scoreTs[i]);
        if (!bl.ok || !sl.ok || bl.n != sl.n) { strides.clear(); break; }
        scrfd::StrideOutput so;
        so.stride = scrfd::InferStride(static_cast<int>(bl.n), kDetSize);
        if (so.stride <= 0) { strides.clear(); break; }
        so.count = static_cast<int>(bl.n);
        so.scores = scoreTs[i].data;
        so.score_channels = sl.channels;
        so.score_face_channel = (sl.channels >= 2) ? 1 : 0;
        so.score_cn = sl.cn;
        so.boxes = boxTs[i].data;
        so.box_cn = bl.cn;
        for (const OrtTensor& kt : kpsTs) {
          BoxLayout kl = ParseBoxLayout(kt, 10);
          if (kl.ok && kl.n == bl.n) { so.kps = kt.data; so.kps_cn = kl.cn; break; }
        }
        strides.push_back(so);
      }
    }

    // ---------- 布局 B：拼接单输出（锚点优先排布，stride 8→16→32） ----------
    if (strides.empty() && boxTs.size() == 1 && scoreTs.size() == 1) {
      BoxLayout bl = ParseBoxLayout(boxTs[0], 4);
      ScoreLayout sl = ParseScoreLayout(scoreTs[0]);
      if (bl.ok && sl.ok && bl.n == sl.n && !bl.cn && !sl.cn) {
        std::vector<int> strList; std::vector<int64_t> offs;
        if (SplitConcat(bl.n, kDetSize, strList, offs)) {
          for (size_t i = 0; i < strList.size(); ++i) {
            scrfd::StrideOutput so;
            so.stride = strList[i];
            so.count = static_cast<int>(offs[i + 1] - offs[i]);
            so.scores = scoreTs[0].data + static_cast<size_t>(offs[i]) * sl.channels;
            so.score_channels = sl.channels;
            so.score_face_channel = (sl.channels >= 2) ? 1 : 0;
            so.score_cn = false;
            so.boxes = boxTs[0].data + static_cast<size_t>(offs[i]) * 4;
            so.box_cn = false;
            for (const OrtTensor& kt : kpsTs) {
              BoxLayout kl = ParseBoxLayout(kt, 10);
              if (kl.ok && kl.n == bl.n && !kl.cn) {
                so.kps = kt.data + static_cast<size_t>(offs[i]) * 10;
                so.kps_cn = false;
                break;
              }
            }
            strides.push_back(so);
          }
        }
      }
    }

    if (!strides.empty()) {
      out = scrfd::Decode(strides, kDetSize, sx, sy, dopt);
      return true;
    }

    // ---------- 回退：绝对坐标框（旧布局 / yolo 类导出） ----------
    if (boxTs.size() == 1 && scoreTs.size() == 1) {
      BoxLayout bl = ParseBoxLayout(boxTs[0], 4);
      ScoreLayout sl = ParseScoreLayout(scoreTs[0]);
      if (bl.ok && sl.ok && bl.n == sl.n) {
        const float* bd = boxTs[0].data;
        const float* sd = scoreTs[0].data;
        const int fc = (sl.channels >= 2) ? 1 : 0;
        for (int64_t i = 0; i < bl.n && static_cast<int>(out.size()) < dopt.max_face_num; ++i) {
          float score = sl.cn ? sd[static_cast<size_t>(fc) * bl.n + i]
                              : sd[static_cast<size_t>(i) * sl.channels + fc];
          if (score < det_thresh) continue;
          auto B = [&](int k) { return bl.cn ? bd[static_cast<size_t>(k) * bl.n + i] : bd[static_cast<size_t>(i) * 4 + k]; };
          float x1 = B(0) * sx, y1 = B(1) * sy, x2 = B(2) * sx, y2 = B(3) * sy;
          if (!(x2 > x1 && y2 > y1)) continue;
          FaceBox fb;
          fb.x = ClampF(x1, 0, static_cast<float>(frame.width - 1));
          fb.y = ClampF(y1, 0, static_cast<float>(frame.height - 1));
          fb.w = std::min(x2 - x1, static_cast<float>(frame.width) - fb.x);
          fb.h = std::min(y2 - y1, static_cast<float>(frame.height) - fb.y);
          fb.score = score;
          out.push_back(fb);
        }
        auto keep = NmsFiltered(out, dopt.nms_thresh);
        std::vector<FaceBox> filtered;
        filtered.reserve(keep.size());
        for (int idx : keep) filtered.push_back(out[idx]);
        out.swap(filtered);
        return true;
      }
    }
    return false;
  }

  bool Extract(const ImageFrame& aligned_face, Feature& out) override {
    if (!rec_) return false;
    constexpr int kSize = 112;
    std::vector<std::string> inNames = GetInputNames(*rec_, alloc_);
    std::vector<std::string> outNames = GetOutputNames(*rec_, alloc_);
    if (inNames.empty() || outNames.empty()) return false;

    std::vector<float> input;
    // ArcFace 标准预处理：BGR→RGB，(v-127.5)/128
    BgrToPlanar(aligned_face, kSize, kSize, /*swapRgb=*/true, /*norm01=*/false, input);
    std::vector<int64_t> shape{1, 3, kSize, kSize};

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inTensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());
    const char* inName = inNames[0].c_str();
    const char* outName = outNames[0].c_str();
    auto outputs = rec_->Run(Ort::RunOptions{nullptr}, &inName, &inTensor, 1, &outName, 1);
    if (outputs.empty()) return false;

    const float* data = outputs[0].GetTensorData<float>();
    size_t count = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
    if (count < kFeatureDim) return false;
    std::vector<float> emb(data, data + kFeatureDim);
    NormalizeL2(emb);
    std::copy(emb.begin(), emb.end(), out.begin());
    return true;
  }

  float Liveness(const ImageFrame& aligned_face) override {
    if (!liv_) return 1.0f;  // 无活体模型视为不检测
    constexpr int kSize = 112;
    std::vector<std::string> inNames = GetInputNames(*liv_, alloc_);
    std::vector<std::string> outNames = GetOutputNames(*liv_, alloc_);
    if (inNames.empty() || outNames.empty()) return 1.0f;

    std::vector<float> input;
    BgrToPlanar(aligned_face, kSize, kSize, /*swapRgb=*/true, /*norm01=*/true, input);
    std::vector<int64_t> shape{1, 3, kSize, kSize};
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inTensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());
    const char* inName = inNames[0].c_str();
    const char* outName = outNames[0].c_str();
    auto outputs = liv_->Run(Ort::RunOptions{nullptr}, &inName, &inTensor, 1, &outName, 1);
    if (outputs.empty()) return 0.5f;
    const float* data = outputs[0].GetTensorData<float>();
    // 输出 0~1 视为概率；输出 logits 时做 sigmoid
    float v = data[0];
    if (v < 0 || v > 1) v = 1.0f / (1.0f + std::exp(-v));
    return ClampF(v, 0.f, 1.f);
  }

 private:
  std::vector<int> NmsFiltered(const std::vector<FaceBox>& boxes, float iou_thresh) {
    std::vector<int> order(boxes.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return boxes[a].score > boxes[b].score; });
    std::vector<int> keep;
    std::vector<char> removed(boxes.size(), 0);
    for (int idx : order) {
      if (removed[idx]) continue;
      keep.push_back(idx);
      for (int j : order) {
        if (removed[j]) continue;
        if (scrfd::IoU(boxes[idx], boxes[j]) > iou_thresh) removed[j] = 1;
      }
    }
    return keep;
  }

  Ort::Env env_;
  Ort::SessionOptions sess_;
  Ort::AllocatorWithDefaultOptions alloc_;
  std::unique_ptr<Ort::Session> det_, rec_, liv_;
};

IInferenceBackend* CreateOnnxBackend(const ModelPaths& paths) {
  return new OnnxBackend(paths);
}

}  // namespace eb

#else  // !HAVE_ONNXRUNTIME

namespace eb {
// 未编译 ONNX 时返回 nullptr，交由上层 fallback 到 Mock。
IInferenceBackend* CreateOnnxBackend(const ModelPaths& paths) {
  (void)paths;
  return nullptr;
}
}  // namespace eb

#endif