// Mode-order selector returns a value within [minN, maxN].
#include "src/modal/mode_selection.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

TEST_CASE("selectModelOrder: bounded result on a 3-mode signal") {
    constexpr double sr = 44100.0;
    constexpr int N = 4096;
    constexpr float pi = 3.14159265358979323846f;
    std::vector<float> x(N);
    for (int n = 0; n < N; ++n) {
        const float t = n / static_cast<float>(sr);
        x[n] = std::exp(-5.0f * t) * std::cos(2.0f * pi * 220.0f * t)
             + 0.6f * std::exp(-7.0f * t) * std::cos(2.0f * pi * 440.0f * t)
             + 0.3f * std::exp(-9.0f * t) * std::cos(2.0f * pi * 880.0f * t);
    }
    const int order = MembrumFit::Modal::selectModelOrder(x, sr, 8, 32);
    REQUIRE(order >= 8);
    REQUIRE(order <= 32);
}

TEST_CASE("selectModelOrder: degenerate input returns midpoint") {
    std::vector<float> tiny(32, 0.0f);
    REQUIRE(MembrumFit::Modal::selectModelOrder(tiny, 44100.0, 8, 32) == 20);
}
