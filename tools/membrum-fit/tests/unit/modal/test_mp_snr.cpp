// Matrix Pencil SNR sweep per spec §7 testing strategy.
#include "src/modal/matrix_pencil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

namespace {
std::vector<float> synthesise(float f, float g, float sr, int n) {
    std::vector<float> x(n);
    constexpr float pi = std::numbers::pi_v<float>;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / sr;
        x[i] = std::exp(-g * t) * std::cos(2.0f * pi * f * t);
    }
    return x;
}

void addNoiseToSNR(std::vector<float>& x, float snrDb, unsigned seed) {
    double e = 0.0;
    for (float v : x) e += v * v;
    const double sigPow = e / static_cast<double>(x.size());
    const double noisePow = sigPow / std::pow(10.0, snrDb / 10.0);
    const double nSigma = std::sqrt(noisePow);
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(0.0, nSigma);
    for (float& v : x) v += static_cast<float>(dist(rng));
}

// One damped sinusoid; each SNR level is its own case so the run-gate shards can run them in parallel
// (as one case with three SECTIONs it ran ~588 s serially and set every full gate's wall clock).
constexpr double kSr = 44100.0;
constexpr int    kN  = 4096;
constexpr float  kF  = 440.0f;
constexpr float  kG  = 8.0f;

float closestFreqError(const MembrumFit::ModalDecomposition& md, float target) {
    float best = 1e9f;
    for (const auto& m : md.modes) {
        best = std::min(best, std::abs(m.freqHz - target) / target);
    }
    return best;
}
}

TEST_CASE("Matrix Pencil SNR sweep: single damped sinusoid, infinite SNR within 1%") {
    auto x = synthesise(kF, kG, kSr, kN);
    auto md = MembrumFit::Modal::extractModesMatrixPencil(x, kSr, 8);
    REQUIRE(!md.modes.empty());
    REQUIRE(closestFreqError(md, kF) < 0.01f);
}

TEST_CASE("Matrix Pencil SNR sweep: single damped sinusoid, 40 dB SNR within 5%") {
    auto x = synthesise(kF, kG, kSr, kN);
    addNoiseToSNR(x, 40.0f, 0xC0FFEE);
    auto md = MembrumFit::Modal::extractModesMatrixPencil(x, kSr, 16);
    REQUIRE(!md.modes.empty());
    REQUIRE(closestFreqError(md, kF) < 0.05f);
}

TEST_CASE("Matrix Pencil SNR sweep: single damped sinusoid, 20 dB SNR within 10%") {
    auto x = synthesise(kF, kG, kSr, kN);
    addNoiseToSNR(x, 20.0f, 0xBADF00D);
    auto md = MembrumFit::Modal::extractModesMatrixPencil(x, kSr, 32);
    REQUIRE(!md.modes.empty());
    REQUIRE(closestFreqError(md, kF) < 0.10f);
}
