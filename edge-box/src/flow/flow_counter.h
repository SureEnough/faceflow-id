// edge-box/src/flow/flow_counter.h
// 客流计数：虚拟线判向。跟踪质心跨越线段时计 1 次进出。
#pragma once

#include <map>
#include <set>
#include <string>

#include "common/common.h"

namespace eb {

struct FlowStats {
  int64_t in = 0;
  int64_t out = 0;
};

class FlowCounter {
 public:
  FlowCounter(float x1, float y1, float x2, float y2) : x1_(x1), y1_(y1), x2_(x2), y2_(y2) {}

  // 更新轨迹质心；跨线时返回方向（0 进 / 1 出），否则 -1。同一 track 只计一次。
  int Update(const std::string& track_id, float cx, float cy);

  FlowStats stats() const { return stats_; }

 private:
  static float Side(const float* a, const float* b, const float* c);  // 叉积符号（>0 左/上侧，<0 右/下侧）

  float x1_, y1_, x2_, y2_;
  FlowStats stats_;
  std::map<std::string, float> side_map_;  // track -> 上一帧侧向
  std::set<std::string> counted_;
};

}  // namespace eb