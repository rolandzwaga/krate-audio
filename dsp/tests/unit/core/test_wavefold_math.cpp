// ==============================================================================
// Unit Tests: Wavefolding Math Library
// ==============================================================================
// Tests for core/wavefold_math.h - Lambert W, triangle fold, and sine fold
// mathematical functions for wavefolding algorithms.
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline (pure functions, independently testable)
// - Principle XII: Test-First Development
//
// Reference: specs/050-wavefolding-math/spec.md
// ==============================================================================

#include <krate/dsp/core/wavefold_math.h>
#include <spectral_analysis.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>
#include <chrono>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// US1: Serge-Style Sine Fold (FR-006, FR-007, FR-008)
// =============================================================================

TEST_CASE("sineFold: linear passthrough at gain=0", "[wavefold_math][US1][sineFold]") {
    // FR-007: At gain=0, return x (linear passthrough, not silence)
    REQUIRE(WavefoldMath::sineFold(0.5f, 0.0f) == Approx(0.5f).margin(0.001f));
    REQUIRE(WavefoldMath::sineFold(-0.7f, 0.0f) == Approx(-0.7f).margin(0.001f));
    REQUIRE(WavefoldMath::sineFold(0.0f, 0.0f) == Approx(0.0f).margin(0.001f));
    REQUIRE(WavefoldMath::sineFold(1.0f, 0.0f) == Approx(1.0f).margin(0.001f));
    REQUIRE(WavefoldMath::sineFold(-1.0f, 0.0f) == Approx(-1.0f).margin(0.001f));
}

TEST_CASE("sineFold: basic folding with sin(gain*x)", "[wavefold_math][US1][sineFold]") {
    // FR-006: Formula is sin(gain * x)
    constexpr float pi = 3.14159265358979f;

    SECTION("gain = pi produces expected results") {
        // sin(pi * 0.5) = sin(pi/2) = 1.0
        REQUIRE(WavefoldMath::sineFold(0.5f, pi) == Approx(std::sin(pi * 0.5f)).margin(0.001f));
        // sin(pi * 1.0) = sin(pi) = 0.0
        REQUIRE(WavefoldMath::sineFold(1.0f, pi) == Approx(std::sin(pi * 1.0f)).margin(0.001f));
        // sin(pi * 0.25) = sin(pi/4) = sqrt(2)/2 ~ 0.707
        REQUIRE(WavefoldMath::sineFold(0.25f, pi) == Approx(std::sin(pi * 0.25f)).margin(0.001f));
    }

    SECTION("gain = 1 produces gentle folding") {
        REQUIRE(WavefoldMath::sineFold(0.5f, 1.0f) == Approx(std::sin(0.5f)).margin(0.001f));
        REQUIRE(WavefoldMath::sineFold(1.0f, 1.0f) == Approx(std::sin(1.0f)).margin(0.001f));
    }

    SECTION("various gain and input combinations") {
        REQUIRE(WavefoldMath::sineFold(0.3f, 2.0f) == Approx(std::sin(0.6f)).margin(0.001f));
        REQUIRE(WavefoldMath::sineFold(-0.5f, 3.0f) == Approx(std::sin(-1.5f)).margin(0.001f));
    }
}

TEST_CASE("sineFold: negative gain treated as absolute value", "[wavefold_math][US1][sineFold]") {
    // FR-006: Negative gain is treated as absolute value
    const float gain = 2.0f;

    REQUIRE(WavefoldMath::sineFold(0.5f, -gain) == WavefoldMath::sineFold(0.5f, gain));
    REQUIRE(WavefoldMath::sineFold(-0.3f, -gain) == WavefoldMath::sineFold(-0.3f, gain));
    REQUIRE(WavefoldMath::sineFold(1.0f, -5.0f) == WavefoldMath::sineFold(1.0f, 5.0f));
}

TEST_CASE("sineFold: output bounded to [-1, 1]", "[wavefold_math][US1][sineFold]") {
    // FR-006: Output always bounded due to sine function
    for (float x = -10.0f; x <= 10.0f; x += 0.5f) {
        float result = WavefoldMath::sineFold(x, 5.0f);
        REQUIRE(result >= -1.0f);
        REQUIRE(result <= 1.0f);
    }

    // Test with aggressive gain
    for (float x = -5.0f; x <= 5.0f; x += 0.25f) {
        float result = WavefoldMath::sineFold(x, 20.0f);
        REQUIRE(result >= -1.0f);
        REQUIRE(result <= 1.0f);
    }
}

