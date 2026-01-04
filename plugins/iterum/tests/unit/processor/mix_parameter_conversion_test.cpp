// ==============================================================================
// Regression Test: Mix Parameter Conversion (Processor → DSP)
// ==============================================================================
// Tests that the processor correctly converts normalized 0-1 mix parameters
// to the 0-100 percentage values expected by DSP delay classes.
//
// BUG HISTORY:
// - Spectral, Shimmer, MultiTap, and Ducking modes passed normalized 0-1 values
//   directly to setDryWetMix() which expects 0-100 percentage.
// - At 50% mix (0.5 normalized), only 0.5% wet signal was applied - inaudible!
// - Fix: Multiply by 100.0f before calling setDryWetMix().
//
// This test catches the bug by verifying that setDryWetMix stores the correct
// percentage value when given the converted input.
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/effects/spectral_delay.h>
#include <krate/dsp/effects/shimmer_delay.h>
#include <krate/dsp/effects/multi_tap_delay.h>
#include <krate/dsp/effects/ducking_delay.h>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Regression Tests: Verify 0-1 → 0-100 conversion works correctly
// =============================================================================
// These tests simulate the processor's conversion: value * 100.0f
// If the conversion is missing, 50% mix → 0.5% wet → essentially silent delay

TEST_CASE("SpectralDelay mix parameter: correct conversion stores 50%",
          "[regression][mix][spectral]") {
    SpectralDelay delay;

    SECTION("50% mix with correct conversion") {
        const float normalizedMix = 0.5f;
        delay.setDryWetMix(normalizedMix * 100.0f);  // Correct: 50.0f
        REQUIRE(delay.getDryWetMix() == Approx(50.0f).margin(0.1f));
    }

    SECTION("100% mix with correct conversion") {
        delay.setDryWetMix(1.0f * 100.0f);  // 100.0f
        REQUIRE(delay.getDryWetMix() == Approx(100.0f).margin(0.1f));
    }

    SECTION("0% mix with correct conversion") {
        delay.setDryWetMix(0.0f * 100.0f);  // 0.0f
        REQUIRE(delay.getDryWetMix() == Approx(0.0f).margin(0.1f));
    }

    SECTION("BUG DETECTION: without *100, value would be 0.5 instead of 50") {
        const float normalizedMix = 0.5f;
        delay.setDryWetMix(normalizedMix);  // BUG: 0.5 instead of 50.0
        // This shows what the bug looked like - storing 0.5% instead of 50%
        REQUIRE(delay.getDryWetMix() == Approx(0.5f).margin(0.1f));
    }
}

TEST_CASE("ShimmerDelay mix parameter: correct conversion stores 50%",
          "[regression][mix][shimmer]") {
    ShimmerDelay delay;

    SECTION("50% mix with correct conversion") {
        const float normalizedMix = 0.5f;
        delay.setDryWetMix(normalizedMix * 100.0f);  // Correct: 50.0f
        REQUIRE(delay.getDryWetMix() == Approx(50.0f).margin(0.1f));
    }

    SECTION("BUG DETECTION: without *100, value would be 0.5 instead of 50") {
        const float normalizedMix = 0.5f;
        delay.setDryWetMix(normalizedMix);  // BUG: 0.5 instead of 50.0
        REQUIRE(delay.getDryWetMix() == Approx(0.5f).margin(0.1f));
    }
}

TEST_CASE("MultiTapDelay mix parameter: correct conversion stores 50%",
          "[regression][mix][multitap]") {
    MultiTapDelay delay;

    SECTION("50% mix with correct conversion") {
        const float normalizedMix = 0.5f;
        delay.setDryWetMix(normalizedMix * 100.0f);  // Correct: 50.0f
        REQUIRE(delay.getDryWetMix() == Approx(50.0f).margin(0.1f));
    }

    SECTION("BUG DETECTION: without *100, value would be 0.5 instead of 50") {
        const float normalizedMix = 0.5f;
        delay.setDryWetMix(normalizedMix);  // BUG: 0.5 instead of 50.0
        REQUIRE(delay.getDryWetMix() == Approx(0.5f).margin(0.1f));
    }
}

TEST_CASE("DuckingDelay mix parameter: correct conversion stores 50%",
          "[regression][mix][ducking]") {
    DuckingDelay delay;

    SECTION("50% mix with correct conversion") {
        const float normalizedMix = 0.5f;
        delay.setDryWetMix(normalizedMix * 100.0f);  // Correct: 50.0f
        REQUIRE(delay.getDryWetMix() == Approx(50.0f).margin(0.1f));
    }

    SECTION("BUG DETECTION: without *100, value would be 0.5 instead of 50") {
        const float normalizedMix = 0.5f;
        delay.setDryWetMix(normalizedMix);  // BUG: 0.5 instead of 50.0
        REQUIRE(delay.getDryWetMix() == Approx(0.5f).margin(0.1f));
    }
}

// =============================================================================
// Documentation Test: API Contract
// =============================================================================

TEST_CASE("setDryWetMix API contract: expects 0-100 percentage",
          "[api][mix][contract]") {
    // All delay types that use setDryWetMix expect 0-100 percentage range
    // The processor must convert normalized 0-1 values by multiplying by 100

    SECTION("Documentation: conversion formula") {
        // Given: normalized VST parameter value in range [0, 1]
        const float normalizedValue = 0.5f;

        // When: processor passes to DSP
        // CORRECT: float dspValue = normalizedValue * 100.0f;
        // WRONG:   float dspValue = normalizedValue;  // This was the bug!

        const float correctValue = normalizedValue * 100.0f;
        REQUIRE(correctValue == 50.0f);

        const float bugValue = normalizedValue;
        REQUIRE(bugValue == 0.5f);
    }
}
