// enrollment-pc/src/feature/ort_feature_extractor.cpp
// ONNXRuntime 特征提取实现。算法移植自 edge-box/src/backend/onnx_backend.cpp
// （SCRFD anchor 解码 / ArcFace 112 预处理 / RGB 活体 sigmoid），
// 保证与边缘盒处于同一 512 维人脸向量空间。
#include "feature/ort_feature_extractor.h"

#if defined(HAVE_ONNXRUNTIME)

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "feature/align.h"

namespace enroll {

bool OrtFeatureExtractorAvailable() { return true; }

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
  if (frame.width <= 0 || frame.height <= 0 || frame.channels < 1) return;
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
  bool cn = true;
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
  bool cn = true;
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
  if (s.size() == 2 && s[1] == expectK) { out.n = 1; out.cn = false; out.ok = true; }
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
  offsets.push_back(off);
  return true;
}

// 锚点解码（与 edge-box scrfd_decode 一致）：偏移量按 stride 网格回归
void DecodeStride(int stride, int input_size, const float* scores, int channels, int face_ch,
                  bool score_cn, int64_t score_n, const float* boxes, bool box_cn,
                  const float* kps, bool kps_cn, float det_thresh, int max_face_num,
                  std::vector<FaceBox>& out, int gridW, int gridH) {
  auto ScoreAt = [&](int64_t i) {
    return score_cn ? scores[static_cast<size_t>(face_ch) * score_n + i]
                    : scores[static_cast<size_t>(i) * channels + face_ch];
  };
  auto BoxAt = [&](int64_t i, int k) {
    return box_cn ? boxes[static_cast<size_t>(k) * score_n + i]
                  : boxes[static_cast<size_t>(i) * 4 + k];
  };
  int idx = 0;
  for (int gy = 0; gy < gridH && static_cast<int>(out.size()) < max_face_num; ++gy) {
    for (int gx = 0; gx < gridW && static_cast<int>(out.size()) < max_face_num; ++gx, ++idx) {
      float score = ScoreAt(idx);
      if (score < det_thresh) continue;
      // SCRFD 回归：中心/宽高按 stride 缩放
      float cx = (gx + BoxAt(idx, 0)) * stride;
      float cy = (gy + BoxAt(idx, 1)) * stride;
      float w = BoxAt(idx, 2) * stride;
      float h = BoxAt(idx, 3) * stride;
      FaceBox fb;
      fb.x = cx - w * 0.5f;
      fb.y = cy - h * 0.5f;
      fb.w = w;
      fb.h = h;
      fb.score = score;
      if (kps) {
        for (int k = 0; k < 5; ++k) {
          float kx = (gx + (kps_cn ? kps[static_cast<size_t>(2 * k) * score_n + idx]
                                   : kps[static_cast<size_t>(idx) * 10 + 2 * k])) * stride;
          float ky = (gy + (kps_cn ? kps[static_cast<size_t>(2 * k + 1) * score_n + idx]
                                   : kps[static_cast<size_t>(idx) * 10 + 2 * k + 1])) * stride;
          fb.kps[static_cast<size_t>(2 * k)] = kx;
          fb.kps[static_cast<size_t>(2 * k + 1)] = ky;
        }
      }
      out.push_back(fb);
    }
  }
}

float IoU(const FaceBox& a, const FaceBox& b) {
  float ax2 = a.x + a.w, ay2 = a.y + a.h, bx2 = b.x + b.w, by2 = b.y + b.h;
  float ix = std::max(0.f, std::min(ax2, bx2) - std::max(a.x, b.x));
  float iy = std::max(0.f, std::min(ay2, by2) - std::max(a.y, b.y));
  float inter = ix * iy;
  float uni = a.w * a.h + b.w * b.h - inter;
  return uni > 0 ? inter / uni : 0.f;
}

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
      if (IoU(boxes[idx], boxes[j]) > iou_thresh) removed[j] = 1;
    }
  }
  return keep;
}

}  // namespace

