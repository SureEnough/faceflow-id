#include "recognizer/recognizer.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace eb {

namespace {
float Dot(const Feature& a, const Feature& b) {
  double s = 0;
  for (int i = 0; i < kFeatureDim; ++i) s += static_cast<double>(a[i]) * b[i];
  return static_cast<float>(s);
}
}  // namespace

void Recognizer::LoadLibrary(const std::vector<Identity>& identities) {
  identities_ = identities;
}

void Recognizer::Remove(int64_t customer_id) {
  identities_.erase(
      std::remove_if(identities_.begin(), identities_.end(),
                     [customer_id](const Identity& it) { return it.customer_id == customer_id; }),
      identities_.end());
}

void Recognizer::Upsert(const Identity& identity) {
  for (auto& it : identities_) {
    if (it.customer_id == identity.customer_id) {
      it = identity;
      return;
    }
  }
  identities_.push_back(identity);
}

MatchResult Recognizer::Search(const Feature& query, float threshold) const {
  MatchResult best;
  for (const auto& id : identities_) {
    if (id.status != 0) continue;  // 黑名单/注销/离职不参与识别
    float sim = Dot(query, id.feature);  // 特征均 L2 归一化，内积=余弦
    if (sim > best.similarity) {
      best.similarity = sim;
      best.customer_id = id.customer_id;
      best.person_type = id.person_type;
    }
  }
  best.hit = best.customer_id >= 0 && best.similarity >= threshold;
  return best;
}

}  // namespace eb