TEST_CASE("sineFold: NaN propagation", "[wavefold_math][US1][sineFold]") {
    // FR-011: NaN input must propagate
    const float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(std::isnan(WavefoldMath::sineFold(nan, 1.0f)));
    REQUIRE(std::isnan(WavefoldMath::sineFold(nan, 0.0f)));
    REQUIRE(std::isnan(WavefoldMath::sineFold(nan, 5.0f)));
}

TEST_CASE("sineFold: continuous behavior (SC-005)", "[wavefold_math][US1][sineFold]") {
    // SC-005: Continuous folding without discontinuities as gain sweeps 0 to 10
    const float x = 0.5f;

    // Check continuity by verifying small gain changes produce small output changes
    float prevResult = WavefoldMath::sineFold(x, 0.0f);
    for (float gain = 0.1f; gain <= 10.0f; gain += 0.1f) {
        float result = WavefoldMath::sineFold(x, gain);
        // Adjacent outputs should not differ by more than a reasonable amount
        // (sine can change rapidly, so we allow up to 0.5 for 0.1 gain step)
        float diff = std::abs(result - prevResult);
        REQUIRE(diff < 0.5f);
        prevResult = result;
    }
}

// =============================================================================
// US2: Triangle Fold (FR-003, FR-004, FR-005)
// =============================================================================

TEST_CASE("triangleFold: no folding within threshold", "[wavefold_math][US2][triangleFold]") {
    // FR-003: Values within threshold pass through unchanged
    REQUIRE(WavefoldMath::triangleFold(0.5f, 1.0f) == Approx(0.5f).margin(0.001f));
    REQUIRE(WavefoldMath::triangleFold(1.0f, 1.0f) == Approx(1.0f).margin(0.001f));
    REQUIRE(WavefoldMath::triangleFold(-0.5f, 1.0f) == Approx(-0.5f).margin(0.001f));
    REQUIRE(WavefoldMath::triangleFold(-1.0f, 1.0f) == Approx(-1.0f).margin(0.001f));
    REQUIRE(WavefoldMath::triangleFold(0.0f, 1.0f) == Approx(0.0f).margin(0.001f));
}

TEST_CASE("triangleFold: single fold at 1.5x threshold", "[wavefold_math][US2][triangleFold]") {
    // FR-003: Peaks above threshold are reflected back
    REQUIRE(WavefoldMath::triangleFold(1.5f, 1.0f) == Approx(0.5f).margin(0.001f));
    REQUIRE(WavefoldMath::triangleFold(2.0f, 1.0f) == Approx(0.0f).margin(0.001f));
    REQUIRE(WavefoldMath::triangleFold(3.0f, 1.0f) == Approx(-1.0f).margin(0.001f));
}

TEST_CASE("triangleFold: multi-fold for large inputs (FR-005)", "[wavefold_math][US2][triangleFold]") {
    // FR-005: Repeated folding should not diverge
    const float threshold = 1.0f;

    // Test large inputs - output always in [-threshold, threshold]
    REQUIRE(WavefoldMath::triangleFold(5.0f, threshold) >= -threshold);
    REQUIRE(WavefoldMath::triangleFold(5.0f, threshold) <= threshold);

    REQUIRE(WavefoldMath::triangleFold(10.0f, threshold) >= -threshold);
    REQUIRE(WavefoldMath::triangleFold(10.0f, threshold) <= threshold);

    REQUIRE(WavefoldMath::triangleFold(100.0f, threshold) >= -threshold);
    REQUIRE(WavefoldMath::triangleFold(100.0f, threshold) <= threshold);

    // Verify predictable pattern (period = 4*threshold)
    // x=0 -> 0, x=1 -> 1, x=2 -> 0, x=3 -> -1, x=4 -> 0, x=5 -> 1, ...
    REQUIRE(WavefoldMath::triangleFold(4.0f, 1.0f) == Approx(0.0f).margin(0.001f));
    REQUIRE(WavefoldMath::triangleFold(5.0f, 1.0f) == Approx(1.0f).margin(0.001f));
}

