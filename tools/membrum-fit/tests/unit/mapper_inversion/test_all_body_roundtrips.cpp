// Per-body size round-trip per spec §7. For each non-Membrane body, render
// a single-mode ModalDecomposition at the body's f0(size) formula, invert,
// assert the recovered size is within 5% of the ground truth.
#include "src/mapper_inversion/bell_inverse.h"
#include "src/mapper_inversion/noise_body_inverse.h"
#include "src/mapper_inversion/plate_inverse.h"
#include "src/mapper_inversion/shell_inverse.h"
#include "src/mapper_inversion/string_inverse.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

namespace {

template <typename Inv>
int countHits(Inv inv, float baseHz, std::mt19937& rng, int N) {
    std::uniform_real_distribution<float> sizeDist(0.2f, 0.8f);
    int hits = 0;
    for (int i = 0; i < N; ++i) {
        const float groundSize = sizeDist(rng);
        const float f0 = baseHz * std::pow(0.1f, groundSize);
        MembrumFit::ModalDecomposition md;
        MembrumFit::Mode m;
        m.freqHz = f0; m.amplitude = 1.0f; m.decayRate = 6.0f;
        md.modes.push_back(m);
        MembrumFit::AttackFeatures f{};
        const auto cfg = inv(md, f, 44100.0);
        if (std::abs(cfg.size - groundSize) / groundSize < 0.05f) ++hits;
    }
    return hits;
}

}  // namespace

TEST_CASE("Plate inversion: 100x size round-trip within 5%") {
    std::mt19937 rng(0x12345678);
    REQUIRE(countHits(MembrumFit::MapperInversion::invertPlate, 800.0f, rng, 100) == 100);
}
TEST_CASE("Shell inversion: 100x size round-trip within 5%") {
    std::mt19937 rng(0x5EEDABCD);
    REQUIRE(countHits(MembrumFit::MapperInversion::invertShell, 1500.0f, rng, 100) == 100);
}
TEST_CASE("Bell inversion: 100x size round-trip within 5%") {
    std::mt19937 rng(0xBE11C0DE);
    REQUIRE(countHits(MembrumFit::MapperInversion::invertBell, 800.0f, rng, 100) == 100);
}
TEST_CASE("String inversion: 100x size round-trip within 5%") {
    std::mt19937 rng(0x57A1B2C3);
    REQUIRE(countHits(MembrumFit::MapperInversion::invertString, 800.0f, rng, 100) == 100);
}
TEST_CASE("NoiseBody inversion: 100x size round-trip within 5%") {
    std::mt19937 rng(0x40115150);
    REQUIRE(countHits(MembrumFit::MapperInversion::invertNoiseBody, 1500.0f, rng, 100) == 100);
}
