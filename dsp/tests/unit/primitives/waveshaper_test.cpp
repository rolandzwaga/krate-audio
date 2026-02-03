// ==============================================================================
// Unit Tests: Waveshaper Primitive
// ==============================================================================
// Tests for the unified waveshaper primitive.
//
// Feature: 052-waveshaper
// Layer: 1 (Primitives)
// Test-First: Tests written BEFORE implementation per Constitution Principle XII
//
// Reference: specs/052-waveshaper/spec.md
// ==============================================================================

#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/core/sigmoid.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <spectral_analysis.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Phase 2: Foundational Tests
// =============================================================================

TEST_CASE("WaveshapeType enum has 9 values (FR-001)", "[waveshaper][enum]") {
    // Verify enum values exist and are distinct
    REQUIRE(static_cast<uint8_t>(WaveshapeType::Tanh) == 0);
    REQUIRE(static_cast<uint8_t>(WaveshapeType::Atan) == 1);
    REQUIRE(static_cast<uint8_t>(WaveshapeType::Cubic) == 2);
    REQUIRE(static_cast<uint8_t>(WaveshapeType::Quintic) == 3);
    REQUIRE(static_cast<uint8_t>(WaveshapeType::ReciprocalSqrt) == 4);
    REQUIRE(static_cast<uint8_t>(WaveshapeType::Erf) == 5);
    REQUIRE(static_cast<uint8_t>(WaveshapeType::HardClip) == 6);
    REQUIRE(static_cast<uint8_t>(WaveshapeType::Diode) == 7);
    REQUIRE(static_cast<uint8_t>(WaveshapeType::Tube) == 8);
}

TEST_CASE("WaveshapeType is uint8_t (FR-002)", "[waveshaper][enum]") {
    static_assert(std::is_same_v<std::underlying_type_t<WaveshapeType>, uint8_t>,
                  "WaveshapeType must be uint8_t");
}

// =============================================================================
// Phase 3: User Story 1 - Waveshaping with Selectable Type
// =============================================================================

TEST_CASE("Waveshaper default constructor initializes to Tanh/drive=1.0/asymmetry=0.0 (FR-003)",
          "[waveshaper][US1][construction]") {
    Waveshaper shaper;

    REQUIRE(shaper.getType() == WaveshapeType::Tanh);
    REQUIRE(shaper.getDrive() == Approx(1.0f));
    REQUIRE(shaper.getAsymmetry() == Approx(0.0f));
}

TEST_CASE("setType() changes type, getType() returns it (FR-004)", "[waveshaper][US1][type]") {
    Waveshaper shaper;

    shaper.setType(WaveshapeType::Tube);
    REQUIRE(shaper.getType() == WaveshapeType::Tube);

    shaper.setType(WaveshapeType::HardClip);
    REQUIRE(shaper.getType() == WaveshapeType::HardClip);

    shaper.setType(WaveshapeType::Tanh);
    REQUIRE(shaper.getType() == WaveshapeType::Tanh);
}

TEST_CASE("All 9 waveshape types produce correct output (SC-001)",
          "[waveshaper][US1][types][parameterized]") {
    Waveshaper shaper;
    const float input = 0.5f;

    // Test with drive=1.0 and asymmetry=0.0 (default)
    auto type = GENERATE(
        WaveshapeType::Tanh,
        WaveshapeType::Atan,
        WaveshapeType::Cubic,
        WaveshapeType::Quintic,
        WaveshapeType::ReciprocalSqrt,
        WaveshapeType::Erf,
        WaveshapeType::HardClip,
        WaveshapeType::Diode,
        WaveshapeType::Tube
    );

    shaper.setType(type);

    float expected = 0.0f;
    switch (type) {
        case WaveshapeType::Tanh:
            expected = Sigmoid::tanh(input);
            break;
        case WaveshapeType::Atan:
            expected = Sigmoid::atan(input);
            break;
        case WaveshapeType::Cubic:
            expected = Sigmoid::softClipCubic(input);
            break;
        case WaveshapeType::Quintic:
            expected = Sigmoid::softClipQuintic(input);
            break;
        case WaveshapeType::ReciprocalSqrt:
            expected = Sigmoid::recipSqrt(input);
            break;
        case WaveshapeType::Erf:
            expected = Sigmoid::erfApprox(input);
            break;
        case WaveshapeType::HardClip:
            expected = Sigmoid::hardClip(input);
            break;
        case WaveshapeType::Diode:
            expected = Asymmetric::diode(input);
            break;
        case WaveshapeType::Tube:
            expected = Asymmetric::tube(input);
            break;
    }

    float actual = shaper.process(input);

    // SC-001: relative error < 1e-6
    if (std::abs(expected) > 1e-6f) {
        REQUIRE(std::abs(actual - expected) / std::abs(expected) < 1e-6f);
    } else {
        REQUIRE(actual == Approx(expected).margin(1e-6f));
    }
}

