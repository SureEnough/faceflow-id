// enrollment-pc/src/feature/ort_feature_extractor.h
// ONNXRuntime 特征提取器（HAVE_ONNXRUNTIME 条件编译）：
//   - DetectAlignExtract：SCRFD 检测（可选）-> 5 点对齐 -> ArcFace 512 维
//   - ExtractAligned：已对齐/正脸图 -> ArcFace 512 维（证件照场景）
//   - Liveness：RGB 活体得分（可选模型，无时返回 1.0）
// 算法与 edge-box/src/backend/onnx_backend.cpp 保持一致（同一向量空间），
// 兼容 SCRFD 布局 A/B 与绝对坐标回退。无 ORT 编译时本类不可用。
#pragma once

#include "feature/feature_extractor.h"

namespace enroll {

// 可用性：编译期是否包含 ORT 实现（无 ORT 时 CreateFeatureExtractor(kOnnx) 返回 nullptr）
bool OrtFeatureExtractorAvailable();

class OrtFeatureExtractor : public IFeatureExtractor {
 public:
  OrtFeatureExtractor();
  ~OrtFeatureExtractor() override;
  OrtFeatureExtractor(const OrtFeatureExtractor&) = delete;
  OrtFeatureExtractor& operator=(const OrtFeatureExtractor&) = delete;

  bool LoadModel(const ModelPaths& paths) override;
  bool DetectAlignExtract(const ImageFrame& frame, Feature& out) override;
  bool ExtractAligned(const ImageFrame& aligned_face, Feature& out) override;
  float Liveness(const ImageFrame& aligned_face) override;

 private:
  struct Impl;
  Impl* impl_ = nullptr;  // pimpl：隐藏 onnxruntime_cxx_api.h 依赖
};

}  // namespace enroll