// Matrix Pencil smoke test on a clean two-mode synthetic signal.
#include "src/modal/matrix_pencil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

TEST_CASE("Matrix Pencil: recovers two damped sinusoids at infinite SNR") {
    constexpr double sr = 44100.0;
    constexpr int    N  = 4096;
    constexpr float  pi = 3.14159265358979323846f;
    const float f1 = 220.0f, g1 = 8.0f;
    const float f2 = 660.0f, g2 = 12.0f;

    std::vector<float> x(N);
    for (int n = 0; n < N; ++n) {
        const float t = static_cast<float>(n) / static_cast<float>(sr);
        x[n] = std::exp(-g1 * t) * std::cos(2.0f * pi * f1 * t)
             + 0.6f * std::exp(-g2 * t) * std::cos(2.0f * pi * f2 * t);
    }

    auto md = MembrumFit::Modal::extractModesMatrixPencil(x, sr, /*maxModes*/ 8);
    REQUIRE(md.modes.size() >= 2);
    // Top two modes (sorted by amplitude desc) should be near f1 and f2 in some order.
    bool hit1 = false, hit2 = false;
    for (const auto& m : md.modes) {
        if (std::abs(m.freqHz - f1) < 5.0f) hit1 = true;
        if (std::abs(m.freqHz - f2) < 5.0f) hit2 = true;
    }
    REQUIRE(hit1);
    REQUIRE(hit2);
}