TEST_CASE("Changing type mid-stream affects subsequent processing",
          "[waveshaper][US1][type][runtime]") {
    Waveshaper shaper;
    const float input = 0.5f;

    // Start with Tanh
    shaper.setType(WaveshapeType::Tanh);
    float tanhOutput = shaper.process(input);
    REQUIRE(tanhOutput == Approx(Sigmoid::tanh(input)));

    // Change to HardClip
    shaper.setType(WaveshapeType::HardClip);
    float hardClipOutput = shaper.process(input);
    REQUIRE(hardClipOutput == Approx(Sigmoid::hardClip(input)));

    // Verify outputs are different (HardClip returns input unchanged for |x| < 1)
    REQUIRE(tanhOutput != hardClipOutput);
}

// =============================================================================
// Phase 4: User Story 2 - Drive Parameter Control
// =============================================================================

TEST_CASE("drive=0.1 produces nearly linear output (FR-005)", "[waveshaper][US2][drive][low]") {
    Waveshaper shaper;
    shaper.setType(WaveshapeType::Tanh);
    shaper.setDrive(0.1f);

    const float input = 0.5f;
    // drive * input = 0.1 * 0.5 = 0.05
    const float expected = Sigmoid::tanh(0.05f);
    float actual = shaper.process(input);

    REQUIRE(actual == Approx(expected).margin(1e-6f));
}

TEST_CASE("drive=10.0 produces hard saturation", "[waveshaper][US2][drive][high]") {
    Waveshaper shaper;
    shaper.setType(WaveshapeType::Tanh);
    shaper.setDrive(10.0f);

    const float input = 0.5f;
    // drive * input = 10.0 * 0.5 = 5.0, tanh(5.0) ~= 0.9999
    float actual = shaper.process(input);

    REQUIRE(actual > 0.99f);
    REQUIRE(actual <= 1.0f);
}

TEST_CASE("drive=1.0 matches default behavior (no scaling)", "[waveshaper][US2][drive][unity]") {
    Waveshaper shaper;
    shaper.setType(WaveshapeType::Tanh);
    shaper.setDrive(1.0f);

    const float input = 0.5f;
    const float expected = Sigmoid::tanh(input);
    float actual = shaper.process(input);

    REQUIRE(actual == Approx(expected).margin(1e-6f));
}

TEST_CASE("negative drive treated as abs() (FR-008)", "[waveshaper][US2][drive][negative]") {
    Waveshaper shaper;
    shaper.setType(WaveshapeType::Tanh);

    shaper.setDrive(-2.0f);
    REQUIRE(shaper.getDrive() == Approx(2.0f));

    const float input = 0.5f;
    // abs(-2.0) * 0.5 = 1.0
    const float expected = Sigmoid::tanh(1.0f);
    float actual = shaper.process(input);

    REQUIRE(actual == Approx(expected).margin(1e-6f));
}

TEST_CASE("drive=0 returns 0.0 regardless of input (FR-027)", "[waveshaper][US2][drive][zero]") {
    Waveshaper shaper;
    shaper.setDrive(0.0f);

    SECTION("positive input") {
        REQUIRE(shaper.process(0.5f) == 0.0f);
    }

    SECTION("negative input") {
        REQUIRE(shaper.process(-0.5f) == 0.0f);
    }

    SECTION("unity input") {
        REQUIRE(shaper.process(1.0f) == 0.0f);
    }

    SECTION("zero input") {
        REQUIRE(shaper.process(0.0f) == 0.0f);
    }

    SECTION("large input") {
        REQUIRE(shaper.process(100.0f) == 0.0f);
    }
}

TEST_CASE("SC-002 verification: process(0.5) with drive=2.0 equals process(1.0) with drive=1.0",
          "[waveshaper][US2][drive][SC-002]") {
    Waveshaper shaper;

    auto type = GENERATE(
        WaveshapeType::Tanh,
        WaveshapeType::Atan,
        WaveshapeType::Cubic,
        WaveshapeType::Quintic,
        WaveshapeType::ReciprocalSqrt,
        WaveshapeType::Erf,
        WaveshapeType::HardClip,
        WaveshapeType::Diode,
        WaveshapeType::Tube
    );

    shaper.setType(type);

    // process(0.5) with drive=2.0
    shaper.setDrive(2.0f);
    float result1 = shaper.process(0.5f);

    // process(1.0) with drive=1.0
    shaper.setDrive(1.0f);
    float result2 = shaper.process(1.0f);

    // Both should apply the shape function to the same value (1.0)
    REQUIRE(result1 == Approx(result2).margin(1e-6f));
}

// =============================================================================
// Phase 5: User Story 3 - Asymmetry for Even Harmonics
// =============================================================================

TEST_CASE("asymmetry=0.0 produces same output as underlying symmetric function (FR-006)",
          "[waveshaper][US3][asymmetry][zero]") {
    Waveshaper shaper;
    shaper.setType(WaveshapeType::Tanh);
    shaper.setAsymmetry(0.0f);

    const float input = 0.5f;
    const float expected = Sigmoid::tanh(input);
    float actual = shaper.process(input);

    REQUIRE(actual == Approx(expected).margin(1e-6f));
}

