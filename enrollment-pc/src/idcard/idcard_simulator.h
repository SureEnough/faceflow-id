// enrollment-pc/src/idcard/idcard_simulator.h
// 读卡器模拟器：无硬件时供开发/演示（自动生成一张演示身份证）。
#pragma once

#include "idcard/idcard_reader.h"

namespace enroll {

class SimulatorIdCardReader : public IIdCardReader {
 public:
  SimulatorIdCardReader() = default;

  ReaderError Open() override;
  void Close() override;
  ReaderError Read(IdCardInfo& out) override;
  ReaderError Reset() override;
  bool IsReady() const override;

 private:
  bool ready_ = false;
};

}  // namespace enroll