// edge-box/src/common/common.h
// 公共类型、日志、工具（仅依赖 C++17 标准库，便于无第三方依赖的自测/CI）
#pragma once

#include <array>
#include <ctime>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace eb {

// ---------- 日志 ----------
inline std::string NowIso() {
  std::time_t t = std::time(nullptr);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

#define LOG_INFO(...)  do { std::printf("[%s][INFO ] ", eb::NowIso().c_str()); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#define LOG_WARN(...)  do { std::printf("[%s][WARN ] ", eb::NowIso().c_str()); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#define LOG_ERROR(...) do { std::fprintf(stderr, "[%s][ERROR] ", eb::NowIso().c_str()); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)

// ---------- 图像帧 ----------
// 与 OpenCV BGR 布局一致；无 OpenCV 时也可用（纯内存数据）。
struct ImageFrame {
  int width = 0;
  int height = 0;
  int channels = 3;              // 3 = BGR
  std::vector<uint8_t> data;     // width*height*channels

  uint8_t* Ptr(int y, int x) { return data.data() + (y * width + x) * channels; }
  const uint8_t* Ptr(int y, int x) const { return data.data() + (y * width + x) * channels; }
};

// ---------- 人脸检测结果 ----------
struct FaceBox {
  float x = 0, y = 0, w = 0, h = 0;  // 原图坐标
  float score = 0;
  std::array<float, 10> kps{};        // 5 点关键点 (x,y)*5
};

// ---------- 人脸特征 ----------
constexpr int kFeatureDim = 512;
using Feature = std::array<float, kFeatureDim>;

// ---------- 人员类型（与后台契约一致） ----------
enum PersonType : int8_t { kCustomer = 0, kStaff = 1 };

// ---------- 识别结果 ----------
struct Recognition {
  std::string track_id;
  int64_t customer_id = -1;       // -1 = 匿名
  PersonType person_type = kCustomer;
  float similarity = 0.f;
  int direction = -1;              // 0 进 / 1 出；-1 未判定
  std::string camera_id;
  int64_t created_at = 0;          // Unix 秒
  std::string snapshot_b64;        // 抓拍图（base64）
  std::string snapshot_mime;       // image/jpeg / image/bmp
};

}  // namespace eb