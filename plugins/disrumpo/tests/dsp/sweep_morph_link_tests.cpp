// ==============================================================================
// Tests: Sweep-Morph Link Curves
// ==============================================================================
// Tests for morph link curve functions that map sweep frequency to morph position.
//
// Reference: specs/007-sweep-system/spec.md (FR-014 to FR-022)
// Reference: specs/007-sweep-system/research.md Section 8
// ==============================================================================

#include "dsp/sweep_morph_link.h"
#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

using Catch::Approx;
using namespace Disrumpo;

// ==============================================================================
// Frequency Normalization Tests
// ==============================================================================

TEST_CASE("normalizeSweepFrequency: basic mapping", "[sweep][morph][link]") {
    SECTION("20 Hz maps to 0.0") {
        REQUIRE(normalizeSweepFrequency(20.0f) == Approx(0.0f).margin(0.001f));
    }

    SECTION("20000 Hz maps to 1.0") {
        REQUIRE(normalizeSweepFrequency(20000.0f) == Approx(1.0f).margin(0.001f));
    }

    SECTION("~632 Hz maps to 0.5 (log-space midpoint)") {
        // Log midpoint: 10^((log10(20) + log10(20000)) / 2) = 10^2.8 = 631.0
        float midFreq = std::sqrt(20.0f * 20000.0f);  // Geometric mean
        REQUIRE(normalizeSweepFrequency(midFreq) == Approx(0.5f).margin(0.01f));
    }

    SECTION("values clamp to valid range") {
        REQUIRE(normalizeSweepFrequency(10.0f) == Approx(0.0f).margin(0.001f));   // Below min
        REQUIRE(normalizeSweepFrequency(30000.0f) == Approx(1.0f).margin(0.001f)); // Above max
    }
}

TEST_CASE("denormalizeSweepFrequency: inverse mapping", "[sweep][morph][link]") {
    SECTION("0.0 maps to 20 Hz") {
        REQUIRE(denormalizeSweepFrequency(0.0f) == Approx(20.0f).margin(0.1f));
    }

    SECTION("1.0 maps to 20000 Hz") {
        REQUIRE(denormalizeSweepFrequency(1.0f) == Approx(20000.0f).margin(1.0f));
    }

    SECTION("round-trip preserves value") {
        float origFreq = 1000.0f;
        float normalized = normalizeSweepFrequency(origFreq);
        float recovered = denormalizeSweepFrequency(normalized);
        REQUIRE(recovered == Approx(origFreq).margin(1.0f));
    }
}

// ==============================================================================
// Linear (SweepFreq) Curve Tests - FR-015
// ==============================================================================

TEST_CASE("applyMorphLinkCurve: Linear (SweepFreq)", "[sweep][morph][link]") {
    SECTION("identity mapping: y = x") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 0.0f) == Approx(0.0f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 0.25f) == Approx(0.25f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 0.5f) == Approx(0.5f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 0.75f) == Approx(0.75f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 1.0f) == Approx(1.0f));
    }

    SECTION("clamping for out-of-range input") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, -0.5f) == Approx(0.0f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::SweepFreq, 1.5f) == Approx(1.0f));
    }
}

// ==============================================================================
// Inverse (InverseSweep) Curve Tests - FR-016
// ==============================================================================

TEST_CASE("applyMorphLinkCurve: Inverse (InverseSweep)", "[sweep][morph][link]") {
    SECTION("inverse mapping: y = 1 - x") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::InverseSweep, 0.0f) == Approx(1.0f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::InverseSweep, 0.25f) == Approx(0.75f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::InverseSweep, 0.5f) == Approx(0.5f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::InverseSweep, 0.75f) == Approx(0.25f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::InverseSweep, 1.0f) == Approx(0.0f));
    }
}

// ==============================================================================
// EaseIn Curve Tests - FR-017
// ==============================================================================

