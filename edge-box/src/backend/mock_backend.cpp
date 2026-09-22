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
  // 全覆盖分段哈希：每维聚合一段字节，位置/内容差异会投影到多个维度（更接近真实 Embedding）
  const size_t total = f.data.size();
  const size_t base = total / kFeatureDim;
  const size_t rem = total % kFeatureDim;
  size_t idx = 0;
  for (int i = 0; i < kFeatureDim; ++i) {
    size_t len = base + (static_cast<size_t>(i) < rem ? 1 : 0);
    uint32_t acc = 2166136261u;  // FNV 初值
    size_t end = std::min(idx + len, total);
    for (; idx < end; ++idx) {
      acc = (acc ^ f.data[idx]) * 16777619u;
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