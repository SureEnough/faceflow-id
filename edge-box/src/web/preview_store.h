// edge-box/src/web/preview_store.h
// 每路相机最近一帧预览缓存：
//   worker 线程在流水线处理完一帧后调用 Capture（节流编码，避免每帧都编码），
//   Web 线程通过 GET /api/preview 读取（见 web_server.cpp）。
// 编码复用 EncodeSnapshotBase64：有 OpenCV -> JPEG，无依赖 -> BMP（浏览器可直显）。
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "common/common.h"

namespace eb {
namespace web {

class PreviewStore {
 public:
  // 保存一帧：同一相机距上次编码 >= kMinIntervalMs 才重新编码一次。
  // 帧会被降采样到宽 <= kMaxPreviewWidth，控制编码成本与传输体积。
  bool Capture(const std::string& camera_id, const ImageFrame& frame);

  // 读取最近一帧编码；无帧/编码失败返回 false
  bool Get(const std::string& camera_id, std::string& mime, std::string& b64) const;
  bool Has(const std::string& camera_id) const;

  // 节流与尺寸
  static constexpr int64_t kMinIntervalMs = 1000;
  static constexpr int kMaxPreviewWidth = 480;

 private:
  struct Item {
    std::string mime;
    std::string b64;
    int64_t updated_ms = 0;  // steady_clock 毫秒
  };

  std::map<std::string, Item> items_;
  mutable std::mutex mu_;
};

}  // namespace web
}  // namespace eb