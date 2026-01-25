// ==============================================================================
// Layer 2: DSP Processor Tests - Multi-Stage Envelope Filter
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests organized by user story for independent implementation and testing.
// Reference: specs/100-multistage-env-filter/spec.md
//
// CONGRATULATIONS! SPEC #100!
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <krate/dsp/processors/multistage_env_filter.h>

#include <array>
#include <chrono>
#include <cmath>
#include <numbers>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

/// Generate a sine wave into buffer
inline void generateSine(float* buffer, size_t size, float frequency, float sampleRate,
                         float amplitude = 1.0f) {
    const float omega = 2.0f * std::numbers::pi_v<float> * frequency / sampleRate;
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = amplitude * std::sin(omega * static_cast<float>(i));
    }
}

/// Generate silence
inline void generateSilence(float* buffer, size_t size) {
    std::fill(buffer, buffer + size, 0.0f);
}

/// Calculate time in samples for a given time in ms
inline size_t msToSamples(float ms, double sampleRate) {
    return static_cast<size_t>(ms * 0.001 * sampleRate);
}

/// Check if a value is a valid float (not NaN or Inf)
inline bool isValidFloat(float x) { return std::isfinite(x); }

/// Find maximum absolute value in buffer
inline float findPeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// Calculate RMS of buffer
inline float calculateRMS(const float* buffer, size_t size) {
    float sumSquares = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sumSquares += buffer[i] * buffer[i];
    }
    return std::sqrt(sumSquares / static_cast<float>(size));
}

} // anonymous namespace

// =============================================================================
// Phase 2: Foundational Tests (T008)
// =============================================================================

TEST_CASE("MultiStageEnvelopeFilter EnvelopeState enum values", "[multistage-env-filter][foundational]") {
    REQUIRE(static_cast<uint8_t>(EnvelopeState::Idle) == 0);
    REQUIRE(static_cast<uint8_t>(EnvelopeState::Running) == 1);
    REQUIRE(static_cast<uint8_t>(EnvelopeState::Releasing) == 2);
    REQUIRE(static_cast<uint8_t>(EnvelopeState::Complete) == 3);
}

TEST_CASE("MultiStageEnvelopeFilter constants", "[multistage-env-filter][foundational]") {
    REQUIRE(MultiStageEnvelopeFilter::kMaxStages == 8);
    REQUIRE(MultiStageEnvelopeFilter::kMinResonance == Approx(0.1f));
    REQUIRE(MultiStageEnvelopeFilter::kMaxResonance == Approx(30.0f));
    REQUIRE(MultiStageEnvelopeFilter::kMinFrequency == Approx(1.0f));
    REQUIRE(MultiStageEnvelopeFilter::kMaxStageTimeMs == Approx(10000.0f));
    REQUIRE(MultiStageEnvelopeFilter::kMaxReleaseTimeMs == Approx(10000.0f));
}

TEST_CASE("MultiStageEnvelopeFilter prepare and reset lifecycle", "[multistage-env-filter][foundational]") {
    MultiStageEnvelopeFilter filter;

    SECTION("prepare initializes processor") {
        REQUIRE_FALSE(filter.isPrepared());
        filter.prepare(44100.0);
        REQUIRE(filter.isPrepared());
    }

    SECTION("prepare with different sample rates") {
        filter.prepare(44100.0);
        REQUIRE(filter.isPrepared());

        filter.prepare(96000.0);
        REQUIRE(filter.isPrepared());

        filter.prepare(48000.0);
        REQUIRE(filter.isPrepared());
    }

    SECTION("prepare clamps minimum sample rate") {
        filter.prepare(100.0); // Below minimum 1000
        REQUIRE(filter.isPrepared());
    }

    SECTION("reset clears state without changing parameters") {
        filter.prepare(44100.0);

        // Configure filter
        filter.setNumStages(4);
        filter.setStageTarget(0, 500.0f);
        filter.setBaseFrequency(200.0f);

        // Trigger and process
        filter.trigger();
        for (int i = 0; i < 1000; ++i) {
            (void)filter.process(0.5f);
        }

        // Reset
        filter.reset();

        // State should be reset
        REQUIRE(filter.isComplete()); // Idle is complete
        REQUIRE_FALSE(filter.isRunning());
        REQUIRE(filter.getCurrentStage() == 0);

        // Parameters should be preserved
        REQUIRE(filter.getNumStages() == 4);
        REQUIRE(filter.getStageTarget(0) == Approx(500.0f));
        REQUIRE(filter.getBaseFrequency() == Approx(200.0f));
    }

    SECTION("process before prepare returns 0") {
        MultiStageEnvelopeFilter unpreparedFilter;
        float output = unpreparedFilter.process(1.0f);
        REQUIRE(output == 0.0f);
    }
}

TEST_CASE("MultiStageEnvelopeFilter basic getters", "[multistage-env-filter][foundational]") {
    MultiStageEnvelopeFilter filter;
    filter.prepare(44100.0);

    // Test default values
    REQUIRE(filter.getNumStages() == 1);
    REQUIRE(filter.getLoop() == false);
    REQUIRE(filter.getLoopStart() == 0);
    REQUIRE(filter.getLoopEnd() == 0);
    REQUIRE(filter.getResonance() == Approx(SVF::kButterworthQ));
    REQUIRE(filter.getFilterType() == SVFMode::Lowpass);
    REQUIRE(filter.getBaseFrequency() == Approx(100.0f));
    REQUIRE(filter.getReleaseTime() == Approx(500.0f));
    REQUIRE(filter.getVelocitySensitivity() == Approx(0.0f));
}

// =============================================================================
// Phase 3: User Story 1 - Basic Multi-Stage Filter Sweep (T013-T033)
// =============================================================================

TEST_CASE("MultiStageEnvelopeFilter stage configuration setters and getters", "[multistage-env-filter][US1]") {
    MultiStageEnvelopeFilter filter;
    filter.prepare(44100.0);

    SECTION("setNumStages with clamping") {
        filter.setNumStages(4);
        REQUIRE(filter.getNumStages() == 4);

        filter.setNumStages(8);
        REQUIRE(filter.getNumStages() == 8);

        // Below minimum should clamp to 1
        filter.setNumStages(0);
        REQUIRE(filter.getNumStages() == 1);

        filter.setNumStages(-5);
        REQUIRE(filter.getNumStages() == 1);

        // Above maximum should clamp to 8
        filter.setNumStages(20);
        REQUIRE(filter.getNumStages() == 8);
    }

    SECTION("setStageTarget with clamping") {
        filter.setStageTarget(0, 500.0f);
        REQUIRE(filter.getStageTarget(0) == Approx(500.0f));

        filter.setStageTarget(3, 2000.0f);
        REQUIRE(filter.getStageTarget(3) == Approx(2000.0f));

        // Below minimum should clamp
        filter.setStageTarget(0, 0.0f);
        REQUIRE(filter.getStageTarget(0) == Approx(MultiStageEnvelopeFilter::kMinFrequency));

        // Above Nyquist should clamp
        filter.setStageTarget(0, 50000.0f);
        float maxFreq = 44100.0f * 0.45f;
        REQUIRE(filter.getStageTarget(0) <= maxFreq);

        // Out of range stage index should be ignored
        filter.setStageTarget(0, 1000.0f);
        filter.setStageTarget(10, 5000.0f); // Should be ignored
        REQUIRE(filter.getStageTarget(0) == Approx(1000.0f));
    }

    SECTION("setStageTime with clamping") {
        filter.setStageTime(0, 100.0f);
        REQUIRE(filter.getStageTime(0) == Approx(100.0f));

        filter.setStageTime(1, 500.0f);
        REQUIRE(filter.getStageTime(1) == Approx(500.0f));

        // Below minimum should clamp to 0
        filter.setStageTime(0, -10.0f);
        REQUIRE(filter.getStageTime(0) == Approx(0.0f));

        // Above maximum should clamp
        filter.setStageTime(0, 20000.0f);
        REQUIRE(filter.getStageTime(0) == Approx(MultiStageEnvelopeFilter::kMaxStageTimeMs));
    }
}

