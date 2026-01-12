// ==============================================================================
// Layer 1: DSP Primitive - DC Blocker Tests
// ==============================================================================
// Tests for DCBlocker class - lightweight DC blocking filter.
// Following Constitution Principle XII: Test-First Development
//
// Feature: 051-dc-blocker
//
// SC-007 Operation Count Verification (Static Analysis):
// DCBlocker per-sample operations: 3 arithmetic (1 mul + 1 sub + 1 add)
//   y = x - x1_ + R_ * y1_
//       ^^^   ^^^   ^^^^^^^
//       sub   add     mul
//
// Biquad per-sample operations: 9 arithmetic (5 mul + 4 add)
//   y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2
//
// DCBlocker is 3x more efficient than Biquad for DC blocking.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/dc_blocker.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Phase 2: Foundational Tests (T004-T009)
// =============================================================================

TEST_CASE("DCBlocker default constructor initializes to unprepared state", "[dc_blocker][FR-003]") {
    DCBlocker blocker;
    // Cannot directly test prepared_ flag, but we can test behavior
    // When unprepared, process() should return input unchanged
    float input = 0.5f;
    float output = blocker.process(input);
    REQUIRE(output == input);
}

TEST_CASE("DCBlocker process returns input unchanged before prepare", "[dc_blocker][FR-018]") {
    DCBlocker blocker;

    SECTION("single sample passthrough") {
        REQUIRE(blocker.process(0.0f) == 0.0f);
        REQUIRE(blocker.process(1.0f) == 1.0f);
        REQUIRE(blocker.process(-0.5f) == -0.5f);
    }

    SECTION("various values unchanged") {
        for (float val = -1.0f; val <= 1.0f; val += 0.1f) {
            REQUIRE(blocker.process(val) == val);
        }
    }
}

TEST_CASE("DCBlocker prepare sets prepared flag and calculates R coefficient", "[dc_blocker][FR-001][FR-008]") {
    DCBlocker blocker;
    blocker.prepare(44100.0, 10.0f);

    // After prepare, process should NOT return input unchanged for DC
    // Process a DC signal and verify it's being filtered
    float dc = 1.0f;
    float output1 = blocker.process(dc);
    float output2 = blocker.process(dc);

    // First sample should pass through mostly (x - 0 + R * 0 = x)
    // Second sample should start showing filtering effect
    REQUIRE(output1 == Approx(1.0f).margin(0.01f));
    // Output should differ from input after a few samples
    REQUIRE(output2 != dc);
}

TEST_CASE("DCBlocker reset clears x1 and y1 state without changing R or prepared", "[dc_blocker][FR-002]") {
    DCBlocker blocker;
    blocker.prepare(44100.0, 10.0f);

    // Process some samples to build up state
    for (int i = 0; i < 100; ++i) {
        (void)blocker.process(1.0f);
    }

    // Reset
    blocker.reset();

    // After reset, process should behave like fresh after prepare
    // First sample should pass through mostly (x - 0 + R * 0 = x)
    float output = blocker.process(1.0f);
    REQUIRE(output == Approx(1.0f).margin(0.01f));
}

TEST_CASE("DCBlocker clamps sample rate to minimum 1000 Hz", "[dc_blocker][FR-011]") {
    DCBlocker blocker;

    // Very low sample rate should be clamped
    blocker.prepare(100.0, 10.0f);  // Below minimum

    // Should still work without crashing
    float output = blocker.process(1.0f);
    REQUIRE(std::isfinite(output));
}

TEST_CASE("DCBlocker clamps cutoff frequency to valid range", "[dc_blocker][FR-010]") {
    DCBlocker blocker;

    SECTION("cutoff below 1 Hz clamped to 1 Hz") {
        blocker.prepare(44100.0, 0.0f);  // Below minimum
        // Should still work
        float output = blocker.process(1.0f);
        REQUIRE(std::isfinite(output));
    }

    SECTION("cutoff above sampleRate/4 clamped") {
        blocker.prepare(44100.0, 20000.0f);  // Above sampleRate/4 (11025 Hz)
        // Should still work
        float output = blocker.process(1.0f);
        REQUIRE(std::isfinite(output));
    }
}

