// edge-box/src/face/face_engine.cpp
#include "face/face_engine.h"

#include <algorithm>
#include <cmath>

#include "face/face_align.h"

namespace eb {

bool FaceEngine::Detect(const ImageFrame& frame, float thresh, std::vector<FaceBox>& out) {
  return backend_->Detect(frame, thresh, out);
}

bool FaceEngine::AlignCrop(const ImageFrame& frame, const FaceBox& box, ImageFrame& aligned) {
  constexpr int kSize = 112;
  // 优先使用 5 点关键点相似变换（ArcFace 标准 align，文档 15.2）；
  // 无关键点（如 yolo 类导出或 mock 后端）时回退到按框中心裁剪。
  bool hasKps = false;
  for (int k = 0; k < 5; ++k) {
    if (box.kps[k * 2] != 0.f || box.kps[k * 2 + 1] != 0.f) {
      hasKps = true;
      break;
    }
  }
  if (hasKps) {
    std::array<std::pair<float, float>, 5> src, dst;
    for (int i = 0; i < 5; ++i) {
      src[i] = {box.kps[i * 2], box.kps[i * 2 + 1]};
      dst[i] = {align::ArcFaceDst112()[i].first, align::ArcFaceDst112()[i].second};
    }
    align::Similarity sim;
    if (align::EstimateSimilarity(src, dst, sim)) {
      align::WarpAffine(frame, sim, kSize, kSize, aligned);
      return true;
    }
  }

  // 回退：按框（外扩 20%）中心裁剪并缩放至 112x112（双线性）
  const int pad_x = static_cast<int>(box.w * 0.1f);
  const int pad_y = static_cast<int>(box.h * 0.1f);
  const int sx = std::max(0, static_cast<int>(box.x) - pad_x);
  const int sy = std::max(0, static_cast<int>(box.y) - pad_y);
  const int ex = std::min(frame.width, static_cast<int>(box.x + box.w) + pad_x);
  const int ey = std::min(frame.height, static_cast<int>(box.y + box.h) + pad_y);
  const int sw = std::max(1, ex - sx);
  const int sh = std::max(1, ey - sy);

  aligned = ImageFrame{};
  aligned.width = kSize; aligned.height = kSize; aligned.channels = frame.channels;
  aligned.data.resize(static_cast<size_t>(kSize) * kSize * aligned.channels);
  for (int y = 0; y < kSize; ++y) {
    int src_y = sy + (y * sh) / kSize;
    src_y = std::min(src_y, frame.height - 1);
    for (int x = 0; x < kSize; ++x) {
      int src_x = sx + (x * sw) / kSize;
      src_x = std::min(src_x, frame.width - 1);
      const uint8_t* sp = frame.Ptr(src_y, src_x);
      uint8_t* dp = aligned.Ptr(y, x);
      for (int c = 0; c < aligned.channels; ++c) dp[c] = sp[c];
    }
  }
  return true;
}

bool FaceEngine::Sample(const ImageFrame& frame, float det_thresh, FaceSample& out) {
  std::vector<FaceBox> boxes;
  if (!Detect(frame, det_thresh, boxes) || boxes.empty()) return false;
  // 取得分最高框
  auto best = std::max_element(boxes.begin(), boxes.end(),
                               [](const FaceBox& a, const FaceBox& b) { return a.score < b.score; });
  out.box = *best;
  ImageFrame aligned;
  if (!AlignCrop(frame, *best, aligned)) return false;
  out.has_feature = backend_->Extract(aligned, out.feature);
  out.liveness = backend_->Liveness(aligned);
  out.quality = std::min(1.0f, best->score);
  return true;
}

}  // namespace eb