// edge-box/tests/web_test.cpp
// Web 配置界面真 HTTP 测试（需 cpp-httplib）：
// 鉴权、配置读写、保存重载标记、状态接口、重启标记、非法输入拒绝。
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "common/base64.h"
#include "config/config.h"
#include "web/config_manager.h"
#include "web/web_server.h"

#if defined(HAVE_CPPHTTPLIB)
#include <httplib.h>
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace eb;

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

// 找一个空闲 TCP 端口（bind 0 → 关闭后复用，测试场景可接受）
static int FreePort() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(fd);
    return 0;
  }
  socklen_t len = sizeof(addr);
  getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
  int port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

int main() {
#if !defined(HAVE_CPPHTTPLIB)
  std::printf("web_test skipped: HAVE_CPPHTTPLIB not defined\n");
  return 0;
#else
  // 临时配置
  std::string cfg_path = "/tmp/edge_box_web_test.json";
  {
    eb::Config cfg;
    cfg.device_id = 7;
    cfg.web_enabled = true;
    cfg.web_port = 0;
    cfg.web_username = "admin";
    cfg.web_password = "pass123";
    cfg.cameras.push_back(eb::CameraConfig{
        "cam-t", "-", "entrance", true, 0, 0, 100, 100, "both"});
    CHECK(cfg.SaveFile(cfg_path), "config save file");
  }

  eb::web::ConfigManager cm(cfg_path, eb::Config{});
  eb::Config disk;
  eb::Config::LoadFile(cfg_path, disk);
  cm.SetConfig(disk);

  eb::web::StatusBoard board;
  {
    std::vector<eb::web::CameraStatus> cams;
    eb::web::CameraStatus cs;
    cs.camera_id = "cam-t";
    cs.opened = true;
    cs.frames = 42;
    cs.flow_in = 3;
    cs.flow_out = 1;
    cams.push_back(cs);
    board.Update(7, "mock", "test", 100, cams, 5);
  }

  const int port = FreePort();
  CHECK(port > 0, "free port found");
  std::atomic<bool> stop_flag{false};
  std::atomic<bool> started{false};
  eb::web::WebServer ws(&cm, &board);
  CHECK(ws.Start(port, "admin", "pass123", "/tmp/edge_box_no_dist", stop_flag), "web server start");
  started.store(true);

  // 等服务起来
  httplib::Client cli("http://127.0.0.1:" + std::to_string(port));
  cli.set_connection_timeout(2);
  bool up = false;
  for (int i = 0; i < 50; ++i) {
    auto res = cli.Get("/api/status");
    if (res) { up = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  CHECK(up, "web server reachable");

  // 未鉴权 → 401
  {
    auto res = cli.Get("/api/config");
    CHECK(res && res->status == 401, "config without auth -> 401");
  }
  // 错误密码 → 401
  {
    httplib::Headers h{{"Authorization", "Basic " + eb::Base64Encode("admin:wrong")}};
    auto res = cli.Get("/api/config", h);
    CHECK(res && res->status == 401, "config wrong password -> 401");
  }

  httplib::Headers auth{{"Authorization", "Basic " + eb::Base64Encode("admin:pass123")}};

  // GET /api/config → 200 且含 cameras
  {
    auto res = cli.Get("/api/config", auth);
    CHECK(res && res->status == 200, "config with auth -> 200");
    CHECK(res && res->body.find("\"cameras\"") != std::string::npos, "config contains cameras");
    CHECK(res && res->body.find("device_psk") != std::string::npos, "config contains device_psk field");
  }

  // GET /api/status → 200
  {
    auto res = cli.Get("/api/status", auth);
    CHECK(res && res->status == 200, "status -> 200");
    CHECK(res && res->body.find("\"flow_in\":3") != std::string::npos, "status contains flow_in");
  }

  // PUT /api/config 修改阈值 → 200 + 重载标记
  {
    std::string body = R"({"det_thresh":0.62,"recog_thresh":0.45,"device_id":8})";
    auto res = cli.Put("/api/config", auth, body, "application/json");
    CHECK(res && res->status == 200, "put config -> 200");
    CHECK(cm.reload_pending(), "reload flag set after put");
    cm.ConsumeReload();
    eb::Config snap = cm.Snapshot();
    CHECK(snap.det_thresh > 0.61f && snap.device_id == 8, "put applied to config");
    // 磁盘上也持久化
    eb::Config disk2;
    eb::Config::LoadFile(cfg_path, disk2);
    CHECK(disk2.det_thresh > 0.61f, "put persisted to file");
  }

  // PUT 非法 JSON → 400，重载标记不置位
  {
    auto res = cli.Put("/api/config", auth, "not-json{", "application/json");
    CHECK(res && res->status == 400, "invalid json -> 400");
    CHECK(!cm.reload_pending(), "reload flag not set on failure");
  }

  // POST /api/reload → 200 且重载标记置位
  {
    auto res = cli.Post("/api/reload", auth, "", "application/json");
    CHECK(res && res->status == 200, "reload -> 200");
    CHECK(cm.reload_pending(), "reload flag set by /api/reload");
    cm.ConsumeReload();
  }

  // POST /api/restart → 200 且 stop_flag 置位
  {
    auto res = cli.Post("/api/restart", auth, "", "application/json");
    CHECK(res && res->status == 200, "restart -> 200");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(stop_flag.load(), "stop_flag set by restart");
  }

  ws.Stop();
  started.store(false);
  std::remove(cfg_path.c_str());
  (void)started;

  std::printf("web test finished: %s\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURE");
  return g_fail == 0 ? 0 : 1;
#endif
}