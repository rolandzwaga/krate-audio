// Layer 0: Core Utility Tests - Pitch Conversion
// Part of Granular Delay feature (spec 034)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/core/pitch_utils.h"

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// semitonesToRatio Tests
// =============================================================================

TEST_CASE("semitonesToRatio converts semitones to playback ratio", "[core][pitch][layer0]") {

    SECTION("0 semitones returns unity ratio") {
        REQUIRE(semitonesToRatio(0.0f) == Approx(1.0f).margin(1e-6f));
    }

    SECTION("+12 semitones returns 2.0 (octave up)") {
        REQUIRE(semitonesToRatio(12.0f) == Approx(2.0f).margin(1e-5f));
    }

    SECTION("-12 semitones returns 0.5 (octave down)") {
        REQUIRE(semitonesToRatio(-12.0f) == Approx(0.5f).margin(1e-5f));
    }

    SECTION("+24 semitones returns 4.0 (two octaves up)") {
        REQUIRE(semitonesToRatio(24.0f) == Approx(4.0f).margin(1e-4f));
    }

    SECTION("-24 semitones returns 0.25 (two octaves down)") {
        REQUIRE(semitonesToRatio(-24.0f) == Approx(0.25f).margin(1e-5f));
    }

    SECTION("+7 semitones returns perfect fifth ratio (~1.498)") {
        // Perfect fifth = 2^(7/12) ≈ 1.4983
        REQUIRE(semitonesToRatio(7.0f) == Approx(1.4983f).margin(1e-3f));
    }

    SECTION("-7 semitones returns inverted perfect fifth (~0.667)") {
        // Inverted perfect fifth = 2^(-7/12) ≈ 0.6674
        REQUIRE(semitonesToRatio(-7.0f) == Approx(0.6674f).margin(1e-3f));
    }

    SECTION("+1 semitone returns semitone ratio (~1.0595)") {
        // One semitone = 2^(1/12) ≈ 1.05946
        REQUIRE(semitonesToRatio(1.0f) == Approx(1.05946f).margin(1e-4f));
    }
}

// =============================================================================
// ratioToSemitones Tests
// =============================================================================

TEST_CASE("ratioToSemitones converts playback ratio to semitones", "[core][pitch][layer0]") {

    SECTION("unity ratio returns 0 semitones") {
        REQUIRE(ratioToSemitones(1.0f) == Approx(0.0f).margin(1e-6f));
    }

    SECTION("2.0 ratio returns +12 semitones (octave up)") {
        REQUIRE(ratioToSemitones(2.0f) == Approx(12.0f).margin(1e-4f));
    }

    SECTION("0.5 ratio returns -12 semitones (octave down)") {
        REQUIRE(ratioToSemitones(0.5f) == Approx(-12.0f).margin(1e-4f));
    }

    SECTION("4.0 ratio returns +24 semitones (two octaves up)") {
        REQUIRE(ratioToSemitones(4.0f) == Approx(24.0f).margin(1e-4f));
    }

    SECTION("0.25 ratio returns -24 semitones (two octaves down)") {
        REQUIRE(ratioToSemitones(0.25f) == Approx(-24.0f).margin(1e-4f));
    }

    SECTION("invalid ratio (0 or negative) returns 0") {
        REQUIRE(ratioToSemitones(0.0f) == 0.0f);
        REQUIRE(ratioToSemitones(-1.0f) == 0.0f);
    }
}

// =============================================================================
// Roundtrip Tests
// =============================================================================

TEST_CASE("semitonesToRatio and ratioToSemitones are inverses", "[core][pitch][layer0]") {

    SECTION("roundtrip: semitones -> ratio -> semitones") {
        const float testValues[] = {-24.0f, -12.0f, -7.0f, -1.0f, 0.0f, 1.0f, 7.0f, 12.0f, 24.0f};

        for (float semitones : testValues) {
            float ratio = semitonesToRatio(semitones);
            float recovered = ratioToSemitones(ratio);
            REQUIRE(recovered == Approx(semitones).margin(1e-4f));
        }
    }

    SECTION("roundtrip: ratio -> semitones -> ratio") {
        const float testRatios[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f};

        for (float ratio : testRatios) {
            float semitones = ratioToSemitones(ratio);
            float recovered = semitonesToRatio(semitones);
            REQUIRE(recovered == Approx(ratio).margin(1e-5f));
        }
    }
}

// =============================================================================
// Pitch Accuracy Test (SC-003: accurate within 10 cents)
// =============================================================================

TEST_CASE("Pitch conversion accuracy within 10 cents", "[core][pitch][layer0][SC-003]") {
    // 10 cents = 0.1 semitones
    constexpr float kMaxErrorCents = 10.0f;
    constexpr float kMaxErrorSemitones = kMaxErrorCents / 100.0f;

    // Test across the full -24 to +24 semitone range at 1-semitone intervals
    for (int semitones = -24; semitones <= 24; ++semitones) {
        float targetSemitones = static_cast<float>(semitones);
        float ratio = semitonesToRatio(targetSemitones);
        float measuredSemitones = ratioToSemitones(ratio);

        // Verify accuracy within 10 cents
        REQUIRE(std::abs(measuredSemitones - targetSemitones) < kMaxErrorSemitones);
    }
}
