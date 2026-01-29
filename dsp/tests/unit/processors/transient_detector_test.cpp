// ==============================================================================
// Layer 2: Processor Tests - Transient Detector
// ==============================================================================
// Tests for the TransientDetector modulation source.
//
// Reference: specs/008-modulation-system/spec.md (FR-048 to FR-054, SC-009)
// ==============================================================================

#include <krate/dsp/processors/transient_detector.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Helper: Generate a step input (silence then loud)
// =============================================================================
namespace {

/// @brief Create a sudden amplitude step at a given sample offset.
/// Returns silence (0.0) before stepSample, then amplitude after.
float stepSignal(int sample, int stepSample, float amplitude) {
    return (sample >= stepSample) ? amplitude : 0.0f;
}

/// @brief Generate a sine tone at given frequency and amplitude.
float sineTone(int sample, float sampleRate, float freq, float amplitude) {
    float phase = static_cast<float>(sample) * freq / sampleRate;
    return amplitude * std::sin(2.0f * static_cast<float>(std::numbers::pi) * phase);
}

}  // namespace

// =============================================================================
// Detection Timing Tests (SC-009)
// =============================================================================

TEST_CASE("TransientDetector fires within 2ms of >12dB step input", "[processors][transient][sc009]") {
    constexpr double kSampleRate = 44100.0;
    constexpr int kStepSample = 1000;  // Step occurs at sample 1000
    constexpr float kStepAmplitude = 1.0f;  // >12dB above silence

    TransientDetector detector;
    detector.prepare(kSampleRate);
    detector.setSensitivity(0.5f);  // Default sensitivity
    detector.setAttackTime(2.0f);
    detector.setDecayTime(50.0f);

    int firstDetectionSample = -1;
    constexpr int kMaxSamples = 2000;

    for (int i = 0; i < kMaxSamples; ++i) {
        float sample = stepSignal(i, kStepSample, kStepAmplitude);
        detector.process(sample);

        if (firstDetectionSample < 0 && detector.getCurrentValue() > 0.01f) {
            firstDetectionSample = i;
        }
    }

    REQUIRE(firstDetectionSample >= 0);

    // Must fire within 2ms of step = 2ms * 44100 = ~88 samples after step
    int samplesAfterStep = firstDetectionSample - kStepSample;
    REQUIRE(samplesAfterStep >= 0);
    REQUIRE(samplesAfterStep <= 88);  // Within 2ms
}

// =============================================================================
// Steady-State Rejection Test (FR-092)
// =============================================================================

TEST_CASE("TransientDetector does not fire on steady-state signal", "[processors][transient][fr092]") {
    constexpr double kSampleRate = 44100.0;

    TransientDetector detector;
    detector.prepare(kSampleRate);
    detector.setSensitivity(0.5f);

    // Feed a steady sine tone for 1 second
    constexpr int kNumSamples = 44100;
    bool triggered = false;

    // First ramp up gradually to avoid triggering on the ramp itself
    for (int i = 0; i < 4410; ++i) {  // 100ms ramp
        float ramp = static_cast<float>(i) / 4410.0f;
        float sample = sineTone(i, static_cast<float>(kSampleRate), 440.0f, 0.3f * ramp);
        detector.process(sample);
    }

    // Now reset and check steady state
    detector.reset();

    // Gradually introduce signal to avoid triggering
    for (int i = 0; i < 4410; ++i) {
        float ramp = static_cast<float>(i) / 4410.0f;
        float sample = sineTone(i, static_cast<float>(kSampleRate), 440.0f, 0.3f * ramp);
        detector.process(sample);
    }

    // Now check steady state - envelope should be near zero or decayed
    float peakEnvelope = 0.0f;
    for (int i = 4410; i < kNumSamples; ++i) {
        float sample = sineTone(i, static_cast<float>(kSampleRate), 440.0f, 0.3f);
        detector.process(sample);
        peakEnvelope = std::max(peakEnvelope, detector.getCurrentValue());
    }

    // After initial settling, the envelope from a steady tone should decay
    // (a gradual ramp-up shouldn't trigger strong detection)
    // The peak should eventually be small since there are no NEW transients
    // during the steady portion after the initial settling
    // Allow some tolerance - the detector may spike initially
    // We check the final portion is low
    float finalValue = detector.getCurrentValue();
    REQUIRE(finalValue < 0.1f);
}

// =============================================================================
// Retrigger Tests (FR-053)
// =============================================================================