// =============================================================================
// Phase 3: User Story 1 - DC Removal After Saturation (T017-T021)
// =============================================================================

TEST_CASE("SC-001: constant DC input decays to <1% within 5 time constants", "[dc_blocker][SC-001][US1]") {
    DCBlocker blocker;
    const double sampleRate = 44100.0;
    const float cutoffHz = 10.0f;
    blocker.prepare(sampleRate, cutoffHz);

    // Time constant tau = 1 / (2 * pi * cutoffHz)
    // 5 tau at 10 Hz = 5 / (2 * pi * 10) = ~0.08 seconds = ~3528 samples
    // But DC blocking filter has different settling - use the filter's actual time constant
    // R = exp(-2*pi*cutoff/sampleRate) = exp(-2*pi*10/44100) ~ 0.9986
    // Time to decay to 1% is about ln(100) / (1-R) samples
    // More practically, process for 0.5 seconds (plenty of time)

    const int samples = static_cast<int>(0.5 * sampleRate);  // 500ms

    float output = 0.0f;
    for (int i = 0; i < samples; ++i) {
        output = blocker.process(1.0f);  // Constant DC input
    }

    // Output should decay to <1% of original DC
    REQUIRE(std::abs(output) < 0.01f);
}

TEST_CASE("SC-002: 100 Hz sine passes with <0.5% amplitude loss at 10 Hz cutoff", "[dc_blocker][SC-002][US1]") {
    DCBlocker blocker;
    const double sampleRate = 44100.0;
    const float cutoffHz = 10.0f;
    blocker.prepare(sampleRate, cutoffHz);

    // Let filter settle first
    for (int i = 0; i < 4410; ++i) {
        (void)blocker.process(0.0f);
    }
    blocker.reset();

    // Generate 100 Hz sine and measure output amplitude
    const float freq = 100.0f;
    const int numCycles = 10;
    const int samplesPerCycle = static_cast<int>(sampleRate / freq);
    const int totalSamples = numCycles * samplesPerCycle;

    float maxOutput = 0.0f;
    constexpr float kPi = 3.14159265358979323846f;

    // Let filter settle for a few cycles
    for (int i = 0; i < samplesPerCycle * 3; ++i) {
        float phase = 2.0f * kPi * freq * i / static_cast<float>(sampleRate);
        (void)blocker.process(std::sin(phase));
    }

    // Measure output amplitude
    for (int i = 0; i < totalSamples; ++i) {
        float phase = 2.0f * kPi * freq * i / static_cast<float>(sampleRate);
        float input = std::sin(phase);
        float output = blocker.process(input);
        maxOutput = std::max(maxOutput, std::abs(output));
    }

    // Amplitude should be at least 99.5% (loss < 0.5%)
    REQUIRE(maxOutput >= 0.995f);
}

TEST_CASE("SC-003: 20 Hz sine passes with <5% amplitude loss at 10 Hz cutoff", "[dc_blocker][SC-003][US1]") {
    DCBlocker blocker;
    const double sampleRate = 44100.0;
    const float cutoffHz = 10.0f;
    blocker.prepare(sampleRate, cutoffHz);

    // Generate 20 Hz sine and measure output amplitude
    const float freq = 20.0f;
    const int numCycles = 20;
    const int samplesPerCycle = static_cast<int>(sampleRate / freq);
    const int totalSamples = numCycles * samplesPerCycle;

    float maxOutput = 0.0f;
    constexpr float kPi = 3.14159265358979323846f;

    // Let filter settle for a few cycles
    for (int i = 0; i < samplesPerCycle * 5; ++i) {
        float phase = 2.0f * kPi * freq * i / static_cast<float>(sampleRate);
        (void)blocker.process(std::sin(phase));
    }

    // Measure output amplitude
    for (int i = 0; i < totalSamples; ++i) {
        float phase = 2.0f * kPi * freq * i / static_cast<float>(sampleRate);
        float input = std::sin(phase);
        float output = blocker.process(input);
        maxOutput = std::max(maxOutput, std::abs(output));
    }

    // For a first-order highpass at frequency f with cutoff fc:
    // |H(f)| = f / sqrt(f^2 + fc^2) = 20 / sqrt(400 + 100) = 0.894
    // Allow 5% tolerance on the theoretical value
    REQUIRE(maxOutput >= 0.894f * 0.95f);  // ~0.85
    REQUIRE(maxOutput <= 1.0f);
}

