// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// transient_shaper_test_helpers.h self-tests (tasks deviation 3, T005)
// ==============================================================================

#include "transient_shaper_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace Krate::DSP;
namespace TST = Krate::DSP::TransientShaperTest;

namespace {

constexpr double kTshFs = 48000.0;

const char* tshToneName(TST::Tone t) { return t == TST::Tone::Sine ? "sine" : "saw"; }

}  // namespace

TEST_CASE("TransientShaperHelpers_BurstFixture", "[processors][profundum][transient-shaper]") {
    const std::array<TST::Tone, 2> tones{TST::Tone::Sine, TST::Tone::Saw};
    const std::array<double, 2> notes{TST::kE1Hz, TST::kC3Hz};
    const std::array<double, 2> preRolls{0.0, 2.0};
    const double hold = 1.0;
    const double tail = 0.25;

    REQUIRE(TST::rampSamples(kTshFs) == 24);

    for (const TST::Tone tone : tones) {
        for (const double f0 : notes) {
            const std::vector<float> ref = TST::steadyTone(tone, f0, kTshFs, hold);
            for (const double pre : preRolls) {
                const std::vector<float> b = TST::burst(tone, f0, kTshFs, hold, pre, tail);
                const std::size_t onset = TST::onsetIndex(kTshFs, pre);
                const std::size_t holdN = static_cast<std::size_t>(std::llround(hold * kTshFs));
                const std::size_t tailN = static_cast<std::size_t>(std::llround(tail * kTshFs));
                INFO(tshToneName(tone) << " f0 " << f0 << " preRoll " << pre);

                // Length = preRoll + hold + tail samples.
                const auto expectedOnset = static_cast<std::size_t>(std::llround(pre * kTshFs));
                REQUIRE(onset == expectedOnset);
                REQUIRE(b.size() == onset + holdN + tailN);
                REQUIRE(ref.size() == holdN);

                // Pre-roll samples exactly 0.0f; burst[onset] == 0.0f.
                bool preZero = true;
                for (std::size_t i = 0; i < onset; ++i)
                    preZero = preZero && (b[i] == 0.0f);
                CHECK(preZero);
                CHECK(b[onset] == 0.0f);

                // Raised-cosine onset of exactly 24 samples: weighted below full gain at k = 23,
                // the tone itself from k = 24.
                const std::size_t R = TST::rampSamples(kTshFs);
                bool rampOk = true;
                for (std::size_t k = 0; k < R; ++k) {
                    const double w = 0.5 * (1.0 - std::cos(3.14159265358979323846 * static_cast<double>(k) /
                                                           static_cast<double>(R)));
                    const double expected = static_cast<double>(ref[k]) * w;
                    rampOk = rampOk && (std::abs(static_cast<double>(b[onset + k]) - expected) <= 1e-7);
                }
                CHECK(rampOk);
                CHECK(ref[R - 1] != 0.0f);
                CHECK(std::abs(b[onset + R - 1]) < std::abs(ref[R - 1]));
                bool holdOk = true;
                for (std::size_t k = R; k < holdN; ++k)
                    holdOk = holdOk && (b[onset + k] == ref[k]);
                CHECK(holdOk);

                // Tail zeros.
                bool tailZero = true;
                for (std::size_t i = onset + holdN; i < b.size(); ++i)
                    tailZero = tailZero && (b[i] == 0.0f);
                CHECK(tailZero);

                // Whole-period RMS over [onset + 0.1 s, onset + hold] = -12 ± 0.1 dBFS.
                const std::size_t from = onset + static_cast<std::size_t>(std::llround(0.1 * kTshFs));
                const std::size_t to = onset + holdN;
                const double lvl = TST::levelDbPeriods(b.data() + from, to - from, kTshFs, f0);
                std::cout << std::setprecision(6) << "TransientShaperHelpers_BurstFixture " << tshToneName(tone)
                          << " f0=" << f0 << " preRoll=" << pre << " levelDb=" << lvl << "\n";
                CHECK(std::abs(lvl - (-12.0)) <= 0.1);
            }
        }
    }
}

TEST_CASE("TransientShaperHelpers_TwoLevelStep", "[processors][profundum][transient-shaper]") {
    const std::vector<float> x = TST::twoLevelStep(TST::Tone::Saw, TST::kE1Hz, kTshFs, 2.0, 2.0);
    REQUIRE(x.size() == static_cast<std::size_t>(4.0 * kTshFs));
    const auto idx = [](double s) { return static_cast<std::size_t>(std::llround(s * kTshFs)); };
    const double low = TST::levelDbPeriods(x.data() + idx(0.5), idx(2.0) - idx(0.5), kTshFs, TST::kE1Hz);
    const double high = TST::levelDbPeriods(x.data() + idx(2.5), idx(4.0) - idx(2.5), kTshFs, TST::kE1Hz);
    std::cout << std::setprecision(6) << "TransientShaperHelpers_TwoLevelStep low=" << low << " high=" << high << "\n";
    CHECK(std::abs(low - (-24.0)) <= 0.1);
    CHECK(std::abs(high - (-12.0)) <= 0.1);
}

