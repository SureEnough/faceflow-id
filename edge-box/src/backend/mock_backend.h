// edge-box/src/backend/mock_backend.h/.cpp
// Mock 推理后端：不需要任何模型/推理库，用确定性规则返回检测框与特征。
// 用途：开发自测、CI、无 GPU/模型环境下验证流水线逻辑。
#pragma once

#include "backend/inference_backend.h"

namespace eb {

class MockBackend : public IInferenceBackend {
 public:
  bool Detect(const ImageFrame& frame, float det_thresh, std::vector<FaceBox>& out) override;
  bool Extract(const ImageFrame& aligned_face, Feature& out) override;
  float Liveness(const ImageFrame&) override { return 1.0f; }
  bool WarmUp() override { return true; }
};

}  // namespace eb