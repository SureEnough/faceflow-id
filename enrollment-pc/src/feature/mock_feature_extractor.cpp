// enrollment-pc/src/feature/mock_feature_extractor.cpp
#include "feature/mock_feature_extractor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace enroll {

namespace {

// 双线性缩放灰度：把任意 BGR 帧缩放到 kSize x kSize 并转灰度。
// 确定性实现，供 Mock 特征生成使用。
void ResizeToGray112(const ImageFrame& src, std::array<uint8_t, 112 * 112>& out) {
  constexpr int kSize = 112;
  out.fill(0);
  if (src.width <= 0 || src.height <= 0 || src.channels < 1) return;

  const int ch = src.channels;
  const float maxX = static_cast<float>(src.width - 1);
  const float maxY = static_cast<float>(src.height - 1);
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      float sx = static_cast<float>(x) * maxX / static_cast<float>(kSize - 1);
      float sy = static_cast<float>(y) * maxY / static_cast<float>(kSize - 1);
      int x0 = static_cast<int>(sx), y0 = static_cast<int>(sy);
      int x1 = std::min(x0 + 1, src.width - 1);
      int y1 = std::min(y0 + 1, src.height - 1);
      float fx = sx - x0, fy = sy - y0;
      auto gray = [&](int yy, int xx) {
        const uint8_t* p = src.Ptr(yy, xx);
        if (ch == 1) return static_cast<float>(p[0]);
        if (ch == 3) return 0.114f * p[0] + 0.587f * p[1] + 0.299f * p[2];  // BGR -> 灰度
        float s = 0;
        for (int c = 0; c < ch; ++c) s += p[c];
        return s / static_cast<float>(ch);
      };
      float top = gray(y0, x0) * (1.f - fx) + gray(y0, x1) * fx;
      float bot = gray(y1, x0) * (1.f - fx) + gray(y1, x1) * fx;
      out[static_cast<size_t>(y) * kSize + x] =
          static_cast<uint8_t>(top * (1.f - fy) + bot * fy + 0.5f);
    }
  }
}

}  // namespace

bool MockFeatureExtractor::LoadModel(const ModelPaths&) { return true; }

bool MockFeatureExtractor::DetectAlignExtract(const ImageFrame& frame, Feature& out) {
  // Mock 不做真正检测：把整图当作对齐人脸，直接缩放到 112x112 提特征。
  return ExtractAligned(frame, out);
}

bool MockFeatureExtractor::ExtractAligned(const ImageFrame& aligned_face, Feature& out) {
  constexpr int kSize = 112;
  std::array<uint8_t, kSize * kSize> gray;
  ResizeToGray112(aligned_face, gray);
  if (aligned_face.width <= 0 || aligned_face.height <= 0 || aligned_face.channels < 1) return false;

  // 512 维 = 112*112 灰度像素的分段平均，再 L2 归一化。
  // 同图必同特征；不同图（像素不同）特征不同。
  constexpr int kTotal = kSize * kSize;  // 12544
  constexpr int kSegment = kTotal / kFeatureDim;  // 24
  double norm2 = 0;
  for (size_t i = 0; i < kFeatureDim; ++i) {
    size_t begin = i * kSegment;
    size_t end = (i + 1) * kSegment;
    if (end > kTotal) end = kTotal;
    uint32_t sum = 0;
    for (size_t j = begin; j < end; ++j) sum += gray[j];
    float v = static_cast<float>(sum) / static_cast<float>(end - begin);
    out[i] = v;
    norm2 += static_cast<double>(v) * v;
  }
  if (norm2 <= 0) return false;
  float inv = static_cast<float>(1.0 / std::sqrt(norm2));
  for (size_t i = 0; i < kFeatureDim; ++i) out[i] *= inv;
  return true;
}

float MockFeatureExtractor::Liveness(const ImageFrame&) { return 1.0f; }

}  // namespace enroll