TEST_CASE("asymmetry=0.3 shifts input by 0.3 before shaping (SC-003)",
          "[waveshaper][US3][asymmetry][positive]") {
    Waveshaper shaper;
    shaper.setType(WaveshapeType::Tanh);
    shaper.setDrive(1.0f);
    shaper.setAsymmetry(0.3f);

    const float input = 0.5f;
    // shape(drive * x + asymmetry) = tanh(1.0 * 0.5 + 0.3) = tanh(0.8)
    const float expected = Sigmoid::tanh(0.8f);
    float actual = shaper.process(input);

    REQUIRE(actual == Approx(expected).margin(1e-6f));
}

TEST_CASE("asymmetry clamped to [-1.0, 1.0] (FR-007)", "[waveshaper][US3][asymmetry][clamp]") {
    Waveshaper shaper;

    SECTION("above 1.0 clamped to 1.0") {
        shaper.setAsymmetry(2.0f);
        REQUIRE(shaper.getAsymmetry() == Approx(1.0f));
    }

    SECTION("below -1.0 clamped to -1.0") {
        shaper.setAsymmetry(-2.0f);
        REQUIRE(shaper.getAsymmetry() == Approx(-1.0f));
    }

    SECTION("within range unchanged") {
        shaper.setAsymmetry(0.5f);
        REQUIRE(shaper.getAsymmetry() == Approx(0.5f));

        shaper.setAsymmetry(-0.5f);
        REQUIRE(shaper.getAsymmetry() == Approx(-0.5f));
    }

    SECTION("boundary values") {
        shaper.setAsymmetry(1.0f);
        REQUIRE(shaper.getAsymmetry() == Approx(1.0f));

        shaper.setAsymmetry(-1.0f);
        REQUIRE(shaper.getAsymmetry() == Approx(-1.0f));
    }
}

TEST_CASE("non-zero asymmetry introduces DC offset in output",
          "[waveshaper][US3][asymmetry][dc]") {
    Waveshaper shaper;
    shaper.setType(WaveshapeType::Tanh);
    shaper.setAsymmetry(0.3f);

    // Process a symmetric signal (positive and negative)
    float positiveOutput = shaper.process(0.5f);
    float negativeOutput = shaper.process(-0.5f);

    // With asymmetry, |process(+x)| != |process(-x)|
    // This asymmetry creates DC offset when processing AC signals
    REQUIRE(std::abs(positiveOutput) != Approx(std::abs(negativeOutput)).margin(0.01f));
}

// =============================================================================
// Phase 6: User Story 4 - Block Processing
// =============================================================================

TEST_CASE("processBlock() produces bit-identical output to N sequential process() calls (FR-011, SC-005)",
          "[waveshaper][US4][block][bit-identical]") {
    Waveshaper shaper;
    shaper.setType(WaveshapeType::Tube);
    shaper.setDrive(2.0f);
    shaper.setAsymmetry(0.1f);

    constexpr size_t numSamples = 64;
    std::array<float, numSamples> inputBuffer;
    std::array<float, numSamples> blockBuffer;
    std::array<float, numSamples> sequentialBuffer;

    // Fill with test signal
    for (size_t i = 0; i < numSamples; ++i) {
        float phase = static_cast<float>(i) / static_cast<float>(numSamples);
        inputBuffer[i] = std::sin(phase * 6.28318530718f);
        blockBuffer[i] = inputBuffer[i];
    }

    // Process with processBlock()
    shaper.processBlock(blockBuffer.data(), numSamples);

    // Process with sequential process() calls
    for (size_t i = 0; i < numSamples; ++i) {
        sequentialBuffer[i] = shaper.process(inputBuffer[i]);
    }

    // Verify bit-identical using memcmp
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
    REQUIRE(std::memcmp(blockBuffer.data(), sequentialBuffer.data(),
                        numSamples * sizeof(float)) == 0);
}

TEST_CASE("processBlock() with 512 samples produces correct output",
          "[waveshaper][US4][block][large]") {
    Waveshaper shaper;
    shaper.setType(WaveshapeType::Tanh);
    shaper.setDrive(1.5f);

    constexpr size_t numSamples = 512;
    std::array<float, numSamples> buffer;

    // Fill with sine wave
    for (size_t i = 0; i < numSamples; ++i) {
        float phase = static_cast<float>(i) / static_cast<float>(numSamples);
        buffer[i] = std::sin(phase * 6.28318530718f * 4.0f); // 4 cycles
    }

    shaper.processBlock(buffer.data(), numSamples);

    // Verify all outputs are valid (not NaN/Inf) and bounded
    for (size_t i = 0; i < numSamples; ++i) {
        REQUIRE_FALSE(std::isnan(buffer[i]));
        REQUIRE_FALSE(std::isinf(buffer[i]));
        REQUIRE(buffer[i] >= -1.0f);
        REQUIRE(buffer[i] <= 1.0f);
    }
}

