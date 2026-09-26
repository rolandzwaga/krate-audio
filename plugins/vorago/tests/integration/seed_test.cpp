// ==============================================================================
// Vorago Phase 12 - seed parameter (SC-014)
// ==============================================================================
// PLACEHOLDER registered by T002 (specs/vorago-phase12-parameters/tasks.md) so no
// later task edits CMake. Filled by T039, T046, T049.
//
// T039: SC-014 (2) same index twice -> identical renders; (4) a seed change while
// silent, then a note, matches a fresh instance seeded at that index before
// setupProcessing(). SC-014 (1) (index 0 == Phase 11) is the SC-002 guard in
// param_surface_test.cpp.
// T046: SC-014 (5) a seed change (3 -> 11) while a note sounds, inside an
// AllocationScope: zero allocations, every sample finite, peak <= the limiter
// ceiling.
// T049: SC-014 (3) Vorago_SeedTableSpread [long]: 16 fresh processors, one per
// seed index, 30 s held-note render each; all 120 pairs rmsDiff > 1e-3 over
// [3072, end). A failing pair is fixed by re-picking the kVoragoSeedValues entry
// (never index 0), never by lowering the gate. Run alone.
// ==============================================================================

#include "plugin_ids.h"
#include "vorago_test_fixture.h"

#include "parameters/param_mapping.h"

#include <allocation_detector.h>
#include <vst_event_list.h>
#include <vst_param_changes.h>

#include <krate/dsp/systems/vorago_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kEightSeconds = std::size_t{8} * 48000u;  // 750 blocks of 512
constexpr float kVelocity100 = 100.0f / 127.0f;     // quantises to 100 (processor.cpp:51-53)
constexpr float kOutputCeiling = 0.9661f;           // 10^(-0.3/20) = 0.96605, vorago_engine.h:203

/// The seed parameter at list index `index`, as one normalized change.
void addSeedChange(Krate::Test::ParameterChanges& pc, int index) {
    pc.addChange(::Vorago::kSeedId, ::Vorago::indexToNormalized(index, ::Vorago::kNumSeeds));
}

/// Parameter-only call BEFORE setupProcessing (processParameterChanges latches
/// the atomic ahead of the not-ready path), then prepare: the seed index is set
/// when setupProcessing() reads it.
void prepareAtSeed(VoragoTest::ProcessorFixture& fx, int index) {
    Krate::Test::ParameterChanges pc;
    addSeedChange(pc, index);
    REQUIRE(fx.processNoOutputs(&pc) == Steinberg::kResultOk);
    fx.prepare(kSampleRate, 2048);
}

/// Block 0 silent (carrying `firstBlockChanges`, may be null), then note C2
/// velocity 100 at the start of block 1 and `samples` in 512-sample blocks.
void renderSilentBlockThenNote(VoragoTest::ProcessorFixture& fx,
                               Steinberg::Vst::IParameterChanges* firstBlockChanges,
                               std::size_t samples) {
    fx.reserveCapture(kBlock + samples);
    REQUIRE(fx.processBlock(kBlock, nullptr, firstBlockChanges) == Steinberg::kResultOk);
    Krate::Test::EventList ev;
    ev.addNoteOn(36, kVelocity100, 0);
    for (std::size_t start = 0; start < samples; start += kBlock) {
        REQUIRE(fx.processBlock(kBlock, (start == 0) ? &ev : nullptr) == Steinberg::kResultOk);
    }
}

void requireRendersMatch(const VoragoTest::ProcessorFixture& a,
                         const VoragoTest::ProcessorFixture& b) {
    REQUIRE(a.capturedL.size() == b.capturedL.size());
    REQUIRE(VoragoTest::allFinite(std::span<const float>(a.capturedL)));
    REQUIRE(VoragoTest::allFinite(std::span<const float>(a.capturedR)));
    // Non-vacuity: the render is audible.
    REQUIRE(VoragoTest::peakOf(std::span<const float>(a.capturedL)) >= 1e-4f);
    const float diffL = VoragoTest::maxAbsDiff(std::span<const float>(a.capturedL),
                                               std::span<const float>(b.capturedL));
    const float diffR = VoragoTest::maxAbsDiff(std::span<const float>(a.capturedR),
                                               std::span<const float>(b.capturedR));
    INFO("maxAbsDiff L=" << diffL << " R=" << diffR);
    REQUIRE(diffL <= 1e-5f);
    REQUIRE(diffR <= 1e-5f);
}

}  // namespace

