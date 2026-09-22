// enrollment-pc/tests/feature_test.cpp
// feature 模块纯 C++ 自测：5 点对齐、Mock 特征提取流水线、与 verify_logic 集成。
#include <cmath>
#include <cstdio>
#include <cstring>

#include "feature/align.h"
#include "feature/feature_extractor.h"
#include "feature/ort_feature_extractor.h"
#include "verify/verify_logic.h"

using namespace enroll;

static int g_fail = 0;
#define CHECK(cond, msg)                                        \
  do {                                                          \
    if (!(cond)) {                                              \
      std::printf("[FAIL] %s\n", msg);                          \
      ++g_fail;                                                 \
    } else {                                                    \
      std::printf("[ OK ] %s\n", msg);                          \
    }                                                           \
  } while (0)

namespace {

// 构造一张纯色 BGR 帧（可加 jitter 生成不同图）
ImageFrame MakeFrame(int w, int h, uint8_t base) {
  ImageFrame f;
  f.width = w; f.height = h; f.channels = 3;
  f.data.assign(static_cast<size_t>(w) * h * 3, base);
  // 加一点梯度，避免“全零帧”退化
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint8_t* p = f.Ptr(y, x);
      p[0] = static_cast<uint8_t>((base + x) % 256);
      p[1] = static_cast<uint8_t>((base + y) % 256);
      p[2] = static_cast<uint8_t>((base + x + y) % 256);
    }
  }
  return f;
}

// 标准 5 点（dst 带点扰动成 src），用于对齐数值验证
std::array<std::pair<float, float>, 5> MakeFacePts(float cx, float cy, float scale) {
  std::array<std::pair<float, float>, 5> pts;
  const auto& dst = align::ArcFaceDst112();
  for (int i = 0; i < 5; ++i) {
    pts[static_cast<size_t>(i)] = {cx + (dst[static_cast<size_t>(i)].first - 56.f) * scale,
                                   cy + (dst[static_cast<size_t>(i)].second - 56.f) * scale};
  }
  return pts;
}

}  // namespace

