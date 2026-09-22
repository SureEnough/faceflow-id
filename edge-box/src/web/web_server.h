// edge-box/src/web/web_server.h
// 边缘盒 Web 配置服务（cpp-httplib Server）。
// 路由：
//   GET  /               前端（web/dist 静态托管；缺失时提示构建）
//   GET  /api/status     运行状态（StatusBoard）
//   GET  /api/config     当前配置（device_psk/web_password 掩码）
//   PUT  /api/config     更新配置（校验 + 原子写盘 + 置重载标记）
//   POST /api/reload     触发热重载（重新读取磁盘配置）
//   POST /api/restart    触发进程退出（systemd 拉起）
// 全部接口要求 Basic Auth（admin / web_password）。
#pragma once

#include <atomic>
#include <string>

#include "web/config_manager.h"

namespace eb {
namespace web {

class WebServer {
 public:
  WebServer(ConfigManager* cm, StatusBoard* status)
      : cm_(cm), status_(status) {}
  ~WebServer();

  // 启动 HTTP 服务（内部独立线程，立即返回）；失败返回 false
  bool Start(int port, const std::string& username, const std::string& password,
             const std::string& static_dir, const std::atomic<bool>& stop_flag);
  // 停止服务并等待线程退出（进程退出/测试清理时调用）
  void Stop();

 private:
  ConfigManager* cm_;
  StatusBoard* status_;
  void* server_ = nullptr;  // HAVE_CPPHTTPLIB 时持有 httplib::Server*
  void* thread_ = nullptr;  // HAVE_CPPHTTPLIB 时持有 std::thread*
};

}  // namespace web
}  // namespace eb