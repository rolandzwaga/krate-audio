// ==============================================================================
// ExciterBank architecture tests (Phase 2 -- T008)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/exciter_bank.h"
#include "dsp/exciter_type.h"

#include <allocation_detector.h>

#include <cstdint>
#include <cstring>

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