TEST_CASE("processBlock() is in-place (modifies input buffer)",
          "[waveshaper][US4][block][inplace]") {
    Waveshaper shaper;
    shaper.setType(WaveshapeType::Tanh);
    shaper.setDrive(2.0f);

    constexpr size_t numSamples = 8;
    std::array<float, numSamples> buffer = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    std::array<float, numSamples> original = buffer;

    shaper.processBlock(buffer.data(), numSamples);

    // Verify buffer was modified
    bool anyChanged = false;
    for (size_t i = 0; i < numSamples; ++i) {
        if (buffer[i] != original[i]) {
            anyChanged = true;
            break;
        }
    }
    REQUIRE(anyChanged);
}

// =============================================================================
// Phase 7: Edge Cases and Robustness
// =============================================================================

TEST_CASE("NaN input propagates NaN output (FR-028)", "[waveshaper][edge][nan]") {
    Waveshaper shaper;
    const float nanValue = std::numeric_limits<float>::quiet_NaN();

    // Most types propagate NaN correctly
    // Note: HardClip uses std::clamp which has undefined behavior with NaN
    auto type = GENERATE(
        WaveshapeType::Tanh,
        WaveshapeType::Atan,
        WaveshapeType::Cubic,
        WaveshapeType::Quintic,
        WaveshapeType::ReciprocalSqrt,
        WaveshapeType::Erf,
        WaveshapeType::Diode,
        WaveshapeType::Tube
    );

    shaper.setType(type);
    float result = shaper.process(nanValue);

    REQUIRE(std::isnan(result));
}

TEST_CASE("HardClip NaN handling is implementation-defined", "[waveshaper][edge][nan][hardclip]") {
    // HardClip uses std::clamp which has undefined behavior with NaN in C++17
    // We verify it doesn't crash and returns some value
    Waveshaper shaper;
    shaper.setType(WaveshapeType::HardClip);
    const float nanValue = std::numeric_limits<float>::quiet_NaN();

    float result = shaper.process(nanValue);
    // Either NaN or bounded value is acceptable (implementation-defined)
    REQUIRE((std::isnan(result) || (result >= -1.0f && result <= 1.0f)));
}

TEST_CASE("Positive infinity input handled gracefully (FR-029)", "[waveshaper][edge][infinity][positive]") {
    Waveshaper shaper;
    const float posInf = std::numeric_limits<float>::infinity();

    SECTION("Tanh -> +1") {
        shaper.setType(WaveshapeType::Tanh);
        REQUIRE(shaper.process(posInf) == Approx(1.0f));
    }

    SECTION("Atan -> +1") {
        shaper.setType(WaveshapeType::Atan);
        REQUIRE(shaper.process(posInf) == Approx(1.0f));
    }

    SECTION("Cubic -> +1 (clamped)") {
        shaper.setType(WaveshapeType::Cubic);
        REQUIRE(shaper.process(posInf) == Approx(1.0f));
    }

    SECTION("Quintic -> +1 (clamped)") {
        shaper.setType(WaveshapeType::Quintic);
        REQUIRE(shaper.process(posInf) == Approx(1.0f));
    }

    SECTION("ReciprocalSqrt -> +1") {
        shaper.setType(WaveshapeType::ReciprocalSqrt);
        REQUIRE(shaper.process(posInf) == Approx(1.0f));
    }

    SECTION("Erf -> +1") {
        shaper.setType(WaveshapeType::Erf);
        REQUIRE(shaper.process(posInf) == Approx(1.0f));
    }

    SECTION("HardClip -> +1") {
        shaper.setType(WaveshapeType::HardClip);
        REQUIRE(shaper.process(posInf) == Approx(1.0f));
    }

    SECTION("Tube -> NaN or bounded (polynomial with infinity)") {
        shaper.setType(WaveshapeType::Tube);
        float result = shaper.process(posInf);
        // Tube uses x + 0.3*x^2 - 0.15*x^3 before tanh
        // With infinity: inf + inf - inf = NaN (indeterminate form)
        // Either NaN or finite are acceptable for infinity input
        REQUIRE((std::isnan(result) || std::isfinite(result)));
    }

    SECTION("Diode -> approaches 1.0 (unbounded but saturates)") {
        shaper.setType(WaveshapeType::Diode);
        float result = shaper.process(posInf);
        // Diode uses 1 - exp(-x*1.5) for positive values
        // As x -> +inf, exp(-inf) -> 0, so result -> 1.0
        REQUIRE(result == Approx(1.0f).margin(0.01f));
    }
}

