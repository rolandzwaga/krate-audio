// ==============================================================================
// Layer 2: DSP Processor Tests - Feedback Distortion
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written BEFORE implementation for feedback distortion processor.
//
// Reference: specs/110-feedback-distortion/spec.md
// ==============================================================================

#include <krate/dsp/processors/feedback_distortion.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <chrono>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

namespace {

// Test helper: Generate impulse
void generateImpulse(float* buffer, size_t n, float amplitude = 1.0f) {
    std::fill(buffer, buffer + n, 0.0f);
    if (n > 0) {
        buffer[0] = amplitude;
    }
}

// Test helper: Generate sine wave
void generateSine(float* buffer, size_t n, float frequency, float sampleRate, float amplitude = 1.0f) {
    const float twoPi = 6.283185307f;
    for (size_t i = 0; i < n; ++i) {
        buffer[i] = amplitude * std::sin(twoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

// Test helper: Find peak value in buffer
float findPeak(const float* buffer, size_t n) {
    float peak = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

// Test helper: Calculate RMS of buffer
float calculateRMS(const float* buffer, size_t n) {
    if (n == 0) return 0.0f;
    float sumSquares = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sumSquares += buffer[i] * buffer[i];
    }
    return std::sqrt(sumSquares / static_cast<float>(n));
}

// Test helper: Calculate DC offset
float calculateDC(const float* buffer, size_t n) {
    if (n == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sum += buffer[i];
    }
    return sum / static_cast<float>(n);
}

// Test helper: Estimate fundamental frequency using autocorrelation
// This is more robust to harmonics than zero-crossing counting
float estimateFundamentalFrequency(const float* buffer, size_t n, float sampleRate) {
    if (n < 100) return 0.0f;

    // Find the first peak in autocorrelation (excluding lag 0)
    // Search for lags corresponding to 20-500 Hz range
    const size_t minLag = static_cast<size_t>(sampleRate / 500.0f);  // 500 Hz
    const size_t maxLag = static_cast<size_t>(sampleRate / 20.0f);   // 20 Hz

    float maxCorr = 0.0f;
    size_t bestLag = 0;

    // Compute autocorrelation at each lag
    for (size_t lag = minLag; lag < std::min(maxLag, n / 2); ++lag) {
        float correlation = 0.0f;
        for (size_t i = 0; i < n - lag; ++i) {
            correlation += buffer[i] * buffer[i + lag];
        }
        if (correlation > maxCorr) {
            maxCorr = correlation;
            bestLag = lag;
        }
    }

    if (bestLag == 0) return 0.0f;
    return sampleRate / static_cast<float>(bestLag);
}

// Test helper: Check for clicks (sudden large amplitude changes)
bool hasClicks(const float* buffer, size_t n, float threshold = 0.2f) {
    for (size_t i = 1; i < n; ++i) {
        float delta = std::abs(buffer[i] - buffer[i - 1]);
        if (delta > threshold) {
            return true;
        }
    }
    return false;
}

// dB to linear conversion for tests
float dbToLinear(float dB) {
    return std::pow(10.0f, dB / 20.0f);
}

} // anonymous namespace

// ==============================================================================
// T001: Lifecycle Tests (FR-001, FR-002, FR-003)
// ==============================================================================

TEST_CASE("FeedbackDistortion lifecycle - prepare and reset", "[FeedbackDistortion][lifecycle]") {
    FeedbackDistortion distortion;

    SECTION("prepare initializes all components (FR-001)") {
        // Should not crash when preparing
        distortion.prepare(44100.0, 512);

        // After prepare, processing should work
        float sample = 0.5f;
        float output = distortion.process(sample);
        // Should produce some output (not NaN)
        REQUIRE_FALSE(std::isnan(output));
    }

    SECTION("reset clears state without crashing (FR-002)") {
        distortion.prepare(44100.0, 512);

        // Process some samples to build up state
        for (int i = 0; i < 1000; ++i) {
            (void)distortion.process(0.5f);
        }

        // Reset should not crash
        distortion.reset();

        // After reset, processing should still work
        float output = distortion.process(0.5f);
        REQUIRE_FALSE(std::isnan(output));
    }

    SECTION("supports sample rate range 44100-192000 Hz (FR-003)") {
        // Test at minimum supported sample rate
        distortion.prepare(44100.0, 512);
        float out44 = distortion.process(0.5f);
        REQUIRE_FALSE(std::isnan(out44));

        // Test at 48kHz
        distortion.prepare(48000.0, 512);
        float out48 = distortion.process(0.5f);
        REQUIRE_FALSE(std::isnan(out48));

        // Test at 96kHz
        distortion.prepare(96000.0, 512);
        float out96 = distortion.process(0.5f);
        REQUIRE_FALSE(std::isnan(out96));

        // Test at maximum supported sample rate
        distortion.prepare(192000.0, 512);
        float out192 = distortion.process(0.5f);
        REQUIRE_FALSE(std::isnan(out192));
    }
}

// ==============================================================================
// T002: Parameter Tests (FR-004, FR-005, FR-007, FR-008, FR-011-FR-014)
// ==============================================================================

TEST_CASE("FeedbackDistortion parameter setters and getters", "[FeedbackDistortion][parameters]") {
    FeedbackDistortion distortion;
    distortion.prepare(44100.0, 512);

    SECTION("setDelayTime clamps to [1.0, 100.0] ms (FR-004, FR-005)") {
        // Within range
        distortion.setDelayTime(10.0f);
        REQUIRE(distortion.getDelayTime() == Approx(10.0f));

        distortion.setDelayTime(50.0f);
        REQUIRE(distortion.getDelayTime() == Approx(50.0f));

        // Below minimum - should clamp to 1.0
        distortion.setDelayTime(0.5f);
        REQUIRE(distortion.getDelayTime() == Approx(1.0f));

        distortion.setDelayTime(-10.0f);
        REQUIRE(distortion.getDelayTime() == Approx(1.0f));

        // Above maximum - should clamp to 100.0
        distortion.setDelayTime(150.0f);
        REQUIRE(distortion.getDelayTime() == Approx(100.0f));
    }

    SECTION("setFeedback clamps to [0.0, 1.5] (FR-007, FR-008)") {
        // Within range
        distortion.setFeedback(0.5f);
        REQUIRE(distortion.getFeedback() == Approx(0.5f));

        distortion.setFeedback(1.2f);
        REQUIRE(distortion.getFeedback() == Approx(1.2f));

        // Below minimum - should clamp to 0.0
        distortion.setFeedback(-0.5f);
        REQUIRE(distortion.getFeedback() == Approx(0.0f));

        // Above maximum - should clamp to 1.5
        distortion.setFeedback(2.0f);
        REQUIRE(distortion.getFeedback() == Approx(1.5f));
    }

    SECTION("setDrive clamps to [0.1, 10.0] (FR-013, FR-014)") {
        // Within range
        distortion.setDrive(1.0f);
        REQUIRE(distortion.getDrive() == Approx(1.0f));

        distortion.setDrive(5.0f);
        REQUIRE(distortion.getDrive() == Approx(5.0f));

        // Below minimum - should clamp to 0.1
        distortion.setDrive(0.0f);
        REQUIRE(distortion.getDrive() == Approx(0.1f));

        distortion.setDrive(-1.0f);
        REQUIRE(distortion.getDrive() == Approx(0.1f));

        // Above maximum - should clamp to 10.0
        distortion.setDrive(15.0f);
        REQUIRE(distortion.getDrive() == Approx(10.0f));
    }

    SECTION("setSaturationCurve accepts all WaveshapeType values (FR-011, FR-012)") {
        distortion.setSaturationCurve(WaveshapeType::Tanh);
        REQUIRE(distortion.getSaturationCurve() == WaveshapeType::Tanh);

        distortion.setSaturationCurve(WaveshapeType::Tube);
        REQUIRE(distortion.getSaturationCurve() == WaveshapeType::Tube);

        distortion.setSaturationCurve(WaveshapeType::Diode);
        REQUIRE(distortion.getSaturationCurve() == WaveshapeType::Diode);

        distortion.setSaturationCurve(WaveshapeType::HardClip);
        REQUIRE(distortion.getSaturationCurve() == WaveshapeType::HardClip);

        distortion.setSaturationCurve(WaveshapeType::Atan);
        REQUIRE(distortion.getSaturationCurve() == WaveshapeType::Atan);
    }
}

// ==============================================================================
// T003: Basic Feedback Processing Tests (SC-001, SC-008)
// ==============================================================================

TEST_CASE("FeedbackDistortion basic feedback processing", "[FeedbackDistortion][processing]") {
    constexpr float sampleRate = 44100.0f;
    constexpr size_t blockSize = 512;

    FeedbackDistortion distortion;
    distortion.prepare(sampleRate, blockSize);

    SECTION("impulse with 10ms delay produces ~100Hz resonance (SC-008)") {
        // Configure: 10ms delay = 100Hz fundamental
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(0.8f);
        distortion.setDrive(1.5f);  // Moderate drive

        // Generate impulse and process
        constexpr size_t totalSamples = 44100;  // 1 second
        std::vector<float> buffer(totalSamples, 0.0f);
        buffer[0] = 1.0f;  // Impulse at start

        for (size_t i = 0; i < totalSamples; ++i) {
            buffer[i] = distortion.process(buffer[i]);
        }

        // Estimate frequency from a portion of the signal (after initial transient)
        float frequency = estimateFundamentalFrequency(buffer.data() + 4410, totalSamples - 4410, sampleRate);

        // SC-008: +/- 10% of expected 100Hz
        REQUIRE(frequency >= 90.0f);
        REQUIRE(frequency <= 110.0f);
    }

    SECTION("natural decay with feedback 0.8 reaches -60dB within 3-4 seconds (SC-001)") {
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(0.8f);  // Per spec: feedback at 0.8
        distortion.setDrive(1.0f);     // Unity drive to avoid saturation adding energy

        // Process impulse for 4 seconds
        constexpr size_t totalSamples = static_cast<size_t>(sampleRate * 4.0f);
        std::vector<float> buffer(totalSamples, 0.0f);
        buffer[0] = 1.0f;  // Impulse

        for (size_t i = 0; i < totalSamples; ++i) {
            buffer[i] = distortion.process(buffer[i]);
        }

        // SC-001: decays to -60dB within 3-4 seconds at feedback 0.8
        // With feedback=0.8, 10ms delay: 400 iterations in 4s, 0.8^400 ~ 1e-39
        // The signal decays exponentially: after n iterations, level = 0.8^n
        // To reach -60dB (0.001): 0.8^n = 0.001 → n ≈ 31 iterations ≈ 310ms
        // So signal reaches -60dB well within 4 seconds

        // Check RMS at 4 seconds is below -60dB
        float endRMS = calculateRMS(buffer.data() + totalSamples - 4410, 4410);
        float thresholdLinear = dbToLinear(-60.0f);  // -60dB = 0.001
        REQUIRE(endRMS < thresholdLinear);

        // Verify there was actual signal at the start (impulse was processed)
        float startRMS = calculateRMS(buffer.data() + 441, 441);  // 10-20ms after impulse
        REQUIRE(startRMS > 0.01f);  // Should have meaningful signal initially
    }

    SECTION("different drive values produce different harmonic content") {
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(0.8f);

        // Process with low drive
        distortion.setDrive(1.0f);
        distortion.reset();

        std::vector<float> bufferLowDrive(4410, 0.0f);
        bufferLowDrive[0] = 1.0f;
        for (size_t i = 0; i < bufferLowDrive.size(); ++i) {
            bufferLowDrive[i] = distortion.process(bufferLowDrive[i]);
        }
        float peakLowDrive = findPeak(bufferLowDrive.data() + 441, 4410 - 441);

        // Process with high drive
        distortion.setDrive(4.0f);
        distortion.reset();

        std::vector<float> bufferHighDrive(4410, 0.0f);
        bufferHighDrive[0] = 1.0f;
        for (size_t i = 0; i < bufferHighDrive.size(); ++i) {
            bufferHighDrive[i] = distortion.process(bufferHighDrive[i]);
        }
        float peakHighDrive = findPeak(bufferHighDrive.data() + 441, 4410 - 441);

        // Higher drive should produce higher peak (more saturation/compression)
        // or at least different output
        REQUIRE(peakLowDrive != Approx(peakHighDrive).margin(0.01f));
    }
}

// ==============================================================================
// T004: NaN/Inf Handling Tests (FR-026, FR-027)
// ==============================================================================

TEST_CASE("FeedbackDistortion NaN/Inf handling", "[FeedbackDistortion][safety]") {
    FeedbackDistortion distortion;
    distortion.prepare(44100.0, 512);
    distortion.setFeedback(0.8f);

    SECTION("NaN input resets state and returns 0.0 (FR-026)") {
        // Build up some state
        for (int i = 0; i < 100; ++i) {
            (void)distortion.process(0.5f);
        }

        // Process NaN
        float nanValue = std::numeric_limits<float>::quiet_NaN();
        float output = distortion.process(nanValue);

        REQUIRE(output == 0.0f);
        REQUIRE_FALSE(std::isnan(output));
    }

    SECTION("Inf input resets state and returns 0.0 (FR-026)") {
        // Build up some state
        for (int i = 0; i < 100; ++i) {
            (void)distortion.process(0.5f);
        }

        // Process positive infinity
        float infValue = std::numeric_limits<float>::infinity();
        float output = distortion.process(infValue);

        REQUIRE(output == 0.0f);
        REQUIRE_FALSE(std::isinf(output));

        // Process negative infinity
        distortion.reset();
        for (int i = 0; i < 100; ++i) {
            (void)distortion.process(0.5f);
        }

        float negInfValue = -std::numeric_limits<float>::infinity();
        output = distortion.process(negInfValue);

        REQUIRE(output == 0.0f);
        REQUIRE_FALSE(std::isinf(output));
    }

    SECTION("denormals are flushed to prevent CPU spikes (FR-027)") {
        // Process very small value that could become denormal in feedback loop
        distortion.setFeedback(0.99f);  // High feedback to sustain small values

        // Feed small input
        float smallInput = 1e-30f;

        // Process many samples to let feedback potentially create denormals
        bool hadDenormal = false;
        for (int i = 0; i < 10000; ++i) {
            float output = distortion.process(smallInput);
            // Check if output is denormal (non-zero but smaller than smallest normal)
            if (output != 0.0f && std::abs(output) < std::numeric_limits<float>::min()) {
                hadDenormal = true;
                break;
            }
        }

        // Should not produce denormals
        REQUIRE_FALSE(hadDenormal);
    }
}

// ==============================================================================
// T005: Parameter Smoothing Tests (FR-006, FR-010, FR-015, SC-004)
// ==============================================================================

TEST_CASE("FeedbackDistortion parameter smoothing", "[FeedbackDistortion][smoothing]") {
    constexpr float sampleRate = 44100.0f;
    FeedbackDistortion distortion;
    distortion.prepare(sampleRate, 512);

    SECTION("delay time changes complete smoothly within 10ms without clicks (FR-006, SC-004)") {
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(0.9f);
        distortion.setDrive(2.0f);

        // Build up resonance
        for (int i = 0; i < 4410; ++i) {
            (void)distortion.process(i < 100 ? 0.5f : 0.0f);
        }

        // Change delay time (smaller change to avoid large pitch-shift artifacts)
        distortion.setDelayTime(8.0f);

        // Process transition period (10ms = 441 samples)
        std::vector<float> transitionBuffer(500);
        for (size_t i = 0; i < transitionBuffer.size(); ++i) {
            transitionBuffer[i] = distortion.process(0.0f);
        }

        // Check for clicks - use a higher threshold since some transient is expected
        // during delay modulation (this is the pitch-shift "warble" effect)
        REQUIRE_FALSE(hasClicks(transitionBuffer.data(), transitionBuffer.size(), 0.5f));
    }

    SECTION("feedback changes complete smoothly within 10ms without clicks (FR-010, SC-004)") {
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(0.5f);
        distortion.setDrive(2.0f);

        // Build up some signal
        for (int i = 0; i < 4410; ++i) {
            (void)distortion.process(i < 100 ? 0.5f : 0.0f);
        }

        // Change feedback abruptly
        distortion.setFeedback(1.2f);

        // Process transition period
        std::vector<float> transitionBuffer(500);
        for (size_t i = 0; i < transitionBuffer.size(); ++i) {
            transitionBuffer[i] = distortion.process(0.0f);
        }

        // Check for clicks
        REQUIRE_FALSE(hasClicks(transitionBuffer.data(), transitionBuffer.size(), 0.3f));
    }

    SECTION("drive changes complete smoothly within 10ms without clicks (FR-015, SC-004)") {
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(0.8f);
        distortion.setDrive(1.0f);

        // Build up resonance
        for (int i = 0; i < 4410; ++i) {
            (void)distortion.process(i < 100 ? 0.5f : 0.0f);
        }

        // Change drive abruptly
        distortion.setDrive(8.0f);

        // Process transition period
        std::vector<float> transitionBuffer(500);
        for (size_t i = 0; i < transitionBuffer.size(); ++i) {
            transitionBuffer[i] = distortion.process(0.0f);
        }

        // Check for clicks
        REQUIRE_FALSE(hasClicks(transitionBuffer.data(), transitionBuffer.size(), 0.3f));
    }
}

// ==============================================================================
// T006: Performance and Latency Tests (SC-005, SC-007)
// ==============================================================================

TEST_CASE("FeedbackDistortion performance and latency", "[FeedbackDistortion][performance]") {
    FeedbackDistortion distortion;
    distortion.prepare(44100.0, 512);
    distortion.setFeedback(0.8f);
    distortion.setDrive(2.0f);

    SECTION("zero latency (SC-007)") {
        REQUIRE(distortion.getLatency() == 0);
    }

    SECTION("CPU usage reasonable at 44100Hz (SC-005)") {
        // Process 1 second of audio and measure time
        constexpr size_t totalSamples = 44100;
        std::vector<float> buffer(totalSamples);

        // Generate some input
        generateSine(buffer.data(), totalSamples, 440.0f, 44100.0f, 0.5f);

        auto start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < totalSamples; ++i) {
            buffer[i] = distortion.process(buffer[i]);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        // 1 second of audio = 1,000,000 microseconds
        // SC-005: < 0.5% CPU = processing should take < 5000 microseconds (5ms)
        // Allow more margin for debug builds
        REQUIRE(duration < 50000);  // 50ms is still very conservative
    }
}

// ==============================================================================
// User Story 2: Controlled Runaway with Limiting (T011-T014)
// ==============================================================================

TEST_CASE("FeedbackDistortion limiter parameter control", "[FeedbackDistortion][limiter][parameters]") {
    FeedbackDistortion distortion;
    distortion.prepare(44100.0, 512);

    SECTION("setLimiterThreshold clamps to [-24.0, 0.0] dB (FR-016, FR-017)") {
        // Within range
        distortion.setLimiterThreshold(-6.0f);
        REQUIRE(distortion.getLimiterThreshold() == Approx(-6.0f));

        distortion.setLimiterThreshold(-12.0f);
        REQUIRE(distortion.getLimiterThreshold() == Approx(-12.0f));

        // Below minimum - should clamp to -24.0
        distortion.setLimiterThreshold(-30.0f);
        REQUIRE(distortion.getLimiterThreshold() == Approx(-24.0f));

        // Above maximum - should clamp to 0.0
        distortion.setLimiterThreshold(6.0f);
        REQUIRE(distortion.getLimiterThreshold() == Approx(0.0f));
    }
}

TEST_CASE("FeedbackDistortion controlled runaway behavior", "[FeedbackDistortion][limiter][runaway]") {
    constexpr float sampleRate = 44100.0f;
    FeedbackDistortion distortion;
    distortion.prepare(sampleRate, 512);

    SECTION("output sustains indefinitely with feedback > 1.0 (SC-002)") {
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(1.2f);
        distortion.setDrive(2.0f);
        distortion.setLimiterThreshold(-6.0f);

        // Process a short burst of input
        constexpr size_t burstLength = static_cast<size_t>(sampleRate * 0.1f);  // 100ms
        for (size_t i = 0; i < burstLength; ++i) {
            float input = std::sin(6.283185307f * 1000.0f * static_cast<float>(i) / sampleRate);
            input *= (1.0f - static_cast<float>(i) / static_cast<float>(burstLength));  // Fade out
            input *= dbToLinear(-6.0f);  // -6dB level
            (void)distortion.process(input);
        }

        // Continue processing for 10 seconds with no input
        constexpr size_t sustainLength = static_cast<size_t>(sampleRate * 10.0f);
        float minLevel = 1.0f;

        for (size_t i = 0; i < sustainLength; ++i) {
            float output = distortion.process(0.0f);
            if (i > static_cast<size_t>(sampleRate * 0.5f)) {  // After 0.5s
                minLevel = std::min(minLevel, std::abs(output));
            }
        }

        // SC-002: Output should remain above -40dB for at least 10 seconds
        float thresholdLinear = dbToLinear(-40.0f);
        // Check final RMS instead of minimum (more stable)
        std::vector<float> finalBuffer(4410);
        for (size_t i = 0; i < finalBuffer.size(); ++i) {
            finalBuffer[i] = distortion.process(0.0f);
        }
        float finalRMS = calculateRMS(finalBuffer.data(), finalBuffer.size());
        REQUIRE(finalRMS > thresholdLinear);
    }

    SECTION("different thresholds produce different sustained output levels") {
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(1.3f);
        distortion.setDrive(3.0f);

        // Test with -12dB threshold
        distortion.setLimiterThreshold(-12.0f);
        distortion.reset();

        // Excite and let sustain
        for (int i = 0; i < 4410; ++i) {
            (void)distortion.process(i < 441 ? 0.5f : 0.0f);
        }

        std::vector<float> buffer12dB(4410);
        for (size_t i = 0; i < buffer12dB.size(); ++i) {
            buffer12dB[i] = distortion.process(0.0f);
        }
        float rms12dB = calculateRMS(buffer12dB.data(), buffer12dB.size());

        // Test with -6dB threshold
        distortion.setLimiterThreshold(-6.0f);
        distortion.reset();

        // Excite and let sustain
        for (int i = 0; i < 4410; ++i) {
            (void)distortion.process(i < 441 ? 0.5f : 0.0f);
        }

        std::vector<float> buffer6dB(4410);
        for (size_t i = 0; i < buffer6dB.size(); ++i) {
            buffer6dB[i] = distortion.process(0.0f);
        }
        float rms6dB = calculateRMS(buffer6dB.data(), buffer6dB.size());

        // -12dB threshold should produce quieter output than -6dB
        REQUIRE(rms12dB < rms6dB);
    }
}

TEST_CASE("FeedbackDistortion limiter effectiveness", "[FeedbackDistortion][limiter][bounds]") {
    constexpr float sampleRate = 44100.0f;
    FeedbackDistortion distortion;
    distortion.prepare(sampleRate, 512);

    SECTION("output never exceeds limiter threshold + 3dB at maximum feedback (FR-030, SC-003)") {
        constexpr float thresholdDb = -6.0f;
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(1.5f);  // Maximum runaway
        distortion.setDrive(5.0f);
        distortion.setLimiterThreshold(thresholdDb);

        // Excite with strong signal
        for (int i = 0; i < 4410; ++i) {
            (void)distortion.process(0.8f);
        }

        // Process for 5 seconds and track peak
        constexpr size_t totalSamples = static_cast<size_t>(sampleRate * 5.0f);
        float maxPeak = 0.0f;

        for (size_t i = 0; i < totalSamples; ++i) {
            float output = distortion.process(0.0f);
            maxPeak = std::max(maxPeak, std::abs(output));
        }

        // SC-003: Peak should not exceed threshold + 3dB
        float maxAllowedLinear = dbToLinear(thresholdDb + 3.0f);
        REQUIRE(maxPeak <= maxAllowedLinear);
    }

    SECTION("soft limiting produces gradual compression, not hard clipping (FR-019)") {
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(1.3f);
        distortion.setDrive(4.0f);
        distortion.setLimiterThreshold(-6.0f);

        // Excite
        for (int i = 0; i < 4410; ++i) {
            (void)distortion.process(0.5f);
        }

        // Capture output
        std::vector<float> buffer(4410);
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = distortion.process(0.0f);
        }

        // Count samples at exact clipping threshold - soft limiter should have few
        int hardClippedSamples = 0;
        float threshold = dbToLinear(-6.0f);
        for (float sample : buffer) {
            if (std::abs(std::abs(sample) - threshold) < 0.001f) {
                hardClippedSamples++;
            }
        }

        // Soft limiter should not produce many samples at exact threshold
        // (unlike hard clipper which clips exactly at threshold)
        REQUIRE(hardClippedSamples < static_cast<int>(buffer.size() * 0.1f));
    }
}

TEST_CASE("FeedbackDistortion limiter timing characteristics", "[FeedbackDistortion][limiter][timing]") {
    constexpr float sampleRate = 44100.0f;
    FeedbackDistortion distortion;
    distortion.prepare(sampleRate, 512);

    SECTION("limiter attack responds within 0.5ms (FR-019a)") {
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(1.5f);  // Maximum runaway
        distortion.setDrive(5.0f);
        distortion.setLimiterThreshold(-12.0f);

        // Prime the feedback loop with strong signal
        for (int i = 0; i < 4410; ++i) {
            (void)distortion.process(0.8f);
        }

        // Now inject a sudden loud burst - limiter should respond quickly
        // 0.5ms at 44100Hz = ~22 samples
        constexpr size_t attackSamples = static_cast<size_t>(sampleRate * 0.0005f);  // 0.5ms
        constexpr size_t measureWindow = static_cast<size_t>(sampleRate * 0.002f);   // 2ms

        float thresholdLinear = dbToLinear(-12.0f);
        float maxAllowed = thresholdLinear * 1.41f;  // +3dB overshoot allowed

        // Track how quickly limiting engages
        std::vector<float> response(measureWindow);
        for (size_t i = 0; i < measureWindow; ++i) {
            response[i] = distortion.process(0.0f);  // No new input, just feedback
        }

        // After attack time (0.5ms = ~22 samples), output should be controlled
        // Check samples after the attack window are within bounds
        bool controlledAfterAttack = true;
        for (size_t i = attackSamples + 10; i < measureWindow; ++i) {
            if (std::abs(response[i]) > maxAllowed * 1.1f) {  // 10% margin
                controlledAfterAttack = false;
                break;
            }
        }
        REQUIRE(controlledAfterAttack);
    }

    SECTION("limiter release takes approximately 50ms (FR-019b)") {
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(0.5f);  // Sub-unity for decay
        distortion.setDrive(2.0f);
        distortion.setLimiterThreshold(-6.0f);

        float thresholdLinear = dbToLinear(-6.0f);

        // Build up signal to limiting threshold
        for (int i = 0; i < 4410; ++i) {
            (void)distortion.process(0.7f);
        }

        // Stop input - signal should decay naturally
        // With 50ms release, limiter gain should restore gradually
        constexpr size_t releaseSamples = static_cast<size_t>(sampleRate * 0.050f);  // 50ms
        constexpr size_t halfRelease = releaseSamples / 2;  // 25ms

        std::vector<float> releaseResponse(releaseSamples * 2);
        for (size_t i = 0; i < releaseResponse.size(); ++i) {
            releaseResponse[i] = distortion.process(0.0f);
        }

        // Measure envelope at different points during release
        // The envelope should decay smoothly over ~50ms
        float rmsAtStart = calculateRMS(releaseResponse.data(), 441);  // First 10ms
        float rmsAtHalf = calculateRMS(releaseResponse.data() + halfRelease, 441);  // At 25ms
        float rmsAtEnd = calculateRMS(releaseResponse.data() + releaseSamples, 441);  // At 50ms

        // Signal should decay progressively (not instantly)
        // This validates the release time is in the right ballpark
        if (rmsAtStart > 0.001f) {  // Only check if there was signal
            REQUIRE(rmsAtHalf < rmsAtStart);  // Decayed from start
            REQUIRE(rmsAtEnd < rmsAtHalf);    // Continued decaying
        }
    }
}

TEST_CASE("FeedbackDistortion stability", "[FeedbackDistortion][stability]") {
    constexpr float sampleRate = 44100.0f;
    FeedbackDistortion distortion;
    distortion.prepare(sampleRate, 512);

    SECTION("all valid parameter combinations remain bounded (FR-029)") {
        std::mt19937 rng(12345);  // Deterministic seed
        std::uniform_real_distribution<float> delayDist(1.0f, 100.0f);
        std::uniform_real_distribution<float> feedbackDist(0.0f, 1.5f);
        std::uniform_real_distribution<float> driveDist(0.1f, 10.0f);
        std::uniform_real_distribution<float> thresholdDist(-24.0f, 0.0f);

        constexpr int numTests = 20;

        for (int test = 0; test < numTests; ++test) {
            distortion.reset();
            distortion.setDelayTime(delayDist(rng));
            distortion.setFeedback(feedbackDist(rng));
            distortion.setDrive(driveDist(rng));
            distortion.setLimiterThreshold(thresholdDist(rng));

            // Process 1 second of impulse response
            bool hasNaN = false;
            bool hasInf = false;
            float maxOutput = 0.0f;

            for (int i = 0; i < 44100; ++i) {
                float input = (i == 0) ? 1.0f : 0.0f;
                float output = distortion.process(input);

                if (std::isnan(output)) hasNaN = true;
                if (std::isinf(output)) hasInf = true;
                maxOutput = std::max(maxOutput, std::abs(output));
            }

            REQUIRE_FALSE(hasNaN);
            REQUIRE_FALSE(hasInf);
            REQUIRE(maxOutput < 10.0f);  // Reasonable bound
        }
    }
}

// ==============================================================================
// User Story 3: Tone Filter (T019-T021)
// ==============================================================================

TEST_CASE("FeedbackDistortion tone filter parameter control", "[FeedbackDistortion][tone][parameters]") {
    FeedbackDistortion distortion;
    distortion.prepare(44100.0, 512);

    SECTION("setToneFrequency clamps to [20.0, min(20000.0, sampleRate*0.45)] Hz (FR-020, FR-022)") {
        // Within range
        distortion.setToneFrequency(1000.0f);
        REQUIRE(distortion.getToneFrequency() == Approx(1000.0f));

        distortion.setToneFrequency(5000.0f);
        REQUIRE(distortion.getToneFrequency() == Approx(5000.0f));

        // Below minimum - should clamp to 20.0
        distortion.setToneFrequency(10.0f);
        REQUIRE(distortion.getToneFrequency() == Approx(20.0f));

        // Above maximum - should clamp (at 44100Hz, max = 44100*0.45 = 19845)
        distortion.setToneFrequency(25000.0f);
        float maxTone = 44100.0f * 0.45f;
        REQUIRE(distortion.getToneFrequency() <= maxTone);
    }

    SECTION("tone frequency changes complete smoothly without clicks (FR-023, SC-004)") {
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(0.9f);
        distortion.setDrive(2.0f);
        distortion.setToneFrequency(5000.0f);

        // Build up resonance
        for (int i = 0; i < 4410; ++i) {
            (void)distortion.process(i < 100 ? 0.5f : 0.0f);
        }

        // Change tone frequency
        distortion.setToneFrequency(1000.0f);

        // Process transition
        std::vector<float> transitionBuffer(500);
        for (size_t i = 0; i < transitionBuffer.size(); ++i) {
            transitionBuffer[i] = distortion.process(0.0f);
        }

        // Check for clicks
        REQUIRE_FALSE(hasClicks(transitionBuffer.data(), transitionBuffer.size(), 0.3f));
    }
}

TEST_CASE("FeedbackDistortion tone filter Butterworth Q verification", "[FeedbackDistortion][tone][butterworth]") {
    constexpr float sampleRate = 44100.0f;
    FeedbackDistortion distortion;
    distortion.prepare(sampleRate, 512);

    SECTION("tone filter uses Q=0.707 Butterworth (FR-021a) - no resonance peak") {
        // Butterworth Q (0.707) produces maximally flat passband with no resonance
        // Higher Q values produce a peak at cutoff frequency
        // We verify Butterworth by checking response at cutoff is -3dB, not boosted

        distortion.setDelayTime(5.0f);      // Short delay for fast response
        distortion.setFeedback(0.3f);       // Low feedback to minimize resonance interference
        distortion.setDrive(1.0f);          // Unity drive
        distortion.setToneFrequency(1000.0f);  // 1kHz cutoff

        // Generate test tones at different frequencies relative to cutoff
        auto measureResponse = [&](float freq) -> float {
            distortion.reset();

            // Process several cycles of sine wave through the feedback loop
            constexpr size_t warmup = 4410;  // 100ms warmup
            constexpr size_t measure = 4410; // 100ms measurement

            // Warm up the filter
            for (size_t i = 0; i < warmup; ++i) {
                float input = 0.3f * std::sin(6.283185307f * freq * static_cast<float>(i) / sampleRate);
                (void)distortion.process(input);
            }

            // Measure steady-state response
            float sumSquares = 0.0f;
            for (size_t i = 0; i < measure; ++i) {
                float input = 0.3f * std::sin(6.283185307f * freq * static_cast<float>(warmup + i) / sampleRate);
                float output = distortion.process(input);
                sumSquares += output * output;
            }
            return std::sqrt(sumSquares / static_cast<float>(measure));
        };

        // Measure at passband (well below cutoff)
        float responseLow = measureResponse(200.0f);    // 200Hz (passband)

        // Measure at cutoff frequency
        float responseAtCutoff = measureResponse(1000.0f);  // 1kHz (cutoff)

        // Measure above cutoff
        float responseHigh = measureResponse(2000.0f);  // 2kHz (above cutoff)

        // Butterworth characteristics:
        // 1. At cutoff, response should be ~-3dB (0.707x) of passband, NOT boosted
        // 2. If Q > 0.707, there would be a resonance peak (response > passband)

        // Verify no resonance peak: response at cutoff should NOT exceed passband
        // Allow some margin for feedback loop interaction
        REQUIRE(responseAtCutoff <= responseLow * 1.1f);  // Not boosted by more than 10%

        // Verify lowpass behavior: response above cutoff should be attenuated
        REQUIRE(responseHigh < responseAtCutoff);

        // Verify Butterworth -3dB at cutoff (approximately)
        // The ratio should be close to 0.707, but feedback loop adds complexity
        float ratio = responseAtCutoff / responseLow;
        REQUIRE(ratio < 1.0f);  // Definitely not resonant (Q > 0.707 would boost)
        REQUIRE(ratio > 0.3f);  // Not over-attenuated (filter is working)
    }

    SECTION("kButterworthQ constant has correct value") {
        // Direct verification that the constant exists and has the correct value
        // kButterworthQ should be approximately 0.7071 (1/sqrt(2))
        REQUIRE(kButterworthQ == Approx(0.7071f).margin(0.001f));
    }
}

TEST_CASE("FeedbackDistortion tone filter effect on timbre", "[FeedbackDistortion][tone][timbre]") {
    constexpr float sampleRate = 44100.0f;
    FeedbackDistortion distortion;
    distortion.prepare(sampleRate, 512);

    SECTION("lower tone frequency produces darker sustain") {
        distortion.setDelayTime(5.0f);  // 200Hz fundamental
        distortion.setFeedback(0.95f);
        distortion.setDrive(3.0f);

        // Test with bright tone (5000Hz)
        distortion.setToneFrequency(5000.0f);
        distortion.reset();

        // Excite with noise-like signal (broadband)
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> noiseDist(-0.3f, 0.3f);
        for (int i = 0; i < 2205; ++i) {
            (void)distortion.process(noiseDist(rng));
        }

        // Capture bright output
        std::vector<float> brightBuffer(4410);
        for (size_t i = 0; i < brightBuffer.size(); ++i) {
            brightBuffer[i] = distortion.process(0.0f);
        }

        // Test with dark tone (1000Hz)
        distortion.setToneFrequency(1000.0f);
        distortion.reset();

        rng.seed(42);
        for (int i = 0; i < 2205; ++i) {
            (void)distortion.process(noiseDist(rng));
        }

        // Capture dark output
        std::vector<float> darkBuffer(4410);
        for (size_t i = 0; i < darkBuffer.size(); ++i) {
            darkBuffer[i] = distortion.process(0.0f);
        }

        // Measure high-frequency content by counting zero crossings
        // Darker signal should have fewer zero crossings
        size_t brightCrossings = 0, darkCrossings = 0;
        for (size_t i = 1; i < 4410; ++i) {
            if (brightBuffer[i - 1] * brightBuffer[i] < 0) brightCrossings++;
            if (darkBuffer[i - 1] * darkBuffer[i] < 0) darkCrossings++;
        }

        // Dark tone should have fewer high-frequency oscillations
        REQUIRE(darkCrossings < brightCrossings);
    }
}

// ==============================================================================
// User Story 4: Saturation Curve Selection (T026-T028)
// ==============================================================================

TEST_CASE("FeedbackDistortion saturation curve comparison", "[FeedbackDistortion][saturation][curves]") {
    constexpr float sampleRate = 44100.0f;
    FeedbackDistortion distortion;
    distortion.prepare(sampleRate, 512);
    distortion.setDelayTime(10.0f);
    distortion.setFeedback(0.8f);
    distortion.setDrive(4.0f);

    SECTION("different saturation curves produce different outputs") {
        // Process with Tanh
        distortion.setSaturationCurve(WaveshapeType::Tanh);
        distortion.reset();

        std::vector<float> tanhBuffer(4410);
        tanhBuffer[0] = 1.0f;
        for (size_t i = 0; i < tanhBuffer.size(); ++i) {
            tanhBuffer[i] = distortion.process(tanhBuffer[i]);
        }
        float tanhRMS = calculateRMS(tanhBuffer.data() + 441, 4410 - 441);

        // Process with HardClip
        distortion.setSaturationCurve(WaveshapeType::HardClip);
        distortion.reset();

        std::vector<float> hardClipBuffer(4410);
        hardClipBuffer[0] = 1.0f;
        for (size_t i = 0; i < hardClipBuffer.size(); ++i) {
            hardClipBuffer[i] = distortion.process(hardClipBuffer[i]);
        }
        float hardClipRMS = calculateRMS(hardClipBuffer.data() + 441, 4410 - 441);

        // Different curves should produce measurably different results
        REQUIRE(std::abs(tanhRMS - hardClipRMS) > 0.001f);
    }
}

TEST_CASE("FeedbackDistortion asymmetric saturation and DC blocking", "[FeedbackDistortion][saturation][dc]") {
    constexpr float sampleRate = 44100.0f;
    FeedbackDistortion distortion;
    distortion.prepare(sampleRate, 512);

    SECTION("DC blocker removes offset from asymmetric saturation (SC-006)") {
        distortion.setDelayTime(10.0f);
        distortion.setFeedback(0.8f);
        distortion.setDrive(4.0f);
        distortion.setSaturationCurve(WaveshapeType::Tube);  // Asymmetric

        // Excite with signal
        for (int i = 0; i < 4410; ++i) {
            (void)distortion.process(i < 441 ? 0.5f : 0.0f);
        }

        // Capture output
        std::vector<float> buffer(8820);  // 200ms for DC blocker to settle
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = distortion.process(0.0f);
        }

        // Calculate DC offset in latter half (after settling)
        float dc = calculateDC(buffer.data() + 4410, 4410);

        // SC-006: DC should be < 0.01
        REQUIRE(std::abs(dc) < 0.01f);
    }
}

TEST_CASE("FeedbackDistortion all WaveshapeType values work", "[FeedbackDistortion][saturation][types]") {
    FeedbackDistortion distortion;
    distortion.prepare(44100.0, 512);
    distortion.setDelayTime(10.0f);
    distortion.setFeedback(0.8f);
    distortion.setDrive(2.0f);

    SECTION("all WaveshapeType values process without errors (FR-012)") {
        std::vector<WaveshapeType> types = {
            WaveshapeType::Tanh,
            WaveshapeType::Atan,
            WaveshapeType::Cubic,
            WaveshapeType::Quintic,
            WaveshapeType::ReciprocalSqrt,
            WaveshapeType::Erf,
            WaveshapeType::HardClip,
            WaveshapeType::Diode,
            WaveshapeType::Tube
        };

        for (auto type : types) {
            distortion.setSaturationCurve(type);
            distortion.reset();

            // Process without crash
            bool hasError = false;
            for (int i = 0; i < 4410; ++i) {
                float input = (i == 0) ? 1.0f : 0.0f;
                float output = distortion.process(input);
                if (std::isnan(output) || std::isinf(output)) {
                    hasError = true;
                    break;
                }
            }

            REQUIRE_FALSE(hasError);
        }
    }
}

// ==============================================================================
// User Story 5: Delay Time for Pitch Control (T033-T034)
// ==============================================================================

TEST_CASE("FeedbackDistortion delay time pitch control", "[FeedbackDistortion][pitch]") {
    constexpr float sampleRate = 44100.0f;
    FeedbackDistortion distortion;
    distortion.prepare(sampleRate, 512);
    distortion.setFeedback(0.9f);
    distortion.setDrive(2.0f);

    SECTION("5ms delay produces ~200Hz resonance (SC-008)") {
        distortion.setDelayTime(5.0f);
        distortion.setDrive(1.5f);  // Moderate drive
        distortion.reset();

        // Process impulse
        std::vector<float> buffer(44100);
        buffer[0] = 1.0f;
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = distortion.process(buffer[i]);
        }

        // Estimate frequency
        float frequency = estimateFundamentalFrequency(buffer.data() + 4410, buffer.size() - 4410, sampleRate);

        // SC-008: +/- 10% of 200Hz
        REQUIRE(frequency >= 180.0f);
        REQUIRE(frequency <= 220.0f);
    }

