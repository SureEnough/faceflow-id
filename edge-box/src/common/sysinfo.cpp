// edge-box/src/common/sysinfo.cpp
#include "common/sysinfo.h"

#include <cstdio>
#include <cstring>

#if defined(__linux__)
#include <sys/statvfs.h>
#endif

namespace eb {

namespace {

// 读取 /proc/stat 首行 CPU 累计：idle（含 iowait）、total
bool ReadProcStat(unsigned long long* idle, unsigned long long* total) {
#if defined(__linux__)
  FILE* f = std::fopen("/proc/stat", "r");
  if (!f) return false;
  unsigned long long user = 0, nice = 0, sys = 0, idle_ = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
  char buf[256];
  if (std::fgets(buf, sizeof(buf), f) &&
      std::sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &sys,
                  &idle_, &iowait, &irq, &softirq, &steal) >= 4) {
    *idle = idle_ + iowait;
    *total = user + nice + sys + idle_ + iowait + irq + softirq + steal;
    std::fclose(f);
    return true;
  }
  std::fclose(f);
#endif
  return false;
}

double MemUsagePct() {
#if defined(__linux__)
  FILE* f = std::fopen("/proc/meminfo", "r");
  if (!f) return 0;
  long long total = 0, avail = 0;
  char key[64];
  long long val = 0;
  while (std::fscanf(f, "%63s %lld kB\n", key, &val) == 2) {
    if (std::strcmp(key, "MemTotal:") == 0) {
      total = val;
    } else if (std::strcmp(key, "MemAvailable:") == 0) {
      avail = val;
      break;
    }
  }
  std::fclose(f);
  if (total <= 0) return 0;
  return static_cast<double>(total - avail) * 100.0 / static_cast<double>(total);
#else
  return 0;
#endif
}

double DiskUsagePct() {
#if defined(__linux__)
  struct statvfs st;
  if (::statvfs("/", &st) != 0 || st.f_blocks <= 0) return 0;
  const unsigned long long total = st.f_blocks;
  const unsigned long long free = st.f_bfree;
  return static_cast<double>(total - free) * 100.0 / static_cast<double>(total);
#else
  return 0;
#endif
}

}  // namespace

SysInfo SysInfoCollector::Sample() {
  SysInfo info;
  info.mem = MemUsagePct();
  info.disk = DiskUsagePct();

  unsigned long long idle = 0, total = 0;
  if (ReadProcStat(&idle, &total) && have_prev_ && total > prev_total_) {
    const unsigned long long idle_delta = idle >= prev_idle_ ? idle - prev_idle_ : 0;
    const unsigned long long total_delta = total - prev_total_;
    info.cpu = 100.0 * (1.0 - static_cast<double>(idle_delta) / static_cast<double>(total_delta));
  }
  prev_idle_ = idle;
  prev_total_ = total;
  have_prev_ = true;
  return info;
}

}  // namespace eb