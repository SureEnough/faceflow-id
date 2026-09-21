#include "tracker/iou_tracker.h"

#include <algorithm>
#include <sstream>

namespace eb {

float IOUTracker::IoU(const Track& t, const FaceBox& b) {
  float ix = Inter(t.x + t.w, b.x + b.w) - std::max(t.x, b.x);
  float iy = Inter(t.y + t.h, b.y + b.h) - std::max(t.y, b.y);
  if (ix <= 0 || iy <= 0) return 0;
  float inter = ix * iy;
  float area_t = t.w * t.h;
  float area_b = b.w * b.h;
  return inter / (area_t + area_b - inter + 1e-6f);
}

std::string IOUTracker::NextId() {
  ++seq_;
  std::ostringstream ss;
  ss << "T-" << seq_;
  return ss.str();
}

void IOUTracker::Update(const std::vector<FaceBox>& boxes, std::vector<Track>& out) {
  std::vector<char> matched(boxes.size(), 0);

  for (auto& kv : tracks_) {
    Track& t = kv.second;
    float best = 0;
    int best_idx = -1;
    for (size_t i = 0; i < boxes.size(); ++i) {
      if (matched[i]) continue;
      float iou = IoU(t, boxes[i]);
      if (iou > best) { best = iou; best_idx = static_cast<int>(i); }
    }
    if (best_idx >= 0 && best >= iou_thresh_) {
      const FaceBox& b = boxes[best_idx];
      t.x = b.x; t.y = b.y; t.w = b.w; t.h = b.h;
      t.cx = b.x + b.w * 0.5f;
      t.cy = b.y + b.h * 0.5f;
      t.lost = 0;
      ++t.alive;
      matched[best_idx] = 1;
    } else {
      ++t.lost;
    }
  }

  // 新建轨迹
  for (size_t i = 0; i < boxes.size(); ++i) {
    if (matched[i]) continue;
    const FaceBox& b = boxes[i];
    Track t{NextId(), b.x, b.y, b.w, b.h, 0, 1, b.x + b.w * 0.5f, b.y + b.h * 0.5f};
    tracks_.emplace(t.id, t);
  }

  // 输出活跃轨迹 + 清理丢失过久的
  out.clear();
  for (auto it = tracks_.begin(); it != tracks_.end();) {
    if (it->second.lost > max_lost_) {
      it = tracks_.erase(it);
    } else {
      if (it->second.alive >= 1) out.push_back(it->second);
      ++it;
    }
  }
}

void IOUTracker::Clear() { tracks_.clear(); }

}  // namespace eb