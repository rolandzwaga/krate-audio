#include "body_classifier.h"

namespace MembrumFit {

BodyScoreList classifyBody(const ModalDecomposition& /*modes*/,
                           const AttackFeatures& /*features*/) {
    // Phase 1 fast-path: Membrane wins with full confidence. Phase 2 replaces
    // this with the full 6-way mode-ratio scoring per spec §4.6.
    BodyScoreList s{};
    for (int i = 0; i < 6; ++i) {
        s[i].body = static_cast<Membrum::BodyModelType>(i);
        s[i].score = (i == 0) ? 0.0f : 1.0f;
        s[i].confidence = (i == 0) ? 1.0f : 0.0f;
    }
    return s;
}

Membrum::BodyModelType pickBestBody(const BodyScoreList& scores) {
    Membrum::BodyModelType best = scores[0].body;
    float bestScore = scores[0].score;
    for (const auto& s : scores) if (s.score < bestScore) { bestScore = s.score; best = s.body; }
    return best;
}

}  // namespace MembrumFit