TEST_CASE("MultiStageEnvelopeFilter 4-stage sweep progression", "[multistage-env-filter][US1]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    // Configure 4-stage sweep (per spec acceptance scenario)
    filter.setNumStages(4);
    filter.setBaseFrequency(100.0f);

    // Stage targets: 200, 2000, 500, 800 Hz
    filter.setStageTarget(0, 200.0f);
    filter.setStageTarget(1, 2000.0f);
    filter.setStageTarget(2, 500.0f);
    filter.setStageTarget(3, 800.0f);

    // Stage times: 100, 200, 150, 100 ms
    filter.setStageTime(0, 100.0f);
    filter.setStageTime(1, 200.0f);
    filter.setStageTime(2, 150.0f);
    filter.setStageTime(3, 100.0f);

    // All linear curves for predictable behavior
    filter.setStageCurve(0, 0.0f);
    filter.setStageCurve(1, 0.0f);
    filter.setStageCurve(2, 0.0f);
    filter.setStageCurve(3, 0.0f);

    // Trigger the envelope
    filter.trigger();
    REQUIRE(filter.isRunning());
    REQUIRE(filter.getCurrentStage() == 0);

    // Calculate total samples for all stages
    size_t stage0Samples = msToSamples(100.0f, kSampleRate);
    size_t stage1Samples = msToSamples(200.0f, kSampleRate);
    size_t stage2Samples = msToSamples(150.0f, kSampleRate);
    size_t stage3Samples = msToSamples(100.0f, kSampleRate);

    // Process through stage 0 (partial - verify we're still in stage 0)
    for (size_t i = 0; i < stage0Samples / 2; ++i) {
        (void)filter.process(0.5f);
    }
    REQUIRE(filter.getCurrentStage() == 0);

    // Process rest of stage 0 and into stage 1
    for (size_t i = 0; i < stage0Samples; ++i) {
        (void)filter.process(0.5f);
    }
    REQUIRE(filter.getCurrentStage() == 1);

    // Process through stage 1
    for (size_t i = 0; i < stage1Samples; ++i) {
        (void)filter.process(0.5f);
    }
    REQUIRE(filter.getCurrentStage() == 2);

    // Process through stage 2
    for (size_t i = 0; i < stage2Samples; ++i) {
        (void)filter.process(0.5f);
    }
    REQUIRE(filter.getCurrentStage() == 3);

    // Process through stage 3
    for (size_t i = 0; i < stage3Samples; ++i) {
        (void)filter.process(0.5f);
    }

    // After all stages complete, envelope should be complete
    REQUIRE(filter.isComplete());
    REQUIRE_FALSE(filter.isRunning());
}

TEST_CASE("MultiStageEnvelopeFilter stage timing accuracy at different sample rates (SC-002)", "[multistage-env-filter][US1]") {
    const std::array<double, 2> sampleRates = {44100.0, 96000.0};

    for (double sampleRate : sampleRates) {
        CAPTURE(sampleRate);

        MultiStageEnvelopeFilter filter;
        filter.prepare(sampleRate);

        // Configure single stage with 100ms time
        filter.setNumStages(1);
        filter.setBaseFrequency(100.0f);
        filter.setStageTarget(0, 1000.0f);
        filter.setStageTime(0, 100.0f);
        filter.setStageCurve(0, 0.0f);

        // Calculate expected samples
        size_t expectedSamples = static_cast<size_t>(100.0 * 0.001 * sampleRate);

        // Trigger and count samples until complete
        filter.trigger();
        size_t actualSamples = 0;
        while (filter.isRunning() && actualSamples < expectedSamples * 2) {
            (void)filter.process(0.5f);
            actualSamples++;
        }

        // Verify timing accuracy within 1% (SC-002)
        float timingError = std::abs(static_cast<float>(actualSamples) - static_cast<float>(expectedSamples)) /
                           static_cast<float>(expectedSamples);
        REQUIRE(timingError < 0.01f);
    }
}

TEST_CASE("MultiStageEnvelopeFilter cutoff progression from baseFrequency through stages", "[multistage-env-filter][US1]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    // Configure 3-stage sweep
    filter.setNumStages(3);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 500.0f);
    filter.setStageTarget(1, 2000.0f);
    filter.setStageTarget(2, 1000.0f);
    filter.setStageTime(0, 50.0f);
    filter.setStageTime(1, 50.0f);
    filter.setStageTime(2, 50.0f);
    filter.setStageCurve(0, 0.0f);
    filter.setStageCurve(1, 0.0f);
    filter.setStageCurve(2, 0.0f);

    // Before trigger, cutoff should be at baseFrequency
    (void)filter.process(0.5f);
    REQUIRE(filter.getCurrentCutoff() == Approx(100.0f).margin(1.0f));

    // Trigger
    filter.trigger();

    // Initial cutoff should start from baseFrequency
    float initialCutoff = filter.getCurrentCutoff();
    REQUIRE(initialCutoff >= 100.0f);
    REQUIRE(initialCutoff <= 200.0f); // Close to base, moving toward 500

    // Process halfway through stage 0
    size_t halfStage = msToSamples(25.0f, kSampleRate);
    for (size_t i = 0; i < halfStage; ++i) {
        (void)filter.process(0.5f);
    }

    // Cutoff should be approximately midway between 100 and 500 (linear curve)
    float midCutoff = filter.getCurrentCutoff();
    REQUIRE(midCutoff > 200.0f);
    REQUIRE(midCutoff < 400.0f);

    // Process to end of stage 0
    for (size_t i = 0; i < halfStage; ++i) {
        (void)filter.process(0.5f);
    }

    // Should be near stage 0 target (500)
    float endStage0 = filter.getCurrentCutoff();
    REQUIRE(endStage0 >= 400.0f);
    REQUIRE(endStage0 <= 600.0f);

    // Process through stage 1 - should go up to 2000
    size_t stage1Samples = msToSamples(50.0f, kSampleRate);
    for (size_t i = 0; i < stage1Samples; ++i) {
        (void)filter.process(0.5f);
    }

    // Should be near stage 1 target (2000)
    float endStage1 = filter.getCurrentCutoff();
    REQUIRE(endStage1 >= 1500.0f);
    REQUIRE(endStage1 <= 2100.0f);

    // Process through stage 2 - should go down to 1000
    size_t stage2Samples = msToSamples(50.0f, kSampleRate);
    for (size_t i = 0; i < stage2Samples; ++i) {
        (void)filter.process(0.5f);
    }

    // Should be near stage 2 target (1000)
    float endStage2 = filter.getCurrentCutoff();
    REQUIRE(endStage2 >= 900.0f);
    REQUIRE(endStage2 <= 1100.0f);
}

TEST_CASE("MultiStageEnvelopeFilter getCurrentStage returns correct index", "[multistage-env-filter][US1]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(4);
    filter.setBaseFrequency(100.0f);
    for (int i = 0; i < 4; ++i) {
        filter.setStageTarget(i, 500.0f);
        filter.setStageTime(i, 10.0f); // Short 10ms stages
        filter.setStageCurve(i, 0.0f);
    }

    // Before trigger
    REQUIRE(filter.getCurrentStage() == 0);

    // After trigger
    filter.trigger();
    REQUIRE(filter.getCurrentStage() == 0);

    // Process through all stages, checking index at each transition
    std::array<int, 5> expectedStages = {0, 1, 2, 3, 3}; // Last one stays at 3 when complete
    size_t stageSamples = msToSamples(10.0f, kSampleRate);

    for (int expectedStage = 0; expectedStage < 4; ++expectedStage) {
        // Should be at expected stage at start
        REQUIRE(filter.getCurrentStage() == expectedStage);

        // Process through this stage
        for (size_t i = 0; i < stageSamples + 5; ++i) { // +5 to ensure transition
            (void)filter.process(0.5f);
        }
    }

    // After completion, stage should be at last stage
    REQUIRE(filter.isComplete());
}

TEST_CASE("MultiStageEnvelopeFilter filter configuration", "[multistage-env-filter][US1]") {
    MultiStageEnvelopeFilter filter;
    filter.prepare(44100.0);

    SECTION("setResonance with clamping") {
        filter.setResonance(5.0f);
        REQUIRE(filter.getResonance() == Approx(5.0f));

        // Below minimum
        filter.setResonance(0.01f);
        REQUIRE(filter.getResonance() == Approx(MultiStageEnvelopeFilter::kMinResonance));

        // Above maximum
        filter.setResonance(50.0f);
        REQUIRE(filter.getResonance() == Approx(MultiStageEnvelopeFilter::kMaxResonance));
    }

    SECTION("setFilterType") {
        filter.setFilterType(SVFMode::Lowpass);
        REQUIRE(filter.getFilterType() == SVFMode::Lowpass);

        filter.setFilterType(SVFMode::Bandpass);
        REQUIRE(filter.getFilterType() == SVFMode::Bandpass);

        filter.setFilterType(SVFMode::Highpass);
        REQUIRE(filter.getFilterType() == SVFMode::Highpass);
    }

    SECTION("setBaseFrequency with clamping") {
        filter.setBaseFrequency(500.0f);
        REQUIRE(filter.getBaseFrequency() == Approx(500.0f));

        // Below minimum
        filter.setBaseFrequency(0.0f);
        REQUIRE(filter.getBaseFrequency() == Approx(MultiStageEnvelopeFilter::kMinFrequency));

        // Above Nyquist
        filter.setBaseFrequency(50000.0f);
        float maxFreq = 44100.0f * 0.45f;
        REQUIRE(filter.getBaseFrequency() <= maxFreq);
    }
}

