// enrollment-pc/src/feature/feature_extractor.h
// 特征提取器抽象（架构文档 15.9 在录入端的对应物）：
//   录入端不再关心底层是 ONNXRuntime / Mock，统一走本接口。
// 纯 C++17，无 Qt 依赖，便于单元自测。
#pragma once

#include <string>

#include "feature/feature_types.h"

namespace enroll {

// 模型路径集合（与 edge-box 的 ModelPaths 对齐）
struct ModelPaths {
  std::string det;       // SCRFD *.onnx（可选：录入端证件照场景可省）
  std::string rec;       // ArcFace *.onnx（必填）
  std::string liveness;  // RGB 活体 *.onnx（可选，缺失则 Liveness() 返回 1.0）
};

enum class FeatureExtractorType { kMock, kOnnx };

class IFeatureExtractor {
 public:
  virtual ~IFeatureExtractor() = default;

  // 加载模型并预热；失败返回 false。Mock 恒返回 true。
  virtual bool LoadModel(const ModelPaths& paths) = 0;

  // 从原始 BGR 帧提取最大人脸特征（检测 -> 5 点对齐 -> ArcFace 512 维）。
  // 无检测模型时退化：整图缩放对齐后提特征。
  virtual bool DetectAlignExtract(const ImageFrame& frame, Feature& out) = 0;

  // 从已对齐/正脸图（BGR）提取 512 维 L2 归一化特征。
  virtual bool ExtractAligned(const ImageFrame& aligned_face, Feature& out) = 0;

  // RGB 活体得分（0~1）：1 越可信；无活体模型时返回 1.0（视为不检测）。
  virtual float Liveness(const ImageFrame& aligned_face) = 0;
};

// 按类型创建特征提取器；kOnnx 但缺少 ORT 编译时返回 nullptr。
IFeatureExtractor* CreateFeatureExtractor(FeatureExtractorType type);

}  // namespace enroll