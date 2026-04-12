// ==============================================================================
// Phase 3.1 -- Live maxPolyphony changes
// ==============================================================================
// T3.1.6 -- satisfies FR-111 / SC-031.
//
// (a) With 6 voices active at maxPolyphony=8, setMaxPolyphony(4) must cause
//     the excess voices to enter FastReleasing, and the Active voice count
//     must be <= 4 after the next processBlock.
// (b) No NaN/Inf samples during the shrink.
// (c) setMaxPolyphony(16) must not crash and must admit more voices.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"

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

TEST_CASE("VoicePool: shrink from 8 to 4 voices releases the excess click-tolerant",
          "[membrum][voice_pool][phase3_1][poly_change]")
{
    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(8);

    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);

    // Trigger 6 notes and let them latch.
    for (int i = 0; i < 6; ++i)
        pool.noteOn(static_cast<std::uint8_t>(36 + i), 0.8f);
    pool.processBlock(outL.data(), outR.data(), kBlockSize);
    REQUIRE(pool.getActiveVoiceCount() == 6);

    // Shrink to 4. Any slot the allocator releases triggers beginFastRelease
    // at the pool layer so the shrink is click-tolerant.
    pool.setMaxPolyphony(4);

    // Next processBlock observes the new polyphony ceiling.
    pool.processBlock(outL.data(), outR.data(), kBlockSize);

    CHECK(pool.getActiveVoiceCount() <= 4);

    // No NaN/Inf during the shrink or subsequent rendering.
    bool allFinite = true;
    for (int b = 0; b < 4; ++b)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        for (int i = 0; i < kBlockSize; ++i)
        {
            if (!isFiniteSample(outL[i]) || !isFiniteSample(outR[i]))
            {
                allFinite = false;
                break;
            }
        }
        if (!allFinite) break;
    }
    CHECK(allFinite);
}

TEST_CASE("VoicePool: expand maxPolyphony back to 16 accepts more voices",
          "[membrum][voice_pool][phase3_1][poly_change]")
{
    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBlockSize);
    pool.setMaxPolyphony(4);

    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);

    for (int i = 0; i < 4; ++i)
        pool.noteOn(static_cast<std::uint8_t>(36 + i), 0.7f);
    pool.processBlock(outL.data(), outR.data(), kBlockSize);
    REQUIRE(pool.getActiveVoiceCount() == 4);

    // Grow to 16 and trigger more notes.
    pool.setMaxPolyphony(16);
    for (int i = 4; i < 12; ++i)
        pool.noteOn(static_cast<std::uint8_t>(36 + i), 0.7f);
    pool.processBlock(outL.data(), outR.data(), kBlockSize);

    CHECK(pool.getActiveVoiceCount() >= 8);
}
