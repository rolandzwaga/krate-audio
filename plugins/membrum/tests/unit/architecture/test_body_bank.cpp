// ==============================================================================
// BodyBank architecture tests (Phase 2 -- T009)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/body_bank.h"
#include "dsp/body_model_type.h"
#include "dsp/voice_common_params.h"

#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

using Catch::Approx;

namespace {

inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

Membrum::VoiceCommonParams defaultParams()
{
    Membrum::VoiceCommonParams p;
    p.material   = 0.5f;
    p.size       = 0.5f;
    p.decay      = 0.3f;
    p.strikePos  = 0.3f;
    p.level      = 0.8f;
    p.modeStretch = 1.0f;
    p.decaySkew   = 0.0f;
    return p;
}

} // namespace

TEST_CASE("BodyBank: Membrane body produces finite output after configureForNoteOn",
          "[membrum][architecture][body_bank]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0);
    bank.configureForNoteOn(defaultParams(), 160.0f);

    // Kick the body with an impulse and process 512 samples.
    float s0 = bank.processSample(1.0f);
    REQUIRE(isFiniteSample(s0));

    for (int i = 0; i < 512; ++i)
    {
        float s = bank.processSample(0.0f);
        REQUIRE(isFiniteSample(s));
    }
}

TEST_CASE("BodyBank: deferred body-model swap defers until configureForNoteOn",
          "[membrum][architecture][body_bank]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0);
    bank.configureForNoteOn(defaultParams(), 160.0f);
    REQUIRE(bank.getCurrentType() == Membrum::BodyModelType::Membrane);

    // Request a different body while voice is still ringing.
    bank.setBodyModel(Membrum::BodyModelType::Plate);
    CHECK(bank.getCurrentType() == Membrum::BodyModelType::Membrane);
    CHECK(bank.getPendingType() == Membrum::BodyModelType::Plate);

    // Keep ringing — no crash, no NaN, no allocation.
    for (int i = 0; i < 256; ++i)
    {
        float s = bank.processSample(0.0f);
        REQUIRE(isFiniteSample(s));
    }

    // Next note-on applies the swap.
    bank.configureForNoteOn(defaultParams(), 160.0f);
    CHECK(bank.getCurrentType() == Membrum::BodyModelType::Plate);
}

TEST_CASE("BodyBank: rapid setBodyModel during sounding voice does not crash",
          "[membrum][architecture][body_bank]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0);
    bank.configureForNoteOn(defaultParams(), 160.0f);

    bank.setBodyModel(Membrum::BodyModelType::Plate);
    bank.setBodyModel(Membrum::BodyModelType::Shell);
    bank.setBodyModel(Membrum::BodyModelType::Bell);
    bank.setBodyModel(Membrum::BodyModelType::String);
    bank.setBodyModel(Membrum::BodyModelType::NoiseBody);
    bank.setBodyModel(Membrum::BodyModelType::Membrane);

    // Still Membrane; the voice continues ringing.
    CHECK(bank.getCurrentType() == Membrum::BodyModelType::Membrane);

    for (int i = 0; i < 512; ++i)
    {
        float s = bank.processSample(0.0f);
        REQUIRE(isFiniteSample(s));
    }
}

TEST_CASE("BodyBank: configureForNoteOn + processSample does not allocate",
          "[membrum][architecture][body_bank][alloc]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0);

    {
        TestHelpers::AllocationScope scope;
        bank.configureForNoteOn(defaultParams(), 160.0f);
        for (int i = 0; i < 256; ++i)
            (void)bank.processSample(0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("BodyBank: cycling through all body types does not crash or NaN",
          "[membrum][architecture][body_bank]")
{
    Membrum::BodyBank bank;
    bank.prepare(44100.0, 0);

    const Membrum::BodyModelType types[] = {
        Membrum::BodyModelType::Membrane,
        Membrum::BodyModelType::Plate,
        Membrum::BodyModelType::Shell,
        Membrum::BodyModelType::String,
        Membrum::BodyModelType::Bell,
        Membrum::BodyModelType::NoiseBody,
    };

    for (auto t : types)
    {
        bank.setBodyModel(t);
        bank.configureForNoteOn(defaultParams(), 160.0f);
        CHECK(bank.getCurrentType() == t);

        for (int i = 0; i < 64; ++i)
        {
            float s = bank.processSample(i == 0 ? 1.0f : 0.0f);
            REQUIRE(isFiniteSample(s));
        }
    }
}

// MB-002 guard: a body that is selected again after another body played must
// render exactly like a freshly prepared one (String's waveguide RNG seed and
// delay lines, NoiseBody's noise layer, the shared modal bank).
namespace {

std::array<float, 4096> renderBody(Membrum::BodyBank& bank, Membrum::BodyModelType type)
{
    bank.setBodyModel(type);
    bank.configureForNoteOn(defaultParams(), 160.0f);

    std::array<float, 64> impulse{};
    impulse[0] = 1.0f;
    const std::array<float, 64> silence{};

    std::array<float, 4096> out{};
    bank.processBlock(out.data(), impulse.data(), 64);
    for (std::size_t i = 64; i < out.size(); i += 64)
        bank.processBlock(out.data() + i, silence.data(), 64);
    return out;
}

const char* bodyTypeName(Membrum::BodyModelType t) noexcept
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

TEST_CASE("BodyBank: a re-selected body renders like a freshly prepared one",
          "[body_bank][rt-safety]")
{
    constexpr int kNumBodies = static_cast<int>(Membrum::BodyModelType::kCount);
    constexpr std::uint32_t kVoiceId = 5u;

    for (int x = 0; x < kNumBodies; ++x)
    {
        const auto typeX = static_cast<Membrum::BodyModelType>(x);
        const auto typeY = static_cast<Membrum::BodyModelType>((x + 1) % kNumBodies);

        Membrum::BodyBank fresh;
        fresh.prepare(48000.0, kVoiceId);
        const auto expected = renderBody(fresh, typeX);

        Membrum::BodyBank reused;
        reused.prepare(48000.0, kVoiceId);
        (void)renderBody(reused, typeX);
        (void)renderBody(reused, typeY);
        const auto actual = renderBody(reused, typeX);

        float maxDiff = 0.0f;
        float peak    = 0.0f;
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            const float d = expected[i] - actual[i];
            maxDiff = std::max(maxDiff, d < 0.0f ? -d : d);
            peak    = std::max(peak, expected[i] < 0.0f ? -expected[i] : expected[i]);
        }
        INFO("body " << bodyTypeName(typeX) << " re-selected after " << bodyTypeName(typeY)
             << ": max abs diff " << maxDiff << ", fresh peak " << peak);
        CHECK(peak > 0.0f);
        CHECK(maxDiff <= 1e-6f);
    }
}
