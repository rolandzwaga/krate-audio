// ==============================================================================
// Tests: Sweep Intensity Calculations (User Story 1)
// ==============================================================================
// Tests for Gaussian and Sharp falloff intensity calculations per SC-001 to SC-005.
//
// Reference: specs/007-sweep-system/spec.md (FR-006, FR-008, FR-009, FR-010)
// ==============================================================================

#include "dsp/sweep_morph_link.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

using Catch::Approx;
using namespace Disrumpo;

// ==============================================================================
// SC-001, SC-002, SC-003: Gaussian Intensity Distribution
// ==============================================================================

TEST_CASE("Gaussian intensity: center equals intensity parameter (SC-001)", "[sweep][intensity][gaussian]") {
    const float sweepCenter = 1000.0f;
    const float widthOctaves = 2.0f;

    SECTION("100% intensity at center") {
        float result = calculateGaussianIntensity(sweepCenter, sweepCenter, widthOctaves, 1.0f);
        REQUIRE(result == Approx(1.0f).margin(0.001f));
    }

    SECTION("50% intensity at center") {
        float result = calculateGaussianIntensity(sweepCenter, sweepCenter, widthOctaves, 0.5f);
        REQUIRE(result == Approx(0.5f).margin(0.001f));
    }

    SECTION("200% intensity at center") {
        float result = calculateGaussianIntensity(sweepCenter, sweepCenter, widthOctaves, 2.0f);
        REQUIRE(result == Approx(2.0f).margin(0.001f));
    }
}

TEST_CASE("Gaussian intensity: 1 sigma = 0.606 * intensity (SC-002)", "[sweep][intensity][gaussian]") {
    const float sweepCenter = 1000.0f;
    const float widthOctaves = 2.0f;  // sigma = 1 octave
    const float intensity = 1.0f;

    // 1 sigma = 1 octave = 2x frequency ratio
    // exp(-0.5) = 0.6065...

    SECTION("1 octave above center") {
        float bandFreq = 2000.0f;  // 1 octave above 1000 Hz
        float result = calculateGaussianIntensity(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.606f).margin(0.01f));
    }

    SECTION("1 octave below center") {
        float bandFreq = 500.0f;  // 1 octave below 1000 Hz
        float result = calculateGaussianIntensity(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.606f).margin(0.01f));
    }

    SECTION("1 sigma with different intensity") {
        float bandFreq = 2000.0f;
        float halfIntensity = 0.5f;
        float result = calculateGaussianIntensity(bandFreq, sweepCenter, widthOctaves, halfIntensity);
        REQUIRE(result == Approx(0.303f).margin(0.01f));  // 0.606 * 0.5
    }
}

TEST_CASE("Gaussian intensity: 2 sigma = 0.135 * intensity (SC-003)", "[sweep][intensity][gaussian]") {
    const float sweepCenter = 1000.0f;
    const float widthOctaves = 2.0f;  // sigma = 1 octave
    const float intensity = 1.0f;

    // 2 sigma = 2 octaves = 4x frequency ratio
    // exp(-2) = 0.1353...

    SECTION("2 octaves above center") {
        float bandFreq = 4000.0f;  // 2 octaves above 1000 Hz
        float result = calculateGaussianIntensity(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.135f).margin(0.01f));
    }

    SECTION("2 octaves below center") {
        float bandFreq = 250.0f;  // 2 octaves below 1000 Hz
        float result = calculateGaussianIntensity(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.135f).margin(0.01f));
    }
}

TEST_CASE("Gaussian intensity: 3 sigma = ~0.011 * intensity", "[sweep][intensity][gaussian]") {
    const float sweepCenter = 1000.0f;
    const float widthOctaves = 2.0f;  // sigma = 1 octave
    const float intensity = 1.0f;

    // 3 sigma = 3 octaves = 8x frequency ratio
    // exp(-4.5) = 0.0111...

    SECTION("3 octaves above center") {
        float bandFreq = 8000.0f;  // 3 octaves above 1000 Hz
        float result = calculateGaussianIntensity(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.011f).margin(0.005f));
    }
}

// ==============================================================================
// SC-004, SC-005: Sharp (Linear) Falloff
// ==============================================================================

TEST_CASE("Sharp falloff: center equals intensity parameter (SC-004)", "[sweep][intensity][sharp]") {
    const float sweepCenter = 1000.0f;
    const float widthOctaves = 2.0f;

    SECTION("100% intensity at center") {
        float result = calculateLinearFalloff(sweepCenter, sweepCenter, widthOctaves, 1.0f);
        REQUIRE(result == Approx(1.0f).margin(0.001f));
    }

    SECTION("50% intensity at center") {
        float result = calculateLinearFalloff(sweepCenter, sweepCenter, widthOctaves, 0.5f);
        REQUIRE(result == Approx(0.5f).margin(0.001f));
    }
}