TEST_CASE("triangleFold: symmetry triangleFold(-x) == -triangleFold(x)", "[wavefold_math][US2][triangleFold]") {
    // FR-004: Odd symmetry
    REQUIRE(WavefoldMath::triangleFold(1.5f, 1.0f) == Approx(-WavefoldMath::triangleFold(-1.5f, 1.0f)).margin(0.001f));
    REQUIRE(WavefoldMath::triangleFold(0.5f, 1.0f) == Approx(-WavefoldMath::triangleFold(-0.5f, 1.0f)).margin(0.001f));
    REQUIRE(WavefoldMath::triangleFold(3.7f, 1.0f) == Approx(-WavefoldMath::triangleFold(-3.7f, 1.0f)).margin(0.001f));

    // Test with different thresholds
    REQUIRE(WavefoldMath::triangleFold(2.5f, 0.5f) == Approx(-WavefoldMath::triangleFold(-2.5f, 0.5f)).margin(0.001f));
}

TEST_CASE("triangleFold: output always bounded to [-threshold, threshold]", "[wavefold_math][US2][triangleFold]") {
    // SC-004: Output always within bounds
    const float threshold = 1.0f;
    for (float x = -10.0f; x <= 10.0f; x += 0.5f) {
        float result = WavefoldMath::triangleFold(x, threshold);
        REQUIRE(result >= -threshold);
        REQUIRE(result <= threshold);
    }

    // Test with custom threshold
    const float threshold2 = 0.7f;
    for (float x = -10.0f; x <= 10.0f; x += 0.5f) {
        float result = WavefoldMath::triangleFold(x, threshold2);
        REQUIRE(result >= -threshold2);
        REQUIRE(result <= threshold2);
    }
}

TEST_CASE("triangleFold: threshold clamped to minimum 0.01f", "[wavefold_math][US2][triangleFold]") {
    // FR-003: Threshold clamped to kMinThreshold = 0.01f
    // Should not crash or produce NaN with zero or negative threshold
    REQUIRE_FALSE(std::isnan(WavefoldMath::triangleFold(1.0f, 0.0f)));
    REQUIRE_FALSE(std::isnan(WavefoldMath::triangleFold(1.0f, -1.0f)));
    REQUIRE_FALSE(std::isnan(WavefoldMath::triangleFold(0.5f, 0.001f)));

    // Output should still be bounded (using minimum threshold of 0.01f)
    float result = WavefoldMath::triangleFold(0.5f, 0.0f);
    REQUIRE(std::abs(result) <= 0.01f);
}

TEST_CASE("triangleFold: NaN propagation", "[wavefold_math][US2][triangleFold]") {
    // FR-011: NaN input must propagate
    const float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(std::isnan(WavefoldMath::triangleFold(nan, 1.0f)));
}

// =============================================================================
// US3: Lambert W Function (FR-001)
// =============================================================================

TEST_CASE("lambertW: basic values W(0)=0, W(e)=1", "[wavefold_math][US3][lambertW]") {
    // FR-001: Basic mathematical properties
    REQUIRE(WavefoldMath::lambertW(0.0f) == Approx(0.0f).margin(0.001f));
    // W(e) = 1
    const float e = std::exp(1.0f);
    REQUIRE(WavefoldMath::lambertW(e) == Approx(1.0f).margin(0.001f));
}

TEST_CASE("lambertW: known values W(0.1)~0.0913, W(1.0)~0.567", "[wavefold_math][US3][lambertW]") {
    // SC-002: Accuracy within 0.001 tolerance
    // Reference values from Wolfram Alpha: LambertW[x]
    // W(0.1) = 0.0912765...
    // W(0.5) = 0.3517337...
    // W(1.0) = 0.5671433...
    // W(2.0) = 0.8526055...
    REQUIRE(WavefoldMath::lambertW(0.1f) == Approx(0.09128f).margin(0.001f));
    REQUIRE(WavefoldMath::lambertW(1.0f) == Approx(0.56714f).margin(0.001f));
    REQUIRE(WavefoldMath::lambertW(0.5f) == Approx(0.35173f).margin(0.001f));
    REQUIRE(WavefoldMath::lambertW(2.0f) == Approx(0.85261f).margin(0.001f));
}

