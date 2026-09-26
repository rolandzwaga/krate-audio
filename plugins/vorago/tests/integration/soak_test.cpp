// ==============================================================================
// Vorago Phase 12 - random-surface soak (SC-020, [long])
// ==============================================================================
// T050 (specs/vorago-phase12-parameters/tasks.md). 5 seeds (12020..12024) x 60 s
// at 48 kHz, notes C2 + G2 velocity 100 held from sample 0. Every registered ID
// (unit/param_table_expected.h, the checked-in 108-row table) EXCEPT master gain
// (0), sustain (4), channel pressure (5), ecology mix (500), body mix (1003) and
// cavern mix (1105) is re-drawn to a seeded uniform normalized value every 5 s
// (t = 0, 5, ..., 55 s), each change landing at its exact sample offset. Those
// six stay at their registered defaults: each can legitimately drive the output
// toward silence, which would make the liveness gate meaningless.
//
// Gates (per seed): every sample finite (bit-pattern check), peak <= the
// -0.3 dBFS limiter ceiling, and the RMS over the last 10 s >= R_default - 60 dB,
// where R_default is the same window of a default-surface render of the same
// notes made in this test (precondition R_default >= 1e-4).
//
// Run alone: vorago_tests.exe "Vorago_RandomSurfaceSoak" > artifacts/sc020_soak.log
// ==============================================================================

#include "plugin_ids.h"
#include "unit/param_table_expected.h"
#include "vorago_test_fixture.h"

#include <vst_event_list.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kTotalSamples = std::size_t{60} * 48000u;     // 60 s = 5625 blocks of 512
constexpr std::size_t kRedrawInterval = std::size_t{5} * 48000u;    // 5 s
constexpr std::size_t kWindowStart = std::size_t{50} * 48000u;      // last 10 s
constexpr float kVelocity100 = 100.0f / 127.0f;         // quantises to 100 (processor.cpp:51-53)
constexpr float kOutputCeiling = 0.9661f;               // 10^(-0.3/20) = 0.96605, vorago_engine.h:255
constexpr double kDefaultRmsFloor = 1e-4;               // SC-020 precondition on R_default
constexpr double kLivenessRatio = 1e-3;                 // -60 dB
constexpr std::array<std::uint32_t, 5> kSeeds{12020u, 12021u, 12022u, 12023u, 12024u};

[[nodiscard]] bool isHeldAtDefault(Steinberg::Vst::ParamID id) noexcept {
    return id == ::Vorago::kMasterGainId || id == ::Vorago::kSustainPedalId ||
           id == ::Vorago::kChannelPressureId || id == ::Vorago::kEcologyMixId ||
           id == ::Vorago::kBodyMixId || id == ::Vorago::kSpaceMixId;
}

struct SoakResult {
    bool finite = true;
    float peak = 0.0f;
    double windowRms = 0.0;  // stereo RMS over [kWindowStart, kTotalSamples)
    std::size_t redraws = 0;
    std::size_t okBlocks = 0;
};

/// Renders 60 s of C2 + G2 held from sample 0. With `randomize`, every
/// non-excluded ID is re-drawn from mt19937{seed} at each 5 s boundary.
[[nodiscard]] SoakResult renderSoak(bool randomize, std::uint32_t seed) {
    VoragoTest::ProcessorFixture fx;
    fx.prepare(kSampleRate, 2048);
    fx.reserveCapture(kBlock);  // cleared (capacity kept) after every block

    std::mt19937 rng{seed};
    std::uniform_real_distribution<double> valueDist(0.0, 1.0);

    VoragoTest::MultiParamChanges pc;
    pc.reserve(VoragoTest::kNumExpectedParams);
    Krate::Test::EventList notes;
    notes.addNoteOn(36, kVelocity100, 0);  // C2
    notes.addNoteOn(43, kVelocity100, 0);  // G2

    SoakResult res;
    double windowSumSq = 0.0;
    std::size_t windowCount = 0;

    for (std::size_t start = 0; start < kTotalSamples; start += kBlock) {
        const std::size_t n = std::min(kBlock, kTotalSamples - start);

        pc.clear();
        // The next 5 s boundary at or after `start`; it lands in this block iff < start + n.
        const std::size_t boundary =
            ((start + kRedrawInterval - 1u) / kRedrawInterval) * kRedrawInterval;
        if (randomize && boundary < start + n) {
            const auto offset = static_cast<Steinberg::int32>(boundary - start);
            for (const VoragoTest::ExpectedParamRow& row : VoragoTest::kExpectedParams) {
                if (isHeldAtDefault(row.id)) {
                    continue;
                }
                pc.addQueue(row.id).addTestPoint(offset, valueDist(rng));
            }
            ++res.redraws;
        }

        fx.capturedL.clear();  // keeps capacity
        fx.capturedR.clear();
        if (fx.processBlock(n, (start == 0) ? &notes : nullptr, &pc) == Steinberg::kResultOk) {
            ++res.okBlocks;
        }

        const std::span<const float> l(fx.capturedL);
        const std::span<const float> r(fx.capturedR);
        res.finite = res.finite && VoragoTest::allFinite(l) && VoragoTest::allFinite(r);
        res.peak = std::max({res.peak, VoragoTest::peakOf(l), VoragoTest::peakOf(r)});

        for (std::size_t i = 0; i < n; ++i) {
            if (start + i >= kWindowStart) {
                windowSumSq += static_cast<double>(l[i]) * static_cast<double>(l[i]) +
                               static_cast<double>(r[i]) * static_cast<double>(r[i]);
                windowCount += 2u;
            }
        }
    }

    res.windowRms =
        (windowCount > 0u) ? std::sqrt(windowSumSq / static_cast<double>(windowCount)) : 0.0;
    return res;
}

}  // namespace

TEST_CASE("Vorago_RandomSurfaceSoak", "[vorago][integration][long]") {
    constexpr std::size_t kExpectedBlocks = (kTotalSamples + kBlock - 1u) / kBlock;
    constexpr std::size_t kExpectedRedraws = kTotalSamples / kRedrawInterval;  // 12

    // Default-surface reference: same notes, no parameter changes.
    const SoakResult ref = renderSoak(false, 0u);
    WARN("SC-020 default: R_default=" << ref.windowRms << " peak=" << ref.peak
                                      << " finite=" << ref.finite);
    REQUIRE(ref.okBlocks == kExpectedBlocks);
    REQUIRE(ref.finite);
    REQUIRE(ref.peak <= kOutputCeiling);
    REQUIRE(ref.windowRms >= kDefaultRmsFloor);

    const double floorRms = ref.windowRms * kLivenessRatio;

    for (const std::uint32_t seed : kSeeds) {
        const SoakResult s = renderSoak(true, seed);
        const double relDb = (s.windowRms > 0.0 && ref.windowRms > 0.0)
                                 ? 20.0 * std::log10(s.windowRms / ref.windowRms)
                                 : -999.0;
        WARN("SC-020 seed " << seed << ": finite=" << s.finite << " peak=" << s.peak
                            << " (ceiling " << kOutputCeiling << ") lastTenSecRms="
                            << s.windowRms << " (" << relDb << " dB vs R_default, floor "
                            << floorRms << ") redraws=" << s.redraws);
        CHECK(s.okBlocks == kExpectedBlocks);
        CHECK(s.redraws == kExpectedRedraws);
        CHECK(s.finite);
        CHECK(s.peak <= kOutputCeiling);
        CHECK(s.windowRms >= floorRms);
    }
}
