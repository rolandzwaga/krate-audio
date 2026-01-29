// ==============================================================================
// Morph Link Mode Unit Tests
// ==============================================================================
// T153: Unit tests for morph link mode mapping functions (US8)
//
// Constitution Principle XII: Test-First Development
// Reference: specs/006-morph-ui/plan.md "Morph Link Mode Equations"
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/morph_link.h"

using Catch::Approx;
using namespace Disrumpo;

// =============================================================================
// T153: All 7 link mode mapping function tests
// =============================================================================

TEST_CASE("MorphLink mode: None returns manual position", "[morph_link][US8]") {
    // When link mode is None, the manual position should be returned unchanged
    float manualPos = 0.7f;
    float sweepNorm = 0.3f;  // Should be ignored

    float result = applyMorphLinkMode(MorphLinkMode::None, sweepNorm, manualPos);
    REQUIRE(result == Approx(manualPos));
}

TEST_CASE("MorphLink mode: SweepFreq linear mapping", "[morph_link][US8]") {
    // SweepFreq: low freq = 0, high freq = 1 (linear mapping)
    SECTION("sweep at 0 returns 0") {
        float result = applyMorphLinkMode(MorphLinkMode::SweepFreq, 0.0f, 0.5f);
        REQUIRE(result == Approx(0.0f));
    }

    SECTION("sweep at 1 returns 1") {
        float result = applyMorphLinkMode(MorphLinkMode::SweepFreq, 1.0f, 0.5f);
        REQUIRE(result == Approx(1.0f));
    }

    SECTION("sweep at 0.5 returns 0.5") {
        float result = applyMorphLinkMode(MorphLinkMode::SweepFreq, 0.5f, 0.5f);
        REQUIRE(result == Approx(0.5f));
    }

    SECTION("linear mapping is proportional") {
        float result = applyMorphLinkMode(MorphLinkMode::SweepFreq, 0.25f, 0.5f);
        REQUIRE(result == Approx(0.25f));
    }
}

TEST_CASE("MorphLink mode: InverseSweep inverted mapping", "[morph_link][US8]") {
    // InverseSweep: high freq = 0, low freq = 1 (inverted)
    SECTION("sweep at 0 returns 1") {
        float result = applyMorphLinkMode(MorphLinkMode::InverseSweep, 0.0f, 0.5f);
        REQUIRE(result == Approx(1.0f));
    }

    SECTION("sweep at 1 returns 0") {
        float result = applyMorphLinkMode(MorphLinkMode::InverseSweep, 1.0f, 0.5f);
        REQUIRE(result == Approx(0.0f));
    }

    SECTION("sweep at 0.5 returns 0.5") {
        float result = applyMorphLinkMode(MorphLinkMode::InverseSweep, 0.5f, 0.5f);
        REQUIRE(result == Approx(0.5f));
    }

    SECTION("inverted relationship holds") {
        float result = applyMorphLinkMode(MorphLinkMode::InverseSweep, 0.75f, 0.5f);
        REQUIRE(result == Approx(0.25f));
    }
}

TEST_CASE("MorphLink mode: EaseIn exponential emphasizing low frequencies", "[morph_link][US8]") {
    // EaseIn: sqrt(sweepNorm) - more range in bass
    SECTION("sweep at 0 returns 0") {
        float result = applyMorphLinkMode(MorphLinkMode::EaseIn, 0.0f, 0.5f);
        REQUIRE(result == Approx(0.0f));
    }

    SECTION("sweep at 1 returns 1") {
        float result = applyMorphLinkMode(MorphLinkMode::EaseIn, 1.0f, 0.5f);
        REQUIRE(result == Approx(1.0f));
    }

    SECTION("sweep at 0.25 returns sqrt(0.25) = 0.5") {
        float result = applyMorphLinkMode(MorphLinkMode::EaseIn, 0.25f, 0.5f);
        REQUIRE(result == Approx(0.5f));
    }

    SECTION("EaseIn has more range in bass (lower values map higher)") {
        // At 0.04 (low bass), result = sqrt(0.04) = 0.2
        // At 0.25, result = sqrt(0.25) = 0.5
        // At 0.64, result = sqrt(0.64) = 0.8
        float lowResult = applyMorphLinkMode(MorphLinkMode::EaseIn, 0.04f, 0.5f);
        float midResult = applyMorphLinkMode(MorphLinkMode::EaseIn, 0.25f, 0.5f);
        float highResult = applyMorphLinkMode(MorphLinkMode::EaseIn, 0.64f, 0.5f);

        REQUIRE(lowResult == Approx(0.2f).margin(0.01f));
        REQUIRE(midResult == Approx(0.5f).margin(0.01f));
        REQUIRE(highResult == Approx(0.8f).margin(0.01f));
    }
}