TEST_CASE("lambertW: domain boundary W(-1/e)=-1, NaN below", "[wavefold_math][US3][lambertW]") {
    // FR-001: Valid range x >= -1/e
    const float negOneOverE = -1.0f / std::exp(1.0f);  // -0.3679

    // At boundary: W(-1/e) = -1
    // Note: The Puiseux series is only an approximation near the branch point,
    // so we allow 0.02 margin for numerical precision near the singularity
    REQUIRE(WavefoldMath::lambertW(negOneOverE) == Approx(-1.0f).margin(0.02f));

    // Below domain: return NaN
    REQUIRE(std::isnan(WavefoldMath::lambertW(-0.5f)));
    REQUIRE(std::isnan(WavefoldMath::lambertW(-1.0f)));
    REQUIRE(std::isnan(WavefoldMath::lambertW(-10.0f)));
}

TEST_CASE("lambertW: special values NaN->NaN, Inf->Inf", "[wavefold_math][US3][lambertW]") {
    // FR-011: Special value handling
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float posInf = std::numeric_limits<float>::infinity();
    const float negInf = -std::numeric_limits<float>::infinity();

    REQUIRE(std::isnan(WavefoldMath::lambertW(nan)));
    REQUIRE(WavefoldMath::lambertW(posInf) == posInf);
    REQUIRE(std::isnan(WavefoldMath::lambertW(negInf)));  // Below domain
}

TEST_CASE("lambertW: large inputs x>100", "[wavefold_math][US3][lambertW]") {
    // Edge case: Large inputs should not overflow
    float w100 = WavefoldMath::lambertW(100.0f);
    float w1000 = WavefoldMath::lambertW(1000.0f);

    REQUIRE(std::isfinite(w100));
    REQUIRE(std::isfinite(w1000));

    // W(x) grows slowly (approximately log(x) - log(log(x)) for large x)
    // W(100) ~ 3.39
    REQUIRE(w100 == Approx(3.39f).margin(0.02f));
    // W(1000) ~ 5.25
    REQUIRE(w1000 == Approx(5.25f).margin(0.02f));
}

TEST_CASE("lambertW: accuracy within 0.001 tolerance (SC-002)", "[wavefold_math][US3][lambertW]") {
    // SC-002: Verify accuracy across valid domain
    // Reference values from Wolfram Alpha / mathematical tables
    struct TestCase {
        float x;
        float expected;
    };

    // Reference values from Wolfram Alpha: LambertW[x]
    const TestCase testCases[] = {
        {0.0f, 0.0f},
        {0.1f, 0.09128f},      // W(0.1) = 0.0912765...
        {0.2f, 0.16891f},      // W(0.2) = 0.1689159...
        {0.5f, 0.35173f},      // W(0.5) = 0.3517337...
        {1.0f, 0.56714f},      // W(1.0) = 0.5671433...
        {2.0f, 0.85261f},      // W(2.0) = 0.8526055...
        {5.0f, 1.32672f},      // W(5.0) = 1.3267246...
        {10.0f, 1.74553f},     // W(10) = 1.7455280...
    };

    for (const auto& tc : testCases) {
        float actual = WavefoldMath::lambertW(tc.x);
        REQUIRE(actual == Approx(tc.expected).margin(0.001f));
    }
}

// =============================================================================
// US4: Fast Lambert W Approximation (FR-002)
// =============================================================================

TEST_CASE("lambertWApprox: accuracy vs exact within 0.01 relative error", "[wavefold_math][US4][lambertWApprox]") {
    // SC-003: < 0.01 relative error for x in [-0.36, 1.0]
    for (float x = -0.36f; x <= 1.0f; x += 0.05f) {
        float exact = WavefoldMath::lambertW(x);
        float approx = WavefoldMath::lambertWApprox(x);

        // Handle values near zero specially
        if (std::abs(exact) < 0.01f) {
            // Use absolute error for small values
            REQUIRE(std::abs(approx - exact) < 0.01f);
        } else {
            float relError = std::abs((approx - exact) / exact);
            REQUIRE(relError < 0.01f);
        }
    }
}

TEST_CASE("lambertWApprox: domain boundary returns NaN below -1/e", "[wavefold_math][US4][lambertWApprox]") {
    // FR-002: Same domain handling as lambertW
    REQUIRE(std::isnan(WavefoldMath::lambertWApprox(-0.5f)));
    REQUIRE(std::isnan(WavefoldMath::lambertWApprox(-1.0f)));

    // At boundary should be valid
    const float negOneOverE = -1.0f / std::exp(1.0f);
    REQUIRE_FALSE(std::isnan(WavefoldMath::lambertWApprox(negOneOverE)));
}

