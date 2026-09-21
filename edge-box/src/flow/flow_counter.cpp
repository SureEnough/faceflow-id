#include "flow/flow_counter.h"

namespace eb {

// 向量 (a->b) 与 (a->c) 的叉积（z 分量）：判断 c 在线段 a-b 哪一侧
float FlowCounter::Side(const float* a, const float* b, const float* c) {
  float v = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  if (v > 0) return 1.0f;
  if (v < 0) return -1.0f;
  return 0;
}

int FlowCounter::Update(const std::string& track_id, float cx, float cy) {
  if (counted_.count(track_id)) return -1;  // 已计过

  const float a[2] = {x1_, y1_};
  const float b[2] = {x2_, y2_};
  const float c[2] = {cx, cy};
  float s = Side(a, b, c);

  auto it = side_map_.find(track_id);
  if (it != side_map_.end() && it->second != 0 && s != 0 && it->second != s) {
    // 跨线：正侧->负侧 = 进(0)，负侧->正侧 = 出(1)
    int dir = (it->second < 0) ? 0 : 1;  // 负侧->正侧 = 进(0)
    if (dir == 0) ++stats_.in; else ++stats_.out;
    counted_.insert(track_id);
    side_map_.erase(it);
    return dir;
  }
  side_map_[track_id] = s;
  return -1;
}

}  // namespace eb