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

// 真实解码：jpg/png/bmp -> BGR 帧
bool DecodeImageBytes(const std::string& data, ImageFrame& out) {
  if (data.empty()) return false;
  std::vector<uint8_t> buf(data.begin(), data.end());
  cv::Mat raw(1, static_cast<int>(buf.size()), CV_8UC1, buf.data());
  cv::Mat mat = cv::imdecode(raw, cv::IMREAD_COLOR);
  if (mat.empty()) return false;
  ImageFrame f;
  f.width = mat.cols;
  f.height = mat.rows;
  f.channels = 3;
  f.data.assign(mat.data, mat.data + mat.total() * mat.channels());
  out = std::move(f);
  return true;
}

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

namespace {
// 最小 BMP 解码（24/32 位 BGR，支持自底向上/自顶向下）
bool DecodeBmp(const std::string& data, ImageFrame& out) {
  if (data.size() < 54 || data[0] != 'B' || data[1] != 'M') return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
  int32_t w = p[18] | (p[19] << 8) | (p[20] << 16) | (p[21] << 24);
  int32_t h_raw = p[22] | (p[23] << 8) | (p[24] << 16) | (p[25] << 24);
  uint16_t bpp = static_cast<uint16_t>(p[28] | (p[29] << 8));
  int32_t data_off = p[10] | (p[11] << 8) | (p[12] << 16) | (p[13] << 24);
  if (w <= 0 || h_raw == 0 || (bpp != 24 && bpp != 32)) return false;
  if (data_off < 0 || static_cast<size_t>(data_off) >= data.size()) return false;
  const bool top_down = h_raw < 0;
  const int32_t h = h_raw < 0 ? -h_raw : h_raw;
  const int bytes_pp = bpp / 8;
  const int row_size = ((w * bytes_pp + 3) / 4) * 4;
  if (static_cast<int64_t>(data_off) + static_cast<int64_t>(row_size) * (h - 1) +
          static_cast<int64_t>(w) * bytes_pp > static_cast<int64_t>(data.size())) {
    return false;
  }
  ImageFrame f;
  f.width = w;
  f.height = h;
  f.channels = 3;
  f.data.resize(static_cast<size_t>(w) * h * 3);
  for (int y = 0; y < h; ++y) {
    int src_y = top_down ? y : (h - 1 - y);
    const uint8_t* row = p + data_off + static_cast<size_t>(src_y) * row_size;
    uint8_t* dst = f.data.data() + static_cast<size_t>(y) * w * 3;
    for (int x = 0; x < w; ++x) {
      dst[x * 3 + 0] = row[x * bytes_pp + 0];  // B
      dst[x * 3 + 1] = row[x * bytes_pp + 1];  // G
      dst[x * 3 + 2] = row[x * bytes_pp + 2];  // R
    }
  }
  out = std::move(f);
  return true;
}
}  // namespace

// 无 OpenCV：BMP 真实解码；其余格式构造合成帧供 Mock 联调
bool DecodeImageBytes(const std::string& data, ImageFrame& out) {
  if (data.empty()) return false;
  if (data.size() >= 2 && data[0] == 'B' && data[1] == 'M') return DecodeBmp(data, out);
  constexpr size_t kFrameBytes = 320 * 240 * 3;
  ImageFrame f;
  f.width = 320;
  f.height = 240;
  f.channels = 3;
  f.data.resize(kFrameBytes);
  for (size_t i = 0; i < kFrameBytes; ++i) f.data[i] = data[i % data.size()];
  out = std::move(f);
  return true;
}
#endif

}  // namespace eb