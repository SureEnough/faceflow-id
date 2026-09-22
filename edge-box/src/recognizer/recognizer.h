// edge-box/src/recognizer/recognizer.h
// 1:N 特征检索：与后台人员库同步的特征做余弦匹配，区分顾客/内部人员。
#pragma once

#include <vector>

#include "common/common.h"
#include "storage/identity.h"

namespace eb {

struct MatchResult {
  int64_t customer_id = -1;
  PersonType person_type = kCustomer;
  float similarity = 0.f;
  bool hit = false;
};

class Recognizer {
 public:
  // 批量加载人员库（后台 /customers/features/sync 下发）
  void LoadLibrary(const std::vector<Identity>& identities);

  // 追加/更新单个
  void Upsert(const Identity& identity);

  // 删除单个（人员失效/注销时从本地库移除）
  void Remove(int64_t customer_id);

  // 1:N 检索：返回相似度最高且 >= threshold 的结果
  MatchResult Search(const Feature& query, float threshold) const;

  size_t Size() const { return identities_.size(); }
  void Clear() { identities_.clear(); }

 private:
  std::vector<Identity> identities_;
};

}  // namespace eb