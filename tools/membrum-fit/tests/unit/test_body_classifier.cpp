// Body classifier smoke: synth-like integer harmonics -> String.
#include "src/body_classifier.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Body classifier: integer-ratio modes => String") {
    MembrumFit::ModalDecomposition md;
    for (int k = 1; k <= 8; ++k) {
        MembrumFit::Mode m;
        m.freqHz    = 200.0f * k;
        m.amplitude = 1.0f / static_cast<float>(k);
        m.decayRate = 5.0f;
        md.modes.push_back(m);
    }
    md.totalRms    = 1.0f;
    md.residualRms = 0.01f;
    MembrumFit::AttackFeatures f{};
    const auto scores = MembrumFit::classifyBody(md, f);
    const auto best = MembrumFit::pickBestBody(scores);
    REQUIRE(best == Membrum::BodyModelType::String);
}

TEST_CASE("Body classifier: shell ratio signature {1, 2.757, 5.404}") {
    MembrumFit::ModalDecomposition md;
    const float ratios[] = { 1.0f, 2.757f, 5.404f, 8.933f, 13.344f };
    for (float r : ratios) {
        MembrumFit::Mode m;
        m.freqHz    = 300.0f * r;
        m.amplitude = 1.0f / r;
        m.decayRate = 6.0f;
        md.modes.push_back(m);
    }
    md.totalRms    = 1.0f;
    md.residualRms = 0.01f;
    MembrumFit::AttackFeatures f{};
    const auto scores = MembrumFit::classifyBody(md, f);
    const auto best = MembrumFit::pickBestBody(scores);
    REQUIRE(best == Membrum::BodyModelType::Shell);
}
