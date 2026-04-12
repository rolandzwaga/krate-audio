// ==============================================================================
// Phase 3.5 -- Polyphony allocation matrix (SC-027, SC-027a)
// ==============================================================================
// T3.5.1 -- satisfies FR-163, FR-185, SC-027, SC-027a.
//
// Drives VoicePool through a 10-second fuzz stream at 44.1 kHz / 128-sample
// blocks with:
//   * 16 concurrent voices
//   * Random MIDI note-ons every ~5 ms (notes 36-67, random velocity)
//   * Random choke-group assignments, mid-test policy/polyphony changes
//   * Voice steals and choke events
//
// Asserts (all tracked by TestHelpers::AllocationDetector):
//   (a) zero heap allocations throughout the tracked region
//   (b) zero NaN/Inf samples in the output
//   (c) getActiveVoiceCount() <= maxPolyphony at all times
//
// The SC-027a static_assert lives in voice_pool.h as a compile-time safety
// net; the definitive runtime evidence is this fuzz test.
//
// NaN detection uses bit manipulation because -ffast-math breaks
// std::isnan/std::isfinite on MSVC (CLAUDE.md Cross-Platform Compatibility).
//
// NOTE: Global operator new/delete overrides live in test_allocation_matrix.cpp
// in this same test binary. ODR-safe since they are defined exactly once per
// TU and the linker picks them up for the entire executable.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "voice_pool_test_helpers.h"

#include <allocation_detector.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr double kSampleRate      = 44100.0;
constexpr int    kBlockSize       = 128;
constexpr double kDurationSeconds = 10.0;
constexpr int    kNoteOnPeriodBlocks =
    4; // ~5 ms at 44.1 kHz / 128-sample blocks (128/44100 ~= 2.9 ms/block)

// Bit-manipulation NaN/Inf check (CLAUDE.md Cross-Platform Compatibility).
inline bool isNaNOrInfBits(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) == 0x7F800000u;
}

} // namespace

// ==============================================================================
// FR-163 / FR-185 / SC-027 : 10 s fuzz allocation matrix
// ==============================================================================
TEST_CASE("Phase35: VoicePool 10-second fuzz is allocation-free",
          "[membrum][voice_pool][phase3_5][rt-safety]")
{
    Membrum::VoicePool pool;

    // prepare() is the only method allowed to allocate. Do it OUTSIDE the
    // tracking scope.
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(16);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.5f, 0.3f, 0.8f);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);

    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    // Prime the pool with one note-on and one block so any first-touch lazy
    // init is paid for OUTSIDE the tracking scope.
    pool.noteOn(36, 0.7f);
    pool.processBlock(outL.data(), outR.data(), kBlockSize);

    const int totalBlocks =
        static_cast<int>((kSampleRate * kDurationSeconds) / kBlockSize);

    std::mt19937 rng(0xC0FFEE01u);
    std::uniform_int_distribution<int>    noteDist(36, 67);
    std::uniform_real_distribution<float> velDist(0.1f, 1.0f);
    std::uniform_int_distribution<int>    chokeDist(0, 8);
    std::uniform_int_distribution<int>    polyDist(4, 16);
    std::uniform_int_distribution<int>    policyDist(0, 2);

    bool     foundNaNOrInf   = false;
    int      maxObservedActive = 0;
    int      observedMaxPoly = 16;
    bool     activeExceededCurrentPoly = false;
    int      worstExcessActive = 0;
    int      worstExcessPoly   = 0;

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();

    for (int b = 0; b < totalBlocks; ++b)
    {
        // Note-on roughly every 4 blocks (~5 ms). A mix of single and double
        // note-on bursts to exercise steal paths under heavy load.
        if ((b % kNoteOnPeriodBlocks) == 0)
        {
            const auto n1 = static_cast<std::uint8_t>(noteDist(rng));
            pool.noteOn(n1, velDist(rng));
            if ((b & 0x3) == 0)
            {
                const auto n2 = static_cast<std::uint8_t>(noteDist(rng));
                pool.noteOn(n2, velDist(rng));
            }
            // Exercise noteOff -- percussion no-op per FR-114 but must still
            // be allocation-free.
            pool.noteOff(n1);
        }

        // Mid-test parameter mutation -- exercises setMaxPolyphony (which may
        // force-free voices), setVoiceStealingPolicy, setChokeGroup. Every
        // 32 blocks (~93 ms).
        if ((b & 0x1F) == 0)
        {
            observedMaxPoly = polyDist(rng);
            pool.setMaxPolyphony(observedMaxPoly);
            pool.setVoiceStealingPolicy(
                static_cast<Membrum::VoiceStealingPolicy>(policyDist(rng)));
            pool.setChokeGroup(static_cast<std::uint8_t>(chokeDist(rng)));
        }

        pool.processBlock(outL.data(), outR.data(), kBlockSize);

        // Scan output for NaN/Inf -- collect, do NOT assert inside the loop
        // (Catch2 anti-pattern #13).
        if (!foundNaNOrInf)
        {
            for (int i = 0; i < kBlockSize; ++i)
            {
                if (isNaNOrInfBits(outL[static_cast<std::size_t>(i)]) ||
                    isNaNOrInfBits(outR[static_cast<std::size_t>(i)]))
                {
                    foundNaNOrInf = true;
                    break;
                }
            }
        }

        const int activeNow = pool.getActiveVoiceCount();
        if (activeNow > maxObservedActive)
            maxObservedActive = activeNow;

        // FR-185: getActiveVoiceCount() must never exceed the CURRENT
        // maxPolyphony setting — track violations vs the live value, not
        // just the hard 16-voice ceiling.
        if (activeNow > observedMaxPoly)
        {
            activeExceededCurrentPoly = true;
            worstExcessActive         = activeNow;
            worstExcessPoly           = observedMaxPoly;
            break;
        }
    }

    const std::size_t allocCount = detector.stopTracking();

    INFO("Phase 3.5 fuzz stream: " << totalBlocks << " blocks, "
                                   << "maxObservedActive=" << maxObservedActive
                                   << ", observedMaxPoly=" << observedMaxPoly
                                   << ", allocations=" << allocCount);
    if (activeExceededCurrentPoly)
    {
        INFO("FR-185 violation: activeNow=" << worstExcessActive
                                            << " > currentMaxPoly="
                                            << worstExcessPoly);
    }

    CHECK(allocCount == 0);                 // FR-163, FR-185, SC-027
    CHECK_FALSE(foundNaNOrInf);             // FR-164 tail safety
    CHECK(maxObservedActive <= 16);         // Hard upper bound (VoicePool kMaxVoices)
    CHECK_FALSE(activeExceededCurrentPoly); // FR-185: active ≤ current maxPoly
}