TEST_CASE("applyMorphLinkCurve: EaseIn (quadratic)", "[sweep][morph][link]") {
    SECTION("quadratic ease-in: y = x^2") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::EaseIn, 0.0f) == Approx(0.0f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::EaseIn, 0.5f) == Approx(0.25f));  // 0.5^2 = 0.25
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::EaseIn, 1.0f) == Approx(1.0f));
    }

    SECTION("characteristic: slow start, fast end") {
        float x1 = 0.2f, x2 = 0.4f;
        float y1 = applyMorphLinkCurve(MorphLinkMode::EaseIn, x1);
        float y2 = applyMorphLinkCurve(MorphLinkMode::EaseIn, x2);
        float rate1 = y1 / x1;  // Average slope to x1
        float rate2 = (y2 - y1) / (x2 - x1);  // Slope from x1 to x2
        REQUIRE(rate2 > rate1);  // Rate increases (slow start, fast end)
    }
}

// ==============================================================================
// EaseOut Curve Tests - FR-018
// ==============================================================================

TEST_CASE("applyMorphLinkCurve: EaseOut (inverse quadratic)", "[sweep][morph][link]") {
    SECTION("inverse quadratic: y = 1 - (1-x)^2") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::EaseOut, 0.0f) == Approx(0.0f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::EaseOut, 0.5f) == Approx(0.75f));  // 1 - 0.5^2 = 0.75
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::EaseOut, 1.0f) == Approx(1.0f));
    }

    SECTION("characteristic: fast start, slow end") {
        float x1 = 0.2f, x2 = 0.4f;
        float y1 = applyMorphLinkCurve(MorphLinkMode::EaseOut, x1);
        float y2 = applyMorphLinkCurve(MorphLinkMode::EaseOut, x2);
        float rate1 = y1 / x1;  // Average slope to x1
        float rate2 = (y2 - y1) / (x2 - x1);  // Slope from x1 to x2
        REQUIRE(rate1 > rate2);  // Rate decreases (fast start, slow end)
    }
}

// ==============================================================================
// HoldRise Curve Tests - FR-020
// ==============================================================================

TEST_CASE("applyMorphLinkCurve: HoldRise", "[sweep][morph][link]") {
    SECTION("holds at 0 until 60%, then rises") {
        // y = 0 if x < 0.6, else (x - 0.6) / 0.4
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::HoldRise, 0.0f) == Approx(0.0f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::HoldRise, 0.3f) == Approx(0.0f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::HoldRise, 0.59f) == Approx(0.0f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::HoldRise, 0.6f) == Approx(0.0f));  // Exactly at threshold
    }

    SECTION("rises linearly after 60%") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::HoldRise, 0.7f) == Approx(0.25f).margin(0.01f));  // (0.7-0.6)/0.4 = 0.25
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::HoldRise, 0.8f) == Approx(0.5f).margin(0.01f));   // (0.8-0.6)/0.4 = 0.5
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::HoldRise, 1.0f) == Approx(1.0f).margin(0.01f));   // (1.0-0.6)/0.4 = 1.0
    }
}

// ==============================================================================
// Stepped Curve Tests - FR-021
// ==============================================================================

TEST_CASE("applyMorphLinkCurve: Stepped", "[sweep][morph][link]") {
    SECTION("quantizes to 4 levels: 0, 0.333, 0.667, 1.0") {
        // y = floor(x * 4) / 3
        // x in [0, 0.25) -> floor(x*4) = 0 -> y = 0
        // x in [0.25, 0.5) -> floor(x*4) = 1 -> y = 0.333
        // x in [0.5, 0.75) -> floor(x*4) = 2 -> y = 0.667
        // x in [0.75, 1.0] -> floor(x*4) = 3 -> y = 1.0

        REQUIRE(applyMorphLinkCurve(MorphLinkMode::Stepped, 0.0f) == Approx(0.0f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::Stepped, 0.1f) == Approx(0.0f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::Stepped, 0.24f) == Approx(0.0f));
    }

    SECTION("step transitions at correct points") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::Stepped, 0.25f) == Approx(1.0f / 3.0f).margin(0.01f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::Stepped, 0.4f) == Approx(1.0f / 3.0f).margin(0.01f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::Stepped, 0.5f) == Approx(2.0f / 3.0f).margin(0.01f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::Stepped, 0.7f) == Approx(2.0f / 3.0f).margin(0.01f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::Stepped, 0.75f) == Approx(1.0f).margin(0.01f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::Stepped, 1.0f) == Approx(1.0f).margin(0.01f));
    }
}

// ==============================================================================
// None Mode Tests - FR-014
// ==============================================================================

