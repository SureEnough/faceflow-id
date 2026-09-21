// edge-box/src/backend/inference_backend.h
// 推理后端抽象（文档 15.9）：业务代码不感知 ONNXRuntime/NCNN，可切换。
#pragma once

#include "common/common.h"

namespace eb {

struct ModelPaths {
  std::string det;       // SCRFD *.onnx
  std::string rec;       // ArcFace *.onnx
  std::string liveness;  // 活体（可选）
};

class IInferenceBackend {
 public:
  virtual ~IInferenceBackend() = default;

  // 人脸检测：输入整帧 BGR，输出人脸框 + 5 关键点
  virtual bool Detect(const ImageFrame& frame, float det_thresh, std::vector<FaceBox>& out) = 0;

  // 特征提取：输入对齐后的人脸图（112x112 RGB 或 BGR），输出 512 维 L2 归一化特征
  virtual bool Extract(const ImageFrame& aligned_face, Feature& out) = 0;

  // RGB 活体：输入对齐后的人脸图，输出 0~1 活体得分（无模型时返回 1.0 表示不检测）
  virtual float Liveness(const ImageFrame& aligned_face) = 0;

  // 加载模型/预热
  virtual bool WarmUp() = 0;
};

enum class BackendType { kMock, kOnnx, kNcnn };

// 各后端创建函数（backend_factory.cpp 统一分发）
IInferenceBackend* CreateMockBackend(const ModelPaths& paths);
IInferenceBackend* CreateOnnxBackend(const ModelPaths& paths);

// 按类型创建后端；缺少实现时返回 nullptr
IInferenceBackend* CreateBackend(BackendType type, const ModelPaths& paths);

}  // namespace eb