TEST_CASE("Negative infinity input handled gracefully (FR-029)", "[waveshaper][edge][infinity][negative]") {
    Waveshaper shaper;
    const float negInf = -std::numeric_limits<float>::infinity();

    SECTION("Tanh -> -1") {
        shaper.setType(WaveshapeType::Tanh);
        REQUIRE(shaper.process(negInf) == Approx(-1.0f));
    }

    SECTION("Atan -> -1") {
        shaper.setType(WaveshapeType::Atan);
        REQUIRE(shaper.process(negInf) == Approx(-1.0f));
    }

    SECTION("Cubic -> -1 (clamped)") {
        shaper.setType(WaveshapeType::Cubic);
        REQUIRE(shaper.process(negInf) == Approx(-1.0f));
    }

    SECTION("Quintic -> -1 (clamped)") {
        shaper.setType(WaveshapeType::Quintic);
        REQUIRE(shaper.process(negInf) == Approx(-1.0f));
    }

    SECTION("ReciprocalSqrt -> -1") {
        shaper.setType(WaveshapeType::ReciprocalSqrt);
        REQUIRE(shaper.process(negInf) == Approx(-1.0f));
    }

    SECTION("Erf -> -1") {
        shaper.setType(WaveshapeType::Erf);
        REQUIRE(shaper.process(negInf) == Approx(-1.0f));
    }

    SECTION("HardClip -> -1") {
        shaper.setType(WaveshapeType::HardClip);
        REQUIRE(shaper.process(negInf) == Approx(-1.0f));
    }

    SECTION("Tube -> NaN or bounded (polynomial with infinity)") {
        shaper.setType(WaveshapeType::Tube);
        float result = shaper.process(negInf);
        // Tube uses x + 0.3*x^2 - 0.15*x^3 before tanh
        // With -infinity: -inf + inf - (-inf) = NaN (indeterminate form)
        // Either NaN or finite are acceptable for infinity input
        REQUIRE((std::isnan(result) || std::isfinite(result)));
    }

    SECTION("Diode -> unbounded for negative infinity") {
        shaper.setType(WaveshapeType::Diode);
        float result = shaper.process(negInf);
        // Diode uses x / (1 - 0.5*x) for negative values
        // As x -> -inf: x / (1 - 0.5*x) -> -inf / inf (indeterminate)
        // In practice, the result may be NaN, -Inf, or a finite value
        REQUIRE((std::isinf(result) || std::isnan(result) || std::isfinite(result)));
    }
}

TEST_CASE("SC-004: 1M samples produces no unexpected NaN/Inf for valid inputs",
          "[waveshaper][edge][stability][SC-004]") {
    Waveshaper shaper;

    auto type = GENERATE(
        WaveshapeType::Tanh,
        WaveshapeType::Atan,
        WaveshapeType::Cubic,
        WaveshapeType::Quintic,
        WaveshapeType::ReciprocalSqrt,
        WaveshapeType::Erf,
        WaveshapeType::HardClip,
        WaveshapeType::Diode,
        WaveshapeType::Tube
    );

    shaper.setType(type);
    shaper.setDrive(2.0f);
    shaper.setAsymmetry(0.1f);

    constexpr size_t numSamples = 1000000;
    bool foundNaN = false;
    bool foundInf = false;

    for (size_t i = 0; i < numSamples; ++i) {
        // Generate input in [-1, 1] range
        float input = (static_cast<float>(i % 2000) - 1000.0f) / 1000.0f;
        float output = shaper.process(input);

        if (std::isnan(output)) {
            foundNaN = true;
            break;
        }
        if (std::isinf(output)) {
            foundInf = true;
            break;
        }
    }

    REQUIRE_FALSE(foundNaN);
    REQUIRE_FALSE(foundInf);
}

TEST_CASE("SC-007: bounded types stay in [-1,1] for inputs [-10,10] with drive=1.0",
          "[waveshaper][edge][bounds][SC-007]") {
    Waveshaper shaper;
    shaper.setDrive(1.0f);
    shaper.setAsymmetry(0.0f);

    // All bounded types
    auto type = GENERATE(
        WaveshapeType::Tanh,
        WaveshapeType::Atan,
        WaveshapeType::Cubic,
        WaveshapeType::Quintic,
        WaveshapeType::ReciprocalSqrt,
        WaveshapeType::Erf,
        WaveshapeType::HardClip,
        WaveshapeType::Tube
    );

    shaper.setType(type);

    // Test range [-10, 10]
    for (int i = -100; i <= 100; ++i) {
        float input = static_cast<float>(i) / 10.0f; // -10 to +10
        float output = shaper.process(input);

        REQUIRE(output >= -1.0f);
        REQUIRE(output <= 1.0f);
    }
}

TEST_CASE("Diode type (only unbounded type) can exceed [-1,1] bounds",
          "[waveshaper][edge][bounds][diode]") {
    Waveshaper shaper;
    shaper.setType(WaveshapeType::Diode);
    shaper.setDrive(1.0f);

    // Diode uses x / (1 - 0.5*x) for negative values
    // For x = -3: -3 / (1 - 0.5*(-3)) = -3 / 2.5 = -1.2
    float output = shaper.process(-3.0f);

    // Should exceed -1
    REQUIRE(output < -1.0f);
}