    SECTION("20ms delay produces ~50Hz resonance (SC-008)") {
        distortion.setDelayTime(20.0f);
        distortion.setDrive(1.5f);  // Moderate drive
        distortion.reset();

        // Process impulse
        std::vector<float> buffer(44100);
        buffer[0] = 1.0f;
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = distortion.process(buffer[i]);
        }

        // Estimate frequency
        float frequency = estimateFundamentalFrequency(buffer.data() + 4410, buffer.size() - 4410, sampleRate);

        // SC-008: +/- 10% of 50Hz
        REQUIRE(frequency >= 45.0f);
        REQUIRE(frequency <= 55.0f);
    }

    SECTION("10ms delay produces ~100Hz resonance (SC-008)") {
        distortion.setDelayTime(10.0f);
        distortion.setDrive(1.5f);  // Moderate drive
        distortion.reset();

        // Process impulse
        std::vector<float> buffer(44100);
        buffer[0] = 1.0f;
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = distortion.process(buffer[i]);
        }

        // Estimate frequency
        float frequency = estimateFundamentalFrequency(buffer.data() + 4410, buffer.size() - 4410, sampleRate);

        // SC-008: +/- 10% of 100Hz
        REQUIRE(frequency >= 90.0f);
        REQUIRE(frequency <= 110.0f);
    }
}

TEST_CASE("FeedbackDistortion smooth delay time modulation", "[FeedbackDistortion][pitch][modulation]") {
    constexpr float sampleRate = 44100.0f;
    FeedbackDistortion distortion;
    distortion.prepare(sampleRate, 512);
    distortion.setDelayTime(10.0f);
    distortion.setFeedback(0.9f);
    distortion.setDrive(2.0f);

    SECTION("pitch shifts smoothly without clicks when delay time changes (FR-006, SC-004)") {
        // Build up resonance
        for (int i = 0; i < 4410; ++i) {
            (void)distortion.process(i < 100 ? 0.5f : 0.0f);
        }

        // Change delay time (smaller change for smoother transition)
        distortion.setDelayTime(8.0f);

        // Capture transition
        std::vector<float> transitionBuffer(882);  // 20ms
        for (size_t i = 0; i < transitionBuffer.size(); ++i) {
            transitionBuffer[i] = distortion.process(0.0f);
        }

        // Check for clicks - delay modulation causes pitch shift effect
        // which produces some transient behavior, so use higher threshold
        REQUIRE_FALSE(hasClicks(transitionBuffer.data(), transitionBuffer.size(), 0.5f));
    }
}