// ---------- 实现（pimpl） ----------
struct OrtFeatureExtractor::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING};
  Ort::SessionOptions sess;
  Ort::AllocatorWithDefaultOptions alloc;
  std::unique_ptr<Ort::Session> det, rec, liv;

  // 检测并过滤人脸框（输入帧坐标）
  bool Detect(const ImageFrame& frame, float det_thresh, std::vector<FaceBox>& out) {
    if (!det) return false;
    out.clear();
    constexpr int kDetSize = 640;
    std::vector<std::string> inNames = GetInputNames(*det, alloc);
    std::vector<std::string> outNames = GetOutputNames(*det, alloc);
    if (inNames.empty() || outNames.empty()) return false;

    std::vector<float> input;
    BgrToPlanar(frame, kDetSize, kDetSize, /*swapRgb=*/false, /*norm01=*/true, input);
    std::vector<int64_t> shape{1, 3, kDetSize, kDetSize};

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inTensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());

    std::vector<const char*> inNamePtrs{inNames[0].c_str()};
    std::vector<const char*> outNamePtrs;
    for (auto& n : outNames) outNamePtrs.push_back(n.c_str());

    auto outputs = det->Run(Ort::RunOptions{nullptr}, inNamePtrs.data(), &inTensor, 1,
                            outNamePtrs.data(), outNamePtrs.size());

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
    std::vector<FaceBox> raw;

    // ---------- 布局 A：每 stride 一个输出（score_1/bbox_1 数量一致） ----------
    if (boxTs.size() >= 2 && boxTs.size() == scoreTs.size()) {
      for (size_t i = 0; i < boxTs.size(); ++i) {
        BoxLayout bl = ParseBoxLayout(boxTs[i], 4);
        ScoreLayout sl = ParseScoreLayout(scoreTs[i]);
        if (!bl.ok || !sl.ok || bl.n != sl.n) { raw.clear(); break; }
        int grid = static_cast<int>(std::sqrt(static_cast<double>(bl.n)));
        if (grid * grid != bl.n || kDetSize % grid != 0) { raw.clear(); break; }
        int stride = kDetSize / grid;
        const float* kps = nullptr;
        bool kps_cn = true;
        for (const OrtTensor& kt : kpsTs) {
          BoxLayout kl = ParseBoxLayout(kt, 10);
          if (kl.ok && kl.n == bl.n) { kps = kt.data; kps_cn = kl.cn; break; }
        }
        DecodeStride(stride, kDetSize, scoreTs[i].data, sl.channels,
                     (sl.channels >= 2) ? 1 : 0, sl.cn, sl.n, boxTs[i].data, bl.cn,
                     kps, kps_cn, det_thresh, 64, raw, grid, grid);
      }
    }

    // ---------- 布局 B：拼接单输出（锚点 stride 8→16→32 顺序） ----------
    if (raw.empty() && boxTs.size() == 1 && scoreTs.size() == 1) {
      BoxLayout bl = ParseBoxLayout(boxTs[0], 4);
      ScoreLayout sl = ParseScoreLayout(scoreTs[0]);
      if (bl.ok && sl.ok && bl.n == sl.n) {
        std::vector<int> strList; std::vector<int64_t> offs;
        if (SplitConcat(bl.n, kDetSize, strList, offs)) {
          const float* kps = nullptr;
          bool kps_cn = true;
          for (const OrtTensor& kt : kpsTs) {
            BoxLayout kl = ParseBoxLayout(kt, 10);
            if (kl.ok && kl.n == bl.n) { kps = kt.data; kps_cn = kl.cn; break; }
          }
          for (size_t i = 0; i < strList.size(); ++i) {
            int stride = strList[i];
            int64_t off = offs[i];
            int64_t count = offs[i + 1] - offs[i];
            int grid = kDetSize / stride;
            // 指针偏移需随布局调整：
            //   [C,N]（channel-major）：锚点维连续，+off
            //   [N,C]（anchor-first）：每锚点 C 个浮点连续，+off*C
            const float* scores = sl.cn ? scoreTs[0].data + off
                                        : scoreTs[0].data + off * sl.channels;
            const float* boxes = bl.cn ? boxTs[0].data + off
                                       : boxTs[0].data + off * 4;
            const float* kpsPtr = kps ? (kps_cn ? kps + off : kps + off * 10) : nullptr;
            DecodeStride(stride, kDetSize,
                         scores, sl.channels,
                         (sl.channels >= 2) ? 1 : 0, sl.cn, sl.n,
                         boxes, bl.cn,
                         kpsPtr, kps_cn,
                         det_thresh, 64, raw, grid, grid);
          }
        }
      }
    }

    // ---------- 回退：绝对坐标框（旧布局 / yolo 类导出） ----------
    if (raw.empty() && boxTs.size() == 1 && scoreTs.size() == 1) {
      BoxLayout bl = ParseBoxLayout(boxTs[0], 4);
      ScoreLayout sl = ParseScoreLayout(scoreTs[0]);
      if (bl.ok && sl.ok && bl.n == sl.n && !bl.cn && !sl.cn) {
        const float* bd = boxTs[0].data;
        const float* sd = scoreTs[0].data;
        const int fc = (sl.channels >= 2) ? 1 : 0;
        for (int64_t i = 0; i < bl.n && static_cast<int>(raw.size()) < 64; ++i) {
          float score = sd[static_cast<size_t>(i) * sl.channels + fc];
          if (score < det_thresh) continue;
          float x1 = bd[static_cast<size_t>(i) * 4 + 0] * sx;
          float y1 = bd[static_cast<size_t>(i) * 4 + 1] * sy;
          float x2 = bd[static_cast<size_t>(i) * 4 + 2] * sx;
          float y2 = bd[static_cast<size_t>(i) * 4 + 3] * sy;
          if (!(x2 > x1 && y2 > y1)) continue;
          FaceBox fb;
          fb.x = ClampF(x1, 0, static_cast<float>(frame.width - 1));
          fb.y = ClampF(y1, 0, static_cast<float>(frame.height - 1));
          fb.w = std::min(x2 - x1, static_cast<float>(frame.width) - fb.x);
          fb.h = std::min(y2 - y1, static_cast<float>(frame.height) - fb.y);
          fb.score = score;
          raw.push_back(fb);
        }
      }
    }

    if (raw.empty()) return false;
    auto keep = NmsFiltered(raw, 0.4f);
    for (int idx : keep) out.push_back(raw[idx]);
    return !out.empty();
  }

  bool ExtractAlignedImpl(const ImageFrame& aligned_face, Feature& out) {
    if (!rec) return false;
    constexpr int kSize = 112;
    std::vector<std::string> inNames = GetInputNames(*rec, alloc);
    std::vector<std::string> outNames = GetOutputNames(*rec, alloc);
    if (inNames.empty() || outNames.empty()) return false;

    std::vector<float> input;
    // ArcFace 标准预处理：BGR→RGB，(v-127.5)/128
    BgrToPlanar(aligned_face, kSize, kSize, /*swapRgb=*/true, /*norm01=*/false, input);
    std::vector<int64_t> shape{1, 3, kSize, kSize};
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inTensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());
    const char* inName = inNames[0].c_str();
    const char* outName = outNames[0].c_str();
    auto outputs = rec->Run(Ort::RunOptions{nullptr}, &inName, &inTensor, 1, &outName, 1);
    if (outputs.empty()) return false;

    const float* data = outputs[0].GetTensorData<float>();
    size_t count = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
    if (count < kFeatureDim) return false;
    std::vector<float> emb(data, data + kFeatureDim);
    NormalizeL2(emb);
    std::copy(emb.begin(), emb.end(), out.begin());
    return true;
  }
};