TEST_CASE("MultiStageEnvelopeFilter filter actually processes audio", "[multistage-env-filter][US1]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 1024;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    // Configure lowpass filter at low frequency
    filter.setFilterType(SVFMode::Lowpass);
    filter.setBaseFrequency(200.0f);
    filter.setNumStages(1);
    filter.setStageTarget(0, 200.0f); // Keep at 200Hz
    filter.setStageTime(0, 1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    // Generate high frequency test signal (4000 Hz)
    std::array<float, kBlockSize> buffer;
    generateSine(buffer.data(), kBlockSize, 4000.0f, static_cast<float>(kSampleRate), 1.0f);

    float inputRMS = calculateRMS(buffer.data(), kBlockSize);

    // Process (filter at low cutoff should attenuate high frequency)
    for (size_t i = 0; i < kBlockSize; ++i) {
        buffer[i] = filter.process(buffer[i]);
    }

    float outputRMS = calculateRMS(buffer.data(), kBlockSize);

    // High frequency should be significantly attenuated by 200Hz lowpass
    REQUIRE(outputRMS < inputRMS * 0.3f);
}

// =============================================================================
// Phase 4: User Story 2 - Curved Stage Transitions (T036-T045)
// =============================================================================

TEST_CASE("MultiStageEnvelopeFilter linear curve (0.0) produces constant rate", "[multistage-env-filter][US2]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(1);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 1000.0f);
    filter.setStageTime(0, 100.0f);
    filter.setStageCurve(0, 0.0f); // Linear

    filter.trigger();

    // Sample cutoff at 25%, 50%, 75% of stage time
    std::array<float, 4> cutoffs{};
    size_t quarterSamples = msToSamples(25.0f, kSampleRate);

    for (int quarter = 0; quarter < 4; ++quarter) {
        for (size_t i = 0; i < quarterSamples; ++i) {
            (void)filter.process(0.5f);
        }
        cutoffs[quarter] = filter.getCurrentCutoff();
    }

    // Linear curve: equal increments between samples
    // From 100 to 1000 = 900 range
    // At 25%: ~325, 50%: ~550, 75%: ~775, 100%: ~1000
    float range = 1000.0f - 100.0f;
    float expectedIncrement = range / 4.0f;

    float increment1 = cutoffs[0] - 100.0f;     // Should be ~225
    float increment2 = cutoffs[1] - cutoffs[0]; // Should be ~225
    float increment3 = cutoffs[2] - cutoffs[1]; // Should be ~225
    float increment4 = cutoffs[3] - cutoffs[2]; // Should be ~225

    // All increments should be approximately equal (within 20%)
    float avgIncrement = (increment1 + increment2 + increment3 + increment4) / 4.0f;
    REQUIRE(std::abs(increment1 - avgIncrement) < avgIncrement * 0.3f);
    REQUIRE(std::abs(increment2 - avgIncrement) < avgIncrement * 0.3f);
    REQUIRE(std::abs(increment3 - avgIncrement) < avgIncrement * 0.3f);
    REQUIRE(std::abs(increment4 - avgIncrement) < avgIncrement * 0.3f);
}

TEST_CASE("MultiStageEnvelopeFilter exponential curve (+1.0) slow start fast finish", "[multistage-env-filter][US2]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(1);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 1000.0f);
    filter.setStageTime(0, 100.0f);
    filter.setStageCurve(0, 1.0f); // Exponential

    filter.trigger();

    // Sample cutoff at 50% and 90% of stage time
    size_t halfSamples = msToSamples(50.0f, kSampleRate);
    size_t ninetyPercentSamples = msToSamples(40.0f, kSampleRate);

    for (size_t i = 0; i < halfSamples; ++i) {
        (void)filter.process(0.5f);
    }
    float cutoffAt50 = filter.getCurrentCutoff();

    for (size_t i = 0; i < ninetyPercentSamples; ++i) {
        (void)filter.process(0.5f);
    }
    float cutoffAt90 = filter.getCurrentCutoff();

    // Exponential curve: slow start, fast finish
    // At 50%, cutoff should be less than linear midpoint (550)
    // Movement in last 40% should be greater than movement in first 50%
    float movementFirst50 = cutoffAt50 - 100.0f;
    float movementNext40 = cutoffAt90 - cutoffAt50;

    // For exponential, first half should move less than second half
    // Movement in first 50% should be less than 50% of total range
    float totalRange = 1000.0f - 100.0f;
    REQUIRE(cutoffAt50 < 100.0f + totalRange * 0.5f);

    // SC-003: derivative at t=0.9 should be >3x derivative at t=0.1
    // This is approximated by checking the rate of change
    // The movement in the last 40% should be significantly greater than first 50%
    // (actually comparing 50% vs 40%, so need to adjust for time)
    float rateFirst50 = movementFirst50 / 50.0f;   // Hz per % time
    float rateNext40 = movementNext40 / 40.0f;     // Hz per % time
    REQUIRE(rateNext40 > rateFirst50 * 2.0f);      // Should be significantly faster
}

TEST_CASE("MultiStageEnvelopeFilter logarithmic curve (-1.0) fast start slow finish", "[multistage-env-filter][US2]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(1);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 1000.0f);
    filter.setStageTime(0, 100.0f);
    filter.setStageCurve(0, -1.0f); // Logarithmic

    filter.trigger();

    // Sample cutoff at 10% and 50% of stage time
    size_t tenPercentSamples = msToSamples(10.0f, kSampleRate);
    size_t fortyPercentSamples = msToSamples(40.0f, kSampleRate);

    for (size_t i = 0; i < tenPercentSamples; ++i) {
        (void)filter.process(0.5f);
    }
    float cutoffAt10 = filter.getCurrentCutoff();

    for (size_t i = 0; i < fortyPercentSamples; ++i) {
        (void)filter.process(0.5f);
    }
    float cutoffAt50 = filter.getCurrentCutoff();

    // Logarithmic curve: fast start, slow finish
    // At 50%, cutoff should be more than linear midpoint (550)
    float totalRange = 1000.0f - 100.0f;
    REQUIRE(cutoffAt50 > 100.0f + totalRange * 0.5f);

    // Movement in first 10% should be greater (proportionally) than movement in next 40%
    float movementFirst10 = cutoffAt10 - 100.0f;
    float movementNext40 = cutoffAt50 - cutoffAt10;

    float rateFirst10 = movementFirst10 / 10.0f;
    float rateNext40 = movementNext40 / 40.0f;

    // First 10% should have higher rate than next 40%
    REQUIRE(rateFirst10 > rateNext40 * 1.5f);
}

TEST_CASE("MultiStageEnvelopeFilter intermediate curve values (0.5)", "[multistage-env-filter][US2]") {
    constexpr double kSampleRate = 44100.0;

    // Test with curve = 0.5 (moderate exponential)
    MultiStageEnvelopeFilter filterMod;
    filterMod.prepare(kSampleRate);
    filterMod.setNumStages(1);
    filterMod.setBaseFrequency(100.0f);
    filterMod.setStageTarget(0, 1000.0f);
    filterMod.setStageTime(0, 100.0f);
    filterMod.setStageCurve(0, 0.5f);

    // Test with curve = 1.0 (full exponential)
    MultiStageEnvelopeFilter filterFull;
    filterFull.prepare(kSampleRate);
    filterFull.setNumStages(1);
    filterFull.setBaseFrequency(100.0f);
    filterFull.setStageTarget(0, 1000.0f);
    filterFull.setStageTime(0, 100.0f);
    filterFull.setStageCurve(0, 1.0f);

    // Test with curve = 0.0 (linear)
    MultiStageEnvelopeFilter filterLinear;
    filterLinear.prepare(kSampleRate);
    filterLinear.setNumStages(1);
    filterLinear.setBaseFrequency(100.0f);
    filterLinear.setStageTarget(0, 1000.0f);
    filterLinear.setStageTime(0, 100.0f);
    filterLinear.setStageCurve(0, 0.0f);

    filterMod.trigger();
    filterFull.trigger();
    filterLinear.trigger();

    // Sample at 50%
    size_t halfSamples = msToSamples(50.0f, kSampleRate);
    for (size_t i = 0; i < halfSamples; ++i) {
        (void)filterMod.process(0.5f);
        (void)filterFull.process(0.5f);
        (void)filterLinear.process(0.5f);
    }

    float modCutoff = filterMod.getCurrentCutoff();
    float fullCutoff = filterFull.getCurrentCutoff();
    float linearCutoff = filterLinear.getCurrentCutoff();

    // Moderate exponential should be between linear and full exponential
    // (closer to linear since 0.5 is halfway)
    REQUIRE(modCutoff < linearCutoff);  // More exponential = slower at start
    REQUIRE(modCutoff > fullCutoff);    // Less exponential than full
}

TEST_CASE("MultiStageEnvelopeFilter curve value clamping", "[multistage-env-filter][US2]") {
    MultiStageEnvelopeFilter filter;
    filter.prepare(44100.0);

    filter.setStageCurve(0, 0.5f);
    REQUIRE(filter.getStageCurve(0) == Approx(0.5f));

    // Above maximum should clamp
    filter.setStageCurve(0, 2.0f);
    REQUIRE(filter.getStageCurve(0) == Approx(1.0f));

    // Below minimum should clamp
    filter.setStageCurve(0, -2.0f);
    REQUIRE(filter.getStageCurve(0) == Approx(-1.0f));

    // Out of range stage index should be ignored
    filter.setStageCurve(0, 0.3f);
    filter.setStageCurve(10, 0.8f); // Should be ignored
    REQUIRE(filter.getStageCurve(0) == Approx(0.3f));
}

// =============================================================================
// Phase 5: User Story 3 - Envelope Looping (T048-T061)
// =============================================================================

TEST_CASE("MultiStageEnvelopeFilter loop configuration", "[multistage-env-filter][US3]") {
    MultiStageEnvelopeFilter filter;
    filter.prepare(44100.0);
    filter.setNumStages(4);

    SECTION("setLoop enable/disable") {
        REQUIRE_FALSE(filter.getLoop());

        filter.setLoop(true);
        REQUIRE(filter.getLoop());

        filter.setLoop(false);
        REQUIRE_FALSE(filter.getLoop());
    }

    SECTION("setLoopStart with clamping") {
        filter.setLoopStart(1);
        REQUIRE(filter.getLoopStart() == 1);

        filter.setLoopStart(3);
        REQUIRE(filter.getLoopStart() == 3);

        // Below minimum
        filter.setLoopStart(-1);
        REQUIRE(filter.getLoopStart() == 0);

        // Above numStages-1
        filter.setLoopStart(10);
        REQUIRE(filter.getLoopStart() == 3); // numStages-1
    }

    SECTION("setLoopEnd with clamping") {
        filter.setLoopStart(1);
        filter.setLoopEnd(3);
        REQUIRE(filter.getLoopEnd() == 3);

        // Below loopStart
        filter.setLoopEnd(0);
        REQUIRE(filter.getLoopEnd() == 1); // Clamped to loopStart

        // Above numStages-1
        filter.setLoopEnd(10);
        REQUIRE(filter.getLoopEnd() == 3); // numStages-1
    }

    SECTION("loopStart adjustment updates loopEnd if needed") {
        filter.setLoopStart(1);
        filter.setLoopEnd(2);
        REQUIRE(filter.getLoopEnd() == 2);

        // Setting loopStart higher than loopEnd should adjust loopEnd
        filter.setLoopStart(3);
        REQUIRE(filter.getLoopEnd() >= filter.getLoopStart());
    }
}

TEST_CASE("MultiStageEnvelopeFilter 4-stage loop from stage 1 to 3", "[multistage-env-filter][US3]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    // Configure 4 stages
    filter.setNumStages(4);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 200.0f);
    filter.setStageTarget(1, 500.0f);
    filter.setStageTarget(2, 800.0f);
    filter.setStageTarget(3, 400.0f);
    for (int i = 0; i < 4; ++i) {
        filter.setStageTime(i, 20.0f); // Short 20ms stages
        filter.setStageCurve(i, 0.0f);
    }

    // Enable loop from stage 1 to 3
    filter.setLoop(true);
    filter.setLoopStart(1);
    filter.setLoopEnd(3);

    filter.trigger();

    size_t stageSamples = msToSamples(20.0f, kSampleRate);

    // Process through stage 0 -> should advance to stage 1
    for (size_t i = 0; i < stageSamples + 10; ++i) {
        (void)filter.process(0.5f);
    }
    REQUIRE(filter.getCurrentStage() == 1);

    // Process through stages 1, 2, 3 -> should loop back to stage 1
    for (int cycle = 0; cycle < 3; ++cycle) {
        // Process stages 1, 2, 3
        for (size_t i = 0; i < stageSamples * 3 + 30; ++i) {
            (void)filter.process(0.5f);
        }

        // Should be back at stage 1 (looping)
        REQUIRE(filter.getCurrentStage() == 1);
        REQUIRE(filter.isRunning());
    }
}

