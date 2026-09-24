// edge-box/src/common/image_codec.h
// 抓拍图编码/解码：有 OpenCV 时 JPEG/PNG/BMP 真实编解码；
// 无依赖时编码输出 BMP，解码支持真实 BMP + 合成帧（Mock 联调）。
#pragma once

#include <string>

#include "common/common.h"

namespace eb {

// 将 BGR 帧编码为图片（JPEG/BMP），输出 base64 与 MIME。
// 失败返回 false（如空帧/尺寸非法）。
bool EncodeSnapshotBase64(const ImageFrame& frame, std::string& out_b64, std::string& out_mime);

// 将图片字节（jpg/png/bmp）解码为 BGR 帧。
// - HAVE_OPENCV：真实解码常见图片格式；
// - 无 OpenCV：真实解码 24/32 位 BMP；其它格式构造合成帧（320x240），
//   供 Mock 推理后端联调（Mock 只依赖帧内容哈希，不真正渲染像素）。
// 失败返回 false（空输入/非法 BMP 越界）。
bool DecodeImageBytes(const std::string& data, ImageFrame& out);

}  // namespace eb