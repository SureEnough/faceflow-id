// edge-box/src/common/image_codec.cpp
#include "common/image_codec.h"

#include <cstring>
#include <vector>

#include "common/base64.h"

namespace eb {

#if defined(HAVE_OPENCV)
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

bool EncodeSnapshotBase64(const ImageFrame& frame, std::string& out_b64, std::string& out_mime) {
  if (frame.width <= 0 || frame.height <= 0 || frame.data.empty()) return false;
  cv::Mat mat(frame.height, frame.width, CV_8UC3,
              const_cast<uint8_t*>(frame.data.data()), frame.width * 3);
  std::vector<uint8_t> buf;
  std::vector<int> params{cv::IMWRITE_JPEG_QUALITY, 85};
  if (!cv::imencode(".jpg", mat, buf, params)) return false;
  out_b64 = Base64Encode(buf.data(), buf.size());
  out_mime = "image/jpeg";
  return true;
}
#else  // !HAVE_OPENCV
// 最小 BMP 编码（24 位 BGR，自底向上，4 字节行对齐）
bool EncodeSnapshotBase64(const ImageFrame& frame, std::string& out_b64, std::string& out_mime) {
  if (frame.width <= 0 || frame.height <= 0 || frame.data.empty()) return false;
  const int w = frame.width, h = frame.height;
  const int rowSize = ((w * 3 + 3) / 4) * 4;
  const int pixelBytes = rowSize * h;
  const int fileSize = 54 + pixelBytes;

  std::vector<uint8_t> bmp(fileSize, 0);
  // BITMAPFILEHEADER (14)
  bmp[0] = 'B'; bmp[1] = 'M';
  bmp[2] = static_cast<uint8_t>(fileSize & 0xFF);
  bmp[3] = static_cast<uint8_t>((fileSize >> 8) & 0xFF);
  bmp[4] = static_cast<uint8_t>((fileSize >> 16) & 0xFF);
  bmp[5] = static_cast<uint8_t>((fileSize >> 24) & 0xFF);
  bmp[10] = 54;  // 数据偏移
  // BITMAPINFOHEADER (40)
  bmp[14] = 40;                       // 头大小
  bmp[18] = static_cast<uint8_t>(w & 0xFF);
  bmp[19] = static_cast<uint8_t>((w >> 8) & 0xFF);
  bmp[20] = static_cast<uint8_t>((w >> 16) & 0xFF);
  bmp[21] = static_cast<uint8_t>((w >> 24) & 0xFF);
  bmp[22] = static_cast<uint8_t>(h & 0xFF);
  bmp[23] = static_cast<uint8_t>((h >> 8) & 0xFF);
  bmp[24] = static_cast<uint8_t>((h >> 16) & 0xFF);
  bmp[25] = static_cast<uint8_t>((h >> 24) & 0xFF);
  bmp[26] = 1;                        // 颜色平面
  bmp[28] = 24;                       // 位深
  bmp[34] = static_cast<uint8_t>(pixelBytes & 0xFF);
  bmp[35] = static_cast<uint8_t>((pixelBytes >> 8) & 0xFF);
  bmp[36] = static_cast<uint8_t>((pixelBytes >> 16) & 0xFF);
  bmp[37] = static_cast<uint8_t>((pixelBytes >> 24) & 0xFF);

  const int ch = frame.channels;
  for (int y = 0; y < h; ++y) {
    const uint8_t* src = frame.Ptr(h - 1 - y, 0);  // 自底向上
    uint8_t* dst = bmp.data() + 54 + static_cast<size_t>(y) * rowSize;
    for (int x = 0; x < w; ++x) {
      dst[x * 3 + 0] = src[x * ch + 0];
      dst[x * 3 + 1] = src[x * ch + 1];
      dst[x * 3 + 2] = src[x * ch + 2];
    }
  }
  out_b64 = Base64Encode(bmp.data(), bmp.size());
  out_mime = "image/bmp";
  return true;
}
#endif

}  // namespace eb