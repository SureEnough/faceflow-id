// edge-box/src/config/config.h
// 边缘盒配置：从 JSON 文件加载/保存（MiniJson，无第三方依赖）。
// 字段与后台 GET /devices/:id/config 返回结构对应；Web 配置界面直接读写本文档。
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

  // ---- 设备运维（A3/A4） ----
  int64_t store_id = 0;         // >0 时启动向后台自注册（device_id<=0 时必填）
  std::string device_name = "edge-box";
  int heartbeat_interval_s = 30;   // 心跳周期
  int config_poll_s = 300;         // 后台配置下发轮询周期

  // ---- Web 配置界面 ----
  bool web_enabled = false;
  int web_port = 8180;
  std::string web_username = "admin";
  std::string web_password = "";  // 为空时禁用 Web 界面
  std::string web_static_dir = "web/dist";  // 前端构建产物目录（相对进程 cwd）

  // 从 JSON 文件加载；失败返回 false 并保留默认值
  static bool LoadFile(const std::string& path, Config& out);
  // 解析 JSON 字符串到配置（先生成临时副本，校验通过才写入 out）
  static bool FromJsonStr(const std::string& json, Config& out, std::string* err);
  // 序列化为 JSON 字符串（web_password 为空则不输出）
  std::string ToJson(bool mask_psk = false) const;
  // 原子写盘（tmp + rename）
  bool SaveFile(const std::string& path) const;
  // 基础校验：阈值范围、端点协议、至少一路摄像头（cameras 允许为空时由调用方控制）
  bool Validate(std::string* err) const;
};

}  // namespace eb