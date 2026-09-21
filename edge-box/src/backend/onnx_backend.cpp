// edge-box/src/backend/onnx_backend.cpp
// ONNXRuntime 后端（HAVE_ONNXRUNTIME 条件编译）：SCRFD 检测 + ArcFace 特征 + RGB 活体
// 需要：onnxruntime_cxx_api.h / onnxruntime(.so|.dll)
// 说明：模型输入输出名通过 Session 动态读取，兼容多数导出版本；
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

// ---------- NMS ----------
std::vector<int> Nms(const std::vector<FaceBox>& boxes, float iou_thresh) {
  std::vector<int> order(boxes.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) { return boxes[a].score > boxes[b].score; });

  std::vector<int> keep;
  std::vector<char> removed(boxes.size(), 0);
  for (int idx : order) {
    if (removed[idx]) continue;
    keep.push_back(idx);
    const FaceBox& a = boxes[idx];
    for (int j : order) {
      if (removed[j]) continue;
      const FaceBox& b = boxes[j];
      float ix = std::max(0.f, std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x));
      float iy = std::max(0.f, std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y));
      float inter = ix * iy;
      float uni = a.w * a.h + b.w * b.h - inter + 1e-6f;
      if (inter / uni > iou_thresh) removed[j] = 1;
    }
  }
  return keep;
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

// 找包含关键字的输出名的索引（找不到返回 -1）
int FindIndex(const std::vector<std::string>& names, const char* key) {
  for (size_t i = 0; i < names.size(); ++i) {
    if (names[i].find(key) != std::string::npos) return static_cast<int>(i);
  }
  return -1;
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
      // 空输入跑一次触发引擎/线程初始化（结果丢弃）
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
    constexpr int kDetSize = 640;  // SCRFD 常用输入尺寸（模型不符可改或按输入 shape 自适应）
    std::vector<std::string> inNames = GetInputNames(*det_, alloc_);
    std::vector<std::string> outNames = GetOutputNames(*det_, alloc_);
    if (inNames.empty() || outNames.empty()) return false;

    std::vector<float> input;
    BgrToPlanar(frame, kDetSize, kDetSize, /*swapRgb=*/false, /*norm01=*/true, input);
    std::vector<int64_t> shape{1, 3, kDetSize, kDetSize};

    const char* inName = inNames[0].c_str();
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    // 名称生命周期：GetInputNames 返回的 vector 在 inNames 存活期间有效，但 c_str 指针指向其元素；
    // 此处立即使用，OK。
    Ort::Value inTensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());

    std::vector<const char*> inNamePtrs{inName};
    std::vector<const char*> outNamePtrs;
    for (auto& n : outNames) outNamePtrs.push_back(n.c_str());

    auto outputs = det_->Run(Ort::RunOptions{nullptr}, inNamePtrs.data(), &inTensor, 1,
                             outNamePtrs.data(), outNamePtrs.size());

    // SCRFD 输出约定：scores / bboxes / kps（不同版本命名 score_1 / bbox_1 / kps_1 等）
    int scoreIdx = FindIndex(outNames, "score");
    int boxIdx = FindIndex(outNames, "bbox");
    int kpsIdx = FindIndex(outNames, "kps");
    if (scoreIdx < 0 || boxIdx < 0) return false;
    if (scoreIdx < 0 || boxIdx < 0 || static_cast<size_t>(scoreIdx) >= outputs.size() ||
        static_cast<size_t>(boxIdx) >= outputs.size()) {
      return false;
    }

    const auto& scoreInfo = outputs[scoreIdx].GetTensorTypeAndShapeInfo();
    const auto& boxInfo = outputs[boxIdx].GetTensorTypeAndShapeInfo();
    int64_t numAnchors = (scoreInfo.GetShape().size() >= 2) ? scoreInfo.GetShape()[1] : scoreInfo.GetElementCount();
    const float* scores = outputs[scoreIdx].GetTensorData<float>();
    const float* boxes = outputs[boxIdx].GetTensorData<float>();

    // 缩放回原图
    const float sx = static_cast<float>(frame.width) / kDetSize;
    const float sy = static_cast<float>(frame.height) / kDetSize;
    const float* kps = nullptr;
    if (kpsIdx >= 0 && static_cast<size_t>(kpsIdx) < outputs.size()) {
      kps = outputs[kpsIdx].GetTensorData<float>();
    }

    for (int64_t i = 0; i < numAnchors; ++i) {
      float sc = scores[i];  // 单类输出；若为 [N,2]（face/背景）则取第 1 列
      if (sc < det_thresh) continue;
      FaceBox b;
      // 多数导出为 [x1,y1,x2,y2]（输入图坐标系）；若为 [cx,cy,w,h] 需按注释切换
      b.x = boxes[i * 4 + 0] * sx;
      b.y = boxes[i * 4 + 1] * sy;
      b.w = (boxes[i * 4 + 2] - boxes[i * 4 + 0]) * sx;
      b.h = (boxes[i * 4 + 3] - boxes[i * 4 + 1]) * sy;
      b.score = sc;
      // 关键点（若有）：10 个值 [x*5,y*5]
      if (kps) {
        for (int k = 0; k < 5; ++k) {
          b.kps[k * 2] = kps[i * 10 + k * 2] * sx;
          b.kps[k * 2 + 1] = kps[i * 10 + k * 2 + 1] * sy;
        }
      }
      // 越界保护
      b.x = ClampF(b.x, 0, static_cast<float>(frame.width - 1));
      b.y = ClampF(b.y, 0, static_cast<float>(frame.height - 1));
      b.w = ClampF(b.w, 1, static_cast<float>(frame.width) - b.x);
      b.h = ClampF(b.h, 1, static_cast<float>(frame.height) - b.y);
      out.push_back(b);
      if (out.size() >= 64) break;  // 单帧上限保护
    }
    std::vector<int> keep = Nms(out, 0.4f);
    std::vector<FaceBox> filtered;
    filtered.reserve(keep.size());
    for (int idx : keep) filtered.push_back(out[idx]);
    out.swap(filtered);
    return true;
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