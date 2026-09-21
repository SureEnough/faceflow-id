// edge-box/src/face/face_engine.h
// 人脸引擎：组合 检测→对齐→特征提取→活体，业务流水线只面向 FaceEngine。
#pragma once

#include <memory>

#include "backend/inference_backend.h"
#include "common/common.h"

namespace eb {

struct FaceSample {
  FaceBox box;
  Feature feature{};
  float liveness = 1.0f;
  float quality = 1.0f;  // 质量分（清晰度等），骨架默认 1
  bool has_feature = false;
};

class FaceEngine {
 public:
  // 调用方负责生命周期
  explicit FaceEngine(IInferenceBackend* backend) : backend_(backend) {}

  bool Detect(const ImageFrame& frame, float thresh, std::vector<FaceBox>& out);
  // 由框裁剪对齐图（骨架：按框中心缩放至 112x112；真实实现应使用 5 点关键点仿射）
  bool AlignCrop(const ImageFrame& frame, const FaceBox& box, ImageFrame& aligned);
  // 完整采样：检测 → 质量最优框 → 对齐 → 特征 + 活体
  bool Sample(const ImageFrame& frame, float det_thresh, FaceSample& out);

 private:
  IInferenceBackend* backend_;
};

}  // namespace eb