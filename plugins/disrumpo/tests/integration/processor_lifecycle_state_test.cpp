// =============================================================================
// Processor lifecycle vs. state: activation must not wipe user state
// =============================================================================
// Audit finding D-047 (fix plan T1.02): setActive(true) and setupProcessing
// called ModulationEngine::reset(), which cleared every mod routing and macro.
// The processor keeps those only inside the engine, so a host re-activation
// (or the usual load order setState -> setActive(true)) silently dropped the
// whole mod matrix, and the next getState saved it empty.
// =============================================================================

#include "../test_support/disrumpo_rig.h"

#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Disrumpo;
using namespace Disrumpo::TestSupport;

namespace {

// Byte offsets of the v12 state layout written by Processor::getState
// (processor_state.cpp). Everything before the custom-curve breakpoints is
// fixed-size; the breakpoint count is read from the stream.
constexpr auto kBands = static_cast<size_t>(kMaxBands);
constexpr size_t kCurveCountOffset = 4 + 3 * 4 + 4   // version, 3 globals, band count
                                   + kBands * (4 + 4 + 3)  // per band: gain, pan, solo/bypass/mute
                                   + (kBands - 1) * 4      // crossovers
                                   + 15               // sweep core
                                   + 12               // sweep LFO
                                   + 13;              // sweep envelope
constexpr size_t kModSourcesSize = 13 + 13  // LFO 1, LFO 2
                                 + 13       // envelope follower
                                 + 9 + 9 + 9  // random, chaos, S&H
                                 + 16 + 12  // pitch follower, transient
                                 + 10;      // rungler
constexpr size_t kMacroSize = 4 + 4 + 4 + 1;

template <typename T>
T readAt(const std::vector<uint8_t>& b, size_t off) {
    T v{};
    REQUIRE(off + sizeof(T) <= b.size());
    std::memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

size_t macrosOffset(const std::vector<uint8_t>& b) {
    const auto points = readAt<int32_t>(b, kCurveCountOffset);
    REQUIRE(points >= 0);
    return kCurveCountOffset + 4 + static_cast<size_t>(points) * 8 + kModSourcesSize;
}

size_t routing0Offset(const std::vector<uint8_t>& b) {
    return macrosOffset(b) + Krate::DSP::kMaxMacros * kMacroSize;
}

// LFO 1 -> Input Gain at full amount, macro 1 at 0.8.
PList modMatrixPatch() {
    return {
        {makeRoutingParamId(0, 0), 1.0 / static_cast<double>(kUIModSourceCount - 1)},  // LFO 1
        {makeRoutingParamId(0, 1),
         static_cast<double>(ModDest::kInputGain) / static_cast<double>(ModDest::kTotalDestinations - 1)},
        {makeRoutingParamId(0, 2), 1.0},  // amount +1
        {makeModParamId(ModParamType::kMacro1Value), 0.8},
    };
}

// Spread (max - min) of the per-block output RMS in dB, skipping the first
// `skipBlocks` blocks (filter start-up and the 120 ms amount smoother ramp).
double blockRmsSpreadDb(const std::vector<float>& x, size_t block, size_t skipBlocks) {
    double lo = 1e9;
    double hi = -1e9;
    for (size_t s = skipBlocks * block; s + block <= x.size(); s += block) {
        const double db = toDb(rmsOf(x.data() + s, block));
        lo = std::min(lo, db);
        hi = std::max(hi, db);
    }
    return hi - lo;
}

}  // namespace

TEST_CASE("Mod routings and macros survive setActive(false)/setActive(true)",
          "[disrumpo][lifecycle][D-047]") {
    constexpr double kSampleRate = 48000.0;
    Rig rig(kSampleRate);
    rig.apply(modMatrixPatch());

    SECTION("getState is unchanged by a re-activation") {
        const auto a = getStateBytes(*rig.p);
        REQUIRE(readAt<int32_t>(a, 0) == kPresetVersion);
        REQUIRE(readAt<uint8_t>(a, routing0Offset(a)) == static_cast<uint8_t>(Krate::DSP::ModSource::LFO1));

        rig.reactivate();
        const auto b = getStateBytes(*rig.p);

        const size_t r0 = routing0Offset(b);
        const size_t m0 = macrosOffset(b);
        const auto source = readAt<uint8_t>(b, r0);
        const auto amount = readAt<float>(b, r0 + 1 + 4);
        const auto macro0 = readAt<float>(b, m0);
        std::printf("D-047 after reactivate: routing0 source=%u amount=%.3f macro0=%.3f\n",
                    static_cast<unsigned>(source), static_cast<double>(amount),
                    static_cast<double>(macro0));

        CHECK(source == static_cast<uint8_t>(Krate::DSP::ModSource::LFO1));
        CHECK(amount == 1.0f);
        CHECK(macro0 == 0.8f);

        // A == B, reported as the first differing byte (not an 18 KB dump).
        REQUIRE(a.size() == b.size());
        const auto firstDiff = static_cast<size_t>(
            std::mismatch(a.begin(), a.end(), b.begin()).first - a.begin());
        INFO("first differing state byte: " << firstDiff << " (routing 0 at " << r0
                                            << ", macros at " << m0 << ")");
        REQUIRE(firstDiff == a.size());
    }

    SECTION("LFO 1 -> Input Gain still modulates the output after re-activation") {
        constexpr double kTwoPi = 6.283185307179586476925;
        constexpr size_t kLen = 24000;  // 0.5 s
        constexpr size_t kBlock = 512;
        constexpr size_t kSkipBlocks = 14;  // ~0.15 s
        const double amp = std::pow(10.0, -24.0 / 20.0);
        std::vector<float> in(kLen);
        for (size_t i = 0; i < kLen; ++i)
            in[i] = static_cast<float>(amp * std::sin(kTwoPi * 1000.0 * static_cast<double>(i) / kSampleRate));

        std::vector<float> outL;
        std::vector<float> outR;
        rig.render(in, in, outL, outR, static_cast<int32>(kBlock));
        const double before = blockRmsSpreadDb(outL, kBlock, kSkipBlocks);

        rig.reactivate();
        rig.render(in, in, outL, outR, static_cast<int32>(kBlock));
        const double after = blockRmsSpreadDb(outL, kBlock, kSkipBlocks);

        std::printf("D-047 block-RMS spread: before=%.2f dB after=%.2f dB\n", before, after);
        REQUIRE(before > 1.0);
        REQUIRE(after > 1.0);
    }
}
