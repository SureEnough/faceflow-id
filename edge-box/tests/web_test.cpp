// edge-box/tests/web_test.cpp
// Web 配置界面真 HTTP 测试（需 cpp-httplib）：
// 鉴权、配置读写、保存重载标记、状态接口、重启标记、非法输入拒绝。
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>

#include "common/base64.h"
#include "config/config.h"
#include "web/config_manager.h"
#include "web/preview_store.h"
#include "web/web_server.h"
#include "store/recognition_store.h"

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

  // GET /api/snapshots（注入 MemStore）→ 鉴权 + 最近 N 条
  {
    auto* store = eb::CreateMemStore();
    const int64_t base = static_cast<int64_t>(std::time(nullptr));
    for (int i = 0; i < 3; ++i) {
      eb::Recognition rec;
      rec.track_id = "snap-t" + std::to_string(i);
      rec.camera_id = "cam-t";
      rec.created_at = base + i;               // i 越大越新
      rec.snapshot_b64 = "QUJD";               // 假 base64（ABC）
      rec.snapshot_mime = "image/jpeg";
      rec.customer_id = (i == 0) ? 5 : -1;     // 最新一条命中顾客 5
      rec.similarity = 0.77f;
      store->Insert(rec);
    }
    const int port2 = FreePort();
    std::atomic<bool> stop2{false};
    eb::web::WebServer ws2(&cm, &board, store);
    CHECK(ws2.Start(port2, "admin", "pass123", "/tmp/edge_box_no_dist", stop2), "snapshots ws start");
    httplib::Client cli2("http://127.0.0.1:" + std::to_string(port2));
    cli2.set_connection_timeout(2);
    bool up2 = false;
    for (int i = 0; i < 50; ++i) {
      auto res = cli2.Get("/api/status");
      if (res) { up2 = true; break; }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(up2, "snapshots ws ready");
    {
      auto res = cli2.Get("/api/snapshots");
      CHECK(res && res->status == 401, "snapshots without auth -> 401");
    }
    httplib::Headers auth2 = {{"Authorization", "Basic YWRtaW46cGFzczEyMw=="}};  // admin:pass123
    auto res = cli2.Get("/api/snapshots?limit=3", auth2);
    CHECK(res && res->status == 200, "snapshots with auth -> 200");
    if (res && res->status == 200) {
      const std::string& b = res->body;
      CHECK(b.find(R"("total")") == std::string::npos && b.find("snap-t1") != std::string::npos,
            "snapshots list contains records");
      CHECK(b.find("snap-t2") != std::string::npos, "snapshots newest included");
      CHECK(b.find("customer_id") != std::string::npos && b.find("snapshot") != std::string::npos,
            "snapshots fields present");
      CHECK(b.find("QUJD") != std::string::npos, "snapshot base64 present");
    }
    auto resL = cli2.Get("/api/snapshots?limit=1", auth2);
    CHECK(resL && resL->status == 200 && resL->body.find("snap-t2") != std::string::npos &&
          resL->body.find("snap-t1") == std::string::npos,
          "snapshots limit honored (newest first)");
    ws2.Stop();
    delete store;
  }

  // GET /api/preview（注入 PreviewStore）：鉴权 + 有帧 200 / 未知相机 404
  {
    eb::web::PreviewStore pv;
    {
      eb::ImageFrame f;
      f.width = 32; f.height = 24; f.channels = 3;
      f.data.assign(static_cast<size_t>(32) * 24 * 3, 128);
      CHECK(pv.Capture("cam-p", f), "preview capture synthetic frame");
    }
    const int port3 = FreePort();
    std::atomic<bool> stop3{false};
    eb::web::WebServer ws3(&cm, &board, nullptr, &pv);
    CHECK(ws3.Start(port3, "admin", "pass123", "/tmp/edge_box_no_dist", stop3), "preview ws start");
    httplib::Client cli3("http://127.0.0.1:" + std::to_string(port3));
    cli3.set_connection_timeout(2);
    bool up3 = false;
    for (int i = 0; i < 50; ++i) {
      auto res = cli3.Get("/api/status");
      if (res) { up3 = true; break; }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(up3, "preview ws ready");
    httplib::Headers auth3 = {{"Authorization", "Basic YWRtaW46cGFzczEyMw=="}};
    auto res1 = cli3.Get("/api/preview?camera_id=cam-p", auth3);
    CHECK(res1 && res1->status == 200, "preview with frame -> 200");
    if (res1 && res1->status == 200) {
      CHECK(res1->body.find("\"b64\"") != std::string::npos &&
            res1->body.find("\"mime\"") != std::string::npos,
            "preview fields present");
      CHECK(res1->body.size() > 64, "preview payload non-trivial");
    }
    auto res2 = cli3.Get("/api/preview?camera_id=unknown", auth3);
    CHECK(res2 && res2->status == 404, "preview unknown camera -> 404");
    auto res3 = cli3.Get("/api/preview?camera_id=cam-p");
    CHECK(res3 && res3->status == 401, "preview without auth -> 401");
    ws3.Stop();
  }

  ws.Stop();
  started.store(false);
  std::remove(cfg_path.c_str());
  (void)started;

  std::printf("web test finished: %s\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURE");
  return g_fail == 0 ? 0 : 1;
#endif
}