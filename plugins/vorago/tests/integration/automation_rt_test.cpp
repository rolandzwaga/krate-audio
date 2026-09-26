// ==============================================================================
// Vorago Phase 12 - full-surface automation RT safety (SC-015)
// ==============================================================================
// T046 (specs/vorago-phase12-parameters/tasks.md). 2 000 blocks x 512 at 48 kHz:
// every registered ID (unit/param_table_expected.h, the checked-in 108-row table
// Vorago_ParameterInfoTable pins to the controller) receives a seeded random
// normalized value EVERY block (std::mt19937{12015}), at a random offset; the
// sustain pedal additionally gets a second point per block; random note-ons /
// note-offs arrive at random offsets. An AllocationScope wraps each process()
// call only - the host-side mocks are filled OUTSIDE it - so every counted
// allocation is the processor's (a push-path defect: fix it, never exclude
// the ID). Every sample finite, peak <= the -0.3 dBFS limiter ceiling,
// getNonFiniteRecoveryCount() == 0.
// ==============================================================================

#include "plugin_ids.h"
#include "unit/param_table_expected.h"
#include "vorago_test_fixture.h"

#include "parameters/param_mapping.h"

#include <allocation_detector.h>
#include <vst_event_list.h>

#include <krate/dsp/systems/vorago_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <span>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kBlocks = 2000;
constexpr std::size_t kMaxNotesPerBlock = 4;
constexpr float kOutputCeiling = 0.9661f;  // 10^(-0.3/20) = 0.96605, vorago_engine.h:203

}  // namespace

TEST_CASE("Vorago_FullSurfaceAutomationAllocFree", "[vorago][integration]") {
    VoragoTest::ProcessorFixture fx;
    fx.prepare(kSampleRate, 2048);
    fx.reserveCapture(kBlock);  // cleared (capacity kept) after every block

    std::mt19937 rng{12015u};
    std::uniform_real_distribution<double> valueDist(0.0, 1.0);
    std::uniform_int_distribution<int> offsetDist(0, static_cast<int>(kBlock) - 1);
    std::uniform_int_distribution<int> noteCountDist(0, static_cast<int>(kMaxNotesPerBlock));
    std::uniform_int_distribution<int> pitchDist(24, 96);
    std::uniform_int_distribution<int> kindDist(0, 99);
    std::uniform_real_distribution<float> velocityDist(0.05f, 1.0f);

    VoragoTest::MultiParamChanges pc;
    pc.reserve(VoragoTest::kNumExpectedParams);
    Krate::Test::EventList ev;

    std::size_t totalAllocs = 0;
    std::size_t firstAllocBlock = kBlocks;
    std::size_t okBlocks = 0;
    std::size_t noteOns = 0;
    std::size_t pedalPoints = 0;
    bool finite = true;
    float peak = 0.0f;
    double lastSeedNorm = 0.0;

    for (std::size_t b = 0; b < kBlocks; ++b) {
        // ---- host side, outside the scope ----
        pc.clear();
        for (const VoragoTest::ExpectedParamRow& row : VoragoTest::kExpectedParams) {
            VoragoTest::MultiPointParamValueQueue& q = pc.addQueue(row.id);
            const double v = valueDist(rng);
            if (row.id == ::Vorago::kSustainPedalId) {
                std::array<int, 2> offs{offsetDist(rng), offsetDist(rng)};
                std::sort(offs.begin(), offs.end());
                q.addTestPoint(offs[0], v);
                q.addTestPoint(offs[1], valueDist(rng));
                pedalPoints += 2;
            } else {
                q.addTestPoint(offsetDist(rng), v);
            }
            if (row.id == ::Vorago::kSeedId) {
                lastSeedNorm = v;
            }
        }

        ev.clear();
        const int n = noteCountDist(rng);
        std::array<int, kMaxNotesPerBlock> noteOffs{};
        for (int k = 0; k < n; ++k) {
            noteOffs[static_cast<std::size_t>(k)] = offsetDist(rng);
        }
        std::sort(noteOffs.begin(), noteOffs.begin() + n);
        for (int k = 0; k < n; ++k) {
            const auto pitch = static_cast<Steinberg::int16>(pitchDist(rng));
            const auto off = static_cast<Steinberg::int32>(noteOffs[static_cast<std::size_t>(k)]);
            if (kindDist(rng) < 60) {
                ev.addNoteOn(pitch, velocityDist(rng), off);
                ++noteOns;
            } else {
                ev.addNoteOff(pitch, off);
            }
        }

        fx.capturedL.clear();  // keeps capacity
        fx.capturedR.clear();

        // ---- audio side: the scope wraps process() only ----
        std::size_t blockAllocs = 0;
        {
            TestHelpers::AllocationScope scope;
            if (fx.processBlock(kBlock, &ev, &pc) == Steinberg::kResultOk) {
                ++okBlocks;
            }
            blockAllocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        if (blockAllocs > 0 && firstAllocBlock == kBlocks) {
            firstAllocBlock = b;
        }
        totalAllocs += blockAllocs;

        const std::span<const float> l(fx.capturedL);
        const std::span<const float> r(fx.capturedR);
        finite = finite && VoragoTest::allFinite(l) && VoragoTest::allFinite(r);
        peak = std::max({peak, VoragoTest::peakOf(l), VoragoTest::peakOf(r)});
    }

    INFO("allocations=" << totalAllocs << " first allocating block=" << firstAllocBlock
                        << " peak=" << peak << " noteOns=" << noteOns
                        << " pedalPoints=" << pedalPoints);
    REQUIRE(okBlocks == kBlocks);
    REQUIRE(totalAllocs == 0u);
    REQUIRE(finite);
    REQUIRE(peak <= kOutputCeiling);

    const Krate::DSP::VoragoEngine* engine = fx.proc->engineForTest();
    REQUIRE(engine != nullptr);
    REQUIRE(engine->getNonFiniteRecoveryCount() == 0u);

    // Non-vacuity: the run played notes, was audible, and the automation reached
    // the engine (the last block's seed point is the engine's live seed).
    REQUIRE(noteOns > 1000u);
    REQUIRE(peak >= 1e-4f);
    const int lastSeedIndex = ::Vorago::indexFromNormalized(lastSeedNorm, ::Vorago::kNumSeeds);
    REQUIRE(engine->getSeed() ==
            ::Vorago::kVoragoSeedValues[static_cast<std::size_t>(lastSeedIndex)]);
}
