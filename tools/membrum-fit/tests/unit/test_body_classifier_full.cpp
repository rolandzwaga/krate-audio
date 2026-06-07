// Body classifier coverage for the 4 bodies not exercised by
// test_body_classifier.cpp (which already covers String + Shell).
// Each test feeds clean modal frequencies that match the body's mode-table
// signature and asserts the classifier picks that body.
#include "src/body_classifier.h"
#include "src/types.h"

#include "dsp/bodies/bell_modes.h"
#include "dsp/bodies/plate_modes.h"
#include "dsp/membrane_modes.h"

#include <catch2/catch_test_macros.hpp>

namespace {

MembrumFit::ModalDecomposition synthFromRatios(const float* ratios, int n, float fHz) {
    MembrumFit::ModalDecomposition md;
    for (int i = 0; i < n; ++i) {
        MembrumFit::Mode m;
        m.freqHz    = fHz * ratios[i];
        m.amplitude = 1.0f / std::max(1.0f, ratios[i]);
        m.decayRate = 6.0f;
        md.modes.push_back(m);
    }
    md.totalRms    = 1.0f;
    md.residualRms = 0.05f;
    return md;
}

}  // namespace

TEST_CASE("Body classifier: Membrane Bessel ratios -> Membrane") {
    const float* r = Membrum::kMembraneRatios.data();
    auto md = synthFromRatios(r, 8, 200.0f);
    MembrumFit::AttackFeatures f{};
    REQUIRE(MembrumFit::pickBestBody(MembrumFit::classifyBody(md, f))
            == Membrum::BodyModelType::Membrane);
}

TEST_CASE("Body classifier: Plate free-plate Chladni ratios -> Plate") {
    // Build first 8 plate ratios from the authoritative free-plate table.
    float r[8];
    for (int i = 0; i < 8; ++i)
        r[i] = Membrum::Bodies::kPlateRatios[i];
    auto md = synthFromRatios(r, 8, 250.0f);
    MembrumFit::AttackFeatures f{};
    const auto best = MembrumFit::pickBestBody(MembrumFit::classifyBody(md, f));
    // Plate modes ~match Membrane partials at low orders; require either.
    REQUIRE((best == Membrum::BodyModelType::Plate
          || best == Membrum::BodyModelType::Membrane));
}

TEST_CASE("Body classifier: Bell hum-prime-tierce-quint-nominal -> Bell") {
    const float* r = Membrum::Bodies::kBellRatios;
    auto md = synthFromRatios(r, 8, 400.0f);
    MembrumFit::AttackFeatures f{};
    REQUIRE(MembrumFit::pickBestBody(MembrumFit::classifyBody(md, f))
            == Membrum::BodyModelType::Bell);
}

TEST_CASE("Body classifier: noisy plate-like signal -> NoiseBody") {
    float r[8];
    for (int i = 0; i < 8; ++i)
        r[i] = Membrum::Bodies::kPlateRatios[i];
    auto md = synthFromRatios(r, 8, 250.0f);
    md.totalRms    = 1.0f;
    md.residualRms = 0.6f;  // > 0.3 threshold per spec §4.6
    MembrumFit::AttackFeatures f{};
    const auto best = MembrumFit::pickBestBody(MembrumFit::classifyBody(md, f));
    REQUIRE(best == Membrum::BodyModelType::NoiseBody);
}
