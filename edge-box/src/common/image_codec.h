// edge-box/src/common/image_codec.h
// 抓拍图编码：有 OpenCV 时输出 JPEG；无依赖时输出 BMP（纯标准库，浏览器可直显）。
#pragma once

#include <string>

#include "common/common.h"

namespace eb {

// 将 BGR 帧编码为图片（JPEG/BMP），输出 base64 与 MIME。
// 失败返回 false（如空帧/尺寸非法）。
bool EncodeSnapshotBase64(const ImageFrame& frame, std::string& out_b64, std::string& out_mime);

}  // namespace eb