// Membrane mapper inversion round-trip per spec §7 testing strategy.
// For 100 random size values in [0.2, 0.8], synthesise a one-mode
// ModalDecomposition at f0 = 500 * 0.1^size, run the inversion, and assert
// the recovered size is within 5 % of the original. (Other PadConfig fields
// like material and decay are weakly identifiable from a single-mode signal,
// so we only gate the strongly-identifiable size parameter here.)
#include "src/mapper_inversion/membrane_inverse.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <random>

TEST_CASE("Membrane inversion: 100x random-size round-trip recovers size within 5%") {
    std::mt19937 rng(0xCAFEBABE);
    std::uniform_real_distribution<float> sizeDist(0.2f, 0.8f);
    int hits = 0;
    constexpr int N = 100;
    for (int i = 0; i < N; ++i) {
        const float groundSize = sizeDist(rng);
        const float f0         = 500.0f * std::pow(0.1f, groundSize);

        // Synthesise a single dominant mode at f0 with a representative decay.
        MembrumFit::ModalDecomposition md;
        MembrumFit::Mode m;
        m.freqHz    = f0;
        m.amplitude = 1.0f;
        m.decayRate = 6.0f;
        md.modes.push_back(m);

        MembrumFit::AttackFeatures f{};
        const auto cfg = MembrumFit::MapperInversion::invertMembrane(md, f, 44100.0);
        if (std::abs(cfg.size - groundSize) / groundSize < 0.05f) ++hits;
    }
    REQUIRE(hits == N);
}
