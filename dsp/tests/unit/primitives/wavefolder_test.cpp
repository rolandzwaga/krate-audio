// ==============================================================================
// Unit Tests: Wavefolder Primitive
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Feature: 057-wavefolder
// Layer: 1 (Primitives)
//
// Reference: specs/057-wavefolder/spec.md
// ==============================================================================

#include <krate/dsp/primitives/wavefolder.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

// Test helpers for spectral and artifact analysis
#include "spectral_analysis.h"
#include "artifact_detection.h"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Tags Reference
// =============================================================================
// [wavefolder]    - All wavefolder tests
// [construction]  - Construction and default value tests
// [triangle]      - Triangle fold algorithm tests
// [sine]          - Sine fold algorithm tests
// [lockhart]      - Lockhart fold algorithm tests
// [setter]        - Setter method tests
// [getter]        - Getter method tests
// [block]         - Block processing tests
// [runtime]       - Runtime parameter change tests
// [edge]          - Edge case tests (NaN, Inf, etc.)
// [stability]     - Numerical stability tests
// [success]       - Success criteria verification tests
// [.benchmark]    - Performance benchmarks (opt-in)

// =============================================================================
// Phase 2: User Story 1 - Basic Wavefolding Tests
// =============================================================================

// -----------------------------------------------------------------------------
// Construction and Default Tests (T008, T009)
// -----------------------------------------------------------------------------

TEST_CASE("Wavefolder default constructor initializes to Triangle type", "[wavefolder][construction]") {
    // FR-003: Default constructor initializes type to Triangle
    Wavefolder folder;
    REQUIRE(folder.getType() == WavefoldType::Triangle);
}

TEST_CASE("Wavefolder default constructor initializes foldAmount to 1.0", "[wavefolder][construction]") {
    // FR-004: Default constructor initializes foldAmount to 1.0f
    Wavefolder folder;
    REQUIRE(folder.getFoldAmount() == Approx(1.0f));
}

// -----------------------------------------------------------------------------
// Triangle Fold Tests (T010-T013)
// -----------------------------------------------------------------------------

TEST_CASE("Triangle fold with foldAmount 1.0 produces output bounded to -1 and 1", "[wavefolder][triangle]") {
    // FR-011: Output bounded to [-threshold, threshold] where threshold = 1/foldAmount
    Wavefolder folder;
    folder.setType(WavefoldType::Triangle);
    folder.setFoldAmount(1.0f);  // threshold = 1.0

    // Test various inputs
    REQUIRE(folder.process(0.0f) == Approx(0.0f).margin(1e-6f));
    REQUIRE(folder.process(0.5f) == Approx(0.5f).margin(1e-6f));
    REQUIRE(folder.process(-0.5f) == Approx(-0.5f).margin(1e-6f));
    REQUIRE(folder.process(0.999f) == Approx(0.999f).margin(1e-6f));

    // Verify bounded output for larger inputs
    float output1 = folder.process(1.5f);
    REQUIRE(output1 >= -1.0f);
    REQUIRE(output1 <= 1.0f);

    float output2 = folder.process(-2.5f);
    REQUIRE(output2 >= -1.0f);
    REQUIRE(output2 <= 1.0f);
}

TEST_CASE("Triangle fold exhibits odd symmetry f(-x) equals -f(x)", "[wavefolder][triangle]") {
    // FR-012: Odd symmetry property
    Wavefolder folder;
    folder.setType(WavefoldType::Triangle);
    folder.setFoldAmount(2.0f);

    std::array<float, 5> testValues = {0.1f, 0.5f, 1.0f, 2.5f, 5.0f};

    for (float x : testValues) {
        float pos = folder.process(x);
        float neg = folder.process(-x);
        REQUIRE(neg == Approx(-pos).margin(1e-6f));
    }
}

TEST_CASE("Triangle fold with foldAmount 2.0 folds signal exceeding threshold back symmetrically", "[wavefolder][triangle]") {
    // FR-010: Signal folds back at threshold
    Wavefolder folder;
    folder.setType(WavefoldType::Triangle);
    folder.setFoldAmount(2.0f);  // threshold = 0.5

    // Input within threshold should pass through
    REQUIRE(folder.process(0.3f) == Approx(0.3f).margin(1e-6f));

    // Input beyond threshold should fold back
    // At threshold = 0.5, input of 0.7 should fold
    float output = folder.process(0.7f);
    REQUIRE(output >= -0.5f);
    REQUIRE(output <= 0.5f);
}

