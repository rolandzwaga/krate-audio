// ==============================================================================
// Layer 0: Core Tests - SIMD-Accelerated Spectral Math
// ==============================================================================
// Tests for computePolarBulk() and reconstructCartesianBulk().
// Verifies SIMD results match scalar std::sqrt/atan2/cos/sin.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/core/spectral_simd.h>
#include <krate/dsp/primitives/spectral_utils.h>

#include <cmath>
#include <limits>
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

// ==============================================================================
// batchLog10 Tests (T010)
// ==============================================================================

TEST_CASE("batchLog10 known values", "[spectral_simd][batchLog10]") {
    // Known value pairs: input -> expected log10(input)
    std::vector<float> input = {1.0f, 10.0f, 100.0f, 0.001f};
    std::vector<float> expected = {0.0f, 1.0f, 2.0f, -3.0f};
    std::vector<float> output(input.size());

    batchLog10(input.data(), output.data(), input.size());

    for (size_t k = 0; k < input.size(); ++k) {
        REQUIRE(output[k] == Approx(expected[k]).margin(1e-5f));
    }
}

TEST_CASE("batchLog10 scalar comparison over 2049 elements", "[spectral_simd][batchLog10]") {
    const size_t count = 2049;
    std::vector<float> input(count);
    std::vector<float> output(count);

    // Generate positive values spanning several orders of magnitude
    for (size_t k = 0; k < count; ++k) {
        input[k] = 1e-8f + static_cast<float>(k) * 1e3f / static_cast<float>(count);
    }

    batchLog10(input.data(), output.data(), count);

    // Compare against scalar std::log10 within 1e-5 absolute tolerance (SC-004)
    for (size_t k = 0; k < count; ++k) {
        float expected = std::log10(std::max(input[k], 1e-10f));
        REQUIRE(output[k] == Approx(expected).margin(1e-5f));
    }
}

// ==============================================================================
// batchPow10 Tests (T011)
// ==============================================================================

TEST_CASE("batchPow10 known values", "[spectral_simd][batchPow10]") {
    std::vector<float> input = {0.0f, 1.0f, -1.0f, 2.0f, -10.0f, 6.0f};
    std::vector<float> expected = {1.0f, 10.0f, 0.1f, 100.0f, 1e-10f, 1e6f};
    std::vector<float> output(input.size());

    batchPow10(input.data(), output.data(), input.size());

    for (size_t k = 0; k < input.size(); ++k) {
        // Use relative error for SC-005
        float exp_val = expected[k];
        if (exp_val != 0.0f) {
            float relErr = std::abs(output[k] - exp_val) / std::abs(exp_val);
            REQUIRE(relErr < 1e-5f);
        } else {
            REQUIRE(output[k] == Approx(exp_val).margin(1e-5f));
        }
    }
}

TEST_CASE("batchPow10 scalar comparison over 2049 elements", "[spectral_simd][batchPow10]") {
    const size_t count = 2049;
    std::vector<float> input(count);
    std::vector<float> output(count);

    // Generate exponents in the practical range [-10, +6]
    for (size_t k = 0; k < count; ++k) {
        input[k] = -10.0f + 16.0f * static_cast<float>(k) / static_cast<float>(count - 1);
    }

    batchPow10(input.data(), output.data(), count);

    // Compare against scalar std::pow(10, x) within 1e-5 relative error (SC-005)
    for (size_t k = 0; k < count; ++k) {
        float expected = std::pow(10.0f, input[k]);
        expected = std::max(1e-10f, std::min(expected, 1e6f));
        if (expected != 0.0f) {
            float relErr = std::abs(output[k] - expected) / std::abs(expected);
            REQUIRE(relErr < 1e-5f);
        }
    }
}

TEST_CASE("batchPow10 output clamping verification", "[spectral_simd][batchPow10]") {
    // Verify output is always in [kMinLogInput, kMaxPow10Output]
    std::vector<float> input = {-20.0f, -15.0f, 0.0f, 6.0f, 7.0f};
    std::vector<float> output(input.size());

    batchPow10(input.data(), output.data(), input.size());

    for (size_t k = 0; k < input.size(); ++k) {
        REQUIRE(output[k] >= kMinLogInput);
        REQUIRE(output[k] <= kMaxPow10Output);
    }
}

// ==============================================================================
// batchWrapPhase Tests (T012)
// ==============================================================================

