// enrollment-pc/src/idcard/idcard_simulator.cpp
#include "idcard/idcard_simulator.h"

#include <ctime>

namespace enroll {

ReaderError SimulatorIdCardReader::Open() {
  ready_ = true;
  return ReaderError::kOk;
}

void SimulatorIdCardReader::Close() { ready_ = false; }

ReaderError SimulatorIdCardReader::Reset() {
  ready_ = true;
  return ReaderError::kOk;
}

bool SimulatorIdCardReader::IsReady() const { return ready_; }

ReaderError SimulatorIdCardReader::Read(IdCardInfo& out) {
  if (!ready_) return ReaderError::kNotReady;
  // 演示数据：固定身份证号（符合校验位规则）+ 空证件照
  out.name = "演示顾客";
  out.gender = "男";
  out.nation = "汉";
  out.birth = "19900101";
  out.address = "北京市朝阳区演示路 1 号";
  out.id_card_no = "110101199001011237";
  out.issue_org = "北京市公安局朝阳分局";
  out.valid_from = "20150101";
  out.valid_until = "20350101";
  out.photo_jpg = "";
  out.valid = true;
  return ReaderError::kOk;
}

IIdCardReader* CreateIdCardReader(const std::string& type) {
  if (type == "simulator") return new SimulatorIdCardReader();
  // 厂商 SDK 适配（待接入，需厂商动态库/授权）：
  //   type == "huawei"  -> new HuaweiCVR100UReader();
  //   type == "jinglun" -> new JinglunIDR210Reader();
  return nullptr;
}

}  // namespace enroll