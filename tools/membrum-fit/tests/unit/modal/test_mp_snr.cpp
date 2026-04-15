// Matrix Pencil SNR sweep per spec §7 testing strategy.
#include "src/modal/matrix_pencil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <random>
#include <vector>

namespace {
std::vector<float> synthesise(float f, float g, float sr, int n) {
    std::vector<float> x(n);
    constexpr float pi = 3.14159265358979323846f;
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
}

TEST_CASE("Matrix Pencil SNR sweep: single damped sinusoid") {
    constexpr double sr = 44100.0;
    constexpr int    N  = 4096;
    const float f = 440.0f;
    const float g = 8.0f;

    auto closestFreqError = [&](const MembrumFit::ModalDecomposition& md, float target) {
        float best = 1e9f;
        for (const auto& m : md.modes) {
            best = std::min(best, std::abs(m.freqHz - target) / target);
        }
        return best;
    };

    SECTION("infinite SNR: within 1%") {
        auto x = synthesise(f, g, sr, N);
        auto md = MembrumFit::Modal::extractModesMatrixPencil(x, sr, 8);
        REQUIRE(!md.modes.empty());
        REQUIRE(closestFreqError(md, f) < 0.01f);
    }
    SECTION("40 dB SNR: within 5%") {
        auto x = synthesise(f, g, sr, N);
        addNoiseToSNR(x, 40.0f, 0xC0FFEE);
        auto md = MembrumFit::Modal::extractModesMatrixPencil(x, sr, 16);
        REQUIRE(!md.modes.empty());
        REQUIRE(closestFreqError(md, f) < 0.05f);
    }
    SECTION("20 dB SNR: within 10%") {
        auto x = synthesise(f, g, sr, N);
        addNoiseToSNR(x, 20.0f, 0xBADF00D);
        auto md = MembrumFit::Modal::extractModesMatrixPencil(x, sr, 32);
        REQUIRE(!md.modes.empty());
        REQUIRE(closestFreqError(md, f) < 0.10f);
    }
}
