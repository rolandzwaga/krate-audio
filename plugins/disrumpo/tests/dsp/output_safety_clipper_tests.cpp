// =============================================================================
// Output safety clipper (user ruling 2026-11-10): exact below the knee, a soft knee
// towards 0.98 above it, a ceiling of 0.999 and no DC added (its DC servo).
// =============================================================================

#include "dsp/output_safety_clipper.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

using Disrumpo::OutputSafetyClipper;

namespace {

constexpr double kSr = 48000.0;

std::vector<float> noise(size_t n, double peak, uint32_t seed) {
    std::vector<float> x(n);
    uint32_t s = seed;
    for (auto& v : x) {
        s = s * 1664525u + 1013904223u;
        v = static_cast<float>(peak * (static_cast<double>(s >> 8) / 8388608.0 - 1.0));
    }
    return x;
}

/// A half-wave-rectified 220 Hz sine, its mean removed: positive peaks 0.68 amp, negative -0.32 amp.
std::vector<float> asymmetric(size_t n, double amp) {
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        const double s = std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(i) / kSr);
        x[i] = static_cast<float>(amp * (std::max(s, 0.0) - 1.0 / std::numbers::pi));
    }
    return x;
}

std::vector<float> run(OutputSafetyClipper& c, const std::vector<float>& x) {
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = c.process(x[i]);
    return y;
}

double meanOf(const std::vector<float>& x, size_t from) {
    double s = 0.0;
    for (size_t i = from; i < x.size(); ++i) s += x[i];
    return s / static_cast<double>(x.size() - from);
}

float peakOf(const std::vector<float>& x) {
    float p = 0.0f;
    for (const float v : x) p = std::max(p, std::fabs(v));
    return p;
}

}  // namespace

TEST_CASE("Output safety clipper is bit-exact at and below the knee", "[disrumpo][safety-clipper]") {
    OutputSafetyClipper c;
    c.prepare(kSr);
    c.reset();
    const auto x = noise(static_cast<size_t>(kSr), OutputSafetyClipper::kKnee, 7u);
    const auto y = run(c, x);
    size_t differing = 0;
    for (size_t i = 0; i < x.size(); ++i) differing += (y[i] == x[i]) ? 0u : 1u;
    CHECK(differing == 0u);
}

TEST_CASE("Output safety clipper holds the ceiling on hot signals", "[disrumpo][safety-clipper]") {
    // Noise peaking at +12 dBFS and a +6 dBFS sine: never above the hard limit, the sine's peak
    // at least on the soft knee's way to 0.98 (the residual-only ADAA1 lags the input by half
    // a sample, y = mean of f over the step + half the step, so a steep peak reaches the clamp)
    OutputSafetyClipper c;
    c.prepare(kSr);
    c.reset();
    const auto hot = run(c, noise(static_cast<size_t>(kSr), 4.0, 11u));
    CHECK(peakOf(hot) <= 0.999f);
    c.reset();
    std::vector<float> sine(static_cast<size_t>(kSr));
    for (size_t i = 0; i < sine.size(); ++i) {
        sine[i] = static_cast<float>(2.0 * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / kSr));
    }
    const float sinePeak = peakOf(run(c, sine));
    std::printf("Safety clipper: +12 dBFS noise peak %.5f, +6 dBFS sine peak %.5f\n", static_cast<double>(peakOf(hot)),
                static_cast<double>(sinePeak));
    CHECK(sinePeak <= 0.999f);
    CHECK(sinePeak >= 0.95f);
}

TEST_CASE("Output safety clipper adds no DC to an asymmetric signal", "[disrumpo][safety-clipper]") {
    // A DC-free half-wave shape whose positive peaks reach +6.7 dBFS (Half Rectify at drive 10
    // through the old symmetric clipper read DC 0.0995): the output mean over the last second
    // stays within 1e-3 of the input's, and every sample within the ceiling
    for (const double amp : {1.0, 2.0, 3.2}) {
        OutputSafetyClipper c;
        c.prepare(kSr);
        c.reset();
        const auto x = asymmetric(static_cast<size_t>(2.0 * kSr), amp);
        const auto y = run(c, x);
        const auto from = static_cast<size_t>(kSr);
        const double dcIn = meanOf(x, from);
        const double dcOut = meanOf(y, from);
        std::printf("Safety clipper asymmetric amp %.1f: DC in %.5f out %.5f, peak out %.4f\n", amp, dcIn, dcOut,
                    static_cast<double>(peakOf(y)));
        CHECK(std::fabs(dcOut - dcIn) < 1e-3);
        CHECK(peakOf(y) <= 0.999f);
    }
}

TEST_CASE("Output safety clipper is bit-exact again once a clipped burst has passed", "[disrumpo][safety-clipper]") {
    // 0.2 s of a hot asymmetric signal, then 3 s of -12 dBFS noise: the DC servo's offset decays
    // to exactly 0 and the last second passes through unchanged
    OutputSafetyClipper c;
    c.prepare(kSr);
    c.reset();
    auto x = asymmetric(static_cast<size_t>(0.2 * kSr), 3.0);
    const auto quiet = noise(static_cast<size_t>(3.0 * kSr), 0.25, 3u);
    x.insert(x.end(), quiet.begin(), quiet.end());
    const auto y = run(c, x);
    size_t differing = 0;
    for (size_t i = x.size() - static_cast<size_t>(kSr); i < x.size(); ++i) differing += (y[i] == x[i]) ? 0u : 1u;
    CHECK(differing == 0u);
}