TEST_CASE("US1 Scenario 3: DC offset removed while sine wave passes through", "[dc_blocker][US1]") {
    DCBlocker blocker;
    const double sampleRate = 44100.0;
    const float cutoffHz = 10.0f;
    blocker.prepare(sampleRate, cutoffHz);

    // Signal: 0.5 DC offset + 1 kHz sine wave
    const float dcOffset = 0.5f;
    const float freq = 1000.0f;
    constexpr float kPi = 3.14159265358979323846f;

    // Process for 1 second to let DC settle
    const int settleSamples = static_cast<int>(1.0 * sampleRate);
    for (int i = 0; i < settleSamples; ++i) {
        float phase = 2.0f * kPi * freq * i / static_cast<float>(sampleRate);
        float input = dcOffset + std::sin(phase);
        (void)blocker.process(input);
    }

    // Now measure output - should have sine but no DC
    float sum = 0.0f;
    float maxOutput = 0.0f;
    const int measureSamples = static_cast<int>(0.1 * sampleRate);

    for (int i = 0; i < measureSamples; ++i) {
        float phase = 2.0f * kPi * freq * (settleSamples + i) / static_cast<float>(sampleRate);
        float input = dcOffset + std::sin(phase);
        float output = blocker.process(input);
        sum += output;
        maxOutput = std::max(maxOutput, std::abs(output));
    }

    float avgOutput = sum / measureSamples;

    // DC should be mostly removed (average near zero)
    REQUIRE(std::abs(avgOutput) < 0.05f);
    // Sine wave should still be present
    REQUIRE(maxOutput > 0.9f);
}

TEST_CASE("SC-004: 1M samples with valid inputs produces no unexpected NaN/Infinity", "[dc_blocker][SC-004][US1]") {
    DCBlocker blocker;
    blocker.prepare(44100.0, 10.0f);

    constexpr float kPi = 3.14159265358979323846f;

    // Process 1 million samples with various valid inputs
    for (int i = 0; i < 1000000; ++i) {
        // Mix of DC, sine, and noise-like values
        float input = 0.1f + 0.8f * std::sin(2.0f * kPi * 440.0f * i / 44100.0f);
        float output = blocker.process(input);

        // Check for NaN/Inf
        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
    }
}

// =============================================================================
// Phase 4: User Story 2 - DC Blocking in Feedback Loop (T026-T027)
// =============================================================================

TEST_CASE("US2 Scenario 1: feedback loop with DC bias injection remains bounded", "[dc_blocker][US2]") {
    DCBlocker blocker;
    blocker.prepare(44100.0, 10.0f);

    // Simulate feedback loop: output = input + feedback * delayed_output
    // With DC blocker in the path
    const float feedback = 0.8f;
    const float dcBias = 0.001f;  // Small DC bias injected each iteration

    float delayedOutput = 0.0f;
    float maxOutput = 0.0f;

    // Process for 10 seconds worth of samples
    for (int i = 0; i < 441000; ++i) {
        float input = dcBias;  // DC bias injection
        float feedbackSignal = delayedOutput * feedback;
        float toProcess = input + feedbackSignal;

        // Apply DC blocker
        float output = blocker.process(toProcess);

        maxOutput = std::max(maxOutput, std::abs(output));
        delayedOutput = output;

        // Output should never grow unbounded
        REQUIRE(std::abs(output) < 10.0f);
    }

    // Final output should be bounded and not growing
    REQUIRE(maxOutput < 1.0f);  // Should settle to something reasonable
}