TEST_CASE("applyMorphLinkCurve: None", "[sweep][morph][link]") {
    SECTION("returns center (0.5) regardless of input") {
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::None, 0.0f) == Approx(0.5f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::None, 0.25f) == Approx(0.5f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::None, 0.5f) == Approx(0.5f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::None, 1.0f) == Approx(0.5f));
    }
}

// ==============================================================================
// Custom Mode Tests - FR-022
// ==============================================================================

TEST_CASE("applyMorphLinkCurve: Custom fallback", "[sweep][morph][link]") {
    SECTION("falls back to linear when no custom curve provided") {
        // Custom mode should use CustomCurve::evaluate() in real usage,
        // but when called directly, returns linear (identity) as fallback
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::Custom, 0.0f) == Approx(0.0f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::Custom, 0.5f) == Approx(0.5f));
        REQUIRE(applyMorphLinkCurve(MorphLinkMode::Custom, 1.0f) == Approx(1.0f));
    }
}

// ==============================================================================
// Intensity Calculation Tests - FR-008, FR-009, FR-010
// ==============================================================================

TEST_CASE("calculateGaussianIntensity: basic behavior", "[sweep][intensity]") {
    const float bandFreq = 1000.0f;
    const float sweepCenter = 1000.0f;
    const float widthOctaves = 2.0f;  // sigma = 1 octave
    const float intensity = 1.0f;  // 100%

    SECTION("at center, intensity equals parameter") {
        float result = calculateGaussianIntensity(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(intensity).margin(0.001f));
    }

    SECTION("at 1 sigma distance, intensity is ~0.606") {
        // 1 octave away from center with sigma=1 octave
        // exp(-0.5 * 1^2) = exp(-0.5) = 0.606...
        float bandAt1Sigma = 2000.0f;  // 1 octave above
        float result = calculateGaussianIntensity(bandAt1Sigma, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.606f).margin(0.01f));
    }

    SECTION("at 2 sigma distance, intensity is ~0.135") {
        // 2 octaves away from center with sigma=1 octave
        // exp(-0.5 * 2^2) = exp(-2) = 0.135...
        float bandAt2Sigma = 4000.0f;  // 2 octaves above
        float result = calculateGaussianIntensity(bandAt2Sigma, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.135f).margin(0.01f));
    }

    SECTION("intensity parameter scales result multiplicatively") {
        float halfIntensity = 0.5f;
        float result = calculateGaussianIntensity(bandFreq, sweepCenter, widthOctaves, halfIntensity);
        REQUIRE(result == Approx(0.5f).margin(0.001f));

        float doubleIntensity = 2.0f;
        result = calculateGaussianIntensity(bandFreq, sweepCenter, widthOctaves, doubleIntensity);
        REQUIRE(result == Approx(2.0f).margin(0.001f));
    }
}

TEST_CASE("calculateLinearFalloff: basic behavior", "[sweep][intensity]") {
    const float bandFreq = 1000.0f;
    const float sweepCenter = 1000.0f;
    const float widthOctaves = 2.0f;  // Half-width = 1 octave
    const float intensity = 1.0f;

    SECTION("at center, intensity equals parameter") {
        float result = calculateLinearFalloff(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(intensity).margin(0.001f));
    }

    SECTION("at half-width edge, intensity is exactly 0") {
        // 1 octave away (the edge)
        float bandAtEdge = 2000.0f;  // 1 octave above
        float result = calculateLinearFalloff(bandAtEdge, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.0f).margin(0.001f));
    }

    SECTION("beyond edge, intensity remains 0") {
        // 2 octaves away (beyond edge)
        float bandBeyond = 4000.0f;
        float result = calculateLinearFalloff(bandBeyond, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.0f).margin(0.001f));
    }

    SECTION("at half-width/2, intensity is 0.5") {
        // 0.5 octaves away (halfway to edge)
        float bandHalfway = sweepCenter * std::pow(2.0f, 0.5f);  // ~1414 Hz
        float result = calculateLinearFalloff(bandHalfway, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.5f).margin(0.01f));
    }

    SECTION("intensity parameter scales result multiplicatively") {
        float halfIntensity = 0.5f;
        float result = calculateLinearFalloff(bandFreq, sweepCenter, widthOctaves, halfIntensity);
        REQUIRE(result == Approx(0.5f).margin(0.001f));
    }
}