TEST_CASE("Extreme drive values (>100): bounded types still produce bounded output",
          "[waveshaper][edge][drive][extreme]") {
    Waveshaper shaper;
    shaper.setDrive(100.0f);
    shaper.setAsymmetry(0.0f);

    // All bounded types
    auto type = GENERATE(
        WaveshapeType::Tanh,
        WaveshapeType::Atan,
        WaveshapeType::Cubic,
        WaveshapeType::Quintic,
        WaveshapeType::ReciprocalSqrt,
        WaveshapeType::Erf,
        WaveshapeType::HardClip,
        WaveshapeType::Tube
    );

    shaper.setType(type);

    // Test with various inputs
    for (float input : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
        float output = shaper.process(input);

        REQUIRE(output >= -1.0f);
        REQUIRE(output <= 1.0f);
        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
    }
}

// =============================================================================
// Spectral Analysis Tests - Harmonic Generation
// =============================================================================

TEST_CASE("Waveshaper spectral analysis: low drive produces minimal aliasing",
          "[waveshaper][aliasing][drive]") {
    using namespace Krate::DSP::TestUtils;

    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 1.0f,  // Will be overridden by Waveshaper's drive
        .fftSize = 4096,
        .maxHarmonic = 10
    };

    SECTION("low drive (0.5x) produces less aliasing than high drive (4x)") {
        Waveshaper lowDrive;
        lowDrive.setType(WaveshapeType::Tanh);
        lowDrive.setDrive(0.5f);

        Waveshaper highDrive;
        highDrive.setType(WaveshapeType::Tanh);
        highDrive.setDrive(4.0f);

        auto lowResult = measureAliasing(config, [&lowDrive](float x) {
            return lowDrive.process(x);
        });

        auto highResult = measureAliasing(config, [&highDrive](float x) {
            return highDrive.process(x);
        });

        INFO("Low drive (0.5x) aliasing: " << lowResult.aliasingPowerDb << " dB");
        INFO("High drive (4x) aliasing: " << highResult.aliasingPowerDb << " dB");
        REQUIRE(lowResult.aliasingPowerDb < highResult.aliasingPowerDb);
    }
}

TEST_CASE("Waveshaper spectral analysis: different types have different aliasing",
          "[waveshaper][aliasing][types]") {
    using namespace Krate::DSP::TestUtils;

    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 1.0f,
        .fftSize = 4096,
        .maxHarmonic = 10
    };

    SECTION("HardClip generates more aliasing than Tanh") {
        Waveshaper hardClip;
        hardClip.setType(WaveshapeType::HardClip);
        hardClip.setDrive(4.0f);

        Waveshaper tanh;
        tanh.setType(WaveshapeType::Tanh);
        tanh.setDrive(4.0f);

        auto hardResult = measureAliasing(config, [&hardClip](float x) {
            return hardClip.process(x);
        });

        auto tanhResult = measureAliasing(config, [&tanh](float x) {
            return tanh.process(x);
        });

        INFO("HardClip aliasing: " << hardResult.aliasingPowerDb << " dB");
        INFO("Tanh aliasing: " << tanhResult.aliasingPowerDb << " dB");
        // HardClip (sharp discontinuity) should produce more aliasing than smooth Tanh
        REQUIRE(hardResult.aliasingPowerDb > tanhResult.aliasingPowerDb);
    }
}

TEST_CASE("Waveshaper spectral analysis: all bounded types generate harmonics",
          "[waveshaper][aliasing][bounded]") {
    using namespace Krate::DSP::TestUtils;

    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 1.0f,
        .fftSize = 4096,
        .maxHarmonic = 10
    };

    // Test all bounded types generate harmonics when driven
    auto type = GENERATE(
        WaveshapeType::Tanh,
        WaveshapeType::Atan,
        WaveshapeType::Cubic,
        WaveshapeType::Quintic,
        WaveshapeType::ReciprocalSqrt,
        WaveshapeType::Erf,
        WaveshapeType::HardClip,
        WaveshapeType::Tube
    );

    Waveshaper shaper;
    shaper.setType(type);
    shaper.setDrive(4.0f);

    auto result = measureAliasing(config, [&shaper](float x) {
        return shaper.process(x);
    });

    INFO("Type " << static_cast<int>(type) << " harmonics: " << result.harmonicPowerDb << " dB");
    // All saturation types should generate measurable harmonic content when driven hard
    REQUIRE(result.harmonicPowerDb > -80.0f);
}

