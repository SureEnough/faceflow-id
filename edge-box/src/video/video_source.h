// edge-box/src/video/video_source.h
// 帧源抽象：真实 OpenCV 摄像头（HAVE_OPENCV）/ 合成帧（EDGE_BOX_MOCK_VIDEO）。
#pragma once

#include <memory>
#include <string>

#include "common/common.h"

namespace eb {

class VideoSource {
 public:
  virtual ~VideoSource() = default;
  virtual bool Open() = 0;
  virtual bool Read(ImageFrame& frame) = 0;
  virtual int Width() const = 0;
  virtual int Height() const = 0;
};

// 创建帧源：url 以 rtsp/http/usb 开头时使用真实视频（需 HAVE_OPENCV）；否则 Mock
std::unique_ptr<VideoSource> CreateVideoSource(const std::string& url, int width = 640, int height = 480);

}  // namespace eb