// Attack feature primitives: spot-check the basics.
#include "src/features.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

namespace {
std::vector<float> sine(float hz, float sr, int n) {
    std::vector<float> x(n);
    constexpr float pi = 3.14159265358979323846f;
    for (int i = 0; i < n; ++i) x[i] = std::sin(2.0f * pi * hz * i / sr);
    return x;
}
}

TEST_CASE("estimateFundamental hits within 1% on a clean sine") {
    constexpr float sr = 44100.0f;
    const auto x = sine(440.0f, sr, 4096);
    const float f = MembrumFit::estimateFundamental(x, sr);
    REQUIRE(std::abs(f - 440.0f) / 440.0f < 0.01f);
}

TEST_CASE("computeSpectralCentroid: high-freq sine has higher centroid than low") {
    constexpr float sr = 44100.0f;
    const auto low  = sine(200.0f,  sr, 4096);
    const auto high = sine(4000.0f, sr, 4096);
    REQUIRE(MembrumFit::computeSpectralCentroid(low,  sr)
          < MembrumFit::computeSpectralCentroid(high, sr));
}

TEST_CASE("computeSpectralFlatness: white noise > pure sine") {
    constexpr float sr = 44100.0f;
    constexpr int N = 4096;
    const auto sineSig = sine(880.0f, sr, N);
    std::vector<float> noise(N);
    unsigned seed = 12345;
    for (int i = 0; i < N; ++i) {
        seed = seed * 1664525u + 1013904223u;
        noise[i] = ((seed >> 16) & 0xFFFF) / 32768.0f - 1.0f;
    }
    REQUIRE(MembrumFit::computeSpectralFlatness(noise,   sr)
          > MembrumFit::computeSpectralFlatness(sineSig, sr));
}
