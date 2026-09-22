// edge-box/src/web/preview_store.cpp
#include "web/preview_store.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "common/image_codec.h"

namespace eb {
namespace web {

namespace {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// 最近邻等比缩放到宽 <= maxW（预览用，避免大图编码开销）
void ResizeDownNearest(const ImageFrame& src, int maxW, ImageFrame& out) {
  if (src.width <= maxW || src.width <= 0 || src.height <= 0 || src.channels < 1) {
    out = src;
    return;
  }
  const int w = maxW;
  const int h = (src.height * maxW) / src.width;
  out = ImageFrame{};
  out.width = w;
  out.height = h;
  out.channels = src.channels;
  out.data.assign(static_cast<size_t>(w) * h * out.channels, 0);
  for (int y = 0; y < h; ++y) {
    const int sy = (y * src.height) / h;
    for (int x = 0; x < w; ++x) {
      const int sx = (x * src.width) / w;
      std::memcpy(out.Ptr(y, x), src.Ptr(sy, sx), static_cast<size_t>(out.channels));
    }
  }
}

}  // namespace

bool PreviewStore::Capture(const std::string& camera_id, const ImageFrame& frame) {
  if (frame.width <= 0 || frame.height <= 0 || frame.channels < 1) return false;
  const int64_t now = NowMs();

  std::lock_guard<std::mutex> lk(mu_);
  auto it = items_.find(camera_id);
  if (it != items_.end() && now - it->second.updated_ms < kMinIntervalMs) {
    return false;  // 节流：未到编码间隔
  }

  ImageFrame small;
  ResizeDownNearest(frame, kMaxPreviewWidth, small);
  std::string b64, mime;
  if (!EncodeSnapshotBase64(small, b64, mime) || b64.empty()) {
    return false;
  }
  items_[camera_id] = Item{mime, b64, now};
  return true;
}

bool PreviewStore::Get(const std::string& camera_id, std::string& mime, std::string& b64) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = items_.find(camera_id);
  if (it == items_.end()) return false;
  mime = it->second.mime;
  b64 = it->second.b64;
  return true;
}

bool PreviewStore::Has(const std::string& camera_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  return items_.find(camera_id) != items_.end();
}

}  // namespace web
}  // namespace eb