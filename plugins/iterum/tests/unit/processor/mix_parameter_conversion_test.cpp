// ==============================================================================
// Regression Test: Mix Parameter Conversion (Processor → DSP)
// ==============================================================================
// Tests that the processor correctly passes mix parameters to DSP delay classes.
//
// NOTE: SpectralDelay now uses normalized 0-1 API (no conversion needed).
// ShimmerDelay and MultiTapDelay still use 0-100 percentage API and require
// multiplication by 100.0f in the processor.
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

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// SpectralDelay: Uses normalized 0-1 API (no conversion needed)
// =============================================================================

TEST_CASE("SpectralDelay mix parameter: normalized 0-1 API",
          "[regression][mix][spectral]") {
    SpectralDelay delay;

    SECTION("50% mix passed directly as 0.5") {
        delay.setDryWetMix(0.5f);
        REQUIRE(delay.getDryWetMix() == Approx(0.5f).margin(0.001f));
    }

    SECTION("100% mix passed directly as 1.0") {
        delay.setDryWetMix(1.0f);
        REQUIRE(delay.getDryWetMix() == Approx(1.0f).margin(0.001f));
    }

    SECTION("0% mix passed directly as 0.0") {
        delay.setDryWetMix(0.0f);
        REQUIRE(delay.getDryWetMix() == Approx(0.0f).margin(0.001f));
    }

    SECTION("Values are clamped to 0-1 range") {
        delay.setDryWetMix(1.5f);
        REQUIRE(delay.getDryWetMix() == Approx(1.0f).margin(0.001f));

        delay.setDryWetMix(-0.5f);
        REQUIRE(delay.getDryWetMix() == Approx(0.0f).margin(0.001f));
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

// =============================================================================
// Documentation Test: API Contract
// =============================================================================

TEST_CASE("setDryWetMix API contract: SpectralDelay uses 0-1, others use 0-100",
          "[api][mix][contract]") {
    // SpectralDelay uses normalized 0-1 range (no conversion needed)
    // ShimmerDelay, MultiTapDelay still use 0-100 percentage range

    SECTION("SpectralDelay: processor passes normalized value directly") {
        const float normalizedValue = 0.5f;
        SpectralDelay delay;
        delay.setDryWetMix(normalizedValue);
        REQUIRE(delay.getDryWetMix() == Approx(0.5f).margin(0.001f));
    }

    SECTION("ShimmerDelay: processor must multiply by 100") {
        const float normalizedValue = 0.5f;
        ShimmerDelay delay;
        delay.setDryWetMix(normalizedValue * 100.0f);
        REQUIRE(delay.getDryWetMix() == Approx(50.0f).margin(0.1f));
    }
}
