// edge-box/src/backend/onnx_backend.cpp
// ONNXRuntime 后端（HAVE_ONNXRUNTIME 条件编译）。
// 需要：onnxruntime_cxx_api.h / onnxruntime.dll(.so)
#include "backend/inference_backend.h"

#if defined(HAVE_ONNXRUNTIME)

#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cmath>

namespace eb {

namespace {
void NormalizeL2(Feature& f) {
  double norm = 0;
  for (float v : f) norm += static_cast<double>(v) * v;
  norm = std::sqrt(norm);
  if (norm > 1e-9) {
    for (float& v : f) v = static_cast<float>(v / norm);
  }
}
}  // namespace

class OnnxBackend : public IInferenceBackend {
 public:
  explicit OnnxBackend(const ModelPaths& paths) : env_(ORT_LOGGING_LEVEL_WARNING) {
    if (!paths.det.empty()) det_ = std::make_unique<Ort::Session>(env_, paths.det.c_str(), sess_);
    if (!paths.rec.empty()) rec_ = std::make_unique<Ort::Session>(env_, paths.rec.c_str(), sess_);
    if (!paths.liveness.empty()) liv_ = std::make_unique<Ort::Session>(env_, paths.liveness.c_str(), sess_);
  }
  bool WarmUp() override { return det_ != nullptr || rec_ != nullptr; }

  bool Detect(const ImageFrame& frame, float det_thresh, std::vector<FaceBox>& out) override {
    if (!det_) return false;
    // TODO(impl): 预处理 -> Session.Run -> SCRFD decode(anchor/stride) + NMS
    (void)frame; (void)det_thresh; (void)out;
    return false;
  }

  bool Extract(const ImageFrame& aligned_face, Feature& out) override {
    if (!rec_) return false;
    // TODO(impl): 112x112 BGR->RGB /255 -> (1,3,112,112) -> Run -> L2
    (void)aligned_face; (void)out;
    return false;
  }

  float Liveness(const ImageFrame& aligned_face) override {
    if (!liv_) return 1.0f;  // 无活体模型视为不检测
    (void)aligned_face;
    return 1.0f;
  }

 private:
  Ort::Env env_{ORT_LOGGING_LEVEL_WARNING};
  Ort::SessionOptions sess_;
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