// ==============================================================================
// Stability guard tests (Phase 5 — T076)
// ==============================================================================
// Covers:
//   - SC-008: FeedbackExciter with every body model at max feedback drive
//     and the highest available velocity — peak output ≤ 0 dBFS over 5 s.
//   - US1-6 edge case: FeedbackExciter with the String body (highest
//     Larsen-feedback risk) + max feedback — no runaway over 10 s.
//
// Both scenarios rely on the built-in FeedbackExciter stability chain
// documented in `feedback_exciter.h`:
//   - velocity-scaled feedbackAmount capped at kMaxFeedback,
//   - EnvelopeFollower-driven energy limiter,
//   - TanhADAA hard-capping magnitude to |1|,
//   - DCBlocker keeping DC from accumulating,
//   - final explicit ±1 clamp as the ultimate rail.
//
// Guidelines (from CLAUDE.md + testing-guide):
//   - No REQUIRE/CHECK inside sample loops — collect metrics, assert after.
//   - Bit-manipulation NaN/Inf detection (no std::isnan under -ffast-math).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/body_model_type.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_type.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

inline bool isNaNOrInfBits(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) == 0x7F800000u;
}

constexpr double kSampleRate = 44100.0;

constexpr const char* bodyName(Membrum::BodyModelType t) noexcept
{
    switch (t)
    {
    case Membrum::BodyModelType::Membrane:  return "Membrane";
    case Membrum::BodyModelType::Plate:     return "Plate";
    case Membrum::BodyModelType::Shell:     return "Shell";
    case Membrum::BodyModelType::String:    return "String";
    case Membrum::BodyModelType::Bell:      return "Bell";
    case Membrum::BodyModelType::NoiseBody: return "NoiseBody";
    default:                                return "Unknown";
    }
}

struct BlockStats
{
    float peakAbs     = 0.0f;
    bool  hasNaNOrInf = false;
};

BlockStats runFeedbackStability(Membrum::BodyModelType body,
                                double seconds)
{
    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate, 0u);

    // Max-drive Phase 1 parameters: long decay, large body, medium material.
    voice.setMaterial(0.7f);
    voice.setSize(0.8f);
    voice.setDecay(1.0f);       // Longest decay — stress resonance ring-out.
    voice.setStrikePosition(0.5f);
    voice.setLevel(1.0f);       // Max level.

    voice.setExciterType(Membrum::ExciterType::Feedback);
    voice.setBodyModel(body);

    // Velocity = 1.0 maps inside FeedbackExciter::trigger() to
    // feedbackAmount_ = 1.0 * kMaxFeedback = 0.85 (max permitted feedback).
    voice.noteOn(1.0f);

    constexpr int kBlockSize = 256;
    std::array<float, kBlockSize> block{};
    BlockStats stats{};

    const int totalSamples = static_cast<int>(seconds * kSampleRate);
    int remaining = totalSamples;
    while (remaining > 0)
    {
        const int thisBlock = remaining < kBlockSize ? remaining : kBlockSize;
        voice.processBlock(block.data(), thisBlock);
        for (int i = 0; i < thisBlock; ++i)
        {
            const float s = block[static_cast<size_t>(i)];
            if (isNaNOrInfBits(s)) stats.hasNaNOrInf = true;
            const float abs_s = s < 0.0f ? -s : s;
            if (abs_s > stats.peakAbs) stats.peakAbs = abs_s;
        }
        remaining -= thisBlock;
    }
    return stats;
}

} // namespace

// ==============================================================================
// SC-008: FeedbackExciter + every body at max drive, 5 seconds, peak ≤ 0 dBFS.
// ==============================================================================
TEST_CASE("StabilityGuard: FeedbackExciter + every body at max feedback stays ≤ 0 dBFS (5 s)",
          "[membrum][stability][phase5]")
{
    constexpr int kNumBodies = static_cast<int>(Membrum::BodyModelType::kCount);
    for (int b = 0; b < kNumBodies; ++b)
    {
        const auto body = static_cast<Membrum::BodyModelType>(b);

        const BlockStats stats = runFeedbackStability(body, 5.0);

        const std::string label = std::string("Feedback + ") + bodyName(body);
        INFO("combo = " << label << "  peakAbs = " << stats.peakAbs);
        CHECK_FALSE(stats.hasNaNOrInf);
        // SC-008: peak ≤ 0 dBFS. Use <= 1.0f to honor the contract.
        CHECK(stats.peakAbs <= 1.0f);
    }
}

// ==============================================================================
// US1-6 / SC-008: FeedbackExciter + String (highest Larsen risk) + max
// feedback — no runaway over 10 s. "No runaway" here means:
//   - no NaN/Inf,
//   - peak ≤ 0 dBFS,
//   - final-second peak is NOT higher than the first-second peak by more
//     than 3 dB (~1.41x) — a real runaway would compound monotonically.
// ==============================================================================
TEST_CASE("StabilityGuard: FeedbackExciter + String + max feedback — no runaway (10 s)",
          "[membrum][stability][phase5]")
{
    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate, 0u);
    voice.setMaterial(0.7f);
    voice.setSize(0.8f);
    voice.setDecay(1.0f);
    voice.setStrikePosition(0.5f);
    voice.setLevel(1.0f);
    voice.setExciterType(Membrum::ExciterType::Feedback);
    voice.setBodyModel(Membrum::BodyModelType::String);
    voice.noteOn(1.0f);

    constexpr int kBlockSize = 256;
    std::array<float, kBlockSize> block{};

    // Divide the 10 seconds into two halves: first 1 s and final 1 s.
    const int totalSamples  = static_cast<int>(10.0 * kSampleRate);
    const int firstWindow   = static_cast<int>(1.0 * kSampleRate);
    const int finalStartIdx = totalSamples - firstWindow;

    float overallPeak  = 0.0f;
    float firstPeak    = 0.0f;
    float finalPeak    = 0.0f;
    bool  hasNaNOrInf  = false;

    int sampleIdx = 0;
    while (sampleIdx < totalSamples)
    {
        const int remaining = totalSamples - sampleIdx;
        const int thisBlock = remaining < kBlockSize ? remaining : kBlockSize;
        voice.processBlock(block.data(), thisBlock);

        for (int i = 0; i < thisBlock; ++i)
        {
            const float s = block[static_cast<size_t>(i)];
            if (isNaNOrInfBits(s)) hasNaNOrInf = true;
            const float abs_s = s < 0.0f ? -s : s;

            if (abs_s > overallPeak) overallPeak = abs_s;

            const int globalIdx = sampleIdx + i;
            if (globalIdx < firstWindow)
            {
                if (abs_s > firstPeak) firstPeak = abs_s;
            }
            else if (globalIdx >= finalStartIdx)
            {
                if (abs_s > finalPeak) finalPeak = abs_s;
            }
        }
        sampleIdx += thisBlock;
    }

    INFO("overallPeak = " << overallPeak
         << "  firstPeak = " << firstPeak
         << "  finalPeak = " << finalPeak);

    CHECK_FALSE(hasNaNOrInf);
    CHECK(overallPeak <= 1.0f);

    // Runaway detection: if the final window's peak is more than ~1.41x the
    // first window's peak (a +3 dB rise), the feedback chain is pumping
    // energy into the body. The energy limiter + tanh saturator in
    // FeedbackExciter must keep final ≤ first * 1.41 over 10 seconds.
    //
    // If firstPeak is very small (body hadn't started ringing yet), the
    // ratio test is meaningless — only check the absolute bound.
    if (firstPeak > 0.01f)
    {
        CHECK(finalPeak <= firstPeak * 1.41f);
    }
}
