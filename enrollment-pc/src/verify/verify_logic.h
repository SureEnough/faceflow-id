// enrollment-pc/src/verify/verify_logic.h
// 1:1 人证比对逻辑（架构文档 15.4）：伯 512 维 L2 归一化特征余弦相似度 + 阈值判定。
// 纯 C++，无 Qt 依赖，便于单元自测。
#pragma once

#include <array>
#include <cstddef>

namespace enroll {

constexpr size_t kFeatureDim = 512;
using Feature = std::array<float, kFeatureDim>;

// 内积 = 余弦相似度（两特征均已 L2 归一化）
float CosineSimilarity(const Feature& a, const Feature& b);

// 判定是否通过：similarity >= threshold
bool VerifyPass(const Feature& live, const Feature& id_photo, float threshold);

}  // namespace enroll