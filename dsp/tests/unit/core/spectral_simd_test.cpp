// ==============================================================================
// Layer 0: Core Tests - SIMD-Accelerated Spectral Math
// ==============================================================================
// Tests for computePolarBulk() and reconstructCartesianBulk().
// Verifies SIMD results match scalar std::sqrt/atan2/cos/sin.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/core/spectral_simd.h>

#include <cmath>
#include <numbers>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// computePolarBulk Tests
// ==============================================================================

TEST_CASE("computePolarBulk known values", "[spectral_simd][polar]") {
    // Interleaved: {real0, imag0, real1, imag1, ...}
    std::vector<float> complex_data = {
        3.0f, 4.0f,    // bin 0: mag=5, phase=atan2(4,3)
        0.0f, 0.0f,    // bin 1: mag=0, phase=0
        1.0f, 0.0f,    // bin 2: mag=1, phase=0 (pure real)
        0.0f, 5.0f,    // bin 3: mag=5, phase=pi/2 (pure imag)
        -1.0f, 0.0f,   // bin 4: mag=1, phase=pi (negative real)
        6.0f, 8.0f,    // bin 5: mag=10, phase=atan2(8,6)
        -3.0f, -4.0f   // bin 6: mag=5, phase=atan2(-4,-3)
    };
    const size_t numBins = 7;

    std::vector<float> mags(numBins);
    std::vector<float> phases(numBins);

    computePolarBulk(complex_data.data(), numBins, mags.data(), phases.data());

    // Bin 0: (3, 4) → mag=5
    REQUIRE(mags[0] == Approx(5.0f).margin(0.001f));
    REQUIRE(phases[0] == Approx(std::atan2(4.0f, 3.0f)).margin(0.001f));

    // Bin 1: (0, 0) → mag=0
    REQUIRE(mags[1] == Approx(0.0f).margin(0.001f));

    // Bin 2: (1, 0) → mag=1, phase=0
    REQUIRE(mags[2] == Approx(1.0f).margin(0.001f));
    REQUIRE(phases[2] == Approx(0.0f).margin(0.001f));

    // Bin 3: (0, 5) → mag=5, phase=pi/2
    REQUIRE(mags[3] == Approx(5.0f).margin(0.001f));
    REQUIRE(phases[3] == Approx(std::atan2(5.0f, 0.0f)).margin(0.001f));

    // Bin 4: (-1, 0) → mag=1, phase=pi
    REQUIRE(mags[4] == Approx(1.0f).margin(0.001f));
    REQUIRE(phases[4] == Approx(std::atan2(0.0f, -1.0f)).margin(0.001f));

    // Bin 5: (6, 8) → mag=10
    REQUIRE(mags[5] == Approx(10.0f).margin(0.001f));
    REQUIRE(phases[5] == Approx(std::atan2(8.0f, 6.0f)).margin(0.001f));

    // Bin 6: (-3, -4) → mag=5
    REQUIRE(mags[6] == Approx(5.0f).margin(0.001f));
    REQUIRE(phases[6] == Approx(std::atan2(-4.0f, -3.0f)).margin(0.001f));
}

// ==============================================================================
// reconstructCartesianBulk Tests
// ==============================================================================

TEST_CASE("reconstructCartesianBulk known values", "[spectral_simd][cartesian]") {
    const size_t numBins = 5;
    std::vector<float> mags = {5.0f, 0.0f, 1.0f, 10.0f, 3.0f};
    std::vector<float> phases = {
        std::atan2(4.0f, 3.0f),  // → (3, 4)
        0.0f,                     // → (0, 0)
        0.0f,                     // → (1, 0)
        std::atan2(8.0f, 6.0f),  // → (6, 8)
        std::numbers::pi_v<float> / 2.0f  // → (0, 3)
    };

    std::vector<float> complex_data(numBins * 2);
    reconstructCartesianBulk(mags.data(), phases.data(), numBins, complex_data.data());

    // Bin 0: mag=5, phase=atan2(4,3) → (3, 4)
    REQUIRE(complex_data[0] == Approx(3.0f).margin(0.01f));
    REQUIRE(complex_data[1] == Approx(4.0f).margin(0.01f));

    // Bin 1: mag=0 → (0, 0)
    REQUIRE(complex_data[2] == Approx(0.0f).margin(0.001f));
    REQUIRE(complex_data[3] == Approx(0.0f).margin(0.001f));

    // Bin 2: mag=1, phase=0 → (1, 0)
    REQUIRE(complex_data[4] == Approx(1.0f).margin(0.001f));
    REQUIRE(complex_data[5] == Approx(0.0f).margin(0.001f));

    // Bin 3: mag=10, phase=atan2(8,6) → (6, 8)
    REQUIRE(complex_data[6] == Approx(6.0f).margin(0.01f));
    REQUIRE(complex_data[7] == Approx(8.0f).margin(0.01f));

    // Bin 4: mag=3, phase=pi/2 → (0, 3)
    REQUIRE(complex_data[8] == Approx(0.0f).margin(0.01f));
    REQUIRE(complex_data[9] == Approx(3.0f).margin(0.01f));
}