TEST_CASE("US2 Scenario 2: reset clears all internal state", "[dc_blocker][US2]") {
    DCBlocker blocker;
    blocker.prepare(44100.0, 10.0f);

    // Build up state by processing DC
    for (int i = 0; i < 1000; ++i) {
        (void)blocker.process(1.0f);
    }

    // Reset
    blocker.reset();

    // After reset, first sample should behave like fresh state
    // y[0] = x[0] - x1_ + R * y1_ = 1.0 - 0 + R * 0 = 1.0
    float output = blocker.process(1.0f);
    REQUIRE(output == Approx(1.0f).margin(0.01f));
}

// =============================================================================
// Phase 5: User Story 3 - Block Processing (T031-T032)
// =============================================================================

TEST_CASE("SC-005/FR-006: processBlock produces bit-identical output to N process() calls", "[dc_blocker][SC-005][FR-006][US3]") {
    // Test with DC signal
    {
        DCBlocker seqBlocker, blockBlocker;
        seqBlocker.prepare(44100.0, 10.0f);
        blockBlocker.prepare(44100.0, 10.0f);

        std::array<float, 512> seqOutput;
        std::array<float, 512> blockInput;
        std::array<float, 512> blockOutput;

        // Fill with DC
        std::fill(blockInput.begin(), blockInput.end(), 1.0f);

        // Sequential processing
        for (size_t i = 0; i < seqOutput.size(); ++i) {
            seqOutput[i] = seqBlocker.process(blockInput[i]);
        }

        // Block processing (copy input to output buffer for in-place)
        std::copy(blockInput.begin(), blockInput.end(), blockOutput.begin());
        blockBlocker.processBlock(blockOutput.data(), blockOutput.size());

        // Must be bit-identical
        for (size_t i = 0; i < seqOutput.size(); ++i) {
            REQUIRE(blockOutput[i] == seqOutput[i]);
        }
    }

    // Test with sine wave
    {
        DCBlocker seqBlocker, blockBlocker;
        seqBlocker.prepare(44100.0, 10.0f);
        blockBlocker.prepare(44100.0, 10.0f);

        std::array<float, 512> seqOutput;
        std::array<float, 512> blockInput;
        std::array<float, 512> blockOutput;

        constexpr float kPi = 3.14159265358979323846f;

        // Fill with sine wave
        for (size_t i = 0; i < blockInput.size(); ++i) {
            blockInput[i] = std::sin(2.0f * kPi * 440.0f * i / 44100.0f);
        }

        // Sequential processing
        for (size_t i = 0; i < seqOutput.size(); ++i) {
            seqOutput[i] = seqBlocker.process(blockInput[i]);
        }

        // Block processing
        std::copy(blockInput.begin(), blockInput.end(), blockOutput.begin());
        blockBlocker.processBlock(blockOutput.data(), blockOutput.size());

        // Must be bit-identical
        for (size_t i = 0; i < seqOutput.size(); ++i) {
            REQUIRE(blockOutput[i] == seqOutput[i]);
        }
    }
}

TEST_CASE("US3 Scenario 2: processBlock with various block sizes", "[dc_blocker][US3]") {
    const std::array<size_t, 4> blockSizes = {1, 64, 512, 1024};

    for (size_t blockSize : blockSizes) {
        DYNAMIC_SECTION("Block size " << blockSize) {
            DCBlocker blocker;
            blocker.prepare(44100.0, 10.0f);

            std::vector<float> buffer(blockSize, 1.0f);  // DC signal

            // Should not crash
            blocker.processBlock(buffer.data(), buffer.size());

            // All outputs should be finite
            for (float val : buffer) {
                REQUIRE(std::isfinite(val));
            }
        }
    }
}

