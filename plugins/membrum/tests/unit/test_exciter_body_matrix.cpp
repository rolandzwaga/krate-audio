// ==============================================================================
// Exciter × Body swap-in matrix tests (Phase 5 — T074)
// ==============================================================================
// Covers:
//   - FR-090: Parameterized over all 36 exciter×body combinations. For each:
//     trigger at velocity 100, process 500 ms, assert peak ∈ (−60, 0) dBFS,
//     no NaN/Inf/denormals (via bit-manipulation checks per CLAUDE.md
//     "Cross-Platform Compatibility" — std::isnan is unreliable under
//     -ffast-math).
//   - US3-1: Audio is produced with peak in (−60, 0) dBFS.
//   - US3-2: No sample contains NaN/Inf/denormals across 500 ms.
//   - US3-3: Exciter-type change while ringing — new exciter applies to the
//     new note without affecting the previous tail.
//   - US3-4: Body-model change while voice silent — new body applies
//     immediately on next note-on.
//   - US3-5: Body-model change while voice sounding — deferred to next
//     note-on; no crash / NaN / allocation while the ring-out continues.
//
// IMPORTANT (per CLAUDE.md + testing-guide ANTI-PATTERNS #13):
//   - No REQUIRE/CHECK inside sample loops. Collect metrics in the loop
//     and assert once after the loop finishes.
//   - NaN/Inf detection uses bit manipulation on raw float bits, NOT
//     std::isnan / std::isfinite (which are broken under -ffast-math on MSVC).
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

// Bit-manipulation NaN/Inf detection. A 32-bit IEEE-754 float is NaN or Inf
// iff its exponent bits are all 1 (0x7F800000). This stays correct under
// -ffast-math where std::isnan() is broken on some compilers.
inline bool isNaNOrInfBits(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) == 0x7F800000u;
}

// Detect subnormal (denormal) floats: exponent bits == 0 AND mantissa != 0.
// Zero is *not* subnormal, so we exclude it.
inline bool isDenormalBits(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    const std::uint32_t exponent = bits & 0x7F800000u;
    const std::uint32_t mantissa = bits & 0x007FFFFFu;
    return (exponent == 0u) && (mantissa != 0u);
}

constexpr double kSampleRate = 44100.0;
constexpr int    kProcessSamples = static_cast<int>(0.5 * kSampleRate); // 500 ms
constexpr float  kVelocity100 = 100.0f / 127.0f;

constexpr const char* exciterName(Membrum::ExciterType t) noexcept
{
    switch (t)
    {
    case Membrum::ExciterType::Impulse:    return "Impulse";
    case Membrum::ExciterType::Mallet:     return "Mallet";
    case Membrum::ExciterType::NoiseBurst: return "NoiseBurst";
    case Membrum::ExciterType::Friction:   return "Friction";
    case Membrum::ExciterType::FMImpulse:  return "FMImpulse";
    case Membrum::ExciterType::Feedback:   return "Feedback";
    default:                               return "Unknown";
    }
}

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

// Configure a voice with default parameters and the requested combination.
void prepareVoice(Membrum::DrumVoice& voice,
                  Membrum::ExciterType ex,
                  Membrum::BodyModelType body)
{
    voice.prepare(kSampleRate, 0u);
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.5f);
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);
    voice.setExciterType(ex);
    voice.setBodyModel(body);
}

struct BlockStats
{
    float peakAbs      = 0.0f;
    bool  hasNaNOrInf  = false;
    bool  hasDenormal  = false;
};

// Process `numSamples` via DrumVoice::processBlock in chunks and collect
// stats. All checks are accumulated into a struct — no REQUIRE inside the
// sample loop.
BlockStats processAndCollect(Membrum::DrumVoice& voice, int numSamples)
{
    constexpr int kBlockSize = 64;
    std::array<float, kBlockSize> block{};
    BlockStats stats{};

    int remaining = numSamples;
    while (remaining > 0)
    {
        const int thisBlock = remaining < kBlockSize ? remaining : kBlockSize;
        voice.processBlock(block.data(), thisBlock);
        for (int i = 0; i < thisBlock; ++i)
        {
            const float s = block[static_cast<size_t>(i)];
            if (isNaNOrInfBits(s))  stats.hasNaNOrInf = true;
            if (isDenormalBits(s))  stats.hasDenormal = true;
            const float abs_s = s < 0.0f ? -s : s;
            if (abs_s > stats.peakAbs) stats.peakAbs = abs_s;
        }
        remaining -= thisBlock;
    }
    return stats;
}

} // namespace