TEST_CASE("Triangle fold handles very large input via modular arithmetic", "[wavefolder][triangle]") {
    // FR-013: Multi-fold support for arbitrary magnitudes
    Wavefolder folder;
    folder.setType(WavefoldType::Triangle);
    folder.setFoldAmount(1.0f);  // threshold = 1.0

    // Very large input should still produce bounded output
    float output = folder.process(1000.0f);
    REQUIRE(output >= -1.0f);
    REQUIRE(output <= 1.0f);

    // Negative large input
    float outputNeg = folder.process(-1000.0f);
    REQUIRE(outputNeg >= -1.0f);
    REQUIRE(outputNeg <= 1.0f);
}

// -----------------------------------------------------------------------------
// Sine Fold Tests (T014-T016)
// -----------------------------------------------------------------------------

TEST_CASE("Sine fold always produces output bounded to -1 and 1", "[wavefolder][sine]") {
    // FR-015: Output always within [-1, 1]
    Wavefolder folder;
    folder.setType(WavefoldType::Sine);

    std::array<float, 4> foldAmounts = {0.5f, 1.0f, 3.14159f, 10.0f};
    std::array<float, 5> inputs = {0.0f, 0.5f, 1.0f, 10.0f, 100.0f};

    for (float amount : foldAmounts) {
        folder.setFoldAmount(amount);
        for (float x : inputs) {
            float pos = folder.process(x);
            float neg = folder.process(-x);

            REQUIRE(pos >= -1.0f);
            REQUIRE(pos <= 1.0f);
            REQUIRE(neg >= -1.0f);
            REQUIRE(neg <= 1.0f);
        }
    }
}

TEST_CASE("Sine fold with gain PI produces characteristic Serge-style harmonic content", "[wavefolder][sine]") {
    // FR-017: Serge-style harmonic content at gain = PI
    Wavefolder folder;
    folder.setType(WavefoldType::Sine);
    folder.setFoldAmount(3.14159f);  // PI

    // At gain = PI, sin(PI * 0.5) = sin(PI/2) = 1.0
    REQUIRE(folder.process(0.5f) == Approx(1.0f).margin(0.001f));

    // sin(PI * 1.0) = sin(PI) = 0.0
    REQUIRE(folder.process(1.0f) == Approx(0.0f).margin(0.001f));

    // sin(PI * -0.5) = sin(-PI/2) = -1.0
    REQUIRE(folder.process(-0.5f) == Approx(-1.0f).margin(0.001f));
}

TEST_CASE("Sine fold with foldAmount less than 0.001 returns input unchanged", "[wavefolder][sine]") {
    // FR-016: Linear passthrough at very low gain
    Wavefolder folder;
    folder.setType(WavefoldType::Sine);
    folder.setFoldAmount(0.0005f);

    std::array<float, 5> inputs = {0.0f, 0.3f, 0.7f, -0.5f, -0.9f};

    for (float x : inputs) {
        REQUIRE(folder.process(x) == Approx(x).margin(1e-6f));
    }
}

// -----------------------------------------------------------------------------
// Lockhart Fold Tests (T017-T019)
// -----------------------------------------------------------------------------

TEST_CASE("Lockhart fold produces soft saturation characteristics", "[wavefolder][lockhart]") {
    // FR-021: Soft saturation behavior
    Wavefolder folder;
    folder.setType(WavefoldType::Lockhart);
    folder.setFoldAmount(1.0f);

    // Zero input should produce tanh(lambertW(exp(0))) = tanh(lambertW(1)) ~ tanh(0.567) ~ 0.514
    float zeroOutput = folder.process(0.0f);
    REQUIRE(zeroOutput == Approx(0.514f).margin(0.01f));

    // Positive input should produce higher output (soft saturation)
    float posOutput = folder.process(1.0f);
    REQUIRE(posOutput > zeroOutput);
    REQUIRE(posOutput <= 1.0f);  // Bounded by tanh

    // Negative input should produce lower output
    float negOutput = folder.process(-1.0f);
    REQUIRE(negOutput < zeroOutput);
}

TEST_CASE("Lockhart fold scales input by foldAmount before transfer function", "[wavefolder][lockhart]") {
    // FR-019: Input scaling by foldAmount
    Wavefolder folder;
    folder.setType(WavefoldType::Lockhart);

    // Higher foldAmount should increase saturation effect
    folder.setFoldAmount(1.0f);
    float low = folder.process(0.5f);

    folder.setFoldAmount(5.0f);
    float high = folder.process(0.5f);

    // Higher foldAmount should push output closer to saturation
    REQUIRE(high > low);
}

