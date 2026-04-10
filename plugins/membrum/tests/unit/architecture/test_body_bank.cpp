// ==============================================================================
// BodyBank architecture tests (Phase 2 -- T009)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/body_bank.h"
#include "dsp/body_model_type.h"
#include "dsp/voice_common_params.h"

#include <allocation_detector.h>

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