OrtFeatureExtractor::OrtFeatureExtractor() : impl_(new Impl()) {}
OrtFeatureExtractor::~OrtFeatureExtractor() { delete impl_; }

bool OrtFeatureExtractor::LoadModel(const ModelPaths& paths) {
  try {
    if (!paths.det.empty()) impl_->det = std::make_unique<Ort::Session>(impl_->env, paths.det.c_str(), impl_->sess);
    if (!paths.rec.empty()) impl_->rec = std::make_unique<Ort::Session>(impl_->env, paths.rec.c_str(), impl_->sess);
    if (!paths.liveness.empty()) impl_->liv = std::make_unique<Ort::Session>(impl_->env, paths.liveness.c_str(), impl_->sess);
  } catch (...) { return false; }
  if (!impl_->rec) return false;  // 特征模型必填
  // 预热（空跑，失败不致命）
  try {
    ImageFrame dummy;
    dummy.width = 64; dummy.height = 64; dummy.channels = 3;
    dummy.data.assign(64 * 64 * 3, 0);
    Feature f{};
    if (impl_->det) { std::vector<FaceBox> boxes; impl_->Detect(dummy, 0.5f, boxes); }
    impl_->ExtractAlignedImpl(dummy, f);
  } catch (...) {}
  return true;
}