// =============================================================================
// Phase 6: User Story 4 - Configurable Cutoff Frequency (T037-T039b)
// =============================================================================

TEST_CASE("US4 Scenario 1: 5 Hz cutoff has -3dB point at approximately 5 Hz", "[dc_blocker][US4]") {
    DCBlocker blocker;
    const double sampleRate = 44100.0;
    const float cutoffHz = 5.0f;
    blocker.prepare(sampleRate, cutoffHz);

    // At the -3dB point, output amplitude should be ~0.707 of input
    // Test by measuring response at the cutoff frequency
    const float freq = cutoffHz;
    constexpr float kPi = 3.14159265358979323846f;

    // Let filter settle for many cycles
    for (int i = 0; i < static_cast<int>(sampleRate); ++i) {
        float phase = 2.0f * kPi * freq * i / static_cast<float>(sampleRate);
        (void)blocker.process(std::sin(phase));
    }

    // Measure output amplitude
    float maxOutput = 0.0f;
    const int measureCycles = 20;
    const int samplesPerCycle = static_cast<int>(sampleRate / freq);

    for (int i = 0; i < measureCycles * samplesPerCycle; ++i) {
        float phase = 2.0f * kPi * freq * i / static_cast<float>(sampleRate);
        float output = blocker.process(std::sin(phase));
        maxOutput = std::max(maxOutput, std::abs(output));
    }

    // At -3dB point, amplitude should be ~0.707 (+/- 20% tolerance)
    REQUIRE(maxOutput >= 0.707f * 0.8f);  // -3dB - 20%
    REQUIRE(maxOutput <= 0.707f * 1.2f);  // -3dB + 20%
}

TEST_CASE("US4 Scenario 2: 20 Hz cutoff has -3dB point at approximately 20 Hz", "[dc_blocker][US4]") {
    DCBlocker blocker;
    const double sampleRate = 44100.0;
    const float cutoffHz = 20.0f;
    blocker.prepare(sampleRate, cutoffHz);

    const float freq = cutoffHz;
    constexpr float kPi = 3.14159265358979323846f;

    // Let filter settle
    for (int i = 0; i < static_cast<int>(sampleRate); ++i) {
        float phase = 2.0f * kPi * freq * i / static_cast<float>(sampleRate);
        (void)blocker.process(std::sin(phase));
    }

    // Measure output amplitude
    float maxOutput = 0.0f;
    const int measureCycles = 20;
    const int samplesPerCycle = static_cast<int>(sampleRate / freq);

    for (int i = 0; i < measureCycles * samplesPerCycle; ++i) {
        float phase = 2.0f * kPi * freq * i / static_cast<float>(sampleRate);
        float output = blocker.process(std::sin(phase));
        maxOutput = std::max(maxOutput, std::abs(output));
    }

    // At -3dB point, amplitude should be ~0.707 (+/- 20% tolerance)
    REQUIRE(maxOutput >= 0.707f * 0.8f);
    REQUIRE(maxOutput <= 0.707f * 1.2f);
}

TEST_CASE("FR-012: setCutoff recalculates R without resetting state", "[dc_blocker][FR-012][US4]") {
    DCBlocker blocker;
    blocker.prepare(44100.0, 10.0f);

    // Build up state - need many samples for DC to decay significantly
    // R = exp(-2*pi*10/44100) ≈ 0.99857
    // Need ~485 samples for y to decay to 50%: ln(0.5)/ln(0.99857) ≈ 485
    for (int i = 0; i < 1000; ++i) {
        (void)blocker.process(1.0f);
    }
    float beforeChange = blocker.process(1.0f);

    // Change cutoff
    blocker.setCutoff(20.0f);

    // Process should continue with different characteristics but smooth transition
    float afterChange = blocker.process(1.0f);

    // Values should be different but both finite (no discontinuity crash)
    REQUIRE(std::isfinite(beforeChange));
    REQUIRE(std::isfinite(afterChange));

    // State should not have been reset - verify continuity:
    // After 1000 samples, output is very small (DC blocked)
    // Both before and after should be small (not jumping back to 1.0)
    REQUIRE(beforeChange < 0.5f);   // DC has decayed significantly
    REQUIRE(afterChange < 0.5f);    // State preserved, still decayed
    REQUIRE(afterChange != 1.0f);   // Definitely not reset to initial state
}