// ==============================================================================
// FR-090 / US3-1 / US3-2: All 36 combinations are audible, finite, and ≤ 0 dBFS.
// ==============================================================================
TEST_CASE("ExciterBodyMatrix: all 36 combos audible finite bounded",
          "[membrum][matrix][phase5]")
{
    constexpr int kNumExciters = static_cast<int>(Membrum::ExciterType::kCount);
    constexpr int kNumBodies   = static_cast<int>(Membrum::BodyModelType::kCount);

    // Lower bound for "audible". The spec language says "peak > −60 dBFS"
    // (10^(−60/20) ≈ 0.001). Some body/exciter combinations (Shell/String
    // struck with FM impulse etc.) are quiet after 500 ms but still above
    // the noise floor — stick to the spec threshold.
    constexpr float kMinPeak = 0.001f; // −60 dBFS
    constexpr float kMaxPeak = 1.0f;   // 0 dBFS

    for (int e = 0; e < kNumExciters; ++e)
    {
        for (int b = 0; b < kNumBodies; ++b)
        {
            const auto ex   = static_cast<Membrum::ExciterType>(e);
            const auto body = static_cast<Membrum::BodyModelType>(b);

            Membrum::DrumVoice voice;
            prepareVoice(voice, ex, body);
            voice.noteOn(kVelocity100);

            const BlockStats stats = processAndCollect(voice, kProcessSamples);

            // Assert once per combination, OUTSIDE the sample loop.
            const std::string label =
                std::string(exciterName(ex)) + " + " + bodyName(body);
            INFO("combo = " << label
                 << "  peakAbs = " << stats.peakAbs);
            CHECK_FALSE(stats.hasNaNOrInf);
            CHECK_FALSE(stats.hasDenormal);
            CHECK(stats.peakAbs > kMinPeak);
            CHECK(stats.peakAbs <= kMaxPeak);
        }
    }
}

// ==============================================================================
// US3-3: Exciter-type change while ringing — new exciter applies to the new
// note without affecting the previous tail. We validate that:
//   (a) the tail of the first note (after setExciterType but before the next
//       noteOn) is still finite and bounded, and
//   (b) on the next noteOn the swap is applied — the ExciterBank reports the
//       new type as current AND the new note is still finite and audible.
// ==============================================================================
TEST_CASE("ExciterBodyMatrix: exciter swap while ringing does not affect previous tail",
          "[membrum][matrix][phase5][swap]")
{
    Membrum::DrumVoice voice;
    prepareVoice(voice,
                 Membrum::ExciterType::Impulse,
                 Membrum::BodyModelType::Membrane);

    voice.noteOn(kVelocity100);

    // Process 50 ms so the body is clearly ringing.
    BlockStats earlyTail = processAndCollect(voice, static_cast<int>(0.05 * kSampleRate));
    CHECK_FALSE(earlyTail.hasNaNOrInf);
    CHECK_FALSE(earlyTail.hasDenormal);

    // Request an exciter-type change WHILE the voice is still ringing. The
    // swap must be deferred — current type must remain Impulse.
    voice.setExciterType(Membrum::ExciterType::Mallet);
    CHECK(voice.exciterBank().getCurrentType() == Membrum::ExciterType::Impulse);
    CHECK(voice.exciterBank().getPendingType() == Membrum::ExciterType::Mallet);

    // Process another 100 ms of the OLD note's tail. The previous exciter
    // is still driving — no NaN/Inf, bounded peak.
    BlockStats lateTail = processAndCollect(voice, static_cast<int>(0.10 * kSampleRate));
    CHECK_FALSE(lateTail.hasNaNOrInf);
    CHECK_FALSE(lateTail.hasDenormal);
    CHECK(lateTail.peakAbs <= 1.0f);

    // Trigger the next note — the swap must now be applied.
    voice.noteOn(kVelocity100);
    CHECK(voice.exciterBank().getCurrentType() == Membrum::ExciterType::Mallet);

    BlockStats newNote = processAndCollect(voice, static_cast<int>(0.20 * kSampleRate));
    CHECK_FALSE(newNote.hasNaNOrInf);
    CHECK_FALSE(newNote.hasDenormal);
    CHECK(newNote.peakAbs > 0.001f);
    CHECK(newNote.peakAbs <= 1.0f);
}

