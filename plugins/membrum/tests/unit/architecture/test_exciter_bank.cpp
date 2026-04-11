// ==============================================================================
// ExciterBank architecture tests (Phase 2 -- T008)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/body_model_type.h"
#include "dsp/drum_voice.h"
#include "dsp/exciter_bank.h"
#include "dsp/exciter_type.h"

#include <allocation_detector.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

using Catch::Approx;

namespace {

// Bit-manipulation NaN/Inf check (CLAUDE.md cross-platform rule — -ffast-math
// breaks std::isnan()).
inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

} // namespace

TEST_CASE("ExciterBank: variant holds all 6 alternatives",
          "[membrum][architecture][exciter_bank]")
{
    Membrum::ExciterBank bank;
    bank.prepare(44100.0, 0);

    const Membrum::ExciterType types[] = {
        Membrum::ExciterType::Impulse,
        Membrum::ExciterType::Mallet,
        Membrum::ExciterType::NoiseBurst,
        Membrum::ExciterType::Friction,
        Membrum::ExciterType::FMImpulse,
        Membrum::ExciterType::Feedback,
    };

    for (auto t : types)
    {
        bank.setExciterType(t);
        bank.trigger(0.5f);
        CHECK(bank.getCurrentType() == t);

        for (int i = 0; i < 32; ++i)
        {
            float s = bank.process(0.0f);
            REQUIRE(isFiniteSample(s));
        }
    }
}

TEST_CASE("ExciterBank: Impulse type produces non-zero finite output",
          "[membrum][architecture][exciter_bank]")
{
    Membrum::ExciterBank bank;
    bank.prepare(44100.0, 0);
    bank.setExciterType(Membrum::ExciterType::Impulse);
    bank.trigger(0.5f);

    float peakAbs = 0.0f;
    bool  anyNonZero = false;
    for (int i = 0; i < 512; ++i)
    {
        float s = bank.process(0.0f);
        REQUIRE(isFiniteSample(s));
        float a = s < 0 ? -s : s;
        if (a > peakAbs) peakAbs = a;
        if (a > 1e-6f) anyNonZero = true;
    }
    CHECK(anyNonZero);
    CHECK(peakAbs < 2.0f);
}

TEST_CASE("ExciterBank: deferred swap defers until trigger",
          "[membrum][architecture][exciter_bank]")
{
    Membrum::ExciterBank bank;
    bank.prepare(44100.0, 0);
    REQUIRE(bank.getCurrentType() == Membrum::ExciterType::Impulse);

    bank.setExciterType(Membrum::ExciterType::Mallet);
    CHECK(bank.getCurrentType() == Membrum::ExciterType::Impulse);
    CHECK(bank.getPendingType() == Membrum::ExciterType::Mallet);

    bank.trigger(0.5f);
    CHECK(bank.getCurrentType() == Membrum::ExciterType::Mallet);
}

TEST_CASE("ExciterBank: trigger + process does not allocate",
          "[membrum][architecture][exciter_bank][alloc]")
{
    Membrum::ExciterBank bank;
    bank.prepare(44100.0, 0);

    {
        TestHelpers::AllocationScope scope;
        bank.trigger(0.8f);
        for (int i = 0; i < 256; ++i)
            (void)bank.process(0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("ExciterBank: isActive() reports false after envelope decays",
          "[membrum][architecture][exciter_bank]")
{
    Membrum::ExciterBank bank;
    bank.prepare(44100.0, 0);
    bank.setExciterType(Membrum::ExciterType::Impulse);
    bank.trigger(1.0f);
    CHECK(bank.isActive());

    // Process ~2 seconds to let the transient envelope fully decay.
    for (int i = 0; i < 44100 * 2; ++i)
        (void)bank.process(0.0f);

    CHECK_FALSE(bank.isActive());
}

// ==============================================================================
// Phase 9 T127 / SC-007:
// Sample-rate sweep across all 36 exciter × body combinations at
// {22050, 44100, 48000, 96000, 192000} Hz. For each (rate, exciter, body):
//   - prepare, noteOn, process 100 ms.
//   - assert no NaN/Inf at any sample rate (bit manipulation check).
//   - assert at least one finite non-zero sample (voice is audible) — guards
//     against a stale filter state at extreme rates producing silence.
// ==============================================================================
namespace {

constexpr const char* exName(Membrum::ExciterType t) noexcept
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

constexpr const char* bdName(Membrum::BodyModelType t) noexcept
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

} // namespace

TEST_CASE("ExciterBodyMatrix: sample-rate sweep (36 combos × 5 rates, no NaN/Inf)",
          "[membrum][architecture][matrix][phase9][samplerate]")
{
    constexpr double kRates[] = {22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
    constexpr int    kNumExciters = static_cast<int>(Membrum::ExciterType::kCount);
    constexpr int    kNumBodies   = static_cast<int>(Membrum::BodyModelType::kCount);

    for (double rate : kRates)
    {
        // 100 ms at this rate.
        const int numSamples = static_cast<int>(rate * 0.1);
        constexpr int kBlockSize = 64;

        for (int e = 0; e < kNumExciters; ++e)
        {
            for (int b = 0; b < kNumBodies; ++b)
            {
                const auto ex   = static_cast<Membrum::ExciterType>(e);
                const auto body = static_cast<Membrum::BodyModelType>(b);

                Membrum::DrumVoice voice;
                voice.prepare(rate, 0u);
                voice.setMaterial(0.5f);
                voice.setSize(0.5f);
                voice.setDecay(0.5f);
                voice.setStrikePosition(0.3f);
                voice.setLevel(0.8f);
                voice.setExciterType(ex);
                voice.setBodyModel(body);
                voice.noteOn(100.0f / 127.0f);

                std::array<float, kBlockSize> blk{};
                bool hasNaNOrInf = false;
                bool anyNonZero  = false;
                float peakAbs    = 0.0f;
                int remaining    = numSamples;

                while (remaining > 0)
                {
                    const int thisBlock = remaining < kBlockSize ? remaining : kBlockSize;
                    voice.processBlock(blk.data(), thisBlock);
                    for (int i = 0; i < thisBlock; ++i)
                    {
                        const float s = blk[static_cast<std::size_t>(i)];
                        if (!isFiniteSample(s)) hasNaNOrInf = true;
                        const float a = s < 0.0f ? -s : s;
                        if (a > 1e-6f) anyNonZero = true;
                        if (a > peakAbs) peakAbs = a;
                    }
                    remaining -= thisBlock;
                }

                const std::string label =
                    std::string(exName(ex)) + "+" + bdName(body)
                    + " @" + std::to_string(static_cast<int>(rate)) + "Hz";
                INFO("combo=" << label
                     << "  peakAbs=" << peakAbs);
                CHECK_FALSE(hasNaNOrInf);
                CHECK(anyNonZero);
                CHECK(peakAbs <= 2.0f);  // generous safety bound
            }
        }
    }
}
