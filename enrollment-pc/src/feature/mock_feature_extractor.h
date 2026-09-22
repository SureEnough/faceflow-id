// enrollment-pc/src/feature/mock_feature_extractor.h
// Mock 特征提取器：无 ONNXRuntime / 无模型时的确定性回退。
// 用途：CI/自测场景验证流水线（检测→对齐→提取→比对）逻辑正确性，
// 输出特征由输入图像内容唯一决定（同图必同特征），但无真实语义。
#pragma once

#include "feature/feature_extractor.h"

namespace enroll {

class MockFeatureExtractor : public IFeatureExtractor {
 public:
  bool LoadModel(const ModelPaths& paths) override;
  bool DetectAlignExtract(const ImageFrame& frame, Feature& out) override;
  bool ExtractAligned(const ImageFrame& aligned_face, Feature& out) override;
  float Liveness(const ImageFrame& aligned_face) override;
};

}  // namespace enroll