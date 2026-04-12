// ==============================================================================
// Phase 3.3 -- Choke allocation-free (FR-116, FR-163)
// ==============================================================================
// T3.3.3: 1000 rapid open/closed-hat alternations (notes 42 and 46 alternating,
// both in group 1) over ~10 seconds at 44.1 kHz / 128-sample blocks.
// Asserts:
//   - zero heap allocations across all noteOn + processBlock calls inside the
//     tracking scope (choke path reuses the Phase 3.2 fast-release mechanism
//     which is already allocation-free)
//   - zero NaN/Inf samples in the output
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"

#include <allocation_detector.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int    kBlockSize  = 128;

inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

} // namespace

TEST_CASE("VoicePool choke: 1000 alternations allocation-free + finite",
          "[membrum][voice_pool][phase3_3][choke_allocation_free][rt-safety]")
{
    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(16);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
    pool.setSharedVoiceParams(0.5f, 0.5f, 0.3f, 0.3f, 0.8f);
    pool.setSharedExciterType(Membrum::ExciterType::Impulse);
    pool.setSharedBodyModel(Membrum::BodyModelType::Membrane);
    pool.setChokeGroup(1);  // both 42 and 46 in group 1

    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);

    // Prime: fire one of each note and process one block so any lazy
    // first-touch init is paid outside the tracking scope.
    pool.noteOn(46, 0.7f);
    pool.processBlock(outL.data(), outR.data(), kBlockSize);
    pool.noteOn(42, 0.7f);
    pool.processBlock(outL.data(), outR.data(), kBlockSize);

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();

    // 1000 alternations over ~10 seconds. At 44.1 kHz with 128-sample
    // blocks, 10 s = ~3445 blocks. 1000 alternations distributed across
    // that = one note-on every ~3.4 blocks.
    constexpr int kAlternations = 1000;
    const int totalBlocks = static_cast<int>(kSampleRate * 10.0) / kBlockSize;

    // Alternation schedule: fire notes evenly across blocks.
    const int notesPerBlock =
        (kAlternations + totalBlocks - 1) / totalBlocks;
    int noteIdx = 0;
    bool anyNonFinite = false;

    for (int b = 0; b < totalBlocks && noteIdx < kAlternations; ++b)
    {
        // Fire 0..notesPerBlock note-ons per block to pace correctly.
        const int notesThisBlock =
            std::min(notesPerBlock, kAlternations - noteIdx);
        for (int n = 0; n < notesThisBlock; ++n)
        {
            const std::uint8_t note = (noteIdx & 1) ? 42 : 46;
            pool.noteOn(note, 0.8f);
            ++noteIdx;
        }
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        for (int i = 0; i < kBlockSize; ++i)
        {
            if (!isFiniteSample(outL[i])) anyNonFinite = true;
            if (!isFiniteSample(outR[i])) anyNonFinite = true;
        }
    }

    // Drain the tail -- let any remaining fast-releases complete.
    for (int b = 0; b < 64; ++b)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        for (int i = 0; i < kBlockSize; ++i)
        {
            if (!isFiniteSample(outL[i])) anyNonFinite = true;
            if (!isFiniteSample(outR[i])) anyNonFinite = true;
        }
    }

    const size_t allocCount = detector.stopTracking();

    CAPTURE(noteIdx, allocCount);
    REQUIRE(noteIdx == kAlternations);
    REQUIRE(allocCount == 0u);
    REQUIRE_FALSE(anyNonFinite);
}