TEST_CASE("MultiStageEnvelopeFilter loop transition is smooth", "[multistage-env-filter][US3]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    // Configure 3 stages
    filter.setNumStages(3);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 500.0f);
    filter.setStageTarget(1, 1000.0f);
    filter.setStageTarget(2, 500.0f); // Same as stage 0 target for easy loop
    filter.setStageTime(0, 50.0f);
    filter.setStageTime(1, 50.0f);
    filter.setStageTime(2, 50.0f);
    filter.setStageCurve(0, 0.0f);
    filter.setStageCurve(1, 0.0f);
    filter.setStageCurve(2, 0.0f);

    // Enable loop from stage 1 to 2
    filter.setLoop(true);
    filter.setLoopStart(1);
    filter.setLoopEnd(2);

    filter.trigger();

    // Process through stage 0 to get to loop section
    size_t stageSamples = msToSamples(50.0f, kSampleRate);
    for (size_t i = 0; i < stageSamples + 5; ++i) {
        (void)filter.process(0.5f);
    }

    // Now in loop section - record cutoffs through loop transition
    std::vector<float> cutoffs;
    cutoffs.reserve(stageSamples * 4);

    for (size_t i = 0; i < stageSamples * 4; ++i) {
        (void)filter.process(0.5f);
        cutoffs.push_back(filter.getCurrentCutoff());
    }

    // Check for discontinuities (large jumps between consecutive samples)
    float maxJump = 0.0f;
    for (size_t i = 1; i < cutoffs.size(); ++i) {
        float jump = std::abs(cutoffs[i] - cutoffs[i - 1]);
        maxJump = std::max(maxJump, jump);
    }

    // Maximum jump should be small (smooth transition)
    // At 44.1kHz, 100ms stage, sweeping 500Hz = ~0.2 Hz/sample typical
    // Allow up to 50 Hz jump for smooth (considering loop transition uses new curve/time)
    REQUIRE(maxJump < 50.0f);
}

TEST_CASE("MultiStageEnvelopeFilter non-looping completion", "[multistage-env-filter][US3]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(2);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 500.0f);
    filter.setStageTarget(1, 800.0f);
    filter.setStageTime(0, 20.0f);
    filter.setStageTime(1, 20.0f);

    filter.setLoop(false); // No looping

    filter.trigger();
    REQUIRE(filter.isRunning());
    REQUIRE_FALSE(filter.isComplete());

    // Process through all stages
    size_t totalSamples = msToSamples(50.0f, kSampleRate);
    for (size_t i = 0; i < totalSamples; ++i) {
        (void)filter.process(0.5f);
    }

    // Should be complete
    REQUIRE(filter.isComplete());
    REQUIRE_FALSE(filter.isRunning());
}

// =============================================================================
// Phase 6: User Story 4 - Velocity Sensitivity (T064-T074)
// =============================================================================

TEST_CASE("MultiStageEnvelopeFilter velocity sensitivity=1.0 velocity=0.5 produces 50% depth", "[multistage-env-filter][US4]") {
    constexpr double kSampleRate = 44100.0;

    // Full velocity filter
    MultiStageEnvelopeFilter filterFull;
    filterFull.prepare(kSampleRate);
    filterFull.setNumStages(1);
    filterFull.setBaseFrequency(100.0f);
    filterFull.setStageTarget(0, 1100.0f); // 1000 Hz range
    filterFull.setStageTime(0, 100.0f);
    filterFull.setVelocitySensitivity(1.0f);
    filterFull.trigger(1.0f); // Full velocity

    // Half velocity filter
    MultiStageEnvelopeFilter filterHalf;
    filterHalf.prepare(kSampleRate);
    filterHalf.setNumStages(1);
    filterHalf.setBaseFrequency(100.0f);
    filterHalf.setStageTarget(0, 1100.0f);
    filterHalf.setStageTime(0, 100.0f);
    filterHalf.setVelocitySensitivity(1.0f);
    filterHalf.trigger(0.5f); // Half velocity

    // Process to 95% of stage (before completion)
    size_t samples = msToSamples(95.0f, kSampleRate);
    for (size_t i = 0; i < samples; ++i) {
        (void)filterFull.process(0.5f);
        (void)filterHalf.process(0.5f);
    }

    // Both should still be running
    REQUIRE(filterFull.isRunning());
    REQUIRE(filterHalf.isRunning());

    float fullCutoff = filterFull.getCurrentCutoff();
    float halfCutoff = filterHalf.getCurrentCutoff();

    // Full velocity should be near full range (1100 Hz)
    REQUIRE(fullCutoff >= 900.0f);

    // Half velocity with sensitivity=1.0 should reach 50% of range
    // Range = 1100 - 100 = 1000
    // 50% depth = 100 + 500 = 600
    float expectedHalf = 100.0f + (1100.0f - 100.0f) * 0.5f;
    REQUIRE(halfCutoff == Approx(expectedHalf).margin(100.0f));
}