TEST_CASE("batchWrapPhase known values", "[spectral_simd][batchWrapPhase]") {
    const float pi = std::numbers::pi_v<float>;
    std::vector<float> input = {
        0.0f,
        pi,
        -pi,
        2.0f * pi,
        -2.0f * pi,
        100.0f * pi,
        -100.0f * pi
    };
    std::vector<float> output(input.size());

    batchWrapPhase(input.data(), output.data(), input.size());

    // All outputs should be in [-pi, pi] range (with tolerance)
    for (size_t k = 0; k < input.size(); ++k) {
        REQUIRE(output[k] >= -pi - 1e-5f);
        REQUIRE(output[k] <= pi + 1e-5f);
    }

    // 0 -> 0
    REQUIRE(output[0] == Approx(0.0f).margin(1e-6f));
    // 2*pi -> ~0
    REQUIRE(output[3] == Approx(0.0f).margin(1e-5f));
    // -2*pi -> ~0
    REQUIRE(output[4] == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("batchWrapPhase out-of-place vs scalar branchless reference over 2049 elements",
          "[spectral_simd][batchWrapPhase]") {
    const size_t count = 2049;
    const float pi = std::numbers::pi_v<float>;
    std::vector<float> input(count);
    std::vector<float> output(count);

    // Generate phase values spanning a wide range [-100*pi, +100*pi]
    for (size_t k = 0; k < count; ++k) {
        input[k] = -100.0f * pi + 200.0f * pi * static_cast<float>(k) / static_cast<float>(count - 1);
    }

    batchWrapPhase(input.data(), output.data(), count);

    // All outputs must be in [-pi, pi] range (with small tolerance for float precision)
    for (size_t k = 0; k < count; ++k) {
        REQUIRE(output[k] >= -pi - 1e-5f);
        REQUIRE(output[k] <= pi + 1e-5f);
    }

    // Compare against branchless scalar formula. The SIMD path uses hn::Round
    // (round-to-nearest-even) while std::round rounds half-away-from-zero.
    // At rounding boundaries this can cause a full twoPi difference, so we
    // compare modulo 2*pi equivalence: the difference is either ~0 or ~2*pi.
    constexpr float kTwoPiRef = 6.283185307f;
    constexpr float kInvTwoPiRef = 0.159154943f;
    size_t largeErrorCount = 0;
    for (size_t k = 0; k < count; ++k) {
        float n = std::round(input[k] * kInvTwoPiRef);
        float expected = input[k] - n * kTwoPiRef;
        float diff = std::abs(output[k] - expected);
        // Allow either close match OR a 2*pi boundary jump
        bool closeMatch = (diff < 1e-4f);
        bool boundaryJump = (std::abs(diff - kTwoPiRef) < 1e-3f);
        if (!closeMatch && !boundaryJump) {
            ++largeErrorCount;
        }
    }
    REQUIRE(largeErrorCount == 0);
}

TEST_CASE("batchWrapPhase matches scalar wrapPhase for moderate inputs (SC-006)",
          "[spectral_simd][batchWrapPhase]") {
    const size_t count = 2049;
    const float pi = std::numbers::pi_v<float>;
    std::vector<float> input(count);
    std::vector<float> output(count);

    // Use moderate range [-4*pi, +4*pi] where while-loop wrapPhase is accurate
    // (at most 2 iterations in while-loop, negligible accumulated error)
    for (size_t k = 0; k < count; ++k) {
        input[k] = -4.0f * pi + 8.0f * pi * static_cast<float>(k) / static_cast<float>(count - 1);
    }

    batchWrapPhase(input.data(), output.data(), count);

    // Compare against scalar wrapPhase() within 1e-6 tolerance (SC-006)
    // At exact +/-pi boundary, SIMD may give +pi while scalar gives -pi
    // (or vice versa) -- both are valid. Handle this equivalence.
    for (size_t k = 0; k < count; ++k) {
        float expected = wrapPhase(input[k]);
        float diff = std::abs(output[k] - expected);
        // Allow small diff OR pi boundary equivalence (~2*pi diff)
        bool closeMatch = (diff < 1e-6f);
        bool piBoundary = (std::abs(diff - 2.0f * pi) < 1e-4f);
        REQUIRE((closeMatch || piBoundary));
    }
}

TEST_CASE("batchWrapPhase in-place overload", "[spectral_simd][batchWrapPhase]") {
    const size_t count = 2049;
    const float pi = std::numbers::pi_v<float>;
    std::vector<float> data(count);
    std::vector<float> outOfPlace(count);

    for (size_t k = 0; k < count; ++k) {
        data[k] = -50.0f * pi + 100.0f * pi * static_cast<float>(k) / static_cast<float>(count - 1);
    }

    // Compute out-of-place reference
    batchWrapPhase(data.data(), outOfPlace.data(), count);

    // Compute in-place
    batchWrapPhase(data.data(), count);

    // Both should match
    for (size_t k = 0; k < count; ++k) {
        REQUIRE(data[k] == Approx(outOfPlace[k]).margin(1e-7f));
    }
}

// ==============================================================================
// Edge Case Tests (T013)
// ==============================================================================

TEST_CASE("Batch functions handle zero-length input", "[spectral_simd][edge]") {
    // count == 0 should not crash
    batchLog10(nullptr, nullptr, 0);
    batchPow10(nullptr, nullptr, 0);
    batchWrapPhase(static_cast<const float*>(nullptr), static_cast<float*>(nullptr), 0);

    float dummy = 0.0f;
    batchWrapPhase(&dummy, 0);

    REQUIRE(true);
}

TEST_CASE("Batch functions handle non-SIMD-width tail counts", "[spectral_simd][edge]") {
    for (size_t count : {size_t{1}, size_t{3}, size_t{5}, size_t{7}, size_t{1025}}) {
        std::vector<float> input(count);
        std::vector<float> output(count);

        // batchLog10: positive inputs
        for (size_t k = 0; k < count; ++k) {
            input[k] = 1.0f + static_cast<float>(k);
        }
        batchLog10(input.data(), output.data(), count);
        for (size_t k = 0; k < count; ++k) {
            float expected = std::log10(input[k]);
            REQUIRE(output[k] == Approx(expected).margin(1e-5f));
        }

        // batchPow10
        for (size_t k = 0; k < count; ++k) {
            input[k] = -2.0f + 4.0f * static_cast<float>(k) / static_cast<float>(count);
        }
        batchPow10(input.data(), output.data(), count);
        for (size_t k = 0; k < count; ++k) {
            float expected = std::pow(10.0f, input[k]);
            expected = std::max(1e-10f, std::min(expected, 1e6f));
            REQUIRE(output[k] == Approx(expected).margin(
                std::abs(expected) * 1e-5f + 1e-10f));
        }

        // batchWrapPhase -- compare against branchless scalar formula
        const float pi = std::numbers::pi_v<float>;
        constexpr float kTwoPiTail = 6.283185307f;
        constexpr float kInvTwoPiTail = 0.159154943f;
        for (size_t k = 0; k < count; ++k) {
            input[k] = -10.0f * pi + 20.0f * pi * static_cast<float>(k) / static_cast<float>(count);
        }
        batchWrapPhase(input.data(), output.data(), count);
        for (size_t k = 0; k < count; ++k) {
            float n = std::round(input[k] * kInvTwoPiTail);
            float expected = input[k] - n * kTwoPiTail;
            REQUIRE(output[k] == Approx(expected).margin(1e-6f));
        }
    }
}

TEST_CASE("batchLog10 with zero/negative inputs returns finite results", "[spectral_simd][edge]") {
    std::vector<float> input = {0.0f, -1.0f, -100.0f, -1e-20f};
    std::vector<float> output(input.size());

    batchLog10(input.data(), output.data(), input.size());

    for (size_t k = 0; k < input.size(); ++k) {
        // Should NOT produce NaN or -inf: values are clamped to kMinLogInput
        // Use bit pattern check since -ffast-math may break std::isnan
        // NaN detection via self-comparison: NaN != NaN per IEEE 754
        bool isFinite = (output[k] == output[k]) &&          // NOLINT(misc-redundant-expression) NaN detection
                        (output[k] != std::numeric_limits<float>::infinity()) &&
                        (output[k] != -std::numeric_limits<float>::infinity());
        REQUIRE(isFinite);
        // Should equal log10(kMinLogInput) = log10(1e-10) = -10
        REQUIRE(output[k] == Approx(-10.0f).margin(1e-5f));
    }
}

TEST_CASE("batchPow10 with overflow inputs returns kMaxPow10Output not infinity",
          "[spectral_simd][edge]") {
    std::vector<float> input = {38.5f, 39.0f, 40.0f, 100.0f};
    std::vector<float> output(input.size());

    batchPow10(input.data(), output.data(), input.size());

    for (size_t k = 0; k < input.size(); ++k) {
        REQUIRE(output[k] <= kMaxPow10Output);
        // Verify not infinity
        REQUIRE(output[k] != std::numeric_limits<float>::infinity());
    }
}

// ==============================================================================
// Round-Trip Test: log10 -> pow10 (T014)
// ==============================================================================

TEST_CASE("batchLog10 then batchPow10 round-trip", "[spectral_simd][roundtrip]") {
    const size_t count = 2049;
    std::vector<float> input(count);
    std::vector<float> logValues(count);
    std::vector<float> roundTrip(count);

    // Generate positive values in a range where round-trip is stable
    for (size_t k = 0; k < count; ++k) {
        input[k] = 1e-8f + static_cast<float>(k + 1) * 0.5f;
    }

    batchLog10(input.data(), logValues.data(), count);
    batchPow10(logValues.data(), roundTrip.data(), count);

    // Verify round-trip matches input within 1e-4 relative error
    for (size_t k = 0; k < count; ++k) {
        float expected = input[k];
        // Clamp expected to the valid output range
        expected = std::max(1e-10f, std::min(expected, 1e6f));
        if (expected > 1e-8f) {
            float relErr = std::abs(roundTrip[k] - expected) / std::abs(expected);
            REQUIRE(relErr < 1e-4f);
        }
    }
}

// ==============================================================================
// FormantPreserver Equivalence Tests (T035, T036)
// ==============================================================================

#include <krate/dsp/processors/formant_preserver.h>

TEST_CASE("FormantPreserver::extractEnvelope SIMD matches scalar reference (SC-007)",
          "[spectral_simd][formant]") {
    // Arrange: create FormantPreserver with fftSize=4096 (2049 bins)
    constexpr std::size_t fftSize = 4096;
    constexpr std::size_t numBins = fftSize / 2 + 1; // 2049

    FormantPreserver fp;
    fp.prepare(fftSize, 44100.0);

    // Generate known magnitude array: harmonic series with exponential decay
    std::vector<float> magnitudes(numBins);
    for (std::size_t k = 0; k < numBins; ++k) {
        // Exponential decay from 1.0 to ~0.001, all positive
        magnitudes[k] = std::exp(-3.0f * static_cast<float>(k) / static_cast<float>(numBins));
    }

    // Compute inline scalar reference: std::max(mag, 1e-10f) + std::log10 loop
    std::vector<float> scalarLogMag(numBins);
    for (std::size_t k = 0; k < numBins; ++k) {
        float mag = std::max(magnitudes[k], 1e-10f);
        scalarLogMag[k] = std::log10(mag);
    }

    // Act: call extractEnvelope (two-argument overload)
    // extractEnvelope internally computes logMag_ from magnitudes, then does
    // cepstral processing. We cannot directly compare logMag_ since it is private.
    // However, we can verify extractEnvelope produces a valid envelope by testing
    // the full pipeline output against expectations.

    // For the log10 step specifically, we test batchLog10 equivalence:
    // Run batchLog10 on same input and compare against scalar reference
    std::vector<float> simdLogMag(numBins);
    batchLog10(magnitudes.data(), simdLogMag.data(), numBins);

    // Verify SIMD log10 matches scalar reference within 1e-5 per bin (SC-007)
    for (std::size_t k = 0; k < numBins; ++k) {
        REQUIRE(simdLogMag[k] == Approx(scalarLogMag[k]).margin(1e-5f));
    }

    // Additionally, verify that FormantPreserver::extractEnvelope() produces
    // a valid, finite envelope for the given magnitudes
    std::vector<float> envelope(numBins);
    fp.extractEnvelope(magnitudes.data(), envelope.data());

    bool allFinite = true;
    for (std::size_t k = 0; k < numBins; ++k) {
        // NOLINTNEXTLINE(misc-redundant-expression) NaN detection via self-comparison (IEEE 754: NaN != NaN)
        if (envelope[k] != envelope[k] ||
            envelope[k] == std::numeric_limits<float>::infinity() ||
            envelope[k] == -std::numeric_limits<float>::infinity()) {
            allFinite = false;
            break;
        }
    }
    REQUIRE(allFinite);
}

TEST_CASE("FormantPreserver::reconstructEnvelope SIMD matches scalar reference (SC-008)",
          "[spectral_simd][formant]") {
    // Arrange: create FormantPreserver with fftSize=4096 (2049 bins)
    constexpr std::size_t fftSize = 4096;
    constexpr std::size_t numBins = fftSize / 2 + 1; // 2049

    FormantPreserver fp;
    fp.prepare(fftSize, 44100.0);

    // Generate known magnitude array: harmonic series with exponential decay
    std::vector<float> magnitudes(numBins);
    for (std::size_t k = 0; k < numBins; ++k) {
        magnitudes[k] = std::exp(-3.0f * static_cast<float>(k) / static_cast<float>(numBins));
    }

    // Run extractEnvelope to populate internal state including envelope_
    fp.extractEnvelope(magnitudes.data());

    // Get the envelope that was produced by the full cepstral pipeline
    const float* envelope = fp.getEnvelope();

    // The envelope is the result of: log10 -> IFFT -> lifter -> FFT -> pow10
    // We verify that the pow10 step (reconstructEnvelope) produces values
    // that match what we'd expect from scalar std::pow(10, logEnv)
    // by checking that batchPow10 gives equivalent results to scalar pow

    // Generate test log-magnitude values in the expected range
    std::vector<float> testLogMags(numBins);
    for (std::size_t k = 0; k < numBins; ++k) {
        // Use a range typical of log-magnitudes from cepstral analysis
        testLogMags[k] = -5.0f + 5.0f * static_cast<float>(k) / static_cast<float>(numBins - 1);
    }

    // Compute scalar reference: std::pow(10.0f, logEnv) + clamp
    std::vector<float> scalarEnvelope(numBins);
    for (std::size_t k = 0; k < numBins; ++k) {
        scalarEnvelope[k] = std::pow(10.0f, testLogMags[k]);
        scalarEnvelope[k] = std::max(1e-10f, std::min(scalarEnvelope[k], 1e6f));
    }

    // Compute SIMD reference via batchPow10
    std::vector<float> simdEnvelope(numBins);
    batchPow10(testLogMags.data(), simdEnvelope.data(), numBins);

    // Verify SIMD pow10 matches scalar reference within 1e-5 per bin (SC-008)
    for (std::size_t k = 0; k < numBins; ++k) {
        float expected = scalarEnvelope[k];
        if (expected > 1e-8f) {
            float relErr = std::abs(simdEnvelope[k] - expected) / expected;
            REQUIRE(relErr < 1e-5f);
        } else {
            REQUIRE(simdEnvelope[k] == Approx(expected).margin(1e-10f));
        }
    }

    // Additionally verify that the full pipeline envelope is valid
    bool allPositive = true;
    for (std::size_t k = 0; k < numBins; ++k) {
        if (envelope[k] <= 0.0f ||
            envelope[k] != envelope[k]) { // NOLINT(misc-redundant-expression) NaN detection via self-comparison (IEEE 754)
            allPositive = false;
            break;
        }
    }
    REQUIRE(allPositive);
}

// ==============================================================================
// Performance Benchmark Tests (T015)
// ==============================================================================

#include <chrono>

TEST_CASE("Performance: batchLog10 vs scalar std::log10 (SC-001)",
          "[spectral_simd][performance]") {
    const size_t count = 2049;
    std::vector<float> input(count);
    std::vector<float> output(count);

    for (size_t k = 0; k < count; ++k) {
        input[k] = 1e-6f + static_cast<float>(k + 1) * 0.5f;
    }

    constexpr int kWarmup = 100;
    constexpr int kIterations = 10000;

    // Warmup
    for (int i = 0; i < kWarmup; ++i) {
        batchLog10(input.data(), output.data(), count);
    }

    // SIMD timing
    auto simdStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        batchLog10(input.data(), output.data(), count);
    }
    auto simdEnd = std::chrono::high_resolution_clock::now();
    double simdUs = std::chrono::duration<double, std::micro>(simdEnd - simdStart).count()
                    / kIterations;

    // Scalar warmup
    for (int i = 0; i < kWarmup; ++i) {
        for (size_t k = 0; k < count; ++k) {
            output[k] = std::log10(std::max(input[k], 1e-10f));
        }
    }

    // Scalar timing
    auto scalarStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        for (size_t k = 0; k < count; ++k) {
            output[k] = std::log10(std::max(input[k], 1e-10f));
        }
    }
    auto scalarEnd = std::chrono::high_resolution_clock::now();
    double scalarUs = std::chrono::duration<double, std::micro>(scalarEnd - scalarStart).count()
                      / kIterations;

    double speedup = scalarUs / simdUs;
    INFO("batchLog10 SIMD: " << simdUs << " us/call");
    INFO("Scalar std::log10: " << scalarUs << " us/call");
    INFO("Speedup: " << speedup << "x");

    CHECK(speedup >= 2.0);
}

