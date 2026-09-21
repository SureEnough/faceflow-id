// edge-box/src/config/config.h
// 边缘盒配置：从 JSON 文件加载（MiniJson，无第三方依赖）。
// 字段与后台 GET /devices/:id/config 返回结构对应。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "common/common.h"

namespace eb {

struct CameraConfig {
  std::string camera_id;
  std::string url;            // rtsp:// 或 usb 标识
  std::string role;           // entrance / counter / ...
  bool count_flow = true;     // 是否计入客流
  float line_x1 = 0, line_y1 = 0, line_x2 = 0, line_y2 = 0;  // 虚拟线（count_flow 时有效）
  std::string direction = "both";  // in / out / both
};

struct Config {
  std::vector<CameraConfig> cameras;
  float det_thresh = 0.5f;
  float recog_thresh = 0.40f;   // 1:N / 历史回查阈值
  float verify_thresh = 0.50f;  // 1:1 阈值（录入端用）
  bool liveness_enabled = true;
  bool staff_enabled = true;    // 启用内部人员识别
  int retention_days = 365;
  int report_interval_s = 60;
  std::string report_endpoint = "http://127.0.0.1:8080/api/v1";
  std::string device_psk = "";
  int64_t device_id = 0;
  int max_frames = 0;           // 0 = 无限（测试可设有限帧）

  // 从 JSON 文件加载；失败返回 false 并保留默认值
  static bool LoadFile(const std::string& path, Config& out);
};

}  // namespace eb