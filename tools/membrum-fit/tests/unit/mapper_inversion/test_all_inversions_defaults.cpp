// FR-055-like invariant: every mapper inversion MUST emit normalised neutral
// PadConfig defaults for any field it doesn't actively touch. Spec §4.7
// requires this for every body so kit presets re-load with bit-identical
// behaviour to the in-plugin defaults.
#include "src/mapper_inversion/bell_inverse.h"
#include "src/mapper_inversion/membrane_inverse.h"
#include "src/mapper_inversion/noise_body_inverse.h"
#include "src/mapper_inversion/plate_inverse.h"
#include "src/mapper_inversion/shell_inverse.h"
#include "src/mapper_inversion/string_inverse.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

namespace {

void requireDefaultsPreserved(const Membrum::PadConfig& cfg) {
    REQUIRE(cfg.modeStretch       == Catch::Approx(0.333333f).margin(1e-5f));
    REQUIRE(cfg.decaySkew         == Catch::Approx(0.5f));
    REQUIRE(cfg.modeInjectAmount  == Catch::Approx(0.0f));
    REQUIRE(cfg.nonlinearCoupling == Catch::Approx(0.0f));
    REQUIRE(cfg.couplingAmount    == Catch::Approx(0.5f));
    REQUIRE(cfg.macroTightness    == Catch::Approx(0.5f));
    REQUIRE(cfg.macroBrightness   == Catch::Approx(0.5f));
    REQUIRE(cfg.macroBodySize     == Catch::Approx(0.5f));
    REQUIRE(cfg.macroPunch        == Catch::Approx(0.5f));
    REQUIRE(cfg.macroComplexity   == Catch::Approx(0.5f));
    REQUIRE(cfg.morphEnabled      == Catch::Approx(0.0f));
    REQUIRE(cfg.morphDuration     == Catch::Approx(0.095477f).margin(1e-5f));
}

}  // namespace

TEST_CASE("FR-055 defaults: all 6 inversions emit normalised neutral PadConfig") {
    MembrumFit::ModalDecomposition empty;
    MembrumFit::AttackFeatures feats{};

    SECTION("Membrane")   { requireDefaultsPreserved(MembrumFit::MapperInversion::invertMembrane (empty, feats, 44100.0)); }
    SECTION("Plate")      { requireDefaultsPreserved(MembrumFit::MapperInversion::invertPlate    (empty, feats, 44100.0)); }
    SECTION("Shell")      { requireDefaultsPreserved(MembrumFit::MapperInversion::invertShell    (empty, feats, 44100.0)); }
    SECTION("Bell")       { requireDefaultsPreserved(MembrumFit::MapperInversion::invertBell     (empty, feats, 44100.0)); }
    SECTION("String")     { requireDefaultsPreserved(MembrumFit::MapperInversion::invertString   (empty, feats, 44100.0)); }
    SECTION("NoiseBody")  { requireDefaultsPreserved(MembrumFit::MapperInversion::invertNoiseBody(empty, feats, 44100.0)); }
}

TEST_CASE("Body class is set correctly by each inversion") {
    MembrumFit::ModalDecomposition empty;
    MembrumFit::AttackFeatures feats{};
    REQUIRE(MembrumFit::MapperInversion::invertMembrane (empty, feats, 44100.0).bodyModel == Membrum::BodyModelType::Membrane);
    REQUIRE(MembrumFit::MapperInversion::invertPlate    (empty, feats, 44100.0).bodyModel == Membrum::BodyModelType::Plate);
    REQUIRE(MembrumFit::MapperInversion::invertShell    (empty, feats, 44100.0).bodyModel == Membrum::BodyModelType::Shell);
    REQUIRE(MembrumFit::MapperInversion::invertBell     (empty, feats, 44100.0).bodyModel == Membrum::BodyModelType::Bell);
    REQUIRE(MembrumFit::MapperInversion::invertString   (empty, feats, 44100.0).bodyModel == Membrum::BodyModelType::String);
    REQUIRE(MembrumFit::MapperInversion::invertNoiseBody(empty, feats, 44100.0).bodyModel == Membrum::BodyModelType::NoiseBody);
}