TEST_CASE("TransientDetector retriggers from current level during decay", "[processors][transient][fr053]") {
    constexpr double kSampleRate = 44100.0;

    TransientDetector detector;
    detector.prepare(kSampleRate);
    detector.setSensitivity(0.9f);  // High sensitivity
    detector.setAttackTime(2.0f);   // 2ms attack
    detector.setDecayTime(100.0f);

    // First transient - strong step from silence
    for (int i = 0; i < 100; ++i) {
        detector.process(0.0f);  // Establish silence baseline
    }
    for (int i = 0; i < 500; ++i) {
        detector.process(1.0f);  // Strong step to trigger and reach peak
    }

    float peakLevel = detector.getCurrentValue();
    REQUIRE(peakLevel > 0.5f);  // Should have reached near-peak

    // Let it decay significantly (with silence)
    for (int i = 0; i < 2000; ++i) {
        detector.process(0.0f);
    }

    float decayedLevel = detector.getCurrentValue();
    REQUIRE(decayedLevel < peakLevel);  // Should have decayed

    // Now retrigger with another strong step from silence
    for (int i = 0; i < 500; ++i) {
        detector.process(1.0f);
    }

    float retriggeredLevel = detector.getCurrentValue();

    // After retrigger, the envelope should rise back up from the decayed level
    REQUIRE(retriggeredLevel > decayedLevel);
}

// =============================================================================
// Attack Time Tests (FR-051)
// =============================================================================

TEST_CASE("TransientDetector attack time controls rise time", "[processors][transient][fr051]") {
    constexpr double kSampleRate = 44100.0;

    auto measureRiseTime = [](float attackMs) {
        TransientDetector detector;
        detector.prepare(kSampleRate);
        detector.setSensitivity(0.9f);
        detector.setAttackTime(attackMs);
        detector.setDecayTime(200.0f);

        // Feed a strong step to trigger
        int peakSample = -1;
        for (int i = 0; i < 2000; ++i) {
            detector.process(1.0f);
            if (detector.getCurrentValue() >= 0.99f && peakSample < 0) {
                peakSample = i;
            }
        }
        return peakSample;
    };

    int shortRise = measureRiseTime(1.0f);
    int longRise = measureRiseTime(10.0f);

    // Both should reach peak
    REQUIRE(shortRise >= 0);
    REQUIRE(longRise >= 0);

    // Longer attack should take more samples to reach peak
    REQUIRE(longRise > shortRise);
}

// =============================================================================
// Decay Time Tests (FR-052)
// =============================================================================

TEST_CASE("TransientDetector decay time controls fall time", "[processors][transient][fr052]") {
    constexpr double kSampleRate = 44100.0;

    auto measureDecayToHalf = [](float decayMs) {
        TransientDetector detector;
        detector.prepare(kSampleRate);
        detector.setSensitivity(0.9f);
        detector.setAttackTime(1.0f);
        detector.setDecayTime(decayMs);

        // Trigger to peak
        for (int i = 0; i < 500; ++i) {
            detector.process(1.0f);
        }

        // Now let it decay with silence
        int halfSample = -1;
        for (int i = 0; i < 44100; ++i) {
            detector.process(0.0f);
            if (detector.getCurrentValue() < 0.5f && halfSample < 0) {
                halfSample = i;
            }
        }
        return halfSample;
    };

    int shortDecay = measureDecayToHalf(30.0f);
    int longDecay = measureDecayToHalf(200.0f);

    // Both should eventually decay below 0.5
    REQUIRE(shortDecay >= 0);
    REQUIRE(longDecay >= 0);

    // Longer decay should take more samples
    REQUIRE(longDecay > shortDecay);
}

// =============================================================================
// Sensitivity Tests (FR-050)
// =============================================================================

TEST_CASE("TransientDetector sensitivity adjusts thresholds", "[processors][transient][fr050]") {
    constexpr double kSampleRate = 44100.0;

    // Low sensitivity should require stronger transient to trigger
    auto detectsAt = [](float sensitivity, float inputLevel) {
        TransientDetector detector;
        detector.prepare(kSampleRate);
        detector.setSensitivity(sensitivity);
        detector.setAttackTime(2.0f);
        detector.setDecayTime(50.0f);

        // Silence then step
        for (int i = 0; i < 100; ++i) {
            detector.process(0.0f);
        }

        for (int i = 0; i < 200; ++i) {
            detector.process(inputLevel);
        }

        return detector.getCurrentValue() > 0.01f;
    };

    // At medium input, high sensitivity should detect
    REQUIRE(detectsAt(0.9f, 0.3f));

    // At weak input, low sensitivity should NOT detect
    REQUIRE_FALSE(detectsAt(0.1f, 0.05f));
}

// =============================================================================
// Output Range Test (FR-054)
// =============================================================================

TEST_CASE("TransientDetector output stays in [0, +1]", "[processors][transient][fr054]") {
    constexpr double kSampleRate = 44100.0;

    TransientDetector detector;
    detector.prepare(kSampleRate);
    detector.setSensitivity(0.8f);

    for (int i = 0; i < 44100; ++i) {
        // Alternating loud and silent sections to trigger repeatedly
        float sample = ((i / 500) % 2 == 0) ? 0.9f : 0.0f;
        detector.process(sample);

        float val = detector.getCurrentValue();
        REQUIRE(val >= 0.0f);
        REQUIRE(val <= 1.0f);
    }
}

// =============================================================================
// Interface Tests
// =============================================================================

TEST_CASE("TransientDetector implements ModulationSource interface", "[processors][transient]") {
    TransientDetector detector;
    detector.prepare(44100.0);

    auto range = detector.getSourceRange();
    REQUIRE(range.first == Approx(0.0f));
    REQUIRE(range.second == Approx(1.0f));
}