TEST_CASE("lambertWApprox: special value handling", "[wavefold_math][US4][lambertWApprox]") {
    // FR-011: Same special value handling as lambertW
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float posInf = std::numeric_limits<float>::infinity();

    REQUIRE(std::isnan(WavefoldMath::lambertWApprox(nan)));
    REQUIRE(WavefoldMath::lambertWApprox(0.0f) == 0.0f);
    REQUIRE(WavefoldMath::lambertWApprox(posInf) == posInf);
}

TEST_CASE("lambertWApprox: speedup at least 3x vs lambertW (SC-003)", "[wavefold_math][US4][lambertWApprox][benchmark][!mayfail]") {
    // SC-003: At least 3x faster than lambertW
    // Note: May fail in Debug builds due to optimizer being disabled

    constexpr int N = 100000;
    volatile float sink = 0.0f;  // Prevent optimization

    // Benchmark exact lambertW
    auto startExact = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        float x = static_cast<float>(i % 100) * 0.01f;
        sink = WavefoldMath::lambertW(x);
    }
    auto endExact = std::chrono::high_resolution_clock::now();
    auto exactTime = std::chrono::duration_cast<std::chrono::microseconds>(endExact - startExact).count();

    // Benchmark approximation
    auto startApprox = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        float x = static_cast<float>(i % 100) * 0.01f;
        sink = WavefoldMath::lambertWApprox(x);
    }
    auto endApprox = std::chrono::high_resolution_clock::now();
    auto approxTime = std::chrono::duration_cast<std::chrono::microseconds>(endApprox - startApprox).count();

    (void)sink;

    // Calculate speedup
    float speedup = static_cast<float>(exactTime) / static_cast<float>(approxTime);
    INFO("lambertWApprox speedup: " << speedup << "x");

#ifdef NDEBUG
    REQUIRE(speedup >= 3.0f);
#else
    // In Debug, just verify approx is faster
    REQUIRE(speedup >= 1.0f);
#endif
}

// =============================================================================
// Phase 7: Stress Tests and Cross-Cutting Concerns
// =============================================================================

TEST_CASE("all functions: 1M sample stress test zero NaN outputs (SC-006)", "[wavefold_math][stress]") {
    // SC-006: Processing 1M samples produces zero NaN for valid inputs
    constexpr int N = 1000000;
    int nanCount = 0;

    for (int i = 0; i < N; ++i) {
        float x = -10.0f + 20.0f * (static_cast<float>(i) / static_cast<float>(N));

        // lambertW only valid for x >= -1/e ~ -0.368
        if (x >= -0.36f) {
            if (std::isnan(WavefoldMath::lambertW(x))) nanCount++;
            if (std::isnan(WavefoldMath::lambertWApprox(x))) nanCount++;
        }

        // triangleFold and sineFold valid for all x
        if (std::isnan(WavefoldMath::triangleFold(x, 1.0f))) nanCount++;
        if (std::isnan(WavefoldMath::sineFold(x, 3.14159f))) nanCount++;
    }

    REQUIRE(nanCount == 0);
}

TEST_CASE("all functions: bounded outputs for inputs in [-10, 10] (SC-001)", "[wavefold_math][stress]") {
    // SC-001: All functions produce bounded outputs for reasonable inputs
    for (float x = -10.0f; x <= 10.0f; x += 0.1f) {
        // sineFold always bounded to [-1, 1]
        float sfResult = WavefoldMath::sineFold(x, 5.0f);
        REQUIRE(sfResult >= -1.0f);
        REQUIRE(sfResult <= 1.0f);
        REQUIRE(std::isfinite(sfResult));

        // triangleFold always bounded to [-threshold, threshold]
        float tfResult = WavefoldMath::triangleFold(x, 1.0f);
        REQUIRE(tfResult >= -1.0f);
        REQUIRE(tfResult <= 1.0f);
        REQUIRE(std::isfinite(tfResult));

        // lambertW bounded for valid domain
        if (x >= -0.36f) {
            float lwResult = WavefoldMath::lambertW(x);
            REQUIRE(std::isfinite(lwResult));
        }
    }
}

// =============================================================================
// Function Attributes (FR-009, FR-010)
// =============================================================================

