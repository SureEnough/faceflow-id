#include "video/video_source.h"

#include <cmath>
#include <cstring>

namespace eb {

// ---------------- Mock 合成帧源 ----------------
// 生成渐变背景 + 中央移动矩形（模拟"人"），保证 Detect 稳定输出中央框。
class MockVideoSource : public VideoSource {
 public:
  MockVideoSource(int w, int h) : w_(w), h_(h) { frame_.width = w; frame_.height = h; frame_.channels = 3; frame_.data.resize(w * h * 3); }
  bool Open() override { return true; }

  bool Read(ImageFrame& frame) override {
    frame_ = ImageFrame{};
    frame_.width = w_; frame_.height = h_; frame_.channels = 3;
    frame_.data.resize(static_cast<size_t>(w_) * h_ * 3);
    ++frame_no_;
    // 移动方块：x 随帧号缓慢右移，模拟跨越中线（便于客流自测）
    int block = std::min(w_, h_) / 6;
    int x = static_cast<int>((static_cast<double>(frame_no_) * 3.0) / w_ * block) % (w_ - block);
    int y = h_ / 2 - block / 2;
    for (int yy = 0; yy < h_; ++yy) {
      for (int xx = 0; xx < w_; ++xx) {
        uint8_t* p = frame_.Ptr(yy, xx);
        bool inBlock = xx >= x && xx < x + block && yy >= y && yy < y + block;
        p[0] = inBlock ? 200 : static_cast<uint8_t>(40 + (xx + yy) % 64);
        p[1] = inBlock ? 120 : static_cast<uint8_t>(40 + (xx * 2) % 64);
        p[2] = inBlock ? 60 : static_cast<uint8_t>(40 + (yy * 2) % 64);
      }
    }
    frame = frame_;
    return true;
  }
  int Width() const override { return w_; }
  int Height() const override { return h_; }

 private:
  int w_, h_;
  size_t frame_no_ = 0;
  ImageFrame frame_;
};

#if defined(HAVE_OPENCV)
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

class OpenCVVideoSource : public VideoSource {
 public:
  explicit OpenCVVideoSource(std::string url) : url_(std::move(url)) {}
  bool Open() override {
    if (url_.rfind("usb", 0) == 0) {
      int idx = 0;
      try { idx = std::stoi(url_.substr(3)); } catch (...) { idx = 0; }
      cap_.open(idx);
    } else {
      cap_.open(url_);
    }
    return cap_.isOpened();
  }
  bool Read(ImageFrame& frame) override {
    cv::Mat mat;
    if (!cap_.read(mat) || mat.empty()) return false;
    frame = ImageFrame{};
    frame.width = mat.cols; frame.height = mat.rows; frame.channels = mat.channels();
    frame.data.assign(mat.data, mat.data + mat.total() * mat.channels());
    return true;
  }
  int Width() const override { return static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH)); }
  int Height() const override { return static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT)); }

 private:
  std::string url_;
  cv::VideoCapture cap_;
};
#endif  // HAVE_OPENCV

std::unique_ptr<VideoSource> CreateVideoSource(const std::string& url, int width, int height) {
#if defined(HAVE_OPENCV)
  if (!url.empty() && url != "-") return std::make_unique<OpenCVVideoSource>(url);
#endif
  (void)url;
  return std::make_unique<MockVideoSource>(width, height);
}

}  // namespace eb