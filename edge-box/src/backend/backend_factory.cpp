// edge-box/src/backend/backend_factory.cpp
// 统一后端创建分发：避免多个 TU 重复定义 CreateBackend。
#include "backend/inference_backend.h"

namespace eb {

IInferenceBackend* CreateBackend(BackendType type, const ModelPaths& paths) {
  switch (type) {
    case BackendType::kMock:
      return CreateMockBackend(paths);
    case BackendType::kOnnx:
      return CreateOnnxBackend(paths);  // 未编译 ONNX 时内部返回 nullptr
    default:
      return nullptr;
  }
}

}  // namespace eb