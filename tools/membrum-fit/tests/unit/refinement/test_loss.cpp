// Loss function identity + monotonicity sanity checks.
#include "src/refinement/loss.h"

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

TEST_CASE("MSS loss: identical signals -> ~0; differing signals -> > 0") {
    constexpr float sr = 44100.0f;
    const auto a = sine(440.0f, sr, 8192);
    const auto b = sine(880.0f, sr, 8192);
    REQUIRE(MembrumFit::computeMSSLoss(a, a) < 1e-3f);
    REQUIRE(MembrumFit::computeMSSLoss(a, b) > 1e-2f);
}

TEST_CASE("MFCC L1: zero distance for identical signals, > 0 for different") {
    constexpr double sr = 44100.0;
    const auto a = sine(440.0f, static_cast<float>(sr), 8192);
    const auto b = sine(2200.0f, static_cast<float>(sr), 8192);
    const auto ma = MembrumFit::computeMFCC(a, sr);
    const auto mb = MembrumFit::computeMFCC(b, sr);
    REQUIRE(MembrumFit::computeMFCCL1(ma, ma) == Catch::Approx(0.0f));
    REQUIRE(MembrumFit::computeMFCCL1(ma, mb) > 1.0f);
}

TEST_CASE("Log envelope L1: zero for identical, > 0 for different amplitudes") {
    constexpr double sr = 44100.0;
    std::vector<float> a(8192, 0.5f);
    std::vector<float> b(8192, 0.05f);
    const auto ea = MembrumFit::computeLogEnvelope(a, sr);
    const auto eb = MembrumFit::computeLogEnvelope(b, sr);
    REQUIRE(MembrumFit::computeEnvelopeL1(ea, ea) == Catch::Approx(0.0f));
    REQUIRE(MembrumFit::computeEnvelopeL1(ea, eb) > 1.0f);
}
