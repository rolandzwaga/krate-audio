// ==============================================================================
// Oversized host block safety (correctness-audit High #1)
// ==============================================================================
// Regression test for the heap buffer overflow on the audio thread when the
// host's maxSamplesPerBlock exceeds the historical kVoicePoolMaxBlock (8192)
// scratch reservation. VST3 permits maxSamplesPerBlock to be arbitrarily large
// (offline / bounce / freeze renders routinely use very large blocks); the
// VoicePool scratch buffers were clamped to 8192 while VoicePool::processBlock
// still wrote `numSamples` worth of audio into them, producing an out-of-bounds
// heap write (and subsequent OOB reads in the accumulation loops).
//
// The decisive failure mode is a heap overflow caught by AddressSanitizer:
//   cmake -S . -B build-asan -DENABLE_ASAN=ON ; ctest ...
// Under a normal build the assertions below still validate that the full
// oversized block is rendered and finite.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "voice_pool_test_helpers.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;

inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

void primeAndConfigure(Membrum::VoicePool& pool)
{
    pool.setMaxPolyphony(16);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);
    Membrum::TestHelpers::setAllPadsVoiceParams(pool, 0.5f, 0.5f, 0.6f, 0.3f, 0.9f);
    Membrum::TestHelpers::setAllPadsExciterType(pool, Membrum::ExciterType::Impulse);
    Membrum::TestHelpers::setAllPadsBodyModel(pool, Membrum::BodyModelType::Membrane);
}

} // namespace

TEST_CASE("VoicePool renders a block larger than kVoicePoolMaxBlock without overflow",
          "[membrum][voice_pool][rt-safety][oversized_block]")
{
    // Declare a host block size well beyond the historical 8192 scratch cap.
    constexpr int kBigBlock = 2 * Membrum::kVoicePoolMaxBlock; // 16384

    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBigBlock);
    primeAndConfigure(pool);

    std::vector<float> outL(static_cast<std::size_t>(kBigBlock), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBigBlock), 0.0f);

    // Mono (legacy) overload -- this is the path Processor::process takes when
    // only the main bus is present.
    pool.noteOn(38, 0.9f); // snare pad, GM note 38
    pool.processBlock(outL.data(), outR.data(), kBigBlock);

    // Every sample across the FULL oversized block must be finite (no OOB
    // garbage, no NaN/Inf). Pre-fix this region past index 8191 was backed by
    // an out-of-bounds heap read.
    for (int i = 0; i < kBigBlock; ++i)
    {
        REQUIRE(isFiniteSample(outL[static_cast<std::size_t>(i)]));
        REQUIRE(isFiniteSample(outR[static_cast<std::size_t>(i)]));
    }

    // The struck voice must actually be rendered into the tail of the block
    // (the region beyond the old 8192 cap), proving the whole block was
    // processed rather than truncated.
    float tailPeak = 0.0f;
    for (int i = Membrum::kVoicePoolMaxBlock; i < kBigBlock; ++i)
        tailPeak = std::max(tailPeak, std::fabs(outL[static_cast<std::size_t>(i)]));
    REQUIRE(tailPeak > 0.0f);
}

TEST_CASE("VoicePool multi-bus path renders an oversized block without overflow",
          "[membrum][voice_pool][rt-safety][oversized_block]")
{
    constexpr int kBigBlock = Membrum::kVoicePoolMaxBlock + 4096; // 12288

    Membrum::VoicePool pool;
    pool.prepare(kSampleRate, kBigBlock);
    primeAndConfigure(pool);

    std::vector<float> outL(static_cast<std::size_t>(kBigBlock), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBigBlock), 0.0f);

    // No aux buses connected: exercises the multi-bus overload's main path
    // (numOutputBuses = 1, busActive/aux pointers null).
    bool busActive[1] = {true};
    pool.noteOn(36, 0.9f); // kick pad, GM note 36
    pool.processBlock(outL.data(), outR.data(),
                      nullptr, nullptr, busActive, 1, kBigBlock);

    for (int i = 0; i < kBigBlock; ++i)
    {
        REQUIRE(isFiniteSample(outL[static_cast<std::size_t>(i)]));
        REQUIRE(isFiniteSample(outR[static_cast<std::size_t>(i)]));
    }
}
