// ==============================================================================
// Profundum_BandLevelMetricSelfTest (Phase 11, T003; spec Conventions, plan 6.3)
// ==============================================================================
// Proves the test-only M/S band-level metric of band_level_metric.h before
// SC-011, SC-014 and SC-022 rely on it: identical renders read 0 dB, a x2 scale
// reads 6.02 dB in its band, a band silent in both renders is excluded, and the
// frame-wise mode sees a late change the aggregate mode averages away.
// ==============================================================================

#include "band_level_metric.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kLen = 96000;  // 2 s at 48 kHz

std::vector<float> sine(double freqHz, double amplitude, std::size_t n) {
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double phase =
            2.0 * std::numbers::pi * freqHz * static_cast<double>(i) / kFs;
        out[i] = static_cast<float>(amplitude * std::sin(phase));
    }
    return out;
}

std::vector<float> scaled(const std::vector<float>& x, float gain) {
    std::vector<float> out(x);
    for (auto& v : out) v *= gain;
    return out;
}

} // namespace

TEST_CASE("Profundum_BandLevelMetricSelfTest", "[profundum][metric]") {
    const std::vector<float> L = sine(100.0, 0.25, kLen);
    const std::vector<float> R = sine(100.0, 0.1, kLen);

    SECTION("IdenticalIsZero") {
        REQUIRE(bandLevelDifferenceDb(L, R, L, R, kFs, false) == 0.0);
        REQUIRE(bandLevelDifferenceDb(L, R, L, R, kFs, true) == 0.0);
    }

    SECTION("SixDbInItsBand") {
        const std::vector<float> L2 = scaled(L, 2.0f);
        const std::vector<float> R2 = scaled(R, 2.0f);

        const double diff = bandLevelDifferenceDb(L, R, L2, R2, kFs, false);
        INFO("aggregate difference = " << diff);
        REQUIRE(diff >= 6.01);
        REQUIRE(diff <= 6.03);

        const BandLevelReading a = bandLevels(L, R, kFs);
        const BandLevelReading b = bandLevels(L2, R2, kFs);
        const double band1Delta = b.db[0][1] - a.db[0][1];
        INFO("M band 1 [80, 160): " << a.db[0][1] << " dB -> " << b.db[0][1] << " dB");
        REQUIRE(band1Delta >= 6.01);
        REQUIRE(band1Delta <= 6.03);
    }

    SECTION("SilentBandExcluded") {
        // Render 1: L == R, so S is identically 0.
        const std::vector<float> L1 = sine(100.0, 0.25, kLen);
        const std::vector<float>& R1 = L1;
        // Render 2: M scaled by 2, plus a side signal of amplitude 1e-9 (~ -180 dBFS).
        const std::vector<float> side = sine(100.0, 1e-9, kLen);
        std::vector<float> L2(kLen);
        std::vector<float> R2(kLen);
        for (std::size_t i = 0; i < kLen; ++i) {
            L2[i] = 2.0f * L1[i] + side[i];
            R2[i] = 2.0f * R1[i] - side[i];
        }

        const BandLevelReading r1 = bandLevels(L1, R1, kFs);
        const BandLevelReading r2 = bandLevels(L2, R2, kFs);
        for (std::size_t k = 0; k < kBandLevelBandCount; ++k) {
            INFO("S band " << k << ": " << r1.db[1][k] << " dB / " << r2.db[1][k] << " dB");
            REQUIRE(r1.db[1][k] < -100.0);
            REQUIRE(r2.db[1][k] < -100.0);
        }

        // Without the exclusion the S bands would read a >= 100 dB difference.
        const double diff = bandLevelDifferenceDb(L1, R1, L2, R2, kFs, false);
        INFO("aggregate difference = " << diff);
        REQUIRE(diff >= 6.01);
        REQUIRE(diff <= 6.03);
    }

    SECTION("FrameWisePeaks") {
        // Identical except the second render is scaled by 2 over its last 0.5 s.
        // The gain reaches 2 through a 0.25 s raised-cosine ramp that ends where
        // the last 0.5 s begins: a hard gain step splatters broadband energy
        // 40-65 dB above the sine's leakage floor in bands 0, 2, 3 and 4, which
        // the aggregate then reads as a > 60 dB change (G3 gate, 2026-10-22).
        std::vector<float> L2(L);
        std::vector<float> R2(R);
        const std::size_t start = kLen - static_cast<std::size_t>(0.5 * kFs);
        const auto ramp = static_cast<std::size_t>(0.25 * kFs);
        for (std::size_t i = start - ramp; i < kLen; ++i) {
            double gain = 2.0;
            if (i < start) {
                const double t = static_cast<double>(i - (start - ramp)) / static_cast<double>(ramp);
                gain = 1.5 - 0.5 * std::cos(std::numbers::pi * t);
            }
            L2[i] = static_cast<float>(L2[i] * gain);
            R2[i] = static_cast<float>(R2[i] * gain);
        }

        const double frameWise = bandLevelDifferenceDb(L, R, L2, R2, kFs, true);
        const double aggregate = bandLevelDifferenceDb(L, R, L2, R2, kFs, false);
        INFO("frame-wise = " << frameWise << " dB, aggregate = " << aggregate << " dB");
        REQUIRE(frameWise >= 6.0);
        REQUIRE(aggregate < 6.0);
    }
}