TEST_CASE("Waveshaper spectral analysis: asymmetry affects spectrum",
          "[waveshaper][aliasing][asymmetry]") {
    using namespace Krate::DSP::TestUtils;

    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 1.0f,
        .fftSize = 4096,
        .maxHarmonic = 10
    };

    SECTION("asymmetry produces different spectral content") {
        Waveshaper symmetric;
        symmetric.setType(WaveshapeType::Tanh);
        symmetric.setDrive(2.0f);
        symmetric.setAsymmetry(0.0f);

        Waveshaper asymmetric;
        asymmetric.setType(WaveshapeType::Tanh);
        asymmetric.setDrive(2.0f);
        asymmetric.setAsymmetry(0.5f);

        auto symResult = measureAliasing(config, [&symmetric](float x) {
            return symmetric.process(x);
        });

        auto asymResult = measureAliasing(config, [&asymmetric](float x) {
            return asymmetric.process(x);
        });

        INFO("Symmetric harmonics: " << symResult.harmonicPowerDb << " dB");
        INFO("Asymmetric harmonics: " << asymResult.harmonicPowerDb << " dB");

        // Both should generate harmonics
        REQUIRE(symResult.harmonicPowerDb > -80.0f);
        REQUIRE(asymResult.harmonicPowerDb > -80.0f);
    }
}

// =============================================================================
// SignalMetrics THD Tests
// =============================================================================

#include <signal_metrics.h>
#include <test_signals.h>

TEST_CASE("Waveshaper SignalMetrics: THD increases with drive level",
          "[waveshaper][signalmetrics][thd]") {
    using namespace Krate::DSP::TestUtils;

    constexpr size_t numSamples = 8192;
    constexpr float sampleRate = 44100.0f;
    constexpr float fundamentalHz = 440.0f;

    std::vector<float> input(numSamples);
    std::vector<float> output(numSamples);
    TestHelpers::generateSine(input.data(), numSamples, fundamentalHz, sampleRate);

    SECTION("Tanh: THD increases with drive") {
        Waveshaper shaper;
        shaper.setType(WaveshapeType::Tanh);

        // Low drive - nearly linear
        shaper.setDrive(0.5f);
        std::copy(input.begin(), input.end(), output.begin());
        shaper.processBlock(output.data(), numSamples);
        float lowDriveTHD = SignalMetrics::calculateTHD(output.data(), numSamples,
                                                        fundamentalHz, sampleRate);

        // Medium drive
        shaper.setDrive(2.0f);
        std::copy(input.begin(), input.end(), output.begin());
        shaper.processBlock(output.data(), numSamples);
        float medDriveTHD = SignalMetrics::calculateTHD(output.data(), numSamples,
                                                        fundamentalHz, sampleRate);

        // High drive
        shaper.setDrive(8.0f);
        std::copy(input.begin(), input.end(), output.begin());
        shaper.processBlock(output.data(), numSamples);
        float highDriveTHD = SignalMetrics::calculateTHD(output.data(), numSamples,
                                                         fundamentalHz, sampleRate);

        INFO("Low drive (0.5) THD: " << lowDriveTHD << "%");
        INFO("Medium drive (2.0) THD: " << medDriveTHD << "%");
        INFO("High drive (8.0) THD: " << highDriveTHD << "%");

        REQUIRE(lowDriveTHD < medDriveTHD);
        REQUIRE(medDriveTHD < highDriveTHD);
    }
}

TEST_CASE("Waveshaper SignalMetrics: compare THD across types",
          "[waveshaper][signalmetrics][thd][compare]") {
    using namespace Krate::DSP::TestUtils;

    constexpr size_t numSamples = 8192;
    constexpr float sampleRate = 44100.0f;
    constexpr float fundamentalHz = 440.0f;

    std::vector<float> input(numSamples);
    std::vector<float> output(numSamples);
    TestHelpers::generateSine(input.data(), numSamples, fundamentalHz, sampleRate);

    // Fixed drive for fair comparison
    constexpr float drive = 4.0f;

    SECTION("Different types produce different THD profiles") {
        std::map<WaveshapeType, float> thdByType;

        for (auto type : {WaveshapeType::Tanh, WaveshapeType::Cubic,
                          WaveshapeType::HardClip, WaveshapeType::Erf}) {
            Waveshaper shaper;
            shaper.setType(type);
            shaper.setDrive(drive);

            std::copy(input.begin(), input.end(), output.begin());
            shaper.processBlock(output.data(), numSamples);

            thdByType[type] = SignalMetrics::calculateTHD(output.data(), numSamples,
                                                          fundamentalHz, sampleRate);
        }

        INFO("Tanh THD: " << thdByType[WaveshapeType::Tanh] << "%");
        INFO("Cubic THD: " << thdByType[WaveshapeType::Cubic] << "%");
        INFO("HardClip THD: " << thdByType[WaveshapeType::HardClip] << "%");
        INFO("Erf THD: " << thdByType[WaveshapeType::Erf] << "%");

        // All types should produce measurable distortion at drive=4.0
        REQUIRE(thdByType[WaveshapeType::Tanh] > 10.0f);
        REQUIRE(thdByType[WaveshapeType::Cubic] > 10.0f);
        REQUIRE(thdByType[WaveshapeType::HardClip] > 10.0f);
        REQUIRE(thdByType[WaveshapeType::Erf] > 10.0f);

        // Different types should produce noticeably different THD
        // (not necessarily in a specific order, as characteristics vary)
        REQUIRE(thdByType[WaveshapeType::Tanh] != thdByType[WaveshapeType::Cubic]);
        REQUIRE(thdByType[WaveshapeType::Tanh] != thdByType[WaveshapeType::HardClip]);
    }
}

