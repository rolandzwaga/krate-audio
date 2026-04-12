// ==============================================================================
// Phase 3.2 -- Fast-release denormal safety (FR-164)
// ==============================================================================
// T3.2.2. Two sub-cases:
//   (a) FTZ/DAZ explicitly ON via `tests/test_helpers/enable_ftz_daz.h`.
//   (b) FTZ/DAZ explicitly OFF.
//
// In both cases, the software `kFastReleaseFloor = 1e-6f` guard inside
// `VoicePool::applyFastRelease` MUST terminate the fade before any denormal
// sample could be produced. The test is valid on any hardware -- the
// bit-manipulation finite check is -ffast-math safe and the denormal
// classification uses the IEEE-754 exponent bits directly, so we do NOT rely
// on std::isnan / std::fpclassify (tasks.md §NaN Detection Pattern).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "voice_pool/voice_pool.h"
#include "enable_ftz_daz.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr int kBlockSize = 128;

// -ffast-math-safe finite-sample check (tasks.md §NaN Detection Pattern).
inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

// Returns true if x is a denormal (subnormal) IEEE-754 single-precision
// value: exponent bits == 0 AND mantissa bits != 0. Zero is NOT considered
// denormal. This does NOT rely on std::isfinite / std::fpclassify so it is
// safe under -ffast-math.
inline bool isDenormal(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    const std::uint32_t exponent = (bits >> 23) & 0xFFu;
    const std::uint32_t mantissa = bits & 0x7FFFFFu;
    return exponent == 0u && mantissa != 0u;
}

// Disable FTZ/DAZ on x86 (the complement of enableFTZDAZ()).
inline void disableFTZDAZ() noexcept
{
#ifdef KRATE_TEST_FTZ_DAZ_X86
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_OFF);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_OFF);
#endif
}

struct Outcome
{
    bool anyNonFinite  = false;
    bool anyDenormal   = false;
    int  terminationSamples = -1;
};

// Drive a voice into fast-release and run until the shadow slot terminates
// (or bail after `maxBlocks`). Return counts and the transition timing.
Outcome driveFastRelease(double sampleRate, int maxBlocks)
{
    Membrum::VoicePool pool;
    pool.prepare(sampleRate, kBlockSize);
    pool.setMaxPolyphony(4);
    pool.setVoiceStealingPolicy(Membrum::VoiceStealingPolicy::Oldest);

    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);

    // Fill pool
    for (int i = 0; i < 4; ++i)
    {
        pool.noteOn(static_cast<std::uint8_t>(36 + i), 0.9f);
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
    }
    // Let voices develop
    for (int b = 0; b < 8; ++b)
        pool.processBlock(outL.data(), outR.data(), kBlockSize);

    // Trigger a steal. Oldest policy -> slot with note 36 is stolen.
    pool.noteOn(67, 0.9f);

    Outcome out;
    int samplesElapsed = 0;
    for (int b = 0; b < maxBlocks; ++b)
    {
        pool.processBlock(outL.data(), outR.data(), kBlockSize);
        samplesElapsed += kBlockSize;

        for (int i = 0; i < kBlockSize; ++i)
        {
            const float s = outL[i];
            if (!isFiniteSample(s)) out.anyNonFinite = true;
            if (isDenormal(s))      out.anyDenormal  = true;
        }

        if (out.terminationSamples < 0)
        {
            for (int slot = 0; slot < 16; ++slot)
            {
                const auto& rm = pool.releasingMeta(slot);
                if (rm.state == Membrum::VoiceSlotState::Free &&
                    rm.fastReleaseGain < 1e-5f)
                {
                    out.terminationSamples = samplesElapsed;
                    break;
                }
            }
        }
    }

    return out;
}

} // namespace

TEST_CASE("VoicePool fast-release: no non-finite or denormal samples (FTZ/DAZ ON)",
          "[membrum][voice_pool][phase3_2][denormals]")
{
    enableFTZDAZ();

    // FR-124 (Option B): `k = exp(-ln(1e6) / (kFastReleaseSecs * sr))` with
    // kFastReleaseSecs = 0.005 reaches the 1e-6 floor in exactly 5 ms ± 1
    // sample regardless of sample rate. Use 6 ms (`ceil(0.006 * sr)`) as a
    // tight bound that still tolerates the +1 ms slack in FR-124.
    const double rates[] = {22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
    for (double sr : rates)
    {
        const int bound = static_cast<int>(std::ceil(0.006 * sr));
        const int maxBlocks = std::max(16, bound / kBlockSize + 4);
        const Outcome r = driveFastRelease(sr, maxBlocks);

        INFO("FTZ/DAZ=on sr=" << sr
             << " terminationSamples=" << r.terminationSamples
             << " bound=" << bound);

        REQUIRE_FALSE(r.anyNonFinite);
        REQUIRE_FALSE(r.anyDenormal);
        REQUIRE(r.terminationSamples > 0);
        REQUIRE(r.terminationSamples <= bound);
    }
}

TEST_CASE("VoicePool fast-release: software 1e-6 floor prevents denormals with FTZ/DAZ OFF",
          "[membrum][voice_pool][phase3_2][denormals]")
{
    // Explicitly turn FTZ/DAZ off so the software floor is the only
    // protection. The test passes on ARM hardware without FTZ/DAZ too.
    disableFTZDAZ();

    const double rates[] = {22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
    for (double sr : rates)
    {
        const int bound = static_cast<int>(std::ceil(0.006 * sr));
        const int maxBlocks = std::max(16, bound / kBlockSize + 4);
        const Outcome r = driveFastRelease(sr, maxBlocks);

        INFO("FTZ/DAZ=off sr=" << sr
             << " terminationSamples=" << r.terminationSamples
             << " bound=" << bound);

        REQUIRE_FALSE(r.anyNonFinite);
        REQUIRE_FALSE(r.anyDenormal);
        REQUIRE(r.terminationSamples > 0);
        REQUIRE(r.terminationSamples <= bound);
    }

    // Restore FTZ/DAZ for subsequent tests.
    enableFTZDAZ();
}