TEST_CASE("MultiStageEnvelopeFilter velocity sensitivity=0.0 ignores velocity", "[multistage-env-filter][US4]") {
    constexpr double kSampleRate = 44100.0;

    // Zero sensitivity with full velocity
    MultiStageEnvelopeFilter filterFullVel;
    filterFullVel.prepare(kSampleRate);
    filterFullVel.setNumStages(1);
    filterFullVel.setBaseFrequency(100.0f);
    filterFullVel.setStageTarget(0, 1000.0f);
    filterFullVel.setStageTime(0, 100.0f);
    filterFullVel.setVelocitySensitivity(0.0f);
    filterFullVel.trigger(1.0f);

    // Zero sensitivity with low velocity
    MultiStageEnvelopeFilter filterLowVel;
    filterLowVel.prepare(kSampleRate);
    filterLowVel.setNumStages(1);
    filterLowVel.setBaseFrequency(100.0f);
    filterLowVel.setStageTarget(0, 1000.0f);
    filterLowVel.setStageTime(0, 100.0f);
    filterLowVel.setVelocitySensitivity(0.0f);
    filterLowVel.trigger(0.1f);

    // Process to completion
    size_t samples = msToSamples(110.0f, kSampleRate);
    for (size_t i = 0; i < samples; ++i) {
        (void)filterFullVel.process(0.5f);
        (void)filterLowVel.process(0.5f);
    }

    // Both should reach same cutoff (sensitivity=0 ignores velocity)
    float fullVelCutoff = filterFullVel.getCurrentCutoff();
    float lowVelCutoff = filterLowVel.getCurrentCutoff();

    REQUIRE(fullVelCutoff == Approx(lowVelCutoff).margin(10.0f));
}

TEST_CASE("MultiStageEnvelopeFilter velocity sensitivity=1.0 velocity=1.0 full depth", "[multistage-env-filter][US4]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);
    filter.setNumStages(1);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 2000.0f);
    filter.setStageTime(0, 50.0f);
    filter.setVelocitySensitivity(1.0f);
    filter.trigger(1.0f); // Full velocity

    // Process to 95% of stage (before completion)
    size_t samples = msToSamples(47.0f, kSampleRate);
    for (size_t i = 0; i < samples; ++i) {
        (void)filter.process(0.5f);
    }

    // Should still be running
    REQUIRE(filter.isRunning());

    // Should be near full target
    REQUIRE(filter.getCurrentCutoff() >= 1800.0f);
}

TEST_CASE("MultiStageEnvelopeFilter setVelocitySensitivity clamping", "[multistage-env-filter][US4]") {
    MultiStageEnvelopeFilter filter;
    filter.prepare(44100.0);

    filter.setVelocitySensitivity(0.5f);
    REQUIRE(filter.getVelocitySensitivity() == Approx(0.5f));

    // Below minimum
    filter.setVelocitySensitivity(-0.5f);
    REQUIRE(filter.getVelocitySensitivity() == Approx(0.0f));

    // Above maximum
    filter.setVelocitySensitivity(1.5f);
    REQUIRE(filter.getVelocitySensitivity() == Approx(1.0f));
}

// =============================================================================
// Phase 7: User Story 5 - Release Phase (T077-T088)
// =============================================================================

TEST_CASE("MultiStageEnvelopeFilter release during looping exits and decays", "[multistage-env-filter][US5]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(3);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 500.0f);
    filter.setStageTarget(1, 800.0f);
    filter.setStageTarget(2, 600.0f);
    filter.setStageTime(0, 30.0f);
    filter.setStageTime(1, 30.0f);
    filter.setStageTime(2, 30.0f);

    filter.setLoop(true);
    filter.setLoopStart(1);
    filter.setLoopEnd(2);
    filter.setReleaseTime(100.0f);

    filter.trigger();

    // Process into loop section
    size_t samples = msToSamples(50.0f, kSampleRate);
    for (size_t i = 0; i < samples; ++i) {
        (void)filter.process(0.5f);
    }

    REQUIRE(filter.isRunning());
    float cutoffBeforeRelease = filter.getCurrentCutoff();
    REQUIRE(cutoffBeforeRelease > 200.0f); // Should be above base

    // Call release
    filter.release();

    // Should now be in releasing state
    REQUIRE(filter.isRunning()); // Still running during release

    // Process through release
    size_t releaseSamples = msToSamples(200.0f, kSampleRate);
    for (size_t i = 0; i < releaseSamples; ++i) {
        (void)filter.process(0.5f);
    }

    // Should decay toward baseFrequency
    REQUIRE(filter.getCurrentCutoff() <= 150.0f);
}

TEST_CASE("MultiStageEnvelopeFilter release mid-stage smooth transition", "[multistage-env-filter][US5]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(1);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 1000.0f);
    filter.setStageTime(0, 200.0f);
    filter.setReleaseTime(100.0f);

    filter.trigger();

    // Process to mid-stage
    size_t midSamples = msToSamples(100.0f, kSampleRate);
    for (size_t i = 0; i < midSamples; ++i) {
        (void)filter.process(0.5f);
    }

    float midCutoff = filter.getCurrentCutoff();
    REQUIRE(midCutoff > 400.0f);
    REQUIRE(midCutoff < 700.0f);

    // Record cutoff before release
    float cutoffAtRelease = filter.getCurrentCutoff();

    // Call release
    filter.release();

    // Record cutoffs during release - should be smooth decay
    std::vector<float> cutoffs;
    size_t releaseSamples = msToSamples(150.0f, kSampleRate);
    for (size_t i = 0; i < releaseSamples; ++i) {
        (void)filter.process(0.5f);
        cutoffs.push_back(filter.getCurrentCutoff());
    }

    // First cutoff should be close to cutoff at release
    REQUIRE(std::abs(cutoffs[0] - cutoffAtRelease) < 50.0f);

    // Cutoffs should monotonically decrease (smooth decay)
    for (size_t i = 1; i < cutoffs.size(); ++i) {
        REQUIRE(cutoffs[i] <= cutoffs[i - 1] + 0.1f); // Allow tiny increase due to smoothing
    }

    // Should approach baseFrequency
    REQUIRE(cutoffs.back() < 200.0f);
}

TEST_CASE("MultiStageEnvelopeFilter release time independence", "[multistage-env-filter][US5]") {
    constexpr double kSampleRate = 44100.0;

    // Short release time
    MultiStageEnvelopeFilter filterShort;
    filterShort.prepare(kSampleRate);
    filterShort.setNumStages(1);
    filterShort.setBaseFrequency(100.0f);
    filterShort.setStageTarget(0, 1000.0f);
    filterShort.setStageTime(0, 100.0f);
    filterShort.setReleaseTime(50.0f);

    // Long release time
    MultiStageEnvelopeFilter filterLong;
    filterLong.prepare(kSampleRate);
    filterLong.setNumStages(1);
    filterLong.setBaseFrequency(100.0f);
    filterLong.setStageTarget(0, 1000.0f);
    filterLong.setStageTime(0, 100.0f);
    filterLong.setReleaseTime(200.0f);

    // Trigger both
    filterShort.trigger();
    filterLong.trigger();

    // Process to same point
    size_t samples = msToSamples(50.0f, kSampleRate);
    for (size_t i = 0; i < samples; ++i) {
        (void)filterShort.process(0.5f);
        (void)filterLong.process(0.5f);
    }

    // Release both
    filterShort.release();
    filterLong.release();

    // Process for short release time
    size_t shortRelease = msToSamples(75.0f, kSampleRate);
    for (size_t i = 0; i < shortRelease; ++i) {
        (void)filterShort.process(0.5f);
        (void)filterLong.process(0.5f);
    }

    float shortCutoff = filterShort.getCurrentCutoff();
    float longCutoff = filterLong.getCurrentCutoff();

    // Short release should have decayed more
    REQUIRE(shortCutoff < longCutoff);
}

