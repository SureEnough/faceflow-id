// edge-box/src/common/sysinfo.h
#pragma once

namespace eb {

// 系统资源快照（百分比 0~100）
struct SysInfo {
  double cpu = 0;   // CPU 使用率 %
  double mem = 0;   // 内存使用率 %
  double disk = 0;  // 根分区磁盘使用率 %
};

// Linux 系统资源采集（/proc/stat、/proc/meminfo、statvfs）；非 Linux / 读取失败返回 0
class SysInfoCollector {
 public:
  // 采样一次；cpu 为相对上次采样的使用率（首次返回 0）
  SysInfo Sample();

 private:
  bool have_prev_ = false;
  unsigned long long prev_idle_ = 0;
  unsigned long long prev_total_ = 0;
};

}  // namespace eb