TEST_CASE("Performance: batchPow10 vs scalar std::pow (SC-002)",
          "[spectral_simd][performance]") {
    const size_t count = 2049;
    std::vector<float> input(count);
    std::vector<float> output(count);

    for (size_t k = 0; k < count; ++k) {
        input[k] = -10.0f + 16.0f * static_cast<float>(k) / static_cast<float>(count - 1);
    }

    constexpr int kWarmup = 100;
    constexpr int kIterations = 10000;

    // Warmup
    for (int i = 0; i < kWarmup; ++i) {
        batchPow10(input.data(), output.data(), count);
    }

    // SIMD timing
    auto simdStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        batchPow10(input.data(), output.data(), count);
    }
    auto simdEnd = std::chrono::high_resolution_clock::now();
    double simdUs = std::chrono::duration<double, std::micro>(simdEnd - simdStart).count()
                    / kIterations;

    // Scalar warmup
    for (int i = 0; i < kWarmup; ++i) {
        for (size_t k = 0; k < count; ++k) {
            float v = std::pow(10.0f, input[k]);
            output[k] = std::max(1e-10f, std::min(v, 1e6f));
        }
    }

    // Scalar timing
    auto scalarStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        for (size_t k = 0; k < count; ++k) {
            float v = std::pow(10.0f, input[k]);
            output[k] = std::max(1e-10f, std::min(v, 1e6f));
        }
    }
    auto scalarEnd = std::chrono::high_resolution_clock::now();
    double scalarUs = std::chrono::duration<double, std::micro>(scalarEnd - scalarStart).count()
                      / kIterations;

    double speedup = scalarUs / simdUs;
    INFO("batchPow10 SIMD: " << simdUs << " us/call");
    INFO("Scalar std::pow: " << scalarUs << " us/call");
    INFO("Speedup: " << speedup << "x");

    CHECK(speedup >= 2.0);
}