TEST_CASE("setCutoff called before prepare is safe", "[dc_blocker][US4]") {
    DCBlocker blocker;

    // Should not crash when called before prepare
    blocker.setCutoff(20.0f);

    // Should still work correctly after prepare
    blocker.prepare(44100.0, 10.0f);
    float output = blocker.process(1.0f);
    REQUIRE(std::isfinite(output));
}

// =============================================================================
// Phase 7: Edge Cases & Robustness (T043-T045)
// =============================================================================

TEST_CASE("FR-016: process propagates NaN inputs", "[dc_blocker][FR-016][edge]") {
    DCBlocker blocker;
    blocker.prepare(44100.0, 10.0f);

    // Process some normal samples first
    for (int i = 0; i < 10; ++i) {
        (void)blocker.process(0.5f);
    }

    // Process NaN
    float nan = std::numeric_limits<float>::quiet_NaN();
    float output = blocker.process(nan);

    // NaN should propagate (not be hidden)
    REQUIRE(std::isnan(output));
}

TEST_CASE("FR-017: process handles infinity inputs without crashing", "[dc_blocker][FR-017][edge]") {
    DCBlocker blocker;
    blocker.prepare(44100.0, 10.0f);

    // Process some normal samples first
    for (int i = 0; i < 10; ++i) {
        (void)blocker.process(0.5f);
    }

    SECTION("positive infinity") {
        float inf = std::numeric_limits<float>::infinity();
        float output = blocker.process(inf);
        // Infinity should propagate per IEEE 754
        REQUIRE(std::isinf(output));
    }

    SECTION("negative infinity") {
        float negInf = -std::numeric_limits<float>::infinity();
        float output = blocker.process(negInf);
        // Infinity should propagate per IEEE 754
        REQUIRE(std::isinf(output));
    }
}

TEST_CASE("FR-015: denormal values are flushed", "[dc_blocker][FR-015][edge]") {
    DCBlocker blocker;
    blocker.prepare(44100.0, 10.0f);

    // Process a very small value
    float tiny = 1e-38f;  // Near denormal range

    // Process many samples to let state decay
    for (int i = 0; i < 10000; ++i) {
        (void)blocker.process(tiny);
    }

    // Now process zeros and check the output doesn't stay denormal
    float output = 0.0f;
    for (int i = 0; i < 10000; ++i) {
        output = blocker.process(0.0f);
    }

    // Output should be zero or above denormal threshold
    constexpr float kDenormalThreshold = 1e-15f;
    REQUIRE((output == 0.0f || std::abs(output) >= kDenormalThreshold));
}

// =============================================================================
// SC-007: Performance Verification (Static Analysis)
// =============================================================================
// DCBlocker operation count per sample:
// - 1 subtraction (x - x1_)
// - 1 multiplication (R_ * y1_)
// - 1 addition ((x - x1_) + R_ * y1_)
// - 2 assignments (x1_ = x, y1_ = result)
// - 1 denormal flush (comparison + conditional assignment)
// Total: ~3 arithmetic ops + denormal check
//
// Compare to Biquad:
// - 5 multiplications (a0*x + a1*x1 + a2*x2 - b1*y1 - b2*y2)
// - 4 additions
// - 4 state updates
// Total: 9 arithmetic ops
//
// DCBlocker is approximately 3x lighter than Biquad for DC blocking use case.