TEST_CASE("Sharp falloff: edge = exactly 0.0 (SC-004)", "[sweep][intensity][sharp]") {
    const float sweepCenter = 1000.0f;
    const float widthOctaves = 2.0f;  // Edge at +/- 1 octave
    const float intensity = 1.0f;

    SECTION("at half-width edge (1 octave above)") {
        float bandFreq = 2000.0f;  // Exactly at edge
        float result = calculateLinearFalloff(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.0f).margin(0.001f));
    }

    SECTION("at half-width edge (1 octave below)") {
        float bandFreq = 500.0f;  // Exactly at edge
        float result = calculateLinearFalloff(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.0f).margin(0.001f));
    }
}

TEST_CASE("Sharp falloff: beyond edge = 0.0 (SC-005)", "[sweep][intensity][sharp]") {
    const float sweepCenter = 1000.0f;
    const float widthOctaves = 2.0f;  // Edge at +/- 1 octave
    const float intensity = 1.0f;

    SECTION("beyond edge (2 octaves above)") {
        float bandFreq = 4000.0f;  // Beyond edge
        float result = calculateLinearFalloff(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.0f).margin(0.001f));
    }

    SECTION("beyond edge (2 octaves below)") {
        float bandFreq = 250.0f;  // Beyond edge
        float result = calculateLinearFalloff(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.0f).margin(0.001f));
    }

    SECTION("far beyond edge") {
        float bandFreq = 20000.0f;  // Way beyond edge
        float result = calculateLinearFalloff(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.0f).margin(0.001f));
    }
}

TEST_CASE("Sharp falloff: linear interpolation within range", "[sweep][intensity][sharp]") {
    const float sweepCenter = 1000.0f;
    const float widthOctaves = 2.0f;  // Half-width = 1 octave
    const float intensity = 1.0f;

    SECTION("halfway to edge = 0.5") {
        // 0.5 octaves from center
        float bandFreq = sweepCenter * std::pow(2.0f, 0.5f);  // ~1414 Hz
        float result = calculateLinearFalloff(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.5f).margin(0.01f));
    }

    SECTION("quarter way to edge = 0.75") {
        // 0.25 octaves from center
        float bandFreq = sweepCenter * std::pow(2.0f, 0.25f);  // ~1189 Hz
        float result = calculateLinearFalloff(bandFreq, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.75f).margin(0.01f));
    }
}

// ==============================================================================
// Width Parameter Variations
// ==============================================================================

TEST_CASE("Intensity calculations: width variations", "[sweep][intensity]") {
    const float sweepCenter = 1000.0f;
    const float intensity = 1.0f;

    SECTION("narrow width (0.5 octaves) - more focused") {
        float widthOctaves = 0.5f;  // sigma = 0.25 octave

        // At 0.25 octave (1 sigma), should be ~0.606
        float bandAt1Sigma = sweepCenter * std::pow(2.0f, 0.25f);
        float result = calculateGaussianIntensity(bandAt1Sigma, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.606f).margin(0.02f));
    }

    SECTION("wide width (4 octaves) - more spread") {
        float widthOctaves = 4.0f;  // sigma = 2 octaves

        // At 2 octave (1 sigma), should be ~0.606
        float bandAt1Sigma = sweepCenter * std::pow(2.0f, 2.0f);  // 4000 Hz
        float result = calculateGaussianIntensity(bandAt1Sigma, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.606f).margin(0.02f));
    }
}

// ==============================================================================
// Sweep Center Variations
// ==============================================================================

TEST_CASE("Intensity calculations: sweep center variations", "[sweep][intensity]") {
    const float widthOctaves = 2.0f;
    const float intensity = 1.0f;

    SECTION("low sweep center (100 Hz)") {
        float sweepCenter = 100.0f;
        float bandAt1Sigma = 200.0f;  // 1 octave above
        float result = calculateGaussianIntensity(bandAt1Sigma, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.606f).margin(0.02f));
    }

    SECTION("high sweep center (10 kHz)") {
        float sweepCenter = 10000.0f;
        float bandAt1Sigma = 5000.0f;  // 1 octave below
        float result = calculateGaussianIntensity(bandAt1Sigma, sweepCenter, widthOctaves, intensity);
        REQUIRE(result == Approx(0.606f).margin(0.02f));
    }
}