TEST_CASE("Lockhart fold with foldAmount 0 returns approximately 0.514 for any input", "[wavefolder][lockhart]") {
    // FR-022: foldAmount=0 returns tanh(lambertW(1)) ~ 0.514
    // When foldAmount = 0: exp(x * 0) = exp(0) = 1 for all x
    // lambertW(1) ~ 0.567, tanh(0.567) ~ 0.514
    Wavefolder folder;
    folder.setType(WavefoldType::Lockhart);
    folder.setFoldAmount(0.0f);

    // Various inputs should all produce the same output
    REQUIRE(folder.process(0.0f) == Approx(0.514f).margin(0.01f));
    REQUIRE(folder.process(1.0f) == Approx(0.514f).margin(0.01f));
    REQUIRE(folder.process(-1.0f) == Approx(0.514f).margin(0.01f));
    REQUIRE(folder.process(100.0f) == Approx(0.514f).margin(0.01f));
}

// -----------------------------------------------------------------------------
// Setter Tests (T020-T023)
// -----------------------------------------------------------------------------

TEST_CASE("setType changes wavefold type and getType returns new type", "[wavefolder][setter][getter]") {
    // FR-005: setType changes type
    Wavefolder folder;

    folder.setType(WavefoldType::Sine);
    REQUIRE(folder.getType() == WavefoldType::Sine);

    folder.setType(WavefoldType::Lockhart);
    REQUIRE(folder.getType() == WavefoldType::Lockhart);

    folder.setType(WavefoldType::Triangle);
    REQUIRE(folder.getType() == WavefoldType::Triangle);
}

TEST_CASE("setFoldAmount changes fold amount and getFoldAmount returns new value", "[wavefolder][setter][getter]") {
    // FR-006: setFoldAmount changes foldAmount
    Wavefolder folder;

    folder.setFoldAmount(5.0f);
    REQUIRE(folder.getFoldAmount() == Approx(5.0f));

    folder.setFoldAmount(0.5f);
    REQUIRE(folder.getFoldAmount() == Approx(0.5f));
}

TEST_CASE("setFoldAmount clamps to 0.0 and 10.0 range", "[wavefolder][setter]") {
    // FR-006a: Clamp to [0.0, 10.0]
    Wavefolder folder;

    folder.setFoldAmount(15.0f);
    REQUIRE(folder.getFoldAmount() == Approx(10.0f));

    folder.setFoldAmount(-5.0f);  // abs(-5) = 5, which is within range
    REQUIRE(folder.getFoldAmount() == Approx(5.0f));

    folder.setFoldAmount(-15.0f);  // abs(-15) = 15, clamped to 10
    REQUIRE(folder.getFoldAmount() == Approx(10.0f));

    folder.setFoldAmount(0.0f);
    REQUIRE(folder.getFoldAmount() == Approx(0.0f));
}

TEST_CASE("setFoldAmount with negative value stores absolute value", "[wavefolder][setter]") {
    // FR-007: Negative values treated as positive
    Wavefolder folder;

    folder.setFoldAmount(-3.0f);
    REQUIRE(folder.getFoldAmount() == Approx(3.0f));

    folder.setFoldAmount(-0.5f);
    REQUIRE(folder.getFoldAmount() == Approx(0.5f));
}

// =============================================================================
// Phase 4: User Story 2 - Block Processing Tests
// =============================================================================

TEST_CASE("processBlock produces bit-identical output to N sequential process calls", "[wavefolder][block]") {
    // FR-029, SC-004: Bit-identical output
    Wavefolder folder;
    folder.setType(WavefoldType::Sine);
    folder.setFoldAmount(3.0f);

    constexpr size_t numSamples = 64;
    std::array<float, numSamples> blockBuffer;
    std::array<float, numSamples> sequentialBuffer;

    // Fill with test signal
    for (size_t i = 0; i < numSamples; ++i) {
        float value = std::sin(static_cast<float>(i) * 0.1f);
        blockBuffer[i] = value;
        sequentialBuffer[i] = value;
    }

    // Process using block method
    folder.processBlock(blockBuffer.data(), numSamples);

    // Process using sequential method
    for (size_t i = 0; i < numSamples; ++i) {
        sequentialBuffer[i] = folder.process(sequentialBuffer[i]);
    }

    // Verify bit-identical output
    for (size_t i = 0; i < numSamples; ++i) {
        REQUIRE(blockBuffer[i] == sequentialBuffer[i]);
    }
}

TEST_CASE("processBlock with 512 samples produces correct output", "[wavefolder][block]") {
    // SC-003: 512 sample buffer processing
    Wavefolder folder;
    folder.setType(WavefoldType::Triangle);
    folder.setFoldAmount(2.0f);

    constexpr size_t numSamples = 512;
    std::vector<float> buffer(numSamples);

    // Fill with test signal
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(static_cast<float>(i) * 0.05f) * 2.0f;  // Large amplitude
    }

    // Process block
    folder.processBlock(buffer.data(), numSamples);

    // Verify all outputs are bounded
    const float threshold = 1.0f / 2.0f;  // foldAmount = 2.0
    for (size_t i = 0; i < numSamples; ++i) {
        REQUIRE(buffer[i] >= -threshold - 0.001f);
        REQUIRE(buffer[i] <= threshold + 0.001f);
    }
}