// ==============================================================================
// US3-4: Body-model change while voice silent — the new body model must be
// applied IMMEDIATELY on the next note-on (not deferred past it).
// ==============================================================================
TEST_CASE("ExciterBodyMatrix: body swap while silent applies on next note",
          "[membrum][matrix][phase5][swap]")
{
    Membrum::DrumVoice voice;
    prepareVoice(voice,
                 Membrum::ExciterType::Impulse,
                 Membrum::BodyModelType::Membrane);

    // Voice is fresh — no noteOn yet, so it is silent.
    REQUIRE_FALSE(voice.isActive());

    // Request a body-model change while the voice is silent.
    voice.setBodyModel(Membrum::BodyModelType::Plate);

    // The swap is performed inside configureForNoteOn(), which runs as part
    // of noteOn(). After noteOn() the current type must be Plate.
    voice.noteOn(kVelocity100);
    CHECK(voice.bodyBank().getCurrentType() == Membrum::BodyModelType::Plate);

    BlockStats stats = processAndCollect(voice, static_cast<int>(0.2 * kSampleRate));
    CHECK_FALSE(stats.hasNaNOrInf);
    CHECK_FALSE(stats.hasDenormal);
    CHECK(stats.peakAbs > 0.001f);
    CHECK(stats.peakAbs <= 1.0f);
}

// ==============================================================================
// US3-5: Body-model change while voice sounding — deferred to next note-on.
// The mid-note change must NOT:
//   - crash
//   - produce NaN/Inf
//   - swap the current body while a note is ringing (the ring-out continues
//     on the old body).
// After the next note-on the new body is applied.
// ==============================================================================
TEST_CASE("ExciterBodyMatrix: body swap while sounding is deferred",
          "[membrum][matrix][phase5][swap]")
{
    Membrum::DrumVoice voice;
    prepareVoice(voice,
                 Membrum::ExciterType::Mallet,
                 Membrum::BodyModelType::Membrane);

    voice.noteOn(kVelocity100);
    REQUIRE(voice.isActive());

    // Process 50 ms on the current (Membrane) body.
    BlockStats earlyTail = processAndCollect(voice, static_cast<int>(0.05 * kSampleRate));
    CHECK_FALSE(earlyTail.hasNaNOrInf);
    CHECK_FALSE(earlyTail.hasDenormal);

    // Mid-note body swap — must be deferred.
    voice.setBodyModel(Membrum::BodyModelType::Bell);
    CHECK(voice.bodyBank().getCurrentType() == Membrum::BodyModelType::Membrane);
    CHECK(voice.bodyBank().getPendingType() == Membrum::BodyModelType::Bell);

    // Process another 200 ms — the OLD body (Membrane) is still ringing. No
    // crash, no NaN/Inf, peak ≤ 0 dBFS.
    BlockStats lateTail = processAndCollect(voice, static_cast<int>(0.20 * kSampleRate));
    CHECK_FALSE(lateTail.hasNaNOrInf);
    CHECK_FALSE(lateTail.hasDenormal);
    CHECK(lateTail.peakAbs <= 1.0f);

    // Trigger the next note — pending swap must now be applied.
    voice.noteOn(kVelocity100);
    CHECK(voice.bodyBank().getCurrentType() == Membrum::BodyModelType::Bell);

    BlockStats newNote = processAndCollect(voice, static_cast<int>(0.20 * kSampleRate));
    CHECK_FALSE(newNote.hasNaNOrInf);
    CHECK_FALSE(newNote.hasDenormal);
    CHECK(newNote.peakAbs > 0.001f);
    CHECK(newNote.peakAbs <= 1.0f);
}
