// edge-box/src/storage/identity.h
// 人员特征条目：后台同步下发的单位。
#pragma once

#include "common/common.h"

namespace eb {

struct Identity {
  int64_t customer_id = -1;
  PersonType person_type = kCustomer;
  Feature feature{};
  int64_t version = 0;
  int8_t status = 0;        // 与后台一致：0 正常 / 1 黑名单 / 2 注销 / 3 离职
  std::string staff_no_hash;  // 内部人员工号哈希（不下发明文）
};

}  // namespace eb