TEST_CASE("MorphLink mode: EaseOut exponential emphasizing high frequencies", "[morph_link][US8]") {
    // EaseOut: sweepNorm^2 - more range in highs
    SECTION("sweep at 0 returns 0") {
        float result = applyMorphLinkMode(MorphLinkMode::EaseOut, 0.0f, 0.5f);
        REQUIRE(result == Approx(0.0f));
    }

    SECTION("sweep at 1 returns 1") {
        float result = applyMorphLinkMode(MorphLinkMode::EaseOut, 1.0f, 0.5f);
        REQUIRE(result == Approx(1.0f));
    }

    SECTION("sweep at 0.5 returns 0.25") {
        float result = applyMorphLinkMode(MorphLinkMode::EaseOut, 0.5f, 0.5f);
        REQUIRE(result == Approx(0.25f));
    }

    SECTION("EaseOut has more range in highs (higher values map lower)") {
        // At 0.5, result = 0.25
        // At 0.7, result = 0.49
        // At 0.9, result = 0.81
        float lowResult = applyMorphLinkMode(MorphLinkMode::EaseOut, 0.5f, 0.5f);
        float midResult = applyMorphLinkMode(MorphLinkMode::EaseOut, 0.7f, 0.5f);
        float highResult = applyMorphLinkMode(MorphLinkMode::EaseOut, 0.9f, 0.5f);

        REQUIRE(lowResult == Approx(0.25f).margin(0.01f));
        REQUIRE(midResult == Approx(0.49f).margin(0.01f));
        REQUIRE(highResult == Approx(0.81f).margin(0.01f));
    }
}

TEST_CASE("MorphLink mode: HoldRise holds then rises", "[morph_link][US8]") {
    // HoldRise: Hold at 0 until midpoint, then rise linearly to 1
    SECTION("sweep at 0 returns 0") {
        float result = applyMorphLinkMode(MorphLinkMode::HoldRise, 0.0f, 0.5f);
        REQUIRE(result == Approx(0.0f));
    }

    SECTION("sweep below midpoint returns 0") {
        float result = applyMorphLinkMode(MorphLinkMode::HoldRise, 0.3f, 0.5f);
        REQUIRE(result == Approx(0.0f));
    }

    SECTION("sweep at exactly midpoint returns 0") {
        float result = applyMorphLinkMode(MorphLinkMode::HoldRise, 0.5f, 0.5f);
        REQUIRE(result == Approx(0.0f));
    }

    SECTION("sweep at 0.75 returns 0.5") {
        // (0.75 - 0.5) * 2 = 0.5
        float result = applyMorphLinkMode(MorphLinkMode::HoldRise, 0.75f, 0.5f);
        REQUIRE(result == Approx(0.5f));
    }

    SECTION("sweep at 1 returns 1") {
        float result = applyMorphLinkMode(MorphLinkMode::HoldRise, 1.0f, 0.5f);
        REQUIRE(result == Approx(1.0f));
    }
}