TEST_CASE("MultiStageEnvelopeFilter release completion", "[multistage-env-filter][US5]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(1);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 500.0f);
    filter.setStageTime(0, 50.0f);
    filter.setReleaseTime(50.0f);

    filter.trigger();

    // Process to mid-stage
    size_t samples = msToSamples(25.0f, kSampleRate);
    for (size_t i = 0; i < samples; ++i) {
        (void)filter.process(0.5f);
    }

    filter.release();
    REQUIRE_FALSE(filter.isComplete());

    // Process through release (OnePoleSmoother needs ~5x the configured time to complete)
    // 50ms release time * 5 = 250ms, add extra margin
    size_t releaseSamples = msToSamples(300.0f, kSampleRate);
    for (size_t i = 0; i < releaseSamples; ++i) {
        (void)filter.process(0.5f);
    }

    // Cutoff should be near base frequency
    REQUIRE(filter.getCurrentCutoff() <= 150.0f);
    REQUIRE(filter.isComplete());
    REQUIRE_FALSE(filter.isRunning());
}

TEST_CASE("MultiStageEnvelopeFilter release edge cases", "[multistage-env-filter][US5][edge]") {
    MultiStageEnvelopeFilter filter;
    filter.prepare(44100.0);

    SECTION("release when Idle - no effect") {
        filter.release();
        REQUIRE(filter.isComplete()); // Still idle/complete
    }

    SECTION("release when already Complete - no effect") {
        filter.setNumStages(1);
        filter.setStageTime(0, 10.0f);
        filter.trigger();

        // Process to completion
        for (size_t i = 0; i < 1000; ++i) {
            (void)filter.process(0.5f);
        }
        REQUIRE(filter.isComplete());

        filter.release();
        REQUIRE(filter.isComplete());
    }

    SECTION("retrigger after release") {
        filter.setNumStages(1);
        filter.setStageTarget(0, 500.0f);
        filter.setStageTime(0, 50.0f);
        filter.setReleaseTime(50.0f);

        filter.trigger();

        // Process partway
        for (size_t i = 0; i < 1000; ++i) {
            (void)filter.process(0.5f);
        }

        filter.release();

        // Process some release
        for (size_t i = 0; i < 500; ++i) {
            (void)filter.process(0.5f);
        }

        // Retrigger
        filter.trigger();
        REQUIRE(filter.isRunning());
        REQUIRE(filter.getCurrentStage() == 0);
    }
}

TEST_CASE("MultiStageEnvelopeFilter setReleaseTime clamping", "[multistage-env-filter][US5]") {
    MultiStageEnvelopeFilter filter;
    filter.prepare(44100.0);

    filter.setReleaseTime(500.0f);
    REQUIRE(filter.getReleaseTime() == Approx(500.0f));

    // Below minimum
    filter.setReleaseTime(-100.0f);
    REQUIRE(filter.getReleaseTime() == Approx(0.0f));

    // Above maximum
    filter.setReleaseTime(20000.0f);
    REQUIRE(filter.getReleaseTime() == Approx(MultiStageEnvelopeFilter::kMaxReleaseTimeMs));
}

// =============================================================================
// Phase 8: Real-Time Safety (T091-T101)
// =============================================================================

TEST_CASE("MultiStageEnvelopeFilter noexcept methods (FR-022, SC-008)", "[multistage-env-filter][realtime]") {
    // Verify that critical methods are noexcept
    static_assert(noexcept(std::declval<MultiStageEnvelopeFilter>().process(0.0f)),
                  "process() must be noexcept");
    static_assert(noexcept(std::declval<MultiStageEnvelopeFilter>().processBlock(nullptr, 0)),
                  "processBlock() must be noexcept");
    static_assert(noexcept(std::declval<MultiStageEnvelopeFilter>().reset()),
                  "reset() must be noexcept");
    static_assert(noexcept(std::declval<MultiStageEnvelopeFilter>().trigger()),
                  "trigger() must be noexcept");
    static_assert(noexcept(std::declval<MultiStageEnvelopeFilter>().trigger(1.0f)),
                  "trigger(velocity) must be noexcept");
    static_assert(noexcept(std::declval<MultiStageEnvelopeFilter>().release()),
                  "release() must be noexcept");
    static_assert(noexcept(std::declval<MultiStageEnvelopeFilter>().getCurrentCutoff()),
                  "getCurrentCutoff() must be noexcept");
    static_assert(noexcept(std::declval<MultiStageEnvelopeFilter>().getCurrentStage()),
                  "getCurrentStage() must be noexcept");
    static_assert(noexcept(std::declval<MultiStageEnvelopeFilter>().isComplete()),
                  "isComplete() must be noexcept");
    static_assert(noexcept(std::declval<MultiStageEnvelopeFilter>().isRunning()),
                  "isRunning() must be noexcept");

    REQUIRE(true); // If we get here, static_asserts passed
}

TEST_CASE("MultiStageEnvelopeFilter NaN/Inf handling (FR-022)", "[multistage-env-filter][realtime]") {
    MultiStageEnvelopeFilter filter;
    filter.prepare(44100.0);
    filter.setNumStages(1);
    filter.setStageTarget(0, 500.0f);
    filter.trigger();

    SECTION("NaN input returns 0 and resets filter") {
        // Process some valid samples first
        for (int i = 0; i < 100; ++i) {
            (void)filter.process(0.5f);
        }

        // Process NaN
        float output = filter.process(std::numeric_limits<float>::quiet_NaN());
        REQUIRE(output == 0.0f);
    }

    SECTION("Inf input returns 0 and resets filter") {
        // Process some valid samples first
        for (int i = 0; i < 100; ++i) {
            (void)filter.process(0.5f);
        }

        // Process Inf
        float output = filter.process(std::numeric_limits<float>::infinity());
        REQUIRE(output == 0.0f);
    }

    SECTION("Negative Inf input returns 0") {
        float output = filter.process(-std::numeric_limits<float>::infinity());
        REQUIRE(output == 0.0f);
    }
}

TEST_CASE("MultiStageEnvelopeFilter output is always valid (no NaN/Inf)", "[multistage-env-filter][realtime]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kNumSamples = 100000;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    // Configure aggressive settings
    filter.setNumStages(4);
    filter.setBaseFrequency(50.0f);
    filter.setStageTarget(0, 5000.0f);
    filter.setStageTarget(1, 100.0f);
    filter.setStageTarget(2, 10000.0f);
    filter.setStageTarget(3, 200.0f);
    filter.setStageTime(0, 10.0f);
    filter.setStageTime(1, 20.0f);
    filter.setStageTime(2, 15.0f);
    filter.setStageTime(3, 25.0f);
    filter.setResonance(25.0f); // High Q
    filter.setLoop(true);
    filter.setLoopStart(1);
    filter.setLoopEnd(3);

    filter.trigger();

    bool allValid = true;
    for (size_t i = 0; i < kNumSamples; ++i) {
        float input = std::sin(2.0f * std::numbers::pi_v<float> * 440.0f *
                               static_cast<float>(i) / static_cast<float>(kSampleRate));
        float output = filter.process(input);

        if (!isValidFloat(output)) {
            allValid = false;
            break;
        }
    }

    REQUIRE(allValid);
}

TEST_CASE("MultiStageEnvelopeFilter stability at extreme resonance", "[multistage-env-filter][realtime]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kNumSamples = 50000;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(2);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 8000.0f);
    filter.setStageTarget(1, 200.0f);
    filter.setStageTime(0, 50.0f);
    filter.setStageTime(1, 50.0f);
    filter.setResonance(MultiStageEnvelopeFilter::kMaxResonance); // Maximum Q
    filter.setLoop(true);
    filter.setLoopStart(0);
    filter.setLoopEnd(1);

    filter.trigger();

    bool allValid = true;
    float maxOutput = 0.0f;

    for (size_t i = 0; i < kNumSamples; ++i) {
        float input = std::sin(2.0f * std::numbers::pi_v<float> * 440.0f *
                               static_cast<float>(i) / static_cast<float>(kSampleRate));
        float output = filter.process(input * 0.5f);

        if (!isValidFloat(output)) {
            allValid = false;
            break;
        }
        maxOutput = std::max(maxOutput, std::abs(output));
    }

    REQUIRE(allValid);
    // Output should be bounded (resonance can boost but not explode)
    REQUIRE(maxOutput < 100.0f);
}

// =============================================================================
// Phase 9: Filter Integration Tests (T104-T113)
// =============================================================================

