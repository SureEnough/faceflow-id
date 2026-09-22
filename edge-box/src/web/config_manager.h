// edge-box/src/web/config_manager.h
// Web 配置界面的共享状态管理（线程安全）+ 状态板（StatusBoard）。
// - ConfigManager：持有当前 Config、配置变更持久化、热重载标记
// - StatusBoard：主循环周期性写入设备/相机/客流/同步状态，Web 线程只读
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "common/common.h"
#include "config/config.h"

namespace eb {
namespace web {

// 单路相机运行状态
struct CameraStatus {
  std::string camera_id;
  bool opened = false;        // 视频源是否打开
  bool last_frame_ok = true;  // 最近一次取帧是否成功
  int64_t frames = 0;         // 已处理帧数
  int64_t flow_in = 0;        // 客流进
  int64_t flow_out = 0;       // 客流出
};

// 设备整体状态（Web /api/status 数据源）
struct StatusBoard {
  void Update(int64_t deviceId, std::string backend, std::string version,
              int64_t startedAt, std::vector<CameraStatus> cams, int64_t syncVersion);
  std::string ToJson() const;

  static std::string Version();

 private:
  mutable std::mutex mu_;
  int64_t device_id = 0;
  std::string backend;      // mock / onnx
  std::string version;      // 版本号
  int64_t started_at = 0;   // Unix 秒
  std::vector<CameraStatus> cameras;
  int64_t sync_version = 0;
};

// 配置持有与持久化
class ConfigManager {
 public:
  ConfigManager(std::string path, Config cfg) : path_(std::move(path)), cfg_(std::move(cfg)) {}

  Config Snapshot() const;
  // 从 JSON 字符串更新配置并原子写盘；成功返回 true（reload_pending_ 置位）
  bool UpdateFromJson(const std::string& json, std::string* err);
  // 不带持久化的更新（如回退默认值）
  void SetConfig(Config cfg);

  bool ConsumeReload();       // 主循环消费重载标记
  // 显式请求重载：重新读取磁盘配置并置位（对应 POST /api/reload）
  void RequestReload();
  bool reload_pending() const { return reload_pending_.load(); }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
  mutable std::mutex mu_;
  Config cfg_;
  std::atomic<bool> reload_pending_{false};
};

}  // namespace web
}  // namespace eb