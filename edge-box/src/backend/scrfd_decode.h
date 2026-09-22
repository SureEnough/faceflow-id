// edge-box/src/backend/scrfd_decode.h
// SCRFD 原版 anchor 解码（文档 15.1）：模型输出为相对锚点中心的距离（按 stride 缩放），
// 非绝对坐标。本模块为纯函数，便于单测；支持 [C,N] 与 [N,C]（/ [N,k]）两种张量布局。
#pragma once

#include <cstddef>
#include <vector>

#include "common/common.h"

namespace eb {
namespace scrfd {

struct DecodeOptions {
  float det_thresh = 0.5f;
  int max_face_num = 64;   // 单帧上限
  float nms_thresh = 0.4f; // IoU
};

// 单个 stride 的原始输出（指针均为原始张量数据，布局由 *_cn 决定）
struct StrideOutput {
  int stride = 8;
  int count = 0;            // 锚点数

  const float* scores = nullptr;
  int score_channels = 1;   // 类别数；>=2 时取 score_face_channel
  int score_face_channel = 1;  // bg/face 约定中 face 的通道下标
  bool score_cn = true;     // true: [C,N]（score[ch*n+i]），false: [N,C]（score[i*C+ch]）

  const float* boxes = nullptr;
  bool box_cn = true;       // true: [4,N]（box[ch*n+i]），false: [N,4]（box[i*4+ch]）

  const float* kps = nullptr;
  bool kps_cn = true;       // true: [10,N]，false: [N,10]
};

// 按布局取分数/框分量/关键点分量
inline float ScoreAt(const StrideOutput& o, int i) {
  return o.score_cn ? o.scores[static_cast<size_t>(o.score_face_channel) * o.count + i]
                    : o.scores[static_cast<size_t>(i) * o.score_channels + o.score_face_channel];
}
inline float BoxAt(const StrideOutput& o, int i, int k) {
  return o.box_cn ? o.boxes[static_cast<size_t>(k) * o.count + i]
                  : o.boxes[static_cast<size_t>(i) * 4 + k];
}
inline float KpsAt(const StrideOutput& o, int i, int k) {
  return o.kps_cn ? o.kps[static_cast<size_t>(k) * o.count + i]
                  : o.kps[static_cast<size_t>(i) * 10 + k];
}

// 将各 stride 原始输出解码到输入图坐标系（0..input_size），再按 NMS 过滤。
// in_scale_x/y 用于从输入图坐标缩放到原图；输出坐标直接落在原图空间。
std::vector<FaceBox> Decode(const std::vector<StrideOutput>& outs, int input_size,
                            float in_scale_x, float in_scale_y,
                            const DecodeOptions& opt);

// 从锚点数推断 stride（锚点网格 = input_size/stride 的平方）
int InferStride(int anchor_count, int input_size);

// 工具：IoU
float IoU(const FaceBox& a, const FaceBox& b);

}  // namespace scrfd
}  // namespace eb