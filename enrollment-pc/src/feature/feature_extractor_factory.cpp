// enrollment-pc/src/feature/feature_extractor_factory.cpp
#include "feature/feature_extractor.h"

#include "feature/mock_feature_extractor.h"
#include "feature/ort_feature_extractor.h"

namespace enroll {

IFeatureExtractor* CreateFeatureExtractor(FeatureExtractorType type) {
  switch (type) {
    case FeatureExtractorType::kMock:
      return new MockFeatureExtractor();
    case FeatureExtractorType::kOnnx:
      if (OrtFeatureExtractorAvailable()) return new OrtFeatureExtractor();
      return nullptr;
  }
  return nullptr;
}

}  // namespace enroll