TEST_CASE("all functions are noexcept (FR-010)", "[wavefold_math][attributes]") {
    // FR-010: All functions MUST be noexcept
    static_assert(noexcept(WavefoldMath::lambertW(0.0f)), "lambertW must be noexcept");
    static_assert(noexcept(WavefoldMath::lambertWApprox(0.0f)), "lambertWApprox must be noexcept");
    static_assert(noexcept(WavefoldMath::triangleFold(0.0f, 1.0f)), "triangleFold must be noexcept");
    static_assert(noexcept(WavefoldMath::sineFold(0.0f, 1.0f)), "sineFold must be noexcept");

    REQUIRE(true);  // Test passes if compilation succeeds
}

// =============================================================================
// Phase 8: Spectral Analysis Tests (Aliasing Measurement)
// =============================================================================
// These tests use FFT-based spectral analysis to quantitatively measure
// aliasing characteristics of the wavefolding functions.

TEST_CASE("sineFold: spectral analysis shows harmonic generation", "[wavefold_math][sineFold][aliasing]") {
    using namespace Krate::DSP::TestUtils;

    // Use 5kHz fundamental to ensure harmonics alias (5kHz * 5 = 25kHz > 22.05kHz Nyquist)
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 1.0f,
        .fftSize = 4096,
        .maxHarmonic = 10
    };

    SECTION("linear passthrough at gain=0 produces minimal aliasing") {
        auto result = measureAliasing(config, [](float x) {
            return WavefoldMath::sineFold(x, 0.0f);  // Linear passthrough
        });

        INFO("Fundamental: " << result.fundamentalPowerDb << " dB");
        INFO("Harmonics: " << result.harmonicPowerDb << " dB");
        INFO("Aliasing: " << result.aliasingPowerDb << " dB");

        // At gain=0, sineFold returns x unchanged, so no actual harmonic generation
        // Measured aliasing is FFT/windowing noise floor (~-50dB)
        // This should be much lower than active folding scenarios
        REQUIRE(result.aliasingPowerDb < -40.0f);  // Well below folding scenarios
    }

    SECTION("gentle gain generates harmonics with measurable aliasing") {
        constexpr float pi = 3.14159265358979f;

        // Use higher drive to ensure clipping/folding occurs
        AliasingTestConfig driveConfig = config;
        driveConfig.driveGain = 3.0f;

        auto result = measureAliasing(driveConfig, [pi](float x) {
            return WavefoldMath::sineFold(x, pi);  // Classic Serge gain
        });

        INFO("Fundamental: " << result.fundamentalPowerDb << " dB");
        INFO("Harmonics: " << result.harmonicPowerDb << " dB");
        INFO("Aliasing: " << result.aliasingPowerDb << " dB");

        // With gain=pi on a driven signal, harmonics are generated
        // Some will alias, output should be valid
        REQUIRE_FALSE(std::isnan(result.aliasingPowerDb));
        REQUIRE(result.isValid());
        // Should have measurable aliasing (not at noise floor)
        REQUIRE(result.aliasingPowerDb > -100.0f);
    }

    SECTION("folding produces significantly more aliasing than linear passthrough") {
        // Use higher drive to ensure folding occurs
        AliasingTestConfig driveConfig = config;
        driveConfig.driveGain = 2.0f;

        auto linearResult = measureAliasing(config, [](float x) {
            return WavefoldMath::sineFold(x, 0.0f);  // Linear passthrough
        });

        auto foldingResult = measureAliasing(driveConfig, [](float x) {
            return WavefoldMath::sineFold(x, 5.0f);  // Active folding
        });

        INFO("Linear passthrough aliasing: " << linearResult.aliasingPowerDb << " dB");
        INFO("Active folding aliasing: " << foldingResult.aliasingPowerDb << " dB");

        // Both should be valid measurements
        REQUIRE(linearResult.isValid());
        REQUIRE(foldingResult.isValid());
        // Active folding produces dramatically more aliasing than passthrough
        // (Note: sineFold aliasing isn't monotonic with gain due to sin() wrapping)
        REQUIRE(foldingResult.aliasingPowerDb > linearResult.aliasingPowerDb + 50.0f);
    }
}

