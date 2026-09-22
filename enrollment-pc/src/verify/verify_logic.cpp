// enrollment-pc/src/verify/verify_logic.cpp
#include "verify/verify_logic.h"

#include <cmath>

namespace enroll {

float CosineSimilarity(const Feature& a, const Feature& b) {
  double s = 0;
  for (size_t i = 0; i < kFeatureDim; ++i) {
    s += static_cast<double>(a[i]) * b[i];
  }
  return static_cast<float>(s);
}

bool VerifyPass(const Feature& live, const Feature& id_photo, float threshold) {
  return CosineSimilarity(live, id_photo) >= threshold;
}

}  // namespace enroll