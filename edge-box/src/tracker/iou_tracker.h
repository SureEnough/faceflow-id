// edge-box/src/tracker/iou_tracker.h
// 轻量 IOU 轨迹跟踪：跨帧关联检测框，输出稳定 track_id。
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "common/common.h"

namespace eb {

struct Track {
  std::string id;
  float x = 0, y = 0, w = 0, h = 0;
  int lost = 0;        // 连续失配帧数
  int alive = 0;       // 存活帧数
  float cx = 0, cy = 0;  // 质心（用于虚拟线判定）
};

class IOUTracker {
 public:
  explicit IOUTracker(float iou_thresh = 0.35f, int max_lost = 8) : iou_thresh_(iou_thresh), max_lost_(max_lost) {}

  // 更新：输入检测框，输出关联后的 Track 列表（含新分配的 id）
  void Update(const std::vector<FaceBox>& boxes, std::vector<Track>& out);

  void Clear();

 private:
  std::string NextId();
  static float IoU(const Track& t, const FaceBox& b);

  float iou_thresh_;
  int max_lost_;
  uint64_t seq_ = 0;
  std::map<std::string, Track> tracks_;
};

}  // namespace eb