TEST_CASE("Vorago_SeedParameter", "[vorago][integration]") {
    SECTION("(2) same index twice renders identically") {
        constexpr int kIndex = 5;
        VoragoTest::ProcessorFixture a;
        VoragoTest::ProcessorFixture b;
        prepareAtSeed(a, kIndex);
        prepareAtSeed(b, kIndex);
        renderSilentBlockThenNote(a, nullptr, kEightSeconds);
        renderSilentBlockThenNote(b, nullptr, kEightSeconds);

        const auto want = ::Vorago::kVoragoSeedValues[static_cast<std::size_t>(kIndex)];
        REQUIRE(a.proc->engineForTest()->getSeed() == want);
        REQUIRE(b.proc->engineForTest()->getSeed() == want);
        requireRendersMatch(a, b);
    }

    SECTION("(4) seed change while silent matches a fresh instance at that seed") {
        constexpr int kIndex = 9;
        const auto want = ::Vorago::kVoragoSeedValues[static_cast<std::size_t>(kIndex)];
        REQUIRE(want != ::Vorago::kVoragoSeedValues[0]);  // a real change from the default

        // A: prepared at the default seed; index 9 arrives in one empty block.
        VoragoTest::ProcessorFixture a;
        a.prepare(kSampleRate, 2048);
        REQUIRE(a.proc->engineForTest()->getSeed() == ::Vorago::kVoragoSeedValues[0]);
        Krate::Test::ParameterChanges change;
        addSeedChange(change, kIndex);
        renderSilentBlockThenNote(a, &change, kEightSeconds);

        // B: the seed parameter was 9 before setupProcessing().
        VoragoTest::ProcessorFixture b;
        prepareAtSeed(b, kIndex);
        renderSilentBlockThenNote(b, nullptr, kEightSeconds);

        REQUIRE(a.proc->engineForTest()->getSeed() == want);
        REQUIRE(b.proc->engineForTest()->getSeed() == want);
        requireRendersMatch(a, b);
    }

    SECTION("(5) LiveReseed: seed change while a note sounds is allocation-free and bounded") {
        constexpr int kFrom = 3;
        constexpr int kTo = 11;
        constexpr std::size_t kWarmBlocks = 188;   // ~2 s at 512 before the change
        constexpr std::size_t kAfterBlocks = 375;  // ~4 s at 512 from the change
        const auto wantFrom = ::Vorago::kVoragoSeedValues[static_cast<std::size_t>(kFrom)];
        const auto wantTo = ::Vorago::kVoragoSeedValues[static_cast<std::size_t>(kTo)];
        REQUIRE(wantFrom != wantTo);

        VoragoTest::ProcessorFixture fx;
        prepareAtSeed(fx, kFrom);
        REQUIRE(fx.proc->engineForTest()->getSeed() == wantFrom);
        fx.reserveCapture((kWarmBlocks + kAfterBlocks) * kBlock);

        // Note C2 velocity 100 from sample 0, held for the whole render.
        Krate::Test::EventList ev;
        ev.addNoteOn(36, kVelocity100, 0);
        for (std::size_t b = 0; b < kWarmBlocks; ++b) {
            REQUIRE(fx.processBlock(kBlock, (b == 0) ? &ev : nullptr) == Steinberg::kResultOk);
        }
        // The note is sounding when the seed changes.
        const std::size_t warmEnd = fx.capturedL.size();
        REQUIRE(VoragoTest::peakOf(std::span<const float>(fx.capturedL)
                                       .subspan(warmEnd - kBlock, kBlock)) >= 1e-4f);

        Krate::Test::ParameterChanges change;  // built outside the scope
        addSeedChange(change, kTo);

        std::size_t allocs = 0;
        std::size_t okBlocks = 0;
        {
            TestHelpers::AllocationScope scope;
            for (std::size_t b = 0; b < kAfterBlocks; ++b) {
                if (fx.processBlock(kBlock, nullptr, (b == 0) ? &change : nullptr) ==
                    Steinberg::kResultOk) {
                    ++okBlocks;
                }
            }
            allocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        REQUIRE(okBlocks == kAfterBlocks);
        REQUIRE(allocs == 0u);
        REQUIRE(fx.proc->engineForTest()->getSeed() == wantTo);  // the reseed happened

        const std::span<const float> l(fx.capturedL);
        const std::span<const float> r(fx.capturedR);
        REQUIRE(VoragoTest::allFinite(l));
        REQUIRE(VoragoTest::allFinite(r));
        const float peak = std::max(VoragoTest::peakOf(l), VoragoTest::peakOf(r));
        INFO("peak=" << peak);
        REQUIRE(peak <= kOutputCeiling);
        // Non-vacuity: still audible after the reseed.
        REQUIRE(VoragoTest::peakOf(l.subspan(warmEnd)) >= 1e-4f);
    }
}

TEST_CASE("Vorago_SeedTableSpread", "[vorago][integration][long]") {
    // SC-014 (3): every pair of the 16 seeds differs audibly over a 30 s
    // held-note render. The gate is never lowered (spec SC-014).
    constexpr std::size_t kThirtySecondBlocks = (std::size_t{30} * 48000u + kBlock - 1u) / kBlock;  // 2813
    constexpr std::size_t kWindowFrom = 3072;
    constexpr double kMinRmsDiff = 1e-3;
    constexpr auto kSeeds = static_cast<std::size_t>(::Vorago::kNumSeeds);
    static_assert(kSeeds == 16);

    std::array<std::vector<float>, kSeeds> rendersL;
    std::array<std::vector<float>, kSeeds> rendersR;
    for (std::size_t i = 0; i < kSeeds; ++i) {
        INFO("seed index " << i);
        VoragoTest::ProcessorFixture fx;
        prepareAtSeed(fx, static_cast<int>(i));
        REQUIRE(fx.proc->engineForTest()->getSeed() == ::Vorago::kVoragoSeedValues[i]);
        fx.reserveCapture(kThirtySecondBlocks * kBlock);
        Krate::Test::EventList ev;
        ev.addNoteOn(36, kVelocity100, 0);  // C2 velocity 100, held for the whole render
        for (std::size_t b = 0; b < kThirtySecondBlocks; ++b) {
            REQUIRE(fx.processBlock(kBlock, (b == 0) ? &ev : nullptr) == Steinberg::kResultOk);
        }
        const std::span<const float> l(fx.capturedL);
        const std::span<const float> r(fx.capturedR);
        REQUIRE(VoragoTest::allFinite(l));
        REQUIRE(VoragoTest::allFinite(r));
        // Non-vacuity: each render is audible in the compared window.
        REQUIRE(VoragoTest::peakOf(l.subspan(kWindowFrom)) >= 1e-4f);
        rendersL[i] = std::move(fx.capturedL);
        rendersR[i] = std::move(fx.capturedR);
    }

    std::size_t pairs = 0;
    std::size_t failures = 0;
    double minL = 1e30;
    double minR = 1e30;
    for (std::size_t i = 0; i < kSeeds; ++i) {
        for (std::size_t j = i + 1; j < kSeeds; ++j) {
            const double dL = VoragoTest::rmsDiff(std::span<const float>(rendersL[i]),
                                                  std::span<const float>(rendersL[j]), kWindowFrom);
            const double dR = VoragoTest::rmsDiff(std::span<const float>(rendersR[i]),
                                                  std::span<const float>(rendersR[j]), kWindowFrom);
            minL = std::min(minL, dL);
            minR = std::min(minR, dR);
            ++pairs;
            const bool ok = (dL > kMinRmsDiff) && (dR > kMinRmsDiff);
            if (!ok) {
                ++failures;
            }
            WARN("SC-014 (3) seed pair (" << i << ", " << j << ") rmsDiff L=" << dL
                                          << " R=" << dR << (ok ? "" : "  FAIL"));
        }
    }
    WARN("SC-014 (3) pairs=" << pairs << " failures=" << failures << " min rmsDiff L=" << minL
                             << " R=" << minR << " (gate > " << kMinRmsDiff << ")");
    REQUIRE(pairs == 120u);
    REQUIRE(failures == 0u);
}
