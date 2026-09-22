// edge-box/src/backend/scrfd_decode.cpp
#include "backend/scrfd_decode.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace eb {
namespace scrfd {

namespace {

float ClampF(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

std::vector<int> NmsOrder(const std::vector<FaceBox>& boxes, float iou_thresh) {
  std::vector<int> order(boxes.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) { return boxes[a].score > boxes[b].score; });

  std::vector<int> keep;
  std::vector<char> removed(boxes.size(), 0);
  for (int idx : order) {
    if (removed[idx]) continue;
    keep.push_back(idx);
    const FaceBox& a = boxes[idx];
    for (int j : order) {
      if (removed[j]) continue;
      const FaceBox& b = boxes[j];
      if (IoU(a, b) > iou_thresh) removed[j] = 1;
    }
  }
  return keep;
}

}  // namespace

float IoU(const FaceBox& a, const FaceBox& b) {
  float ix = std::max(0.f, std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x));
  float iy = std::max(0.f, std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y));
  float inter = ix * iy;
  float uni = a.w * a.h + b.w * b.h - inter + 1e-6f;
  return inter / uni;
}

int InferStride(int anchor_count, int input_size) {
  if (anchor_count <= 0) return 0;
  float side = std::sqrt(static_cast<float>(anchor_count));
  float stride = static_cast<float>(input_size) / side;
  int s = static_cast<int>(stride + 0.5f);
  // 只接受常见 stride（4/8/16/32）；否则视为非法
  return (s == 4 || s == 8 || s == 16 || s == 32) ? s : 0;
}

std::vector<FaceBox> Decode(const std::vector<StrideOutput>& outs, int input_size,
                            float in_scale_x, float in_scale_y, const DecodeOptions& opt) {
  std::vector<FaceBox> raw;
  for (const StrideOutput& o : outs) {
    if (!o.scores || !o.boxes || o.count <= 0 || o.stride <= 0) continue;
    int grid = input_size / o.stride;  // 网格边长
    if (grid <= 0 || grid > 4096) continue;
    int maxAnchors = std::min(o.count, grid * grid);
    for (int r = 0; r < grid; ++r) {
      for (int c = 0; c < grid; ++c) {
        int i = r * grid + c;
        if (i >= maxAnchors) break;
        float score = ScoreAt(o, i);
        if (score < opt.det_thresh) continue;
        // 锚点中心（insightface scrfd.py: mgrid[:h,:w][::-1] * stride）
        float cx = static_cast<float>(c) * o.stride;
        float cy = static_cast<float>(r) * o.stride;
        float x1 = cx - BoxAt(o, i, 0) * o.stride;
        float y1 = cy - BoxAt(o, i, 1) * o.stride;
        float x2 = cx + BoxAt(o, i, 2) * o.stride;
        float y2 = cy + BoxAt(o, i, 3) * o.stride;
        if (!(x2 > x1 && y2 > y1)) continue;

        FaceBox fb;
        fb.x = x1 * in_scale_x;
        fb.y = y1 * in_scale_y;
        fb.w = (x2 - x1) * in_scale_x;
        fb.h = (y2 - y1) * in_scale_y;
        fb.score = score;

        if (o.kps) {
          for (int p = 0; p < 5; ++p) {
            float kx = cx + KpsAt(o, i, p * 2) * o.stride;
            float ky = cy + KpsAt(o, i, p * 2 + 1) * o.stride;
            fb.kps[p * 2] = kx * in_scale_x;
            fb.kps[p * 2 + 1] = ky * in_scale_y;
          }
        }
        raw.push_back(fb);
        if (static_cast<int>(raw.size()) >= opt.max_face_num * 8) break;  // 防爆
      }
    }
  }

  std::vector<FaceBox> out;
  out.reserve(raw.size());
  auto keep = NmsOrder(raw, opt.nms_thresh);
  for (int idx : keep) {
    out.push_back(raw[idx]);
    if (static_cast<int>(out.size()) >= opt.max_face_num) break;
  }
  return out;
}

}  // namespace scrfd
}  // namespace eb