// enrollment-pc/src/feature/feature_types.h
// 特征提取层基础类型（纯 C++17，无 Qt 依赖）：
//   - ImageFrame：与 OpenCV BGR 布局一致的内存帧（无 OpenCV 也可用）
//   - FaceBox：人脸框 + 5 点关键点
// 特征向量复用 verify_logic.h 的 enroll::Feature（512 维，L2 归一化），
// 与 admin-backend / edge-box 的人脸向量空间保持一致。
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "verify/verify_logic.h"  // enroll::Feature (512)

namespace enroll {

// 图像帧：BGR 通道顺序，data 尺寸 = width*height*channels
struct ImageFrame {
  int width = 0;
  int height = 0;
  int channels = 3;            // 3 = BGR
  std::vector<uint8_t> data;   // width*height*channels

  uint8_t* Ptr(int y, int x) { return data.data() + (y * width + x) * channels; }
  const uint8_t* Ptr(int y, int x) const { return data.data() + (y * width + x) * channels; }
};

// 人脸检测结果（原图坐标）
struct FaceBox {
  float x = 0, y = 0, w = 0, h = 0;  // 原图坐标
  float score = 0;
  std::array<float, 10> kps{};       // 5 点关键点 (x,y)*5
};

}  // namespace enroll