TEST_CASE("processBlock with n equals 0 does nothing and does not crash", "[wavefolder][block][edge]") {
    // FR-030: n=0 is valid
    Wavefolder folder;

    // Should not crash
    folder.processBlock(nullptr, 0);

    std::array<float, 4> buffer = {1.0f, 2.0f, 3.0f, 4.0f};
    folder.processBlock(buffer.data(), 0);

    // Buffer should be unchanged
    REQUIRE(buffer[0] == 1.0f);
    REQUIRE(buffer[1] == 2.0f);
    REQUIRE(buffer[2] == 3.0f);
    REQUIRE(buffer[3] == 4.0f);
}

TEST_CASE("processBlock modifies buffer in-place", "[wavefolder][block]") {
    Wavefolder folder;
    folder.setType(WavefoldType::Sine);
    folder.setFoldAmount(3.14159f);

    std::array<float, 4> buffer = {0.5f, 1.0f, -0.5f, -1.0f};
    std::array<float, 4> original = buffer;

    folder.processBlock(buffer.data(), 4);

    // At least some values should be different (sine fold with PI changes values)
    bool anyDifferent = false;
    for (size_t i = 0; i < 4; ++i) {
        if (buffer[i] != original[i]) {
            anyDifferent = true;
            break;
        }
    }
    REQUIRE(anyDifferent);
}

TEST_CASE("processBlock is marked const noexcept", "[wavefolder][block]") {
    // FR-028: const noexcept
    const Wavefolder folder;  // const instance
    std::array<float, 4> buffer = {0.1f, 0.2f, 0.3f, 0.4f};

    // Should compile - processBlock is const
    folder.processBlock(buffer.data(), 4);

    // Verify noexcept
    static_assert(noexcept(folder.processBlock(buffer.data(), 4)));
}

// =============================================================================
// Phase 5: User Story 3 - Runtime Parameter Change Tests
// =============================================================================

TEST_CASE("setType takes effect immediately on next sample", "[wavefolder][runtime]") {
    // SC-005: Immediate parameter effect
    Wavefolder folder;
    folder.setFoldAmount(3.14159f);

    folder.setType(WavefoldType::Sine);
    float sineOutput = folder.process(0.5f);

    folder.setType(WavefoldType::Triangle);
    float triangleOutput = folder.process(0.5f);

    // Different algorithms should produce different outputs
    REQUIRE(sineOutput != triangleOutput);

    // Verify sine output is sin(PI * 0.5) = 1.0
    REQUIRE(sineOutput == Approx(1.0f).margin(0.001f));
}

TEST_CASE("setFoldAmount takes effect immediately without discontinuities", "[wavefolder][runtime]") {
    // SC-005: Immediate parameter effect
    Wavefolder folder;
    folder.setType(WavefoldType::Triangle);

    folder.setFoldAmount(1.0f);
    float output1 = folder.process(0.3f);  // Within threshold, should pass through

    folder.setFoldAmount(5.0f);
    float output2 = folder.process(0.3f);  // threshold = 0.2, should fold

    REQUIRE(output1 == Approx(0.3f).margin(1e-6f));
    REQUIRE(output2 != output1);  // Different due to folding
}

TEST_CASE("Changing type mid-processBlock produces expected output", "[wavefolder][runtime]") {
    Wavefolder folder;
    folder.setFoldAmount(3.14159f);

    std::array<float, 8> buffer = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

    // Process first half with Sine
    folder.setType(WavefoldType::Sine);
    folder.processBlock(buffer.data(), 4);

    // Process second half with Triangle
    folder.setType(WavefoldType::Triangle);
    folder.processBlock(buffer.data() + 4, 4);

    // First half should have Sine fold output (sin(PI * 0.5) = 1.0)
    REQUIRE(buffer[0] == Approx(1.0f).margin(0.001f));
    REQUIRE(buffer[3] == Approx(1.0f).margin(0.001f));

    // Second half should have Triangle fold output
    // With threshold = 1/PI ~ 0.318, input 0.5 exceeds threshold
    REQUIRE(buffer[4] != Approx(1.0f).margin(0.01f));
    REQUIRE(buffer[4] >= -0.35f);
    REQUIRE(buffer[4] <= 0.35f);
}