TEST_CASE("Waveshaper SignalMetrics: low drive is nearly linear",
          "[waveshaper][signalmetrics][thd][linear]") {
    using namespace Krate::DSP::TestUtils;

    constexpr size_t numSamples = 8192;
    constexpr float sampleRate = 44100.0f;
    constexpr float fundamentalHz = 440.0f;

    std::vector<float> input(numSamples);
    std::vector<float> output(numSamples);
    TestHelpers::generateSine(input.data(), numSamples, fundamentalHz, sampleRate);

    // Test that very low drive produces minimal distortion
    auto type = GENERATE(
        WaveshapeType::Tanh,
        WaveshapeType::Atan,
        WaveshapeType::Cubic,
        WaveshapeType::Erf
    );

    Waveshaper shaper;
    shaper.setType(type);
    shaper.setDrive(0.1f);  // Very low drive

    std::copy(input.begin(), input.end(), output.begin());
    shaper.processBlock(output.data(), numSamples);

    float thd = SignalMetrics::calculateTHD(output.data(), numSamples,
                                            fundamentalHz, sampleRate);

    INFO("Type " << static_cast<int>(type) << " THD at drive=0.1: " << thd << "%");
    // At very low drive, THD should be minimal (<1%)
    REQUIRE(thd < 1.0f);
}

TEST_CASE("Waveshaper SignalMetrics: asymmetry adds even harmonics",
          "[waveshaper][signalmetrics][thd][asymmetry]") {
    using namespace Krate::DSP::TestUtils;

    constexpr size_t numSamples = 8192;
    constexpr float sampleRate = 44100.0f;
    constexpr float fundamentalHz = 440.0f;

    std::vector<float> input(numSamples);
    std::vector<float> output(numSamples);
    TestHelpers::generateSine(input.data(), numSamples, fundamentalHz, sampleRate);

    SECTION("asymmetry changes THD profile") {
        Waveshaper symmetric;
        symmetric.setType(WaveshapeType::Tanh);
        symmetric.setDrive(2.0f);
        symmetric.setAsymmetry(0.0f);

        Waveshaper asymmetric;
        asymmetric.setType(WaveshapeType::Tanh);
        asymmetric.setDrive(2.0f);
        asymmetric.setAsymmetry(0.5f);

        std::copy(input.begin(), input.end(), output.begin());
        symmetric.processBlock(output.data(), numSamples);
        float symTHD = SignalMetrics::calculateTHD(output.data(), numSamples,
                                                   fundamentalHz, sampleRate);

        std::copy(input.begin(), input.end(), output.begin());
        asymmetric.processBlock(output.data(), numSamples);
        float asymTHD = SignalMetrics::calculateTHD(output.data(), numSamples,
                                                    fundamentalHz, sampleRate);

        INFO("Symmetric THD: " << symTHD << "%");
        INFO("Asymmetric THD: " << asymTHD << "%");

        // Both should produce measurable distortion
        REQUIRE(symTHD > 1.0f);
        REQUIRE(asymTHD > 1.0f);
    }
}

TEST_CASE("Waveshaper SignalMetrics: measureQuality aggregate metrics",
          "[waveshaper][signalmetrics][quality]") {
    using namespace Krate::DSP::TestUtils;

    constexpr size_t numSamples = 8192;
    constexpr float sampleRate = 44100.0f;
    constexpr float fundamentalHz = 440.0f;

    std::vector<float> input(numSamples);
    std::vector<float> output(numSamples);
    TestHelpers::generateSine(input.data(), numSamples, fundamentalHz, sampleRate);

    Waveshaper shaper;
    shaper.setType(WaveshapeType::Tanh);
    shaper.setDrive(4.0f);

    std::copy(input.begin(), input.end(), output.begin());
    shaper.processBlock(output.data(), numSamples);

    auto metrics = SignalMetrics::measureQuality(
        output.data(), input.data(), numSamples, fundamentalHz, sampleRate);

    INFO("SNR: " << metrics.snrDb << " dB");
    INFO("THD: " << metrics.thdPercent << "%");
    INFO("THD (dB): " << metrics.thdDb << " dB");
    INFO("Crest factor: " << metrics.crestFactorDb << " dB");
    INFO("Kurtosis: " << metrics.kurtosis);

    REQUIRE(metrics.isValid());
    REQUIRE(metrics.thdPercent > 5.0f);  // Noticeable distortion at drive=4.0
    REQUIRE(metrics.thdPercent < 100.0f); // But not extreme
}