TEST_CASE("TransientShaperHelpers_TransientTime", "[processors][profundum][transient-shaper]") {
    const std::size_t onset = 4800;
    const std::size_t n = 4800 + 9600;
    std::vector<float> trace(n, 0.0f);
    for (std::size_t i = onset; i < n; ++i) {
        const double t = static_cast<double>(i - onset) / kTshFs;
        trace[i] = static_cast<float>(12.0 * std::exp(-t / 0.020));
    }
    const double expected = 20.0 * std::log(12.0 / 9.0);   // 5.7536 ms
    const double measured = TST::transientTimeMs(trace, onset, kTshFs);
    std::cout << std::setprecision(6) << "TransientShaperHelpers_TransientTime measured=" << measured
              << " expected=" << expected << "\n";
    CHECK(std::abs(measured - expected) <= 1000.0 / kTshFs);

    // Never falls 3 dB: 12 dB decaying towards a 10 dB floor.
    std::vector<float> flat(n, 0.0f);
    for (std::size_t i = onset; i < n; ++i) {
        const double t = static_cast<double>(i - onset) / kTshFs;
        flat[i] = static_cast<float>(10.0 + 2.0 * std::exp(-t / 0.020));
    }
    const double never = TST::transientTimeMs(flat, onset, kTshFs);
    CHECK(ProfundumTest::isPosInf(never));
}

TEST_CASE("TransientShaperHelpers_DTr", "[processors][profundum][transient-shaper]") {
    const std::vector<float> x = TST::steadyTone(TST::Tone::Sine, TST::kE1Hz, kTshFs, 1.5);
    const std::size_t onset = 0;
    const std::size_t boostEnd = onset + static_cast<std::size_t>(std::llround(0.1 * kTshFs));
    std::vector<float> y = x;
    for (std::size_t i = onset; i < boostEnd; ++i)
        y[i] = 2.0f * x[i];

    const std::size_t window = static_cast<std::size_t>(std::llround(0.375 * kTshFs));
    const std::size_t steadyStart = static_cast<std::size_t>(std::llround(1.0 * kTshFs));
    const double d = TST::dTrDb(y.data(), y.size(), onset, steadyStart, window);
    const double expected = 10.0 * std::log10((0.1 * 4.0 + 0.275) / 0.375);   // 2.553 dB
    std::cout << std::setprecision(6) << "TransientShaperHelpers_DTr dTr=" << d << " expected=" << expected << "\n";
    CHECK(std::abs(d - expected) <= 0.05);

    CHECK(TST::dTrWindow(48000.0, 250.0) == 18000);
    CHECK(TST::dTrWindow(48000.0, 20.0) == 2400);
}

TEST_CASE("TransientShaperHelpers_SideMidAndBits", "[processors][profundum][transient-shaper]") {
    const std::vector<float> L = TST::steadyTone(TST::Tone::Sine, TST::kE1Hz, kTshFs, 1.0);
    std::vector<float> R(L.size(), 0.0f);
    for (std::size_t i = 0; i < L.size(); ++i)
        R[i] = 0.5f * L[i];
    const double ratio = TST::sideMidRatioDb(L.data(), R.data(), L.size());
    const double expected = 10.0 * std::log10(0.0625 / 0.5625);   // -9.542 dB
    std::cout << std::setprecision(6) << "TransientShaperHelpers_SideMidAndBits ratio=" << ratio << "\n";
    CHECK(std::abs(ratio - expected) <= 1e-3);

    CHECK_FALSE(TST::isFiniteBits(std::bit_cast<float>(0x7FC00000u)));
    CHECK_FALSE(TST::isFiniteBits(std::bit_cast<float>(0x7F800000u)));
    CHECK_FALSE(TST::isFiniteBits(std::bit_cast<float>(0xFF800000u)));
    CHECK(TST::isFiniteBits(0.0f));
    CHECK(TST::isFiniteBits(1e-30f));
    CHECK(TST::isFiniteBits(-1e-30f));
    CHECK(TST::isFiniteBits(3e38f));
    CHECK(TST::isFiniteBits(-3e38f));
}