TEST_CASE("MorphLink mode: Stepped quantizes to discrete steps", "[morph_link][US8]") {
    // Stepped: Quantize to 0, 0.25, 0.5, 0.75, 1.0 (5 discrete steps)
    SECTION("sweep at 0 returns 0") {
        float result = applyMorphLinkMode(MorphLinkMode::Stepped, 0.0f, 0.5f);
        REQUIRE(result == Approx(0.0f));
    }

    SECTION("sweep at 1 returns 1") {
        float result = applyMorphLinkMode(MorphLinkMode::Stepped, 1.0f, 0.5f);
        REQUIRE(result == Approx(1.0f));
    }

    SECTION("sweep just below 0.2 returns 0") {
        float result = applyMorphLinkMode(MorphLinkMode::Stepped, 0.19f, 0.5f);
        REQUIRE(result == Approx(0.0f));
    }

    SECTION("sweep at 0.2 returns 0.25") {
        float result = applyMorphLinkMode(MorphLinkMode::Stepped, 0.2f, 0.5f);
        REQUIRE(result == Approx(0.25f));
    }

    SECTION("sweep at 0.4 returns 0.5") {
        float result = applyMorphLinkMode(MorphLinkMode::Stepped, 0.4f, 0.5f);
        REQUIRE(result == Approx(0.5f));
    }

    SECTION("sweep at 0.6 returns 0.75") {
        float result = applyMorphLinkMode(MorphLinkMode::Stepped, 0.6f, 0.5f);
        REQUIRE(result == Approx(0.75f));
    }

    SECTION("sweep at 0.8 returns 1.0") {
        float result = applyMorphLinkMode(MorphLinkMode::Stepped, 0.8f, 0.5f);
        REQUIRE(result == Approx(1.0f));
    }
}

// =============================================================================
// Edge cases and boundary tests
// =============================================================================

TEST_CASE("MorphLink edge cases", "[morph_link][US8]") {
    SECTION("all modes handle exact 0 input") {
        for (int i = 0; i < static_cast<int>(MorphLinkMode::COUNT); ++i) {
            auto mode = static_cast<MorphLinkMode>(i);
            float result = applyMorphLinkMode(mode, 0.0f, 0.5f);
            // Result should be valid (no NaN/Inf)
            REQUIRE(result >= 0.0f);
            REQUIRE(result <= 1.0f);
        }
    }

    SECTION("all modes handle exact 1 input") {
        for (int i = 0; i < static_cast<int>(MorphLinkMode::COUNT); ++i) {
            auto mode = static_cast<MorphLinkMode>(i);
            float result = applyMorphLinkMode(mode, 1.0f, 0.5f);
            // Result should be valid (no NaN/Inf)
            REQUIRE(result >= 0.0f);
            REQUIRE(result <= 1.0f);
        }
    }

    SECTION("all modes produce results in [0, 1] range") {
        for (int i = 0; i < static_cast<int>(MorphLinkMode::COUNT); ++i) {
            auto mode = static_cast<MorphLinkMode>(i);
            for (float sweep = 0.0f; sweep <= 1.0f; sweep += 0.1f) {
                float result = applyMorphLinkMode(mode, sweep, 0.5f);
                REQUIRE(result >= 0.0f);
                REQUIRE(result <= 1.0f);
            }
        }
    }
}

// =============================================================================
// Sweep frequency to normalized position conversion tests
// =============================================================================

TEST_CASE("Sweep frequency to normalized conversion", "[morph_link][US8]") {
    SECTION("20Hz maps to 0") {
        float norm = sweepFrequencyToNormalized(20.0f);
        REQUIRE(norm == Approx(0.0f).margin(0.01f));
    }

    SECTION("20kHz maps to 1") {
        float norm = sweepFrequencyToNormalized(20000.0f);
        REQUIRE(norm == Approx(1.0f).margin(0.01f));
    }

    SECTION("1kHz maps to approximately 0.567 (log scale)") {
        // log(1000/20) / log(20000/20) = log(50) / log(1000)
        // = 1.699 / 3.0 = 0.566
        float norm = sweepFrequencyToNormalized(1000.0f);
        REQUIRE(norm == Approx(0.567f).margin(0.02f));
    }

    SECTION("frequencies below 20Hz are clamped") {
        float norm = sweepFrequencyToNormalized(10.0f);
        REQUIRE(norm == Approx(0.0f));
    }

    SECTION("frequencies above 20kHz are clamped") {
        float norm = sweepFrequencyToNormalized(25000.0f);
        REQUIRE(norm == Approx(1.0f));
    }
}
