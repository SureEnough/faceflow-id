// enrollment-pc/src/idcard/idcard_reader.h
// 身份证读卡器硬件抽象层（架构文档 4.2.3）：不同厂商 SDK 实现统一接口，
// 业务层不感知具体设备；开发期用模拟器联调。纯 C++，无 Qt 依赖。
#pragma once

#include <cstdint>
#include <string>

namespace enroll {

// 读卡器状态码
enum class ReaderError {
  kOk = 0,
  kNotReady,     // 未连接/未就绪
  kNoCard,       // 无卡
  kReadFail,     // 读卡失败
  kTimeout,      // 超时
  kDriver,       // SDK 底层错误
};

// 身份证信息（脱敏字段由上层决定是否展示）
struct IdCardInfo {
  std::string name;        // 姓名
  std::string gender;      // 性别（男/女）
  std::string nation;      // 民族
  std::string birth;       // 出生日期 YYYYMMDD
  std::string address;     // 住址
  std::string id_card_no;  // 身份证号
  std::string issue_org;   // 签发机关
  std::string valid_from;  // 有效期起始 YYYYMMDD
  std::string valid_until; // 有效期截止（"长期" 或 YYYYMMDD）
  std::string photo_jpg;   // 证件照 JPEG（base64 或路径，由实现决定）
  bool valid = false;
};

// 读卡器抽象接口
class IIdCardReader {
 public:
  virtual ~IIdCardReader() = default;

  // 打开设备；返回 kOk 表示成功
  virtual ReaderError Open() = 0;
  // 关闭设备
  virtual void Close() = 0;

  // 阻塞读卡（一般 <5s）；成功返回 kOk 且 out.valid=true
  virtual ReaderError Read(IdCardInfo& out) = 0;

  // 复位设备（卡住/异常时调用）
  virtual ReaderError Reset() = 0;

  // 设备是否就绪
  virtual bool IsReady() const = 0;
};

// 创建读卡器实例：type="simulator" 返回模拟器；type="huawei"/"jinglun"
// 返回对应的厂商 SDK 适配（骨架阶段仅 simulator 可用，其余返回 nullptr）
IIdCardReader* CreateIdCardReader(const std::string& type);

}  // namespace enroll