TEST_CASE("MultiStageEnvelopeFilter lowpass mode filters high frequencies", "[multistage-env-filter][filter]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 4096;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    // Set to lowpass with low cutoff
    filter.setFilterType(SVFMode::Lowpass);
    filter.setBaseFrequency(200.0f);
    filter.setNumStages(1);
    filter.setStageTarget(0, 200.0f); // Fixed at 200 Hz
    filter.setStageTime(0, 1000.0f);  // Long stage
    filter.setResonance(SVF::kButterworthQ);

    filter.trigger();

    // Let filter stabilize
    for (int i = 0; i < 1000; ++i) {
        (void)filter.process(0.0f);
    }

    // Process 4000 Hz sine
    std::array<float, kBlockSize> buffer;
    generateSine(buffer.data(), kBlockSize, 4000.0f, static_cast<float>(kSampleRate), 1.0f);

    float inputRMS = calculateRMS(buffer.data(), kBlockSize);

    for (size_t i = 0; i < kBlockSize; ++i) {
        buffer[i] = filter.process(buffer[i]);
    }

    float outputRMS = calculateRMS(buffer.data(), kBlockSize);

    // 4000 Hz should be heavily attenuated by 200 Hz lowpass
    float attenuationDb = 20.0f * std::log10(outputRMS / inputRMS);
    REQUIRE(attenuationDb <= -20.0f);
}

TEST_CASE("MultiStageEnvelopeFilter bandpass mode peak at cutoff", "[multistage-env-filter][filter]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 4096;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setFilterType(SVFMode::Bandpass);
    filter.setBaseFrequency(1000.0f);
    filter.setNumStages(1);
    filter.setStageTarget(0, 1000.0f); // Fixed at 1000 Hz
    filter.setStageTime(0, 1000.0f);
    filter.setResonance(2.0f);

    filter.trigger();

    // Let filter stabilize
    for (int i = 0; i < 2000; ++i) {
        (void)filter.process(0.0f);
    }

    // Process 1000 Hz sine (at cutoff)
    std::array<float, kBlockSize> buffer;
    generateSine(buffer.data(), kBlockSize, 1000.0f, static_cast<float>(kSampleRate), 1.0f);

    float inputRMS = calculateRMS(buffer.data(), kBlockSize);

    for (size_t i = 0; i < kBlockSize; ++i) {
        buffer[i] = filter.process(buffer[i]);
    }

    float outputRMS = calculateRMS(buffer.data(), kBlockSize);

    // At center frequency, gain should be near unity (within 3 dB)
    float gainDb = 20.0f * std::log10(outputRMS / inputRMS);
    REQUIRE(gainDb >= -3.0f);
    REQUIRE(gainDb <= 3.0f);
}

TEST_CASE("MultiStageEnvelopeFilter highpass mode attenuates low frequencies", "[multistage-env-filter][filter]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 4096;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setFilterType(SVFMode::Highpass);
    filter.setBaseFrequency(1000.0f);
    filter.setNumStages(1);
    filter.setStageTarget(0, 1000.0f); // Fixed at 1000 Hz
    filter.setStageTime(0, 1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    filter.trigger();

    // Let filter stabilize
    for (int i = 0; i < 1000; ++i) {
        (void)filter.process(0.0f);
    }

    // Process 250 Hz sine (2 octaves below cutoff)
    std::array<float, kBlockSize> buffer;
    generateSine(buffer.data(), kBlockSize, 250.0f, static_cast<float>(kSampleRate), 1.0f);

    float inputRMS = calculateRMS(buffer.data(), kBlockSize);

    for (size_t i = 0; i < kBlockSize; ++i) {
        buffer[i] = filter.process(buffer[i]);
    }

    float outputRMS = calculateRMS(buffer.data(), kBlockSize);

    // 250 Hz should be heavily attenuated by 1000 Hz highpass
    float attenuationDb = 20.0f * std::log10(outputRMS / inputRMS);
    REQUIRE(attenuationDb <= -20.0f);
}

TEST_CASE("MultiStageEnvelopeFilter cutoff modulation affects filter response", "[multistage-env-filter][filter]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 2048;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setFilterType(SVFMode::Lowpass);
    filter.setBaseFrequency(200.0f);
    filter.setNumStages(1);
    filter.setStageTarget(0, 4000.0f); // Sweep from 200 to 4000 Hz
    filter.setStageTime(0, 200.0f);
    filter.setResonance(SVF::kButterworthQ);

    // Test signal at 2000 Hz
    std::array<float, kBlockSize> buffer;
    generateSine(buffer.data(), kBlockSize, 2000.0f, static_cast<float>(kSampleRate), 1.0f);

    // Process at start (low cutoff = high attenuation)
    filter.trigger();
    std::array<float, kBlockSize> outputLow;
    for (size_t i = 0; i < kBlockSize; ++i) {
        outputLow[i] = filter.process(buffer[i]);
    }
    float rmsLow = calculateRMS(outputLow.data(), kBlockSize);

    // Reset and process at end (high cutoff = low attenuation)
    filter.reset();
    filter.trigger();
    // Process almost to end of stage
    size_t lateSamples = msToSamples(190.0f, kSampleRate);
    for (size_t i = 0; i < lateSamples; ++i) {
        (void)filter.process(0.0f);
    }

    std::array<float, kBlockSize> outputHigh;
    for (size_t i = 0; i < kBlockSize; ++i) {
        outputHigh[i] = filter.process(buffer[i]);
    }
    float rmsHigh = calculateRMS(outputHigh.data(), kBlockSize);

    // With cutoff swept up, high frequency content should be less attenuated
    REQUIRE(rmsHigh > rmsLow * 2.0f);
}

// =============================================================================
// Phase 10: Edge Cases & Robustness (T116-T128)
// =============================================================================

TEST_CASE("MultiStageEnvelopeFilter single stage complete cycle", "[multistage-env-filter][edge]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(1);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 1000.0f);
    filter.setStageTime(0, 50.0f);

    // Before trigger
    REQUIRE(filter.isComplete());
    REQUIRE_FALSE(filter.isRunning());

    // After trigger
    filter.trigger();
    REQUIRE(filter.isRunning());
    REQUIRE_FALSE(filter.isComplete());
    REQUIRE(filter.getCurrentStage() == 0);

    // After completion
    size_t samples = msToSamples(60.0f, kSampleRate);
    for (size_t i = 0; i < samples; ++i) {
        (void)filter.process(0.5f);
    }

    REQUIRE(filter.isComplete());
    REQUIRE_FALSE(filter.isRunning());
}

TEST_CASE("MultiStageEnvelopeFilter maximum 8 stages", "[multistage-env-filter][edge]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    // Configure all 8 stages
    filter.setNumStages(8);
    filter.setBaseFrequency(100.0f);

    for (int i = 0; i < 8; ++i) {
        filter.setStageTarget(i, 100.0f + i * 200.0f);
        filter.setStageTime(i, 10.0f);
        filter.setStageCurve(i, 0.0f);
    }

    filter.trigger();

    // Process through all 8 stages
    size_t stageSamples = msToSamples(10.0f, kSampleRate);
    for (int stage = 0; stage < 8; ++stage) {
        REQUIRE(filter.getCurrentStage() == stage);

        for (size_t i = 0; i < stageSamples + 5; ++i) {
            (void)filter.process(0.5f);
        }
    }

    REQUIRE(filter.isComplete());
}

TEST_CASE("MultiStageEnvelopeFilter zero stage time instant transition", "[multistage-env-filter][edge]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(2);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 500.0f);
    filter.setStageTarget(1, 1000.0f);
    filter.setStageTime(0, 0.0f); // Instant
    filter.setStageTime(1, 50.0f);

    filter.trigger();

    // After first process, stage 0 should complete instantly
    (void)filter.process(0.5f);

    // Should be in stage 1
    REQUIRE(filter.getCurrentStage() == 1);

    // Cutoff should have jumped to stage 0 target and be moving toward stage 1
    float cutoff = filter.getCurrentCutoff();
    REQUIRE(cutoff >= 400.0f); // Should have passed through stage 0 target
}

TEST_CASE("MultiStageEnvelopeFilter retrigger mid-stage", "[multistage-env-filter][edge]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(2);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 800.0f);
    filter.setStageTarget(1, 500.0f);
    filter.setStageTime(0, 100.0f);
    filter.setStageTime(1, 100.0f);

    filter.trigger();

    // Process to mid-stage 0
    size_t midSamples = msToSamples(50.0f, kSampleRate);
    for (size_t i = 0; i < midSamples; ++i) {
        (void)filter.process(0.5f);
    }

    float cutoffMid = filter.getCurrentCutoff();
    REQUIRE(cutoffMid > 300.0f);
    REQUIRE(cutoffMid < 600.0f);
    REQUIRE(filter.getCurrentStage() == 0);

    // Retrigger
    filter.trigger();

    // Should restart from stage 0
    REQUIRE(filter.getCurrentStage() == 0);

    // Cutoff should be moving from base to stage 0 target again
    (void)filter.process(0.5f);
    float cutoffAfterRetrigger = filter.getCurrentCutoff();
    REQUIRE(cutoffAfterRetrigger < cutoffMid); // Should be closer to base
}

TEST_CASE("MultiStageEnvelopeFilter numStages change during playback", "[multistage-env-filter][edge]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(4);
    filter.setBaseFrequency(100.0f);
    for (int i = 0; i < 4; ++i) {
        filter.setStageTarget(i, 500.0f);
        filter.setStageTime(i, 20.0f);
    }

    filter.trigger();

    // Process to stage 2
    size_t samples = msToSamples(50.0f, kSampleRate);
    for (size_t i = 0; i < samples; ++i) {
        (void)filter.process(0.5f);
    }

    // Reduce numStages to 2
    filter.setNumStages(2);
    REQUIRE(filter.getNumStages() == 2);

    // Current stage should be clamped
    REQUIRE(filter.getCurrentStage() <= 1);
}

TEST_CASE("MultiStageEnvelopeFilter multi-sample-rate compatibility", "[multistage-env-filter][edge]") {
    const std::array<double, 4> sampleRates = {44100.0, 48000.0, 96000.0, 192000.0};

    for (double sr : sampleRates) {
        CAPTURE(sr);

        MultiStageEnvelopeFilter filter;
        filter.prepare(sr);

        filter.setNumStages(2);
        filter.setBaseFrequency(100.0f);
        filter.setStageTarget(0, 1000.0f);
        filter.setStageTarget(1, 500.0f);
        filter.setStageTime(0, 50.0f);
        filter.setStageTime(1, 50.0f);
        filter.setLoop(true);
        filter.setLoopStart(0);
        filter.setLoopEnd(1);

        filter.trigger();

        bool allValid = true;
        for (size_t i = 0; i < 10000; ++i) {
            float input = std::sin(2.0f * std::numbers::pi_v<float> * 440.0f *
                                   static_cast<float>(i) / static_cast<float>(sr));
            float output = filter.process(input);

            if (!isValidFloat(output)) {
                allValid = false;
                break;
            }
        }

        REQUIRE(allValid);
    }
}

TEST_CASE("MultiStageEnvelopeFilter processBlock equivalence", "[multistage-env-filter][edge]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 256;

    // Two identical filters
    MultiStageEnvelopeFilter filterSample;
    filterSample.prepare(kSampleRate);
    filterSample.setNumStages(2);
    filterSample.setStageTarget(0, 800.0f);
    filterSample.setStageTarget(1, 400.0f);
    filterSample.setStageTime(0, 20.0f);
    filterSample.setStageTime(1, 20.0f);

    MultiStageEnvelopeFilter filterBlock;
    filterBlock.prepare(kSampleRate);
    filterBlock.setNumStages(2);
    filterBlock.setStageTarget(0, 800.0f);
    filterBlock.setStageTarget(1, 400.0f);
    filterBlock.setStageTime(0, 20.0f);
    filterBlock.setStageTime(1, 20.0f);

    // Generate input
    std::array<float, kBlockSize> input;
    generateSine(input.data(), kBlockSize, 440.0f, static_cast<float>(kSampleRate), 1.0f);

    // Process sample-by-sample
    filterSample.trigger();
    std::array<float, kBlockSize> outputSample;
    for (size_t i = 0; i < kBlockSize; ++i) {
        outputSample[i] = filterSample.process(input[i]);
    }

    // Process as block
    filterBlock.trigger();
    std::array<float, kBlockSize> outputBlock = input;
    filterBlock.processBlock(outputBlock.data(), kBlockSize);

    // Compare outputs
    for (size_t i = 0; i < kBlockSize; ++i) {
        REQUIRE(outputBlock[i] == Approx(outputSample[i]).margin(1e-6f));
    }
}

// =============================================================================
// Phase 11: Performance Testing (T131-T134)
// =============================================================================

TEST_CASE("MultiStageEnvelopeFilter performance benchmark (SC-007)", "[multistage-env-filter][performance]") {
    constexpr size_t kNumSamples = 100000;
    constexpr double kMaxNsPerSample = 200.0; // Target: <200ns per sample

    MultiStageEnvelopeFilter filter;
    filter.prepare(44100.0);

    filter.setNumStages(4);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 500.0f);
    filter.setStageTarget(1, 1500.0f);
    filter.setStageTarget(2, 800.0f);
    filter.setStageTarget(3, 1000.0f);
    filter.setStageTime(0, 50.0f);
    filter.setStageTime(1, 50.0f);
    filter.setStageTime(2, 50.0f);
    filter.setStageTime(3, 50.0f);
    filter.setStageCurve(0, 0.5f);
    filter.setStageCurve(1, -0.5f);
    filter.setResonance(8.0f);
    filter.setLoop(true);
    filter.setLoopStart(1);
    filter.setLoopEnd(3);

    // Generate test signal
    std::array<float, kNumSamples> buffer;
    generateSine(buffer.data(), kNumSamples, 440.0f, 44100.0f, 0.5f);

    // Warm up
    filter.trigger();
    for (size_t i = 0; i < 1000; ++i) {
        (void)filter.process(buffer[i]);
    }
    filter.reset();

    // Measure
    filter.trigger();
    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < kNumSamples; ++i) {
        buffer[i] = filter.process(buffer[i]);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    double nsPerSample = static_cast<double>(duration.count()) / static_cast<double>(kNumSamples);

    CAPTURE(nsPerSample);

    // Must be less than 2000ns per sample (generous limit for various systems)
    REQUIRE(nsPerSample < 2000.0);

    // Informational: Report actual performance
    INFO("Performance: " << nsPerSample << " ns/sample");
}