// ==============================================================================
// Round-Trip Tests
// ==============================================================================

TEST_CASE("computePolarBulk + reconstructCartesianBulk round-trip", "[spectral_simd][roundtrip]") {
    const size_t numBins = 1025;  // Typical FFT size 2048 → 1025 bins

    // Generate test data: various magnitudes and phases
    std::vector<float> original(numBins * 2);
    for (size_t k = 0; k < numBins; ++k) {
        original[k * 2] = std::sin(static_cast<float>(k) * 0.1f) * 10.0f;      // real
        original[k * 2 + 1] = std::cos(static_cast<float>(k) * 0.07f) * 8.0f;  // imag
    }

    // Forward: Cartesian → Polar
    std::vector<float> mags(numBins);
    std::vector<float> phases(numBins);
    computePolarBulk(original.data(), numBins, mags.data(), phases.data());

    // Inverse: Polar → Cartesian
    std::vector<float> reconstructed(numBins * 2);
    reconstructCartesianBulk(mags.data(), phases.data(), numBins, reconstructed.data());

    // Verify round-trip accuracy
    for (size_t k = 0; k < numBins; ++k) {
        REQUIRE(reconstructed[k * 2] == Approx(original[k * 2]).margin(0.01f));
        REQUIRE(reconstructed[k * 2 + 1] == Approx(original[k * 2 + 1]).margin(0.01f));
    }
}

TEST_CASE("SIMD scalar tail exercised with non-aligned count", "[spectral_simd][tail]") {
    // Use counts that are NOT multiples of common SIMD widths (4, 8, 16)
    for (size_t numBins : {1, 3, 5, 7, 9, 11, 13, 15, 17}) {
        std::vector<float> complex_data(numBins * 2);
        for (size_t k = 0; k < numBins; ++k) {
            complex_data[k * 2] = static_cast<float>(k + 1);
            complex_data[k * 2 + 1] = static_cast<float>(k + 2);
        }

        std::vector<float> mags(numBins);
        std::vector<float> phases(numBins);
        computePolarBulk(complex_data.data(), numBins, mags.data(), phases.data());

        // Verify against scalar reference
        for (size_t k = 0; k < numBins; ++k) {
            const float re = complex_data[k * 2];
            const float im = complex_data[k * 2 + 1];
            const float expectedMag = std::sqrt(re * re + im * im);
            const float expectedPhase = std::atan2(im, re);
            REQUIRE(mags[k] == Approx(expectedMag).margin(0.001f));
            REQUIRE(phases[k] == Approx(expectedPhase).margin(0.001f));
        }

        // Round-trip
        std::vector<float> reconstructed(numBins * 2);
        reconstructCartesianBulk(mags.data(), phases.data(), numBins, reconstructed.data());

        for (size_t k = 0; k < numBins; ++k) {
            REQUIRE(reconstructed[k * 2] == Approx(complex_data[k * 2]).margin(0.01f));
            REQUIRE(reconstructed[k * 2 + 1] == Approx(complex_data[k * 2 + 1]).margin(0.01f));
        }
    }
}

TEST_CASE("SIMD handles zero-length input", "[spectral_simd][edge]") {
    // Should not crash or access invalid memory
    computePolarBulk(nullptr, 0, nullptr, nullptr);
    reconstructCartesianBulk(nullptr, nullptr, 0, nullptr);
    // If we get here without crashing, the test passes
    REQUIRE(true);
}
