// edge-box/src/face/face_align.h
// 5 点关键点相似变换对齐（ArcFace 标准 align，文档 15.2）。
// 仅依赖 C++17 标准库：2x2 SVD 求解最小二乘相似变换（Umeyama 算法），
// 避免对 OpenCV 的强依赖（无 OpenCV 时 CI/自测仍可编译）。
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "common/common.h"

namespace eb {
namespace align {

// ArcFace 112x112 目标关键点（InsightFace estimate_norm 标准坐标）
inline const std::array<std::pair<float, float>, 5>& ArcFaceDst112() {
  static const std::array<std::pair<float, float>, 5> dst = {{
      {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f},
      {41.5493f, 92.3655f}, {70.7299f, 92.2041f},
  }};
  return dst;
}

// 相似变换参数：dst = scale * R(rot) * src + (tx, ty)
struct Similarity {
  float scale = 1.f;
  float cos = 1.f, sin = 0.f;  // 旋转矩阵元素
  float tx = 0.f, ty = 0.f;

  // 将源坐标映射到目标坐标
  float MapX(float sx, float sy) const { return scale * (cos * sx - sin * sy) + tx; }
  float MapY(float sx, float sy) const { return scale * (sin * sx + cos * sy) + ty; }
  // 逆映射（目标 → 源）：R 正交，逆 = 转置
  float InvX(float dx, float dy) const { return (cos * (dx - tx) + sin * (dy - ty)) / scale; }
  float InvY(float dx, float dy) const { return (-sin * (dx - tx) + cos * (dy - ty)) / scale; }
};

// 由 5 组对应点估计相似变换（src -> dst，Umeyama 最小二乘，无反射）
bool EstimateSimilarity(const std::array<std::pair<float, float>, 5>& src,
                        const std::array<std::pair<float, float>, 5>& dst, Similarity& out);

// 按变换对 BGR 帧做双线性采样并输出 dstW x dstH
void WarpAffine(const ImageFrame& src, const Similarity& sim, int dstW, int dstH, ImageFrame& out);

}  // namespace align
}  // namespace eb