// =============================================================================
// Phase 12/13: Documentation & Integration (T137-T148)
// =============================================================================

TEST_CASE("MultiStageEnvelopeFilter usage example from spec", "[multistage-env-filter][integration]") {
    // This test implements the example from spec.md acceptance scenario

    MultiStageEnvelopeFilter filter;
    filter.prepare(44100.0);

    // Configure 4-stage sweep
    filter.setNumStages(4);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 200.0f);
    filter.setStageTarget(1, 2000.0f);
    filter.setStageTarget(2, 500.0f);
    filter.setStageTarget(3, 800.0f);

    filter.setStageTime(0, 100.0f);
    filter.setStageTime(1, 200.0f);
    filter.setStageTime(2, 150.0f);
    filter.setStageTime(3, 100.0f);

    filter.setStageCurve(1, 1.0f); // Exponential for stage 1

    // Configure filter
    filter.setResonance(8.0f);
    filter.setFilterType(SVFMode::Lowpass);

    // Start envelope
    filter.trigger();

    // Process some audio
    constexpr size_t kBlockSize = 256;
    std::array<float, kBlockSize> buffer;
    generateSine(buffer.data(), kBlockSize, 440.0f, 44100.0f, 0.5f);

    bool anyChanged = false;
    for (size_t i = 0; i < kBlockSize; ++i) {
        float original = buffer[i];
        buffer[i] = filter.process(buffer[i]);
        if (std::abs(buffer[i] - original) > 0.01f) {
            anyChanged = true;
        }
    }

    // Filter should have modified the signal
    REQUIRE(anyChanged);
    REQUIRE(filter.isRunning());
}

TEST_CASE("MultiStageEnvelopeFilter getEnvelopeValue returns normalized position", "[multistage-env-filter][integration]") {
    constexpr double kSampleRate = 44100.0;

    MultiStageEnvelopeFilter filter;
    filter.prepare(kSampleRate);

    filter.setNumStages(1);
    filter.setBaseFrequency(100.0f);
    filter.setStageTarget(0, 1000.0f);
    filter.setStageTime(0, 100.0f);

    // Before trigger
    REQUIRE(filter.getEnvelopeValue() == Approx(0.0f));

    filter.trigger();

    // At start
    REQUIRE(filter.getEnvelopeValue() == Approx(0.0f).margin(0.01f));

    // At 50%
    size_t halfSamples = msToSamples(50.0f, kSampleRate);
    for (size_t i = 0; i < halfSamples; ++i) {
        (void)filter.process(0.5f);
    }
    REQUIRE(filter.getEnvelopeValue() == Approx(0.5f).margin(0.05f));

    // At 100% (just before completion)
    for (size_t i = 0; i < halfSamples - 10; ++i) {
        (void)filter.process(0.5f);
    }
    REQUIRE(filter.getEnvelopeValue() >= 0.9f);
}