bool OrtFeatureExtractor::DetectAlignExtract(const ImageFrame& frame, Feature& out) {
  std::vector<FaceBox> boxes;
  if (impl_->det && impl_->Detect(frame, 0.5f, boxes)) {
    // 取最高分人脸，用 5 点关键点对齐到 ArcFace 112x112
    const FaceBox& fb = boxes[0];
    bool hasKps = fb.kps[0] != 0.f || fb.kps[1] != 0.f ||
                  fb.kps[2] != 0.f || fb.kps[3] != 0.f ||
                  fb.kps[4] != 0.f || fb.kps[5] != 0.f ||
                  fb.kps[6] != 0.f || fb.kps[7] != 0.f ||
                  fb.kps[8] != 0.f || fb.kps[9] != 0.f;
    if (hasKps) {
      std::array<std::pair<float, float>, 5> src;
      for (int i = 0; i < 5; ++i) {
        src[static_cast<size_t>(i)] = {fb.kps[static_cast<size_t>(2 * i)],
                                       fb.kps[static_cast<size_t>(2 * i + 1)]};
      }
      align::Similarity sim;
      if (align::EstimateSimilarity(src, align::ArcFaceDst112(), sim)) {
        ImageFrame aligned;
        align::WarpAffine(frame, sim, 112, 112, aligned);
        return ExtractAligned(aligned, out);
      }
    }
    // 无关键点/对齐失败：按人脸框中心裁剪缩放
    ImageFrame crop;
    crop.width = static_cast<int>(fb.w); crop.height = static_cast<int>(fb.h);
    crop.channels = frame.channels;
    crop.data.assign(static_cast<size_t>(crop.width) * crop.height * crop.channels, 0);
    int srcX = std::max(0, static_cast<int>(fb.x));
    int srcY = std::max(0, static_cast<int>(fb.y));
    for (int y = 0; y < crop.height && srcY + y < frame.height; ++y) {
      for (int x = 0; x < crop.width && srcX + x < frame.width; ++x) {
        std::memcpy(crop.Ptr(y, x), frame.Ptr(srcY + y, srcX + x),
                    static_cast<size_t>(frame.channels));
      }
    }
    return ExtractAligned(crop, out);
  }
  // 无检测模型：整图缩放对齐后提特征（证件照/已正脸场景）
  return ExtractAligned(frame, out);
}

bool OrtFeatureExtractor::ExtractAligned(const ImageFrame& aligned_face, Feature& out) {
  return impl_->ExtractAlignedImpl(aligned_face, out);
}

float OrtFeatureExtractor::Liveness(const ImageFrame& aligned_face) {
  if (!impl_->liv) return 1.0f;  // 无活体模型视为不检测
  constexpr int kSize = 112;
  std::vector<std::string> inNames = GetInputNames(*impl_->liv, impl_->alloc);
  std::vector<std::string> outNames = GetOutputNames(*impl_->liv, impl_->alloc);
  if (inNames.empty() || outNames.empty()) return 1.0f;

  std::vector<float> input;
  BgrToPlanar(aligned_face, kSize, kSize, /*swapRgb=*/true, /*norm01=*/true, input);
  std::vector<int64_t> shape{1, 3, kSize, kSize};
  Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  Ort::Value inTensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());
  const char* inName = inNames[0].c_str();
  const char* outName = outNames[0].c_str();
  auto outputs = impl_->liv->Run(Ort::RunOptions{nullptr}, &inName, &inTensor, 1, &outName, 1);
  if (outputs.empty()) return 0.5f;
  const float* data = outputs[0].GetTensorData<float>();
  float v = data[0];
  if (v < 0 || v > 1) v = 1.0f / (1.0f + std::exp(-v));  // logits → sigmoid
  return ClampF(v, 0.f, 1.f);
}

}  // namespace enroll

#else  // !HAVE_ONNXRUNTIME

#include <utility>

namespace enroll {

bool OrtFeatureExtractorAvailable() { return false; }

struct OrtFeatureExtractor::Impl {};

OrtFeatureExtractor::OrtFeatureExtractor() : impl_(nullptr) {}
OrtFeatureExtractor::~OrtFeatureExtractor() = default;

bool OrtFeatureExtractor::LoadModel(const ModelPaths&) { return false; }
bool OrtFeatureExtractor::DetectAlignExtract(const ImageFrame&, Feature&) { return false; }
bool OrtFeatureExtractor::ExtractAligned(const ImageFrame&, Feature&) { return false; }
float OrtFeatureExtractor::Liveness(const ImageFrame&) { return 1.0f; }

}  // namespace enroll

#endif  // HAVE_ONNXRUNTIME