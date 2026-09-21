#include "backend/mock_backend.h"

#include <cmath>
#include <cstring>

namespace eb {

namespace {
// 从帧内容生成确定性 512 维伪特征（L2 归一化）。
// Mock 设计：同一个人脸区域内容相似时特征也相似，便于 1:N 自测。
Feature FeatureFromBytes(const ImageFrame& f) {
  Feature feat{};
  if (f.data.empty()) return feat;
  const size_t step = f.data.size() / (kFeatureDim * 2) + 1;
  for (int i = 0; i < kFeatureDim; ++i) {
    uint32_t acc = 0;
    size_t idx = static_cast<size_t>(i) * step;
    for (int j = 0; j < 8 && idx < f.data.size(); ++j, ++idx) {
      acc = acc * 31 + f.data[idx];
    }
    feat[i] = (acc % 1000) / 1000.0f - 0.5f;  // [-0.5, 0.5)
  }
  // L2 归一化
  double norm = 0;
  for (float v : feat) norm += double(v) * v;
  norm = std::sqrt(norm);
  if (norm > 1e-9) {
    for (float& v : feat) v = static_cast<float>(v / norm);
  }
  return feat;
}
}  // namespace

bool MockBackend::Detect(const ImageFrame& frame, float /*det_thresh*/,
                         std::vector<FaceBox>& out) {
  out.clear();
  if (frame.width < 32 || frame.height < 32) return false;
  // 在画面中央返回一个确定性的人脸框（覆盖 1/4 区域）
  FaceBox box;
  box.w = frame.width * 0.5f;
  box.h = frame.height * 0.5f;
  box.x = (frame.width - box.w) * 0.5f;
  box.y = (frame.height - box.h) * 0.5f;
  box.score = 0.95f;
  out.push_back(box);
  return true;
}

bool MockBackend::Extract(const ImageFrame& aligned_face, Feature& out) {
  out = FeatureFromBytes(aligned_face);
  return true;
}

IInferenceBackend* CreateMockBackend(const ModelPaths& paths) {
  (void)paths;
  return new MockBackend();
}

}  // namespace eb