// edge-box/src/face/face_align.cpp
#include "face/face_align.h"

#include <cmath>

namespace eb {
namespace align {

namespace {

// 2x2 SVD（Golub-Kahan 一步 / 解析式）。返回 U,S,Vt 使 M = U * diag(S) * Vt。
// 确定性实现，足够 Umeyama 相似变换使用（2 维输入下无歧义）。
bool Svd2x2(double m00, double m01, double m10, double m11,
            double outU[2][2], double outS[2], double outVt[2][2]) {
  // 对称矩阵 A = M^T M 的特征分解求 V、S，再由 U = M V / S 求 U。
  double a = m00 * m00 + m10 * m10;
  double b = m00 * m01 + m10 * m11;
  double c = m01 * m01 + m11 * m11;
  double tr = a + c;
  double det = a * c - b * b;
  if (tr <= 0 || det < 0) return false;
  double disc = std::sqrt(std::max(0.0, tr * tr / 4.0 - det));
  double lambda1 = tr / 2.0 + disc;
  double lambda2 = tr / 2.0 - disc;

  auto eigenvec = [](double a, double b, double c, double lam, double v[2]) {
    // (A - lam I) v = 0；取 [b, lam-a] 归一化（避免除零）
    double x = b;
    double y = lam - a;
    if (std::fabs(x) < 1e-12 && std::fabs(y) < 1e-12) { v[0] = 1; v[1] = 0; return; }
    double n = std::sqrt(x * x + y * y);
    v[0] = x / n;
    v[1] = y / n;
  };

  double v1[2], v2[2];
  eigenvec(a, b, c, lambda1, v1);
  eigenvec(a, b, c, lambda2, v2);
  // 正交化（特征向量本身正交，必要时翻转使其保持右手系）
  double cross = v1[0] * v2[1] - v1[1] * v2[0];
  if (cross < 0) { v2[0] = -v2[0]; v2[1] = -v2[1]; }

  outS[0] = std::sqrt(std::max(0.0, lambda1));
  outS[1] = std::sqrt(std::max(0.0, lambda2));
  outVt[0][0] = v1[0]; outVt[0][1] = v1[1];
  outVt[1][0] = v2[0]; outVt[1][1] = v2[1];

  if (outS[0] < 1e-12 || outS[1] < 1e-12) return false;

  // U = M V / S（每列）
  outU[0][0] = (m00 * v1[0] + m01 * v1[1]) / outS[0];
  outU[1][0] = (m10 * v1[0] + m11 * v1[1]) / outS[0];
  outU[0][1] = (m00 * v2[0] + m01 * v2[1]) / outS[1];
  outU[1][1] = (m10 * v2[0] + m11 * v2[1]) / outS[1];
  return true;
}

double ClampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

bool EstimateSimilarity(const std::array<std::pair<float, float>, 5>& src,
                        const std::array<std::pair<float, float>, 5>& dst, Similarity& out) {
  double scx = 0, scy = 0, dtx = 0, dty = 0;
  for (int i = 0; i < 5; ++i) {
    scx += src[i].first; scy += src[i].second;
    dtx += dst[i].first; dty += dst[i].second;
  }
  scx /= 5; scy /= 5; dtx /= 5; dty /= 5;

  // 协方差 S = Σ (src_i - c_src) (dst_i - c_dst)^T
  double m00 = 0, m01 = 0, m10 = 0, m11 = 0;
  double srcVar = 0;  // Σ ||src_i - c_src||^2（Umeyama 缩放分母）
  for (int i = 0; i < 5; ++i) {
    double sx = src[i].first - scx, sy = src[i].second - scy;
    double dx = dst[i].first - dtx, dy = dst[i].second - dty;
    m00 += sx * dx; m01 += sx * dy;
    m10 += sy * dx; m11 += sy * dy;
    srcVar += sx * sx + sy * sy;
  }
  if (srcVar < 1e-12) return false;

  double u[2][2], s[2], vt[2][2];
  if (!Svd2x2(m00, m01, m10, m11, u, s, vt)) return false;

  // R = U Vt；防止反射（det < 0 时翻转 Vt 最后一行 / U 最后一列）
  double det = u[0][0] * u[1][1] - u[0][1] * u[1][0];
  if (det < 0) {
    u[0][1] = -u[0][1];
    u[1][1] = -u[1][1];
  }
  double r00 = u[0][0] * vt[0][0] + u[0][1] * vt[1][0];
  double r01 = u[0][0] * vt[0][1] + u[0][1] * vt[1][1];
  double r10 = u[1][0] * vt[0][0] + u[1][1] * vt[1][0];
  double r11 = u[1][0] * vt[0][1] + u[1][1] * vt[1][1];

  // scale = trace(Sigma) / Σ||src - c_src||^2（Umeyama: dst ≈ scale*R*src + t）
  double scale = (s[0] + s[1]) / srcVar;

  out.scale = static_cast<float>(scale);
  out.cos = static_cast<float>(r00);
  out.sin = static_cast<float>(r10);
  // t = c_dst - scale * R * c_src
  out.tx = static_cast<float>(dtx - scale * (r00 * scx + r01 * scy));
  out.ty = static_cast<float>(dty - scale * (r10 * scx + r11 * scy));
  return true;
}

void WarpAffine(const ImageFrame& src, const Similarity& sim, int dstW, int dstH, ImageFrame& out) {
  out = ImageFrame{};
  out.width = dstW; out.height = dstH; out.channels = src.channels;
  out.data.assign(static_cast<size_t>(dstW) * dstH * src.channels, 0);
  if (src.width <= 0 || src.height <= 0 || src.channels < 1) return;

  const int ch = src.channels;
  const float maxX = static_cast<float>(src.width - 1);
  const float maxY = static_cast<float>(src.height - 1);
  for (int y = 0; y < dstH; ++y) {
    for (int x = 0; x < dstW; ++x) {
      float sx = sim.InvX(static_cast<float>(x), static_cast<float>(y));
      float sy = sim.InvY(static_cast<float>(x), static_cast<float>(y));
      if (sx < 0 || sy < 0 || sx > maxX || sy > maxY) continue;  // 越界填 0
      // 双线性
      int x0 = static_cast<int>(sx), y0 = static_cast<int>(sy);
      int x1 = std::min(x0 + 1, src.width - 1);
      int y1 = std::min(y0 + 1, src.height - 1);
      float fx = sx - x0, fy = sy - y0;
      const uint8_t* p00 = src.Ptr(y0, x0);
      const uint8_t* p10 = src.Ptr(y0, x1);
      const uint8_t* p01 = src.Ptr(y1, x0);
      const uint8_t* p11 = src.Ptr(y1, x1);
      uint8_t* dp = out.Ptr(y, x);
      for (int c = 0; c < ch; ++c) {
        float top = p00[c] * (1.f - fx) + p10[c] * fx;
        float bot = p01[c] * (1.f - fx) + p11[c] * fx;
        dp[c] = static_cast<uint8_t>(top * (1.f - fy) + bot * fy + 0.5f);
      }
    }
  }
}

}  // namespace align
}  // namespace eb