int main() {
  // ========== 1. 对齐：已知相似变换恢复 ==========
  {
    auto src = MakeFacePts(220.f, 180.f, 1.2f);   // 脸部中心在 (220,180)，尺度 1.2
    const auto& dst = align::ArcFaceDst112();
    align::Similarity sim;
    CHECK(align::EstimateSimilarity(src, dst, sim), "estimate similarity from face pts");
    // 源点映射后应落在目标点附近
    bool okMap = true;
    for (int i = 0; i < 5; ++i) {
      float mx = sim.MapX(src[static_cast<size_t>(i)].first, src[static_cast<size_t>(i)].second);
      float my = sim.MapY(src[static_cast<size_t>(i)].first, src[static_cast<size_t>(i)].second);
      if (std::fabs(mx - dst[static_cast<size_t>(i)].first) > 0.5f ||
          std::fabs(my - dst[static_cast<size_t>(i)].second) > 0.5f) okMap = false;
    }
    CHECK(okMap, "mapped pts close to ArcFace dst (error < 0.5px)");
    CHECK(std::fabs(sim.scale - 1.f / 1.2f) < 0.02f, "estimated scale ~ 1/1.2");
  }

  // 2. 对齐：warp 输出尺寸与像素采样
  {
    ImageFrame src = MakeFrame(300, 300, 70);
    const auto& dst = align::ArcFaceDst112();
    auto pts = MakeFacePts(150.f, 150.f, 1.0f);
    align::Similarity sim;
    CHECK(align::EstimateSimilarity(pts, dst, sim), "estimate similarity for warp");
    ImageFrame warped;
    align::WarpAffine(src, sim, 112, 112, warped);
    CHECK(warped.width == 112 && warped.height == 112, "warp output 112x112");
    CHECK(warped.channels == 3, "warp keeps channels");
    CHECK(warped.data.size() == 112u * 112u * 3u, "warp buffer size");
    // 脸部中心（56,56）应采样自 src 中心附近（颜色与 base 相关，非全黑）
    uint8_t* cpx = warped.Ptr(56, 56);
    CHECK(cpx[0] != 0 || cpx[1] != 0 || cpx[2] != 0, "warp center not empty");
    // 越界区域填 0（缩放后 112x112 基本全部落在 300x300 内，这里只验证函数不崩）
    ImageFrame bad{};
    ImageFrame w2;
    align::WarpAffine(bad, sim, 16, 16, w2);
    CHECK(w2.width == 16 && w2.height == 16 && w2.data.size() == 16u * 16u * 3u && w2.data[0] == 0,
          "empty src -> fixed-size zero-filled output");
  }

  // ========== 3. Mock 特征提取流水线 ==========
  {
    IFeatureExtractor* ex = CreateFeatureExtractor(FeatureExtractorType::kMock);
    CHECK(ex != nullptr, "create mock extractor");
    if (ex) {
      CHECK(ex->LoadModel(ModelPaths{}), "mock load model");
      ImageFrame a = MakeFrame(112, 112, 30);
      ImageFrame b = MakeFrame(112, 112, 200);

      Feature fa1{}, fa2{}, fb{};
      CHECK(ex->DetectAlignExtract(a, fa1), "mock extract frame a");
      CHECK(ex->ExtractAligned(a, fa2), "mock extract aligned a");
      CHECK(ex->ExtractAligned(b, fb), "mock extract aligned b");

      bool same = true;
      for (size_t i = 0; i < kFeatureDim; ++i) {
        if (std::fabs(fa1[i] - fa2[i]) > 1e-6f) same = false;
      }
      CHECK(same, "same image -> same feature");

      bool diff = false;
      for (size_t i = 0; i < kFeatureDim; ++i) {
        if (std::fabs(fa1[i] - fb[i]) > 1e-4f) diff = true;
      }
      CHECK(diff, "different image -> different feature");

      // L2 范数 ≈ 1
      double norm2 = 0;
      for (size_t i = 0; i < kFeatureDim; ++i) norm2 += static_cast<double>(fa1[i]) * fa1[i];
      CHECK(std::fabs(norm2 - 1.0) < 1e-3, "feature normalized (norm2 ~ 1)");

      CHECK(std::fabs(ex->Liveness(a) - 1.0f) < 1e-6f, "mock liveness = 1.0 (no model)");

      // 与 verify_logic 集成：同图特征 1:1 通过，不同图显著低于阈值
      CHECK(VerifyPass(fa1, fa2, 0.5f), "verify pass same image");
      CHECK(!VerifyPass(fa1, fb, 0.99f), "verify reject different image @0.99");
    }
    delete ex;
  }

  // ========== 4. ORT 可用性 ==========
  {
#if defined(HAVE_ONNXRUNTIME)
    CHECK(OrtFeatureExtractorAvailable(), "ort available (HAVE_ONNXRUNTIME)");
    IFeatureExtractor* ex = CreateFeatureExtractor(FeatureExtractorType::kOnnx);
    CHECK(ex != nullptr, "create ort extractor");
    delete ex;
#else
    CHECK(!OrtFeatureExtractorAvailable(), "ort unavailable without HAVE_ONNXRUNTIME");
    CHECK(CreateFeatureExtractor(FeatureExtractorType::kOnnx) == nullptr,
          "kOnnx factory returns nullptr without ORT");
#endif
  }

  // ========== 5. 空帧健壮性 ==========
  {
    IFeatureExtractor* ex = CreateFeatureExtractor(FeatureExtractorType::kMock);
    if (ex) {
      ImageFrame empty{};
      Feature f{};
      CHECK(!ex->ExtractAligned(empty, f), "mock reject empty frame");
    }
    delete ex;
  }

  std::printf("feature test finished: %s\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURE");
  return g_fail == 0 ? 0 : 1;
}