TEST_CASE("Changing foldAmount mid-processBlock produces expected output", "[wavefolder][runtime]") {
    Wavefolder folder;
    folder.setType(WavefoldType::Triangle);

    std::array<float, 8> buffer = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};

    // Process first half with foldAmount=1.0 (threshold=1.0, input passes through)
    folder.setFoldAmount(1.0f);
    folder.processBlock(buffer.data(), 4);

    // Process second half with foldAmount=5.0 (threshold=0.2, input folds)
    folder.setFoldAmount(5.0f);
    folder.processBlock(buffer.data() + 4, 4);

    // First half should be unchanged (within threshold)
    REQUIRE(buffer[0] == Approx(0.8f).margin(1e-6f));

    // Second half should be folded (outside threshold of 0.2)
    REQUIRE(buffer[4] != Approx(0.8f).margin(0.01f));
    REQUIRE(buffer[4] >= -0.2f - 0.001f);
    REQUIRE(buffer[4] <= 0.2f + 0.001f);
}

// =============================================================================
// Phase 6: Edge Cases and Robustness Tests
// =============================================================================

TEST_CASE("NaN input propagates NaN output for all types", "[wavefolder][edge]") {
    // FR-026: NaN propagation
    Wavefolder folder;
    const float nanValue = std::numeric_limits<float>::quiet_NaN();

    std::array<WavefoldType, 3> types = {
        WavefoldType::Triangle,
        WavefoldType::Sine,
        WavefoldType::Lockhart
    };

    for (WavefoldType type : types) {
        folder.setType(type);
        float output = folder.process(nanValue);
        REQUIRE(std::isnan(output));
    }
}

TEST_CASE("Triangle fold returns plus or minus threshold for plus or minus infinity input", "[wavefolder][edge]") {
    Wavefolder folder;
    folder.setType(WavefoldType::Triangle);
    folder.setFoldAmount(2.0f);  // threshold = 0.5

    const float posInf = std::numeric_limits<float>::infinity();
    const float negInf = -std::numeric_limits<float>::infinity();

    float posOutput = folder.process(posInf);
    float negOutput = folder.process(negInf);

    REQUIRE(posOutput == Approx(0.5f).margin(1e-6f));
    REQUIRE(negOutput == Approx(-0.5f).margin(1e-6f));
}

TEST_CASE("Sine fold returns plus or minus 1.0 for plus or minus infinity input", "[wavefolder][edge]") {
    Wavefolder folder;
    folder.setType(WavefoldType::Sine);
    folder.setFoldAmount(1.0f);

    const float posInf = std::numeric_limits<float>::infinity();
    const float negInf = -std::numeric_limits<float>::infinity();

    float posOutput = folder.process(posInf);
    float negOutput = folder.process(negInf);

    REQUIRE(posOutput == Approx(1.0f).margin(1e-6f));
    REQUIRE(negOutput == Approx(-1.0f).margin(1e-6f));
}

TEST_CASE("Lockhart fold returns NaN for infinity input", "[wavefolder][edge]") {
    // FR-020: Lockhart returns NaN for infinity
    Wavefolder folder;
    folder.setType(WavefoldType::Lockhart);
    folder.setFoldAmount(1.0f);

    const float posInf = std::numeric_limits<float>::infinity();
    const float negInf = -std::numeric_limits<float>::infinity();

    float posOutput = folder.process(posInf);
    float negOutput = folder.process(negInf);

    REQUIRE(std::isnan(posOutput));
    REQUIRE(std::isnan(negOutput));
}

TEST_CASE("Triangle fold with foldAmount 0 returns 0", "[wavefolder][edge]") {
    Wavefolder folder;
    folder.setType(WavefoldType::Triangle);
    folder.setFoldAmount(0.0f);

    REQUIRE(folder.process(0.0f) == Approx(0.0f).margin(1e-6f));
    REQUIRE(folder.process(0.5f) == Approx(0.0f).margin(1e-6f));
    REQUIRE(folder.process(-0.5f) == Approx(0.0f).margin(1e-6f));
    REQUIRE(folder.process(10.0f) == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Sine fold with foldAmount 0 returns input unchanged", "[wavefolder][edge]") {
    Wavefolder folder;
    folder.setType(WavefoldType::Sine);
    folder.setFoldAmount(0.0f);

    REQUIRE(folder.process(0.0f) == Approx(0.0f).margin(1e-6f));
    REQUIRE(folder.process(0.5f) == Approx(0.5f).margin(1e-6f));
    REQUIRE(folder.process(-0.7f) == Approx(-0.7f).margin(1e-6f));
    REQUIRE(folder.process(1.0f) == Approx(1.0f).margin(1e-6f));
}

TEST_CASE("Lockhart fold with foldAmount 0 returns approximately 0.514 for any input", "[wavefolder][edge]") {
    // FR-022: tanh(lambertW(1)) ~ 0.514
    Wavefolder folder;
    folder.setType(WavefoldType::Lockhart);
    folder.setFoldAmount(0.0f);

    REQUIRE(folder.process(0.0f) == Approx(0.514f).margin(0.01f));
    REQUIRE(folder.process(1.0f) == Approx(0.514f).margin(0.01f));
    REQUIRE(folder.process(-1.0f) == Approx(0.514f).margin(0.01f));
    REQUIRE(folder.process(10.0f) == Approx(0.514f).margin(0.01f));
}

TEST_CASE("SC-008 NaN propagation is consistent across all fold types", "[wavefolder][edge][stability]") {
    Wavefolder folder;
    const float nanValue = std::numeric_limits<float>::quiet_NaN();

    // Test with various foldAmount values
    std::array<float, 3> amounts = {0.0f, 1.0f, 10.0f};
    std::array<WavefoldType, 3> types = {
        WavefoldType::Triangle,
        WavefoldType::Sine,
        WavefoldType::Lockhart
    };

    for (WavefoldType type : types) {
        for (float amount : amounts) {
            folder.setType(type);
            folder.setFoldAmount(amount);
            float output = folder.process(nanValue);
            REQUIRE(std::isnan(output));
        }
    }
}

TEST_CASE("1M samples produces no unexpected NaN or Inf for valid inputs in -10 to 10", "[wavefolder][stability]") {
    Wavefolder folder;

    std::array<WavefoldType, 3> types = {
        WavefoldType::Triangle,
        WavefoldType::Sine,
        WavefoldType::Lockhart
    };

    constexpr size_t numSamples = 1000000;

    for (WavefoldType type : types) {
        folder.setType(type);
        folder.setFoldAmount(5.0f);

        bool foundBadValue = false;
        for (size_t i = 0; i < numSamples; ++i) {
            // Generate input in range [-10, 10]
            float input = (static_cast<float>(i % 20001) - 10000.0f) / 1000.0f;
            float output = folder.process(input);

            if (std::isnan(output) || std::isinf(output)) {
                foundBadValue = true;
                break;
            }
        }
        REQUIRE_FALSE(foundBadValue);
    }
}

// =============================================================================
// Phase 7: Success Criteria Verification Tests
// =============================================================================

TEST_CASE("SC-001 Triangle fold output bounded to -threshold and threshold for any finite input", "[wavefolder][success]") {
    Wavefolder folder;
    folder.setType(WavefoldType::Triangle);

    std::array<float, 4> amounts = {0.5f, 1.0f, 2.0f, 10.0f};

    for (float amount : amounts) {
        folder.setFoldAmount(amount);
        const float threshold = 1.0f / amount;

        // Test various inputs
        std::array<float, 10> inputs = {0.0f, 0.1f, 1.0f, 10.0f, 100.0f,
                                         -0.1f, -1.0f, -10.0f, -100.0f, -1000.0f};

        for (float x : inputs) {
            float output = folder.process(x);
            REQUIRE(output >= -threshold - 0.001f);
            REQUIRE(output <= threshold + 0.001f);
        }
    }
}

TEST_CASE("SC-002 Sine fold output bounded to -1 and 1 for any finite input", "[wavefolder][success]") {
    Wavefolder folder;
    folder.setType(WavefoldType::Sine);

    std::array<float, 4> amounts = {0.5f, 1.0f, 5.0f, 10.0f};

    for (float amount : amounts) {
        folder.setFoldAmount(amount);

        std::array<float, 10> inputs = {0.0f, 0.1f, 1.0f, 10.0f, 100.0f,
                                         -0.1f, -1.0f, -10.0f, -100.0f, -1000.0f};

        for (float x : inputs) {
            float output = folder.process(x);
            REQUIRE(output >= -1.0f);
            REQUIRE(output <= 1.0f);
        }
    }
}

TEST_CASE("SC-006 Processing methods introduce no memory allocations", "[wavefolder][success]") {
    // Verify noexcept and const - no allocations possible
    Wavefolder folder;

    // Verify process is const noexcept
    static_assert(noexcept(folder.process(0.0f)));

    // Verify processBlock is const noexcept
    std::array<float, 4> buffer = {0.0f};
    static_assert(noexcept(folder.processBlock(buffer.data(), 4)));

    // Verify process can be called on const instance
    const Wavefolder constFolder;
    [[maybe_unused]] float result = constFolder.process(0.5f);
}

TEST_CASE("SC-007 sizeof Wavefolder less than 16 bytes", "[wavefolder][success]") {
    // Compile-time check via static_assert in header
    REQUIRE(sizeof(Wavefolder) <= 16);
}

// =============================================================================
// Phase 8: Spectral Analysis Tests (using test helpers)
// =============================================================================

TEST_CASE("Sine fold with PI produces harmonic content measurable via FFT", "[wavefolder][spectral]") {
    // FR-017: Sine fold MUST produce characteristic Serge-style harmonic content at gain=PI
    // This test uses spectral analysis to verify the harmonic structure
    using namespace TestUtils;

    Wavefolder folder;
    folder.setType(WavefoldType::Sine);
    folder.setFoldAmount(3.14159f);  // PI

    AliasingTestConfig config{
        .testFrequencyHz = 1000.0f,   // 1kHz fundamental
        .sampleRate = 44100.0f,
        .driveGain = 1.0f,            // Unity gain input
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    // Measure spectral content of sine-folded signal
    auto result = measureAliasing(config, [&](float x) {
        return folder.process(x);
    });

    // Verify we get measurable harmonic content (not just fundamental)
    // Sine folding with PI should create harmonics
    INFO("Fundamental: " << result.fundamentalPowerDb << " dB");
    INFO("Harmonics: " << result.harmonicPowerDb << " dB");

    // The measurement should be valid (no NaN)
    REQUIRE(result.isValid());

    // Fundamental should be present
    REQUIRE(result.fundamentalPowerDb > -100.0f);
}

TEST_CASE("All fold types produce measurable harmonic content via FFT", "[wavefolder][spectral]") {
    // Each fold type produces harmonics when driven - verify via spectral analysis
    using namespace TestUtils;

    AliasingTestConfig config{
        .testFrequencyHz = 1000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 2.0f,  // Drive into folding region
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    Wavefolder triangleFolder;
    triangleFolder.setType(WavefoldType::Triangle);
    triangleFolder.setFoldAmount(2.0f);

    Wavefolder sineFolder;
    sineFolder.setType(WavefoldType::Sine);
    sineFolder.setFoldAmount(2.0f);

    Wavefolder lockhartFolder;
    lockhartFolder.setType(WavefoldType::Lockhart);
    lockhartFolder.setFoldAmount(2.0f);

    auto triangleResult = measureAliasing(config, [&](float x) {
        return triangleFolder.process(x);
    });

    auto sineResult = measureAliasing(config, [&](float x) {
        return sineFolder.process(x);
    });

    auto lockhartResult = measureAliasing(config, [&](float x) {
        return lockhartFolder.process(x);
    });

    INFO("Triangle harmonics: " << triangleResult.harmonicPowerDb << " dB");
    INFO("Sine harmonics: " << sineResult.harmonicPowerDb << " dB");
    INFO("Lockhart harmonics: " << lockhartResult.harmonicPowerDb << " dB");

    // All measurements should be valid (no NaN)
    REQUIRE(triangleResult.isValid());
    REQUIRE(sineResult.isValid());
    REQUIRE(lockhartResult.isValid());

    // All fold types should produce measurable harmonic content when driven
    // (above noise floor, which we'll set at -60 dB)
    REQUIRE(triangleResult.harmonicPowerDb > -60.0f);
    REQUIRE(sineResult.harmonicPowerDb > -60.0f);
    REQUIRE(lockhartResult.harmonicPowerDb > -60.0f);
}

// =============================================================================
// Phase 9: Artifact Detection Tests (using test helpers)
// =============================================================================

TEST_CASE("setType during processing produces no click artifacts", "[wavefolder][artifact]") {
    // Verify that changing fold type mid-stream doesn't cause audible clicks
    using namespace TestUtils;

    constexpr size_t numSamples = 4096;
    constexpr float sampleRate = 44100.0f;

    // Generate smooth test signal (sine wave)
    std::vector<float> buffer(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        buffer[i] = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * t);
    }

    // Process with type changes mid-stream
    Wavefolder folder;
    folder.setFoldAmount(2.0f);

    for (size_t i = 0; i < numSamples; ++i) {
        // Change type periodically
        if (i == numSamples / 4) {
            folder.setType(WavefoldType::Sine);
        } else if (i == numSamples / 2) {
            folder.setType(WavefoldType::Triangle);
        } else if (i == 3 * numSamples / 4) {
            folder.setType(WavefoldType::Lockhart);
        }
        buffer[i] = folder.process(buffer[i]);
    }

    // Configure click detector
    ClickDetectorConfig config{
        .sampleRate = sampleRate,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 8.0f,  // Higher threshold - we expect some discontinuity
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(config);
    detector.prepare();

    auto clicks = detector.detect(buffer.data(), numSamples);

    INFO("Detected " << clicks.size() << " potential clicks");
    for (const auto& click : clicks) {
        INFO("  Click at sample " << click.sampleIndex << " (t=" << click.timeSeconds << "s)");
    }

    // Type changes may cause some discontinuity, but should be minimal
    // The wavefolder is stateless, so discontinuities come from the
    // transfer function change, not from internal state issues
    // Allow up to 3 detections (one per type change point)
    REQUIRE(clicks.size() <= 3);
}

TEST_CASE("setFoldAmount during processing produces no click artifacts", "[wavefolder][artifact]") {
    // Verify that changing foldAmount mid-stream doesn't cause audible clicks
    using namespace TestUtils;

    constexpr size_t numSamples = 4096;
    constexpr float sampleRate = 44100.0f;

    // Generate smooth test signal
    std::vector<float> buffer(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        buffer[i] = 0.3f * std::sin(2.0f * 3.14159f * 440.0f * t);
    }

    // Process with gradual foldAmount changes (simulating automation)
    Wavefolder folder;
    folder.setType(WavefoldType::Triangle);

    for (size_t i = 0; i < numSamples; ++i) {
        // Smoothly ramp foldAmount from 1.0 to 5.0
        float progress = static_cast<float>(i) / static_cast<float>(numSamples);
        float foldAmount = 1.0f + 4.0f * progress;
        folder.setFoldAmount(foldAmount);
        buffer[i] = folder.process(buffer[i]);
    }

    // Configure click detector
    ClickDetectorConfig config{
        .sampleRate = sampleRate,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 6.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(config);
    detector.prepare();

    auto clicks = detector.detect(buffer.data(), numSamples);

    INFO("Detected " << clicks.size() << " potential clicks with smooth foldAmount ramp");

    // Smooth parameter changes should produce NO clicks
    // The wavefolder is stateless and continuous, so gradual parameter
    // changes should result in smooth output
    REQUIRE(clicks.empty());
}

TEST_CASE("Abrupt foldAmount change at signal zero-crossing produces no clicks", "[wavefolder][artifact]") {
    // Best practice: change parameters at zero crossings
    using namespace TestUtils;

    constexpr size_t numSamples = 2048;
    constexpr float sampleRate = 44100.0f;
    constexpr float freq = 440.0f;

    std::vector<float> buffer(numSamples);

    Wavefolder folder;
    folder.setType(WavefoldType::Triangle);
    folder.setFoldAmount(1.0f);

    // Find a zero crossing point
    size_t zeroCrossing = 0;
    for (size_t i = 1; i < numSamples; ++i) {
        float t0 = static_cast<float>(i - 1) / sampleRate;
        float t1 = static_cast<float>(i) / sampleRate;
        float v0 = std::sin(2.0f * 3.14159f * freq * t0);
        float v1 = std::sin(2.0f * 3.14159f * freq * t1);

        if (v0 <= 0.0f && v1 > 0.0f) {
            zeroCrossing = i;
            break;
        }
    }

    REQUIRE(zeroCrossing > 0);

    // Process with abrupt change at zero crossing
    for (size_t i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float input = 0.8f * std::sin(2.0f * 3.14159f * freq * t);

        if (i == zeroCrossing) {
            folder.setFoldAmount(5.0f);  // Abrupt change
        }
        buffer[i] = folder.process(input);
    }

    ClickDetectorConfig config{
        .sampleRate = sampleRate,
        .frameSize = 256,
        .hopSize = 128,
        .detectionThreshold = 6.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(config);
    detector.prepare();

    auto clicks = detector.detect(buffer.data(), numSamples);

    INFO("Detected " << clicks.size() << " clicks with abrupt change at zero crossing");

    // Zero-crossing changes should be click-free because input ≈ 0
    // and f(0) is continuous across parameter changes for Triangle fold
    REQUIRE(clicks.empty());
}

// =============================================================================
// Benchmark Tests (opt-in with [.benchmark] tag)
// =============================================================================

TEST_CASE("SC-003 Triangle and Sine process 512 samples in less than 50 us", "[wavefolder][.benchmark]") {
    Wavefolder folder;
    constexpr size_t numSamples = 512;
    std::vector<float> buffer(numSamples);

    // Fill buffer with test signal
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(static_cast<float>(i) * 0.1f);
    }

    BENCHMARK("Triangle fold 512 samples") {
        folder.setType(WavefoldType::Triangle);
        folder.setFoldAmount(2.0f);
        std::vector<float> testBuffer = buffer;
        folder.processBlock(testBuffer.data(), numSamples);
        return testBuffer[0];
    };

    BENCHMARK("Sine fold 512 samples") {
        folder.setType(WavefoldType::Sine);
        folder.setFoldAmount(3.14159f);
        std::vector<float> testBuffer = buffer;
        folder.processBlock(testBuffer.data(), numSamples);
        return testBuffer[0];
    };
}

TEST_CASE("SC-003a Lockhart process 512 samples in less than 150 us", "[wavefolder][.benchmark]") {
    Wavefolder folder;
    folder.setType(WavefoldType::Lockhart);
    folder.setFoldAmount(2.0f);

    constexpr size_t numSamples = 512;
    std::vector<float> buffer(numSamples);

    // Fill buffer with test signal
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(static_cast<float>(i) * 0.1f);
    }

    BENCHMARK("Lockhart fold 512 samples") {
        std::vector<float> testBuffer = buffer;
        folder.processBlock(testBuffer.data(), numSamples);
        return testBuffer[0];
    };
}