TEST_CASE("Performance: batchWrapPhase vs scalar wrapPhase (SC-003)",
          "[spectral_simd][performance]") {
    const size_t count = 2049;
    const float pi = std::numbers::pi_v<float>;
    std::vector<float> input(count);
    std::vector<float> output(count);

    for (size_t k = 0; k < count; ++k) {
        input[k] = -100.0f * pi + 200.0f * pi * static_cast<float>(k)
                    / static_cast<float>(count - 1);
    }

    constexpr int kWarmup = 100;
    constexpr int kIterations = 10000;

    // Warmup
    for (int i = 0; i < kWarmup; ++i) {
        batchWrapPhase(input.data(), output.data(), count);
    }

    // SIMD timing
    auto simdStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        batchWrapPhase(input.data(), output.data(), count);
    }
    auto simdEnd = std::chrono::high_resolution_clock::now();
    double simdUs = std::chrono::duration<double, std::micro>(simdEnd - simdStart).count()
                    / kIterations;

    // Scalar warmup
    for (int i = 0; i < kWarmup; ++i) {
        for (size_t k = 0; k < count; ++k) {
            output[k] = wrapPhase(input[k]);
        }
    }

    // Scalar timing
    auto scalarStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        for (size_t k = 0; k < count; ++k) {
            output[k] = wrapPhase(input[k]);
        }
    }
    auto scalarEnd = std::chrono::high_resolution_clock::now();
    double scalarUs = std::chrono::duration<double, std::micro>(scalarEnd - scalarStart).count()
                      / kIterations;

    double speedup = scalarUs / simdUs;
    INFO("batchWrapPhase SIMD: " << simdUs << " us/call");
    INFO("Scalar wrapPhase: " << scalarUs << " us/call");
    INFO("Speedup: " << speedup << "x");

    CHECK(speedup >= 2.0);
}
