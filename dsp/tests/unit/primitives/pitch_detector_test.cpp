// ==============================================================================
// Unit Tests: PitchDetector
// ==============================================================================
// Layer 1: DSP Primitive Tests
// Constitution Principle VIII: DSP algorithms must be independently testable
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/pitch_detector.h>

#include <cmath>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

namespace {

constexpr float kTestSampleRate = 44100.0f;
constexpr float kTwoPi = 6.283185307179586f;

// Generate a sine wave at specified frequency
void generateSine(std::vector<float>& buffer, float frequency, float sampleRate) {
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

// Generate white noise
void generateNoise(std::vector<float>& buffer, unsigned int seed = 42) {
    // Simple LCG for reproducible noise
    unsigned int state = seed;
    for (size_t i = 0; i < buffer.size(); ++i) {
        state = state * 1103515245 + 12345;
        buffer[i] = (static_cast<float>(state % 65536) / 32768.0f) - 1.0f;
    }
}

} // namespace

// ==============================================================================
// Basic Functionality Tests
// ==============================================================================

TEST_CASE("PitchDetector: Prepare and reset", "[pitch_detector]") {
    PitchDetector detector;

    SECTION("Prepare with default window size") {
        detector.prepare(kTestSampleRate);
        // Should not crash, default period should be reasonable
        float defaultPeriod = detector.getDefaultPeriod();
        CHECK(defaultPeriod > 0.0f);
        CHECK(defaultPeriod < kTestSampleRate);  // Less than 1 second
    }

    SECTION("Prepare with custom window size") {
        detector.prepare(kTestSampleRate, 512);
        CHECK(detector.getDefaultPeriod() > 0.0f);
    }

    SECTION("Reset clears state") {
        detector.prepare(kTestSampleRate);

        // Push some samples
        std::vector<float> buffer(256);
        generateSine(buffer, 440.0f, kTestSampleRate);
        detector.pushBlock(buffer.data(), buffer.size());

        // Reset and verify
        detector.reset();
        CHECK(detector.getConfidence() == 0.0f);
    }
}

// ==============================================================================
// Pitch Detection Tests
// ==============================================================================

TEST_CASE("PitchDetector: Detect sine wave pitch", "[pitch_detector]") {
    PitchDetector detector;
    detector.prepare(kTestSampleRate);

    SECTION("Detect 440 Hz (A4)") {
        std::vector<float> buffer(2048);
        generateSine(buffer, 440.0f, kTestSampleRate);

        detector.pushBlock(buffer.data(), buffer.size());
        detector.detect();

        float detectedFreq = detector.getDetectedFrequency();
        float expectedFreq = 440.0f;

        // Allow 5% tolerance (pitch detection isn't perfect)
        CHECK(detectedFreq == Approx(expectedFreq).epsilon(0.05f));
        CHECK(detector.isPitchValid());
    }

    SECTION("Detect 200 Hz") {
        std::vector<float> buffer(2048);
        generateSine(buffer, 200.0f, kTestSampleRate);

        detector.pushBlock(buffer.data(), buffer.size());
        detector.detect();

        float detectedFreq = detector.getDetectedFrequency();
        // Lower frequencies have more tolerance due to limited analysis window
        CHECK(detectedFreq == Approx(200.0f).epsilon(0.15f));
        CHECK(detector.isPitchValid());
    }

    SECTION("Detect 800 Hz") {
        std::vector<float> buffer(2048);
        generateSine(buffer, 800.0f, kTestSampleRate);

        detector.pushBlock(buffer.data(), buffer.size());
        detector.detect();

        float detectedFreq = detector.getDetectedFrequency();
        CHECK(detectedFreq == Approx(800.0f).epsilon(0.05f));
        CHECK(detector.isPitchValid());
    }
}

TEST_CASE("PitchDetector: Handle unpitched content", "[pitch_detector]") {
    PitchDetector detector;
    detector.prepare(kTestSampleRate);

    SECTION("White noise has low confidence") {
        std::vector<float> buffer(2048);
        generateNoise(buffer);

        detector.pushBlock(buffer.data(), buffer.size());
        detector.detect();

        // Noise should have low confidence
        CHECK(detector.getConfidence() < 0.5f);
    }

    SECTION("Silence returns default period") {
        std::vector<float> buffer(2048, 0.0f);

        detector.pushBlock(buffer.data(), buffer.size());
        detector.detect();

        // Silence should return default period
        float period = detector.getDetectedPeriod();
        CHECK(period == Approx(detector.getDefaultPeriod()).margin(1.0f));
    }
}

// ==============================================================================
// Latency and Performance Tests
// ==============================================================================

TEST_CASE("PitchDetector: Typical shimmer feedback frequencies", "[pitch_detector]") {
    PitchDetector detector;
    detector.prepare(kTestSampleRate);

    // Shimmer feedback is typically rich harmonics from shifted audio
    // Test frequencies common in shimmer feedback path

    SECTION("330 Hz (typical shimmer fundamental)") {
        std::vector<float> buffer(2048);
        generateSine(buffer, 330.0f, kTestSampleRate);

        detector.pushBlock(buffer.data(), buffer.size());
        detector.detect();

        float period = detector.getDetectedPeriod();
        float expectedPeriod = kTestSampleRate / 330.0f;  // ~133.6 samples

        CHECK(period == Approx(expectedPeriod).epsilon(0.1f));

        // Latency check: period should be much less than 46ms (current granular)
        float latencyMs = period / kTestSampleRate * 1000.0f;
        CHECK(latencyMs < 10.0f);  // Should be < 10ms for this frequency
    }

    SECTION("660 Hz (octave shifted shimmer)") {
        std::vector<float> buffer(2048);
        generateSine(buffer, 660.0f, kTestSampleRate);

        detector.pushBlock(buffer.data(), buffer.size());
        detector.detect();

        float period = detector.getDetectedPeriod();
        float expectedPeriod = kTestSampleRate / 660.0f;  // ~66.8 samples

        CHECK(period == Approx(expectedPeriod).epsilon(0.1f));

        float latencyMs = period / kTestSampleRate * 1000.0f;
        CHECK(latencyMs < 5.0f);  // Should be < 5ms for this frequency
    }
}

// ==============================================================================
// Edge Cases
// ==============================================================================

TEST_CASE("PitchDetector: Edge cases", "[pitch_detector]") {
    PitchDetector detector;
    detector.prepare(kTestSampleRate);

    SECTION("Very low frequency (near limit)") {
        std::vector<float> buffer(4096);
        generateSine(buffer, 60.0f, kTestSampleRate);  // Near 50 Hz limit

        detector.pushBlock(buffer.data(), buffer.size());
        detector.detect();

        // Should either detect correctly or return default
        CHECK(detector.getDetectedPeriod() > 0.0f);
    }

    SECTION("Very high frequency (near limit)") {
        std::vector<float> buffer(2048);
        generateSine(buffer, 900.0f, kTestSampleRate);  // Near 1000 Hz limit

        detector.pushBlock(buffer.data(), buffer.size());
        detector.detect();

        // Should detect correctly
        CHECK(detector.isPitchValid());
        CHECK(detector.getDetectedFrequency() == Approx(900.0f).epsilon(0.1f));
    }

    SECTION("DC offset doesn't break detection") {
        std::vector<float> buffer(2048);
        generateSine(buffer, 440.0f, kTestSampleRate);

        // Add DC offset
        for (auto& s : buffer) {
            s += 0.5f;
        }

        detector.pushBlock(buffer.data(), buffer.size());
        detector.detect();

        // Should still detect the pitch
        float detectedFreq = detector.getDetectedFrequency();
        CHECK(detectedFreq == Approx(440.0f).epsilon(0.1f));
    }
}
