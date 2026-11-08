// ==============================================================================
// Morph Link Mode Unit Tests
// ==============================================================================
// T153: Unit tests for morph link mode mapping functions (US8)
//
// The link curves have one definition, applyMorphLinkCurve in
// dsp/sweep_morph_link.h (T1.12: the controller copy and its sqrt / x^2 /
// 5-step variants are gone; the processor applies the link). These are the
// cases of the old controller-curve test that still hold for that definition;
// the per-mode table against dsp-details.md is in dsp/sweep_morph_link_tests.cpp.
//
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/sweep_morph_link.h"

using Catch::Approx;
using namespace Disrumpo;

TEST_CASE("MorphLink mode: SweepFreq linear mapping", "[morph_link][US8]") {
    // SweepFreq: low freq = 0, high freq = 1 (linear mapping)
    SECTION("sweep at 0 returns 0") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 0.0f) == Approx(0.0f));
    }

    SECTION("sweep at 1 returns 1") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 1.0f) == Approx(1.0f));
    }

    SECTION("sweep at 0.5 returns 0.5") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 0.5f) == Approx(0.5f));
    }

    SECTION("linear mapping is proportional") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 0.25f) == Approx(0.25f));
    }
}

TEST_CASE("MorphLink mode: InverseSweep inverted mapping", "[morph_link][US8]") {
    // InverseSweep: high freq = 0, low freq = 1 (inverted)
    SECTION("sweep at 0 returns 1") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::InverseSweep, 0.0f) == Approx(1.0f));
    }

    SECTION("sweep at 1 returns 0") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::InverseSweep, 1.0f) == Approx(0.0f));
    }

    SECTION("sweep at 0.5 returns 0.5") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::InverseSweep, 0.5f) == Approx(0.5f));
    }

    SECTION("inverted relationship holds") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::InverseSweep, 0.75f) == Approx(0.25f));
    }
}

// =============================================================================
// Edge cases and boundary tests
// =============================================================================

TEST_CASE("MorphLink edge cases", "[morph_link][US8]") {
    SECTION("all modes handle exact 0 input") {
        for (int i = 0; i < static_cast<int>(MorphLinkMode::COUNT); ++i) {
            const float result = applyMorphLinkCurve(static_cast<MorphLinkMode>(i), 0.0f);
            REQUIRE(result >= 0.0f);
            REQUIRE(result <= 1.0f);
        }
    }

    SECTION("all modes handle exact 1 input") {
        for (int i = 0; i < static_cast<int>(MorphLinkMode::COUNT); ++i) {
            const float result = applyMorphLinkCurve(static_cast<MorphLinkMode>(i), 1.0f);
            REQUIRE(result >= 0.0f);
            REQUIRE(result <= 1.0f);
        }
    }

    SECTION("all modes produce results in [0, 1] range") {
        for (int i = 0; i < static_cast<int>(MorphLinkMode::COUNT); ++i) {
            const auto mode = static_cast<MorphLinkMode>(i);
            for (int k = 0; k <= 10; ++k) {
                const float result = applyMorphLinkCurve(mode, static_cast<float>(k) * 0.1f);
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
        REQUIRE(normalizeSweepFrequency(20.0f) == Approx(0.0f).margin(0.01f));
    }

    SECTION("20kHz maps to 1") {
        REQUIRE(normalizeSweepFrequency(20000.0f) == Approx(1.0f).margin(0.01f));
    }

    SECTION("1kHz maps to approximately 0.567 (log scale)") {
        // log(1000/20) / log(20000/20) = log(50) / log(1000) = 1.699 / 3.0 = 0.566
        REQUIRE(normalizeSweepFrequency(1000.0f) == Approx(0.567f).margin(0.02f));
    }

    SECTION("frequencies below 20Hz are clamped") {
        REQUIRE(normalizeSweepFrequency(10.0f) == Approx(0.0f).margin(1e-6f));
    }

    SECTION("frequencies above 20kHz are clamped") {
        REQUIRE(normalizeSweepFrequency(25000.0f) == Approx(1.0f).margin(1e-6f));
    }
}
