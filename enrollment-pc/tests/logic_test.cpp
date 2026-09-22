// enrollment-pc/tests/logic_test.cpp
// 纯 C++ 逻辑自测（无需 Qt）：读卡器模拟器 + 1:1 比对。
#include <cstdio>
#include <cmath>

#include "idcard/idcard_reader.h"
#include "verify/verify_logic.h"

using namespace enroll;

static int g_fail = 0;
#define CHECK(cond, msg)                                        \
  do {                                                          \
    if (!(cond)) {                                              \
      std::printf("[FAIL] %s\n", msg);                          \
      ++g_fail;                                                 \
    } else {                                                    \
      std::printf("[ OK ] %s\n", msg);                          \
    }                                                           \
  } while (0)

int main() {
  // 1. 读卡器模拟器
  {
    IIdCardReader* r = CreateIdCardReader("simulator");
    CHECK(r != nullptr, "create simulator reader");
    if (r) {
      CHECK(r->Open() == ReaderError::kOk, "simulator open");
      IdCardInfo card;
      CHECK(r->Read(card) == ReaderError::kOk && card.valid, "simulator read card");
      CHECK(card.id_card_no.size() == 18, "simulator id_card_no 18 chars");
      CHECK(r->IsReady(), "simulator ready");
    }
    delete r;
    CHECK(CreateIdCardReader("huawei") == nullptr, "unsupported vendor returns nullptr");
  }

  // 2. 1:1 比对：同特征通过；正交特征不通过
  {
    Feature a{}, b{};
    a[0] = 1.f;            // 已归一化
    b[0] = -1.f;           // 与 a 方向相反（cos=-1）
    Feature c{};
    c[0] = 1.f;            // 与 a 相同
    CHECK(std::fabs(CosineSimilarity(a, c) - 1.f) < 1e-5, "same feature cos ~ 1");
    CHECK(CosineSimilarity(a, b) < -0.99f, "opposite feature cos ~ -1");
    CHECK(VerifyPass(a, c, 0.5f), "verify pass at same feature");
    CHECK(!VerifyPass(a, b, 0.5f), "verify reject at opposite feature");
  }

  std::printf("logic test finished: %s\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURE");
  return g_fail == 0 ? 0 : 1;
}