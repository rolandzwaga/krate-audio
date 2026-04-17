// ==============================================================================
// Phase 3.1 -- VoicePool::noteOn/noteOff/processBlock allocation behaviour
// ==============================================================================
// T3.1.1 -- satisfies FR-113, FR-114, FR-115, SC-020 subset.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr double kTestSampleRate = 44100.0;
constexpr int    kTestBlockSize  = 256;

// -ffast-math-safe finite-sample check (tasks.md §NaN Detection Pattern).
inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

void runBlocks(Membrum::VoicePool& pool,
               std::vector<float>& outL,
               std::vector<float>& outR,
               int numBlocks)
{
    for (int b = 0; b < numBlocks; ++b)
        pool.processBlock(outL.data(), outR.data(), kTestBlockSize);
}

} // namespace

TEST_CASE("VoicePool: 8 concurrent note-ons yield 8 active voices",
          "[membrum][voice_pool][phase3_1]")
{
    Membrum::VoicePool pool;
    pool.prepare(kTestSampleRate, kTestBlockSize);
    pool.setMaxPolyphony(16);

    std::vector<float> outL(kTestBlockSize, 0.0f);
    std::vector<float> outR(kTestBlockSize, 0.0f);

    for (int i = 0; i < 8; ++i)
        pool.noteOn(static_cast<std::uint8_t>(36 + i), 0.8f);

    // The allocator only flips a voice into its rendered state once we have
    // processed at least one block. Run a single block to let every voice
    // latch in.
    pool.processBlock(outL.data(), outR.data(), kTestBlockSize);

    REQUIRE(pool.getActiveVoiceCount() == 8);
}

TEST_CASE("VoicePool: note-off is a no-op for percussion voices",
          "[membrum][voice_pool][phase3_1]")
{
    Membrum::VoicePool pool;
    pool.prepare(kTestSampleRate, kTestBlockSize);
    pool.setMaxPolyphony(16);

    std::vector<float> outL(kTestBlockSize, 0.0f);
    std::vector<float> outR(kTestBlockSize, 0.0f);

    pool.noteOn(36, 0.9f);
    pool.processBlock(outL.data(), outR.data(), kTestBlockSize);
    REQUIRE(pool.getActiveVoiceCount() == 1);

    // FR-114: note-off must NOT decrement the voice count immediately.
    pool.noteOff(36);
    pool.processBlock(outL.data(), outR.data(), kTestBlockSize);
    REQUIRE(pool.getActiveVoiceCount() == 1);
}

TEST_CASE("VoicePool: voice naturally becomes inactive after amp envelope decays",
          "[membrum][voice_pool][phase3_1]")
{
    Membrum::VoicePool pool;
    pool.prepare(kTestSampleRate, kTestBlockSize);
    pool.setMaxPolyphony(16);

    std::vector<float> outL(kTestBlockSize, 0.0f);
    std::vector<float> outR(kTestBlockSize, 0.0f);

    pool.noteOn(36, 0.9f);

    // Phase 8A.5: voices now retire either via auto-release (block peak
    // below pool silence threshold) or via explicit noteOff (host releases
    // the key, triggering ModalResonatorBank::damp()). Render half a
    // second of the held note, then release the gate. The post-release
    // window is long enough for the damped body to drop below the pool's
    // silence threshold regardless of Phase-1 default t60.
    const int blocksHeld =
        (static_cast<int>(kTestSampleRate) / 2) / kTestBlockSize + 1;   // 500 ms held
    runBlocks(pool, outL, outR, blocksHeld);
    pool.noteOff(36);
    const int blocksTail =
        (static_cast<int>(kTestSampleRate) * 3) / kTestBlockSize + 1;   // 3 s tail
    runBlocks(pool, outL, outR, blocksTail);

    REQUIRE(pool.getActiveVoiceCount() == 0);
}

TEST_CASE("VoicePool: 500 ms of 8-voice output has no NaN/Inf samples",
          "[membrum][voice_pool][phase3_1]")
{
    Membrum::VoicePool pool;
    pool.prepare(kTestSampleRate, kTestBlockSize);
    pool.setMaxPolyphony(16);

    std::vector<float> outL(kTestBlockSize, 0.0f);
    std::vector<float> outR(kTestBlockSize, 0.0f);

    for (int i = 0; i < 8; ++i)
        pool.noteOn(static_cast<std::uint8_t>(36 + i), 0.7f);

    const int blocks500ms =
        (static_cast<int>(kTestSampleRate) / 2) / kTestBlockSize + 1;

    bool allFinite = true;
    for (int b = 0; b < blocks500ms; ++b)
    {
        pool.processBlock(outL.data(), outR.data(), kTestBlockSize);
        for (int i = 0; i < kTestBlockSize; ++i)
        {
            if (!isFiniteSample(outL[i]) || !isFiniteSample(outR[i]))
            {
                allFinite = false;
                break;
            }
        }
        if (!allFinite) break;
    }
    REQUIRE(allFinite);
}

TEST_CASE("VoicePool: maxPolyphony=1 with MIDI note 36 produces audible non-NaN output",
          "[membrum][voice_pool][phase3_1]")
{
    Membrum::VoicePool pool;
    pool.prepare(kTestSampleRate, kTestBlockSize);
    pool.setMaxPolyphony(1);

    std::vector<float> outL(kTestBlockSize, 0.0f);
    std::vector<float> outR(kTestBlockSize, 0.0f);

    pool.noteOn(36, 100.0f / 127.0f);
    pool.processBlock(outL.data(), outR.data(), kTestBlockSize);

    float peak = 0.0f;
    bool allFinite = true;
    for (int i = 0; i < kTestBlockSize; ++i)
    {
        if (!isFiniteSample(outL[i])) { allFinite = false; break; }
        const float a = std::fabs(outL[i]);
        if (a > peak) peak = a;
    }
    REQUIRE(allFinite);
    REQUIRE(peak > 0.001f);  // At least -60 dBFS to prove the path is live.
}