TEST_CASE("triangleFold: spectral analysis shows harmonic generation", "[wavefold_math][triangleFold][aliasing]") {
    using namespace Krate::DSP::TestUtils;

    // Use 5kHz fundamental to ensure harmonics alias (5kHz * 5 = 25kHz > 22.05kHz Nyquist)
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 1.0f,
        .fftSize = 4096,
        .maxHarmonic = 10
    };

    SECTION("no folding when within threshold produces minimal aliasing") {
        // Input amplitude 1.0, threshold 2.0 -> no folding occurs
        auto result = measureAliasing(config, [](float x) {
            return WavefoldMath::triangleFold(x, 2.0f);  // Threshold > amplitude
        });

        INFO("Fundamental: " << result.fundamentalPowerDb << " dB");
        INFO("Harmonics: " << result.harmonicPowerDb << " dB");
        INFO("Aliasing: " << result.aliasingPowerDb << " dB");

        // No folding means output = input (linear), no actual harmonic generation
        // Measured aliasing is FFT numeric noise floor (~-50dB)
        REQUIRE(result.aliasingPowerDb < -40.0f);
    }

    SECTION("folding with drive > threshold generates harmonics") {
        // Increase drive to cause folding
        AliasingTestConfig driveConfig = config;
        driveConfig.driveGain = 3.0f;  // 3x amplitude with threshold 1.0

        auto result = measureAliasing(driveConfig, [](float x) {
            return WavefoldMath::triangleFold(x, 1.0f);
        });

        INFO("Fundamental: " << result.fundamentalPowerDb << " dB");
        INFO("Harmonics: " << result.harmonicPowerDb << " dB");
        INFO("Aliasing: " << result.aliasingPowerDb << " dB");

        // With folding, harmonics are generated
        REQUIRE(result.isValid());
        // Aliasing should be measurable (not at noise floor)
        REQUIRE(result.aliasingPowerDb > -100.0f);
    }

    SECTION("more drive produces more aliasing") {
        AliasingTestConfig config2x = config;
        config2x.driveGain = 2.0f;

        AliasingTestConfig config5x = config;
        config5x.driveGain = 5.0f;

        auto result2x = measureAliasing(config2x, [](float x) {
            return WavefoldMath::triangleFold(x, 1.0f);
        });

        auto result5x = measureAliasing(config5x, [](float x) {
            return WavefoldMath::triangleFold(x, 1.0f);
        });

        INFO("Drive 2x aliasing: " << result2x.aliasingPowerDb << " dB");
        INFO("Drive 5x aliasing: " << result5x.aliasingPowerDb << " dB");

        // Both should produce measurable aliasing
        REQUIRE(result2x.aliasingPowerDb > -100.0f);
        REQUIRE(result5x.aliasingPowerDb > -100.0f);
        // More drive = more folds = more aliasing
        REQUIRE(result5x.aliasingPowerDb > result2x.aliasingPowerDb);
    }
}

TEST_CASE("wavefold comparison: sineFold vs triangleFold aliasing characteristics",
          "[wavefold_math][aliasing][comparison]") {
    using namespace Krate::DSP::TestUtils;

    // Use same test conditions for both
    AliasingTestConfig config{
        .testFrequencyHz = 2000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 3.0f,  // Drive to cause folding
        .fftSize = 4096,
        .maxHarmonic = 15
    };

    constexpr float pi = 3.14159265358979f;

    // Measure sineFold with typical Serge gain
    auto sineResult = measureAliasing(config, [pi](float x) {
        return WavefoldMath::sineFold(x, pi);
    });

    // Measure triangleFold with threshold 1.0
    auto triangleResult = measureAliasing(config, [](float x) {
        return WavefoldMath::triangleFold(x, 1.0f);
    });

    INFO("sineFold (gain=pi) aliasing: " << sineResult.aliasingPowerDb << " dB");
    INFO("triangleFold (threshold=1) aliasing: " << triangleResult.aliasingPowerDb << " dB");

    // Both should produce valid measurements
    REQUIRE(sineResult.isValid());
    REQUIRE(triangleResult.isValid());

    // Document the characteristic: triangleFold typically produces more aliasing
    // than sineFold at equivalent settings because it has sharp corners (discontinuous
    // first derivative) while sineFold uses smooth sine function
    // Note: This is a characterization test, not a strict requirement
    INFO("Aliasing difference: " << (triangleResult.aliasingPowerDb - sineResult.aliasingPowerDb) << " dB");
}

// =============================================================================
// Test Coverage Summary (SC-007)
// =============================================================================
// All 4 public functions have tests:
// - lambertW: 6 test cases
// - lambertWApprox: 4 test cases
// - triangleFold: 7 test cases + 3 spectral analysis tests
// - sineFold: 6 test cases + 3 spectral analysis tests
// - Cross-cutting: 3 test cases (stress, bounds, attributes)
// - Spectral comparison: 1 test case
// Total: 29 test cases covering all functional requirements and success criteria
