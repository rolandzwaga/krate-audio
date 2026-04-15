// Membrane inversion smoke test -- defaults invariant.
#include "src/mapper_inversion/membrane_inverse.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

TEST_CASE("Membrane inversion: emits normalised neutral defaults (FR-055-like)") {
    MembrumFit::ModalDecomposition empty;
    MembrumFit::AttackFeatures feats{};
    auto cfg = MembrumFit::MapperInversion::invertMembrane(empty, feats, 44100.0);
    REQUIRE(cfg.bodyModel == Membrum::BodyModelType::Membrane);
    REQUIRE(cfg.modeStretch       == Catch::Approx(0.333333f).margin(1e-5f));
    REQUIRE(cfg.decaySkew         == Catch::Approx(0.5f));
    REQUIRE(cfg.couplingAmount    == Catch::Approx(0.5f));
    REQUIRE(cfg.macroTightness    == Catch::Approx(0.5f));
    REQUIRE(cfg.macroBrightness   == Catch::Approx(0.5f));
    REQUIRE(cfg.macroBodySize     == Catch::Approx(0.5f));
    REQUIRE(cfg.macroPunch        == Catch::Approx(0.5f));
    REQUIRE(cfg.macroComplexity   == Catch::Approx(0.5f));
}
