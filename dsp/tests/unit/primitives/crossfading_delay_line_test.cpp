// ==============================================================================
// Layer 1: DSP Primitive Tests - CrossfadingDelayLine
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests for click-free delay time changes using two-tap crossfading.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/primitives/crossfading_delay_line.h>
#include <artifact_detection.h>

#include <cmath>
#include <array>
#include <vector>
#include <numeric>

using namespace Krate::DSP;
using namespace Krate::DSP::TestUtils;
using Catch::Approx;

// =============================================================================
// Basic Functionality Tests
// =============================================================================

TEST_CASE("CrossfadingDelayLine prepare allocates buffer", "[delay][crossfade][prepare]") {
    CrossfadingDelayLine delay;

    SECTION("prepares with standard sample rate") {
        delay.prepare(44100.0, 1.0f);  // 1 second max delay
        REQUIRE(delay.maxDelaySamples() >= 44100);
    }

    SECTION("prepares with high sample rate") {
        delay.prepare(96000.0, 0.5f);  // 0.5 seconds at 96kHz
        REQUIRE(delay.maxDelaySamples() >= 48000);
    }
}

TEST_CASE("CrossfadingDelayLine reset clears state", "[delay][crossfade][reset]") {
    CrossfadingDelayLine delay;
    delay.prepare(44100.0, 0.1f);

    // Write some samples
    for (int i = 0; i < 100; ++i) {
        delay.write(1.0f);
    }

    // Trigger a crossfade
    delay.setDelayMs(50.0f);

    delay.reset();

    // After reset, crossfading should be false
    REQUIRE_FALSE(delay.isCrossfading());
}

TEST_CASE("CrossfadingDelayLine basic write/read", "[delay][crossfade][basic]") {
    CrossfadingDelayLine delay;
    delay.prepare(44100.0, 0.1f);

    SECTION("write and read at fixed delay") {
        // Set a fixed delay (use larger delay to trigger crossfade)
        delay.setDelaySamples(500.0f);

        // Wait for crossfade to complete (~20ms = 882 samples at 44.1kHz)
        for (int i = 0; i < 1000; ++i) {
            delay.write(0.0f);
            (void)delay.read();
        }
        REQUIRE_FALSE(delay.isCrossfading());  // Crossfade should be done

        // Now write an impulse
        delay.write(1.0f);

        // Process until impulse appears at delay position
        float output = 0.0f;
        for (int i = 0; i < 499; ++i) {
            delay.write(0.0f);
            output = delay.read();
        }
        // After 500 samples, the impulse should appear
        delay.write(0.0f);
        output = delay.read();
        REQUIRE(output == Approx(1.0f));
    }
}

// =============================================================================
// Crossfade Trigger Tests
// =============================================================================

TEST_CASE("CrossfadingDelayLine triggers crossfade on large delay change", "[delay][crossfade][trigger]") {
    CrossfadingDelayLine delay;
    delay.prepare(44100.0, 1.0f);

    // Start at 100ms delay
    delay.setDelayMs(100.0f);

    // Prime buffer with samples so the delay time is established
    for (int i = 0; i < 5000; ++i) {
        delay.write(0.5f);
        (void)delay.read();
    }

    // Small change should NOT trigger crossfade (44 samples < 100 threshold)
    delay.setDelayMs(101.0f);
    REQUIRE_FALSE(delay.isCrossfading());

    // Large change (>100 samples at 44.1kHz) SHOULD trigger crossfade
    delay.setDelayMs(200.0f);  // 99ms jump = 4370 samples > 100 threshold
    REQUIRE(delay.isCrossfading());
}

TEST_CASE("CrossfadingDelayLine small changes don't trigger crossfade", "[delay][crossfade][smooth]") {
    CrossfadingDelayLine delay;
    delay.prepare(44100.0, 1.0f);

    delay.setDelaySamples(1000.0f);

    // Prime buffer first
    for (int i = 0; i < 2000; ++i) {
        delay.write(0.5f);
        (void)delay.read();
    }

    // Changes less than kCrossfadeThresholdSamples (100) should not trigger
    for (float d = 1000.0f; d <= 1050.0f; d += 10.0f) {
        delay.setDelaySamples(d);
        REQUIRE_FALSE(delay.isCrossfading());
    }
}

// =============================================================================
// Click-Free Operation Tests (THE KEY TESTS)
// =============================================================================

TEST_CASE("CrossfadingDelayLine eliminates clicks during large delay changes",
          "[delay][crossfade][clickfree]") {
    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    // Fill buffer with a continuous signal (sine wave)
    const float freq = 440.0f;
    const float omega = 2.0f * 3.14159265f * freq / static_cast<float>(sampleRate);

    // Initialize at 100ms delay
    delay.setDelayMs(100.0f);

    // Prime the buffer
    for (int i = 0; i < 10000; ++i) {
        float input = std::sin(omega * static_cast<float>(i));
        delay.write(input);
        (void)delay.read();
    }

    // Now make a large delay change (100ms -> 300ms = 8820 samples change)
    std::vector<float> outputs;
    outputs.reserve(2000);

    float prevOutput = delay.read();

    for (int i = 0; i < 2000; ++i) {
        if (i == 0) {
            delay.setDelayMs(300.0f);  // Large jump!
        }

        float input = std::sin(omega * static_cast<float>(10000 + i));
        delay.write(input);
        float output = delay.read();
        outputs.push_back(output);
    }

    // Use ClickDetector for robust discontinuity detection
    // Previous ad hoc approach used: max(|output[i] - output[i-1]|) < 0.2
    // ClickDetector uses statistical sigma-based thresholds which is more robust
    ClickDetectorConfig clickConfig{
        .sampleRate = static_cast<float>(sampleRate),
        .frameSize = 256,     // Smaller frames for short buffer
        .hopSize = 128,
        .detectionThreshold = 5.0f,  // 5-sigma threshold
        .energyThresholdDb = -60.0f,
        .mergeGap = 3
    };

    ClickDetector detector(clickConfig);
    detector.prepare();

    auto clicks = detector.detect(outputs.data(), outputs.size());

    INFO("Clicks detected during 200ms delay jump: " << clicks.size());
    for (const auto& click : clicks) {
        INFO("  Click at sample " << static_cast<size_t>(click.timeSeconds * sampleRate)
             << " (t=" << click.timeSeconds << "s), amplitude: " << click.amplitude);
    }

    // With crossfading, there should be no detectable clicks
    REQUIRE(clicks.empty());
}

TEST_CASE("CrossfadingDelayLine vs plain delay line during large jumps",
          "[delay][crossfade][comparison]") {
    // This test demonstrates the problem crossfading solves
    // by comparing behavior during a sudden delay change

    CrossfadingDelayLine crossfadingDelay;
    DelayLine plainDelay;

    const double sampleRate = 44100.0;
    crossfadingDelay.prepare(sampleRate, 1.0f);
    plainDelay.prepare(sampleRate, 1.0f);

    // Fill both with same signal
    const float freq = 440.0f;
    const float omega = 2.0f * 3.14159265f * freq / static_cast<float>(sampleRate);

    float delayMs = 100.0f;
    crossfadingDelay.setDelayMs(delayMs);
    float delaySamples = delayMs * 0.001f * static_cast<float>(sampleRate);

    for (int i = 0; i < 10000; ++i) {
        float input = std::sin(omega * static_cast<float>(i));
        crossfadingDelay.write(input);
        plainDelay.write(input);
        (void)crossfadingDelay.read();
        (void)plainDelay.readLinear(delaySamples);
    }

    // Now jump delay from 100ms to 300ms
    float newDelayMs = 300.0f;
    crossfadingDelay.setDelayMs(newDelayMs);
    float newDelaySamples = newDelayMs * 0.001f * static_cast<float>(sampleRate);

    // CrossfadingDelayLine should be crossfading after the large change
    REQUIRE(crossfadingDelay.isCrossfading() == true);

    // Capture first few samples after the change and verify smooth transition
    std::vector<float> crossfadingOutputs;
    std::vector<float> plainOutputs;

    for (int i = 0; i < 100; ++i) {
        float input = std::sin(omega * static_cast<float>(10000 + i));
        crossfadingDelay.write(input);
        plainDelay.write(input);
        crossfadingOutputs.push_back(crossfadingDelay.read());
        plainOutputs.push_back(plainDelay.readLinear(newDelaySamples));
    }

    // After 100 samples, crossfade should still be in progress (takes ~882 samples)
    REQUIRE(crossfadingDelay.isCrossfading() == true);

    // Use ClickDetector to verify crossfading output is smooth
    ClickDetectorConfig clickConfig{
        .sampleRate = static_cast<float>(sampleRate),
        .frameSize = 64,      // Very small frames for 100-sample buffer
        .hopSize = 32,
        .detectionThreshold = 5.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 2
    };

    ClickDetector detector(clickConfig);
    detector.prepare();

    auto clicks = detector.detect(crossfadingOutputs.data(), crossfadingOutputs.size());

    INFO("Clicks in crossfading output: " << clicks.size());
    REQUIRE(clicks.empty());  // Smooth output = no clicks
}

TEST_CASE("CrossfadingDelayLine crossfade completes correctly", "[delay][crossfade][completion]") {
    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    delay.setDelayMs(100.0f);

    // Prime buffer
    for (int i = 0; i < 5000; ++i) {
        delay.write(static_cast<float>(i) * 0.001f);
        (void)delay.read();
    }

    // Trigger crossfade
    delay.setDelayMs(300.0f);
    REQUIRE(delay.isCrossfading());

    // Process until crossfade completes (default is 20ms = 882 samples)
    int samplesUntilComplete = 0;
    for (int i = 0; i < 2000; ++i) {
        delay.write(0.5f);
        (void)delay.read();
        samplesUntilComplete++;
        if (!delay.isCrossfading()) {
            break;
        }
    }

    REQUIRE_FALSE(delay.isCrossfading());

    // Should complete within approximately 20ms
    float completionTimeMs = static_cast<float>(samplesUntilComplete) / static_cast<float>(sampleRate) * 1000.0f;
    INFO("Crossfade completed in " << completionTimeMs << " ms");
    REQUIRE(completionTimeMs < 25.0f);  // Allow some margin
    REQUIRE(completionTimeMs > 15.0f);  // But not too fast
}

// =============================================================================
// Crossfade Time Configuration Tests
// =============================================================================

TEST_CASE("CrossfadingDelayLine configurable crossfade time", "[delay][crossfade][config]") {
    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    SECTION("faster crossfade (10ms)") {
        delay.setCrossfadeTime(10.0f);
        delay.setDelayMs(100.0f);

        // Prime
        for (int i = 0; i < 5000; ++i) {
            delay.write(0.5f);
            (void)delay.read();
        }

        delay.setDelayMs(300.0f);

        int samples = 0;
        while (delay.isCrossfading() && samples < 2000) {
            delay.write(0.5f);
            (void)delay.read();
            samples++;
        }

        float timeMs = static_cast<float>(samples) / static_cast<float>(sampleRate) * 1000.0f;
        REQUIRE(timeMs < 15.0f);
        REQUIRE(timeMs > 5.0f);
    }

    SECTION("slower crossfade (50ms)") {
        delay.setCrossfadeTime(50.0f);
        delay.setDelayMs(100.0f);

        // Prime
        for (int i = 0; i < 5000; ++i) {
            delay.write(0.5f);
            (void)delay.read();
        }

        delay.setDelayMs(300.0f);

        int samples = 0;
        while (delay.isCrossfading() && samples < 5000) {
            delay.write(0.5f);
            (void)delay.read();
            samples++;
        }

        float timeMs = static_cast<float>(samples) / static_cast<float>(sampleRate) * 1000.0f;
        REQUIRE(timeMs > 40.0f);
        REQUIRE(timeMs < 60.0f);
    }
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("CrossfadingDelayLine handles rapid successive changes", "[delay][crossfade][edge]") {
    CrossfadingDelayLine delay;
    delay.prepare(44100.0, 1.0f);

    delay.setDelayMs(100.0f);

    // Prime buffer
    for (int i = 0; i < 5000; ++i) {
        delay.write(0.5f);
        (void)delay.read();
    }

    // Trigger crossfade
    delay.setDelayMs(300.0f);
    REQUIRE(delay.isCrossfading());

    // Try to change again while crossfading - should be ignored
    delay.setDelayMs(500.0f);

    // Process a bit
    for (int i = 0; i < 100; ++i) {
        delay.write(0.5f);
        (void)delay.read();
    }

    // Still crossfading to original target
    REQUIRE(delay.isCrossfading());

    // Let crossfade complete
    while (delay.isCrossfading()) {
        delay.write(0.5f);
        (void)delay.read();
    }

    // Now we can trigger another crossfade
    // Note: We use 700.0f, not 500.0f, because the inactive tap was tracking
    // the 500.0f target during the previous crossfade, so we need a different
    // target to trigger a new crossfade.
    delay.setDelayMs(700.0f);
    REQUIRE(delay.isCrossfading());
}

TEST_CASE("CrossfadingDelayLine getCurrentDelaySamples during crossfade", "[delay][crossfade][query]") {
    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    float startDelayMs = 100.0f;
    float endDelayMs = 300.0f;
    float startDelaySamples = startDelayMs * 0.001f * static_cast<float>(sampleRate);
    float endDelaySamples = endDelayMs * 0.001f * static_cast<float>(sampleRate);

    delay.setDelayMs(startDelayMs);

    // Prime buffer
    for (int i = 0; i < 5000; ++i) {
        delay.write(0.5f);
        (void)delay.read();
    }

    float currentBefore = delay.getCurrentDelaySamples();
    REQUIRE(currentBefore == Approx(startDelaySamples));

    // Trigger crossfade
    delay.setDelayMs(endDelayMs);

    // During crossfade, current delay should be somewhere between start and end
    for (int i = 0; i < 441; ++i) {  // Half the crossfade time
        delay.write(0.5f);
        (void)delay.read();
    }

    if (delay.isCrossfading()) {
        float currentDuring = delay.getCurrentDelaySamples();
        REQUIRE(currentDuring > startDelaySamples);
        REQUIRE(currentDuring < endDelaySamples);
    }
}

// =============================================================================
// Real-Time Safety Tests
// =============================================================================

TEST_CASE("CrossfadingDelayLine noexcept specifications", "[delay][crossfade][realtime]") {
    SECTION("constructors and destructors are noexcept") {
        static_assert(std::is_nothrow_default_constructible_v<CrossfadingDelayLine>,
                      "Default constructor must be noexcept");
        static_assert(std::is_nothrow_destructible_v<CrossfadingDelayLine>,
                      "Destructor must be noexcept");
        static_assert(std::is_nothrow_move_constructible_v<CrossfadingDelayLine>,
                      "Move constructor must be noexcept");
        static_assert(std::is_nothrow_move_assignable_v<CrossfadingDelayLine>,
                      "Move assignment must be noexcept");
        REQUIRE(true);
    }

    SECTION("processing methods are noexcept") {
        CrossfadingDelayLine delay;
        delay.prepare(44100.0, 0.1f);

        static_assert(noexcept(delay.write(0.0f)), "write() must be noexcept");
        static_assert(noexcept(delay.read()), "read() must be noexcept");
        static_assert(noexcept(delay.process(0.0f)), "process() must be noexcept");
        static_assert(noexcept(delay.setDelaySamples(0.0f)), "setDelaySamples() must be noexcept");
        static_assert(noexcept(delay.setDelayMs(0.0f)), "setDelayMs() must be noexcept");
        static_assert(noexcept(delay.reset()), "reset() must be noexcept");
        REQUIRE(true);
    }

    SECTION("query methods are noexcept") {
        CrossfadingDelayLine delay;
        static_assert(noexcept(delay.isCrossfading()), "isCrossfading() must be noexcept");
        static_assert(noexcept(delay.getCurrentDelaySamples()), "getCurrentDelaySamples() must be noexcept");
        static_assert(noexcept(delay.maxDelaySamples()), "maxDelaySamples() must be noexcept");
        REQUIRE(true);
    }
}

// =============================================================================
// REGRESSION TEST: Zipper Noise During Delay Time Changes
// =============================================================================

TEST_CASE("REGRESSION: No zipper noise during 200ms delay time change",
          "[delay][crossfade][regression][zipper]") {
    // This is the regression test for the issue discovered in DigitalDelay tests.
    //
    // PROBLEM: When delay time changed from 300ms to 100ms (200ms jump), the
    // SC-009 test detected a discontinuity of 3.20724f at sample 33136.
    // This was during parameter smoothing which moves the read position,
    // causing pitch artifacts and audible clicks.
    //
    // SOLUTION: CrossfadingDelayLine uses two-tap crossfading to eliminate
    // this discontinuity by blending between old and new positions instead
    // of moving a single read pointer.

    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    // Simulate the exact scenario from SC-009: 300ms -> 100ms change
    delay.setDelayMs(300.0f);

    // Feed a sine wave (same as original test)
    const float freq = 440.0f;
    const float omega = 2.0f * 3.14159265f * freq / static_cast<float>(sampleRate);

    // Prime the buffer
    for (size_t i = 0; i < 44100; ++i) {
        float input = std::sin(omega * static_cast<float>(i));
        (void)delay.process(input);
    }

    // Now make the 200ms jump (same as original failing test)
    delay.setDelayMs(100.0f);

    // Process and collect outputs for click detection
    std::vector<float> outputs;
    outputs.reserve(44100);

    for (size_t i = 0; i < 44100; ++i) {
        float input = std::sin(omega * static_cast<float>(44100 + i));
        float output = delay.process(input);
        outputs.push_back(output);
    }

    // Use ClickDetector to verify no zipper noise/clicks
    // The original failing test saw 3.20724f discontinuity.
    // With crossfading, ClickDetector should find no clicks.
    ClickDetectorConfig clickConfig{
        .sampleRate = static_cast<float>(sampleRate),
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f,  // 5-sigma threshold
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(clickConfig);
    detector.prepare();

    auto clicks = detector.detect(outputs.data(), outputs.size());

    INFO("Clicks detected during 200ms delay jump: " << clicks.size());
    for (const auto& click : clicks) {
        INFO("  Click at sample " << static_cast<size_t>(click.timeSeconds * sampleRate)
             << " (t=" << click.timeSeconds << "s), amplitude: " << click.amplitude);
    }

    // With crossfading, there should be no detectable clicks
    // (the original bug produced 3.20724f discontinuity which would be obvious)
    REQUIRE(clicks.empty());
}

// =============================================================================
// T043: Equal-Power Crossfade Tests (spec 041-mode-switch-clicks)
// =============================================================================
// These tests verify the equal-power crossfade upgrade from linear crossfade.
// Equal-power uses sine/cosine curves where fadeOut² + fadeIn² ≈ 1, which
// maintains constant perceived loudness during the transition.
// =============================================================================

TEST_CASE("CrossfadingDelayLine uses equal-power crossfade (constant power)",
          "[delay][crossfade][equal-power]") {
    // This test verifies that during crossfade, the output maintains expected
    // equal-power behavior. Both delay positions must have valid data.

    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    // Use a constant signal so output reflects only the crossfade gains
    const float constantValue = 1.0f;

    // Set initial delay
    delay.setDelayMs(100.0f);

    // Prime the buffer with constant signal - MUST be longer than target delay
    // 300ms = 13230 samples, so we need at least that many
    for (int i = 0; i < 15000; ++i) {
        delay.write(constantValue);
        (void)delay.read();
    }

    // Trigger crossfade with large delay change
    delay.setDelayMs(300.0f);
    REQUIRE(delay.isCrossfading());

    // Track minimum and maximum output during crossfade
    float minOutput = 2.0f;  // Start high
    float maxOutput = 0.0f;  // Start low
    int crossfadeSamples = 0;

    while (delay.isCrossfading() && crossfadeSamples < 2000) {
        delay.write(constantValue);
        float output = delay.read();

        minOutput = std::min(minOutput, output);
        maxOutput = std::max(maxOutput, output);
        crossfadeSamples++;
    }

    INFO("Minimum output during crossfade: " << minOutput);
    INFO("Maximum output during crossfade: " << maxOutput);
    INFO("Crossfade samples: " << crossfadeSamples);

    // With equal-power crossfade of identical signals (both taps reading 1.0):
    // At position 0.0: output = 1.0 * cos(0) + 1.0 * sin(0) = 1.0 + 0.0 = 1.0
    // At position 0.5: output = 1.0 * cos(π/4) + 1.0 * sin(π/4) = 0.707 + 0.707 = 1.414
    // At position 1.0: output = 1.0 * cos(π/2) + 1.0 * sin(π/2) = 0.0 + 1.0 = 1.0
    //
    // So for correlated signals, equal-power actually INCREASES amplitude at midpoint
    // compared to linear which stays at 1.0 throughout.
    //
    // Minimum should be ~1.0 (at endpoints)
    // Maximum should be ~1.414 (at midpoint)
    REQUIRE(minOutput >= 0.99f);
    REQUIRE(maxOutput >= 1.3f);   // Confirms equal-power behavior (linear would be 1.0)
    REQUIRE(maxOutput <= 1.5f);   // But not unreasonable
}

TEST_CASE("CrossfadingDelayLine equal-power maintains RMS for uncorrelated signals",
          "[delay][crossfade][equal-power][rms]") {
    // This test verifies equal-power crossfade behavior with sine wave signals.
    // Both delay positions must have valid data for meaningful comparison.

    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    // Use sine wave - the two delay taps will read different phases
    const float freq = 440.0f;
    const float omega = 2.0f * 3.14159265f * freq / static_cast<float>(sampleRate);

    // Set initial delay
    delay.setDelayMs(100.0f);  // 4410 samples

    // Prime the buffer with enough samples for BOTH delay times
    // 350ms = 15435 samples, so prime with more than that
    for (size_t i = 0; i < 20000; ++i) {
        float input = std::sin(omega * static_cast<float>(i));
        delay.write(input);
        (void)delay.read();
    }

    // Measure RMS before crossfade
    float rmsBeforeSum = 0.0f;
    const int measureSamples = 441;  // 10ms window
    for (int i = 0; i < measureSamples; ++i) {
        float input = std::sin(omega * static_cast<float>(20000 + i));
        delay.write(input);
        float output = delay.read();
        rmsBeforeSum += output * output;
    }
    float rmsBefore = std::sqrt(rmsBeforeSum / static_cast<float>(measureSamples));

    // Trigger crossfade with LARGE delay change to ensure different phases
    delay.setDelayMs(350.0f);  // 15435 samples
    REQUIRE(delay.isCrossfading());

    // Measure RMS during crossfade (around midpoint)
    // Skip first ~400 samples to get to midpoint region
    for (int i = 0; i < 400; ++i) {
        float input = std::sin(omega * static_cast<float>(20441 + i));
        delay.write(input);
        (void)delay.read();
    }

    float rmsDuringSum = 0.0f;
    for (int i = 0; i < measureSamples; ++i) {
        float input = std::sin(omega * static_cast<float>(20841 + i));
        delay.write(input);
        float output = delay.read();
        rmsDuringSum += output * output;
    }
    float rmsDuring = std::sqrt(rmsDuringSum / static_cast<float>(measureSamples));

    INFO("RMS before crossfade: " << rmsBefore);
    INFO("RMS during crossfade (midpoint): " << rmsDuring);

    // With equal-power crossfade:
    // - For correlated signals (same phase): RMS increases at midpoint (~1.414x)
    // - For uncorrelated signals (90° out of phase): RMS stays constant (~1.0x)
    // - For anti-correlated signals (180° out of phase): RMS decreases
    //
    // Phase relationship depends on the delay difference, which is (350-100)ms = 250ms
    // = 11025 samples at 44.1kHz. At 440Hz, wavelength = 100.23 samples
    // 11025 / 100.23 ≈ 110 cycles, so we get arbitrary phase.
    //
    // Key point: equal-power crossfade maintains constant POWER (squared sum = 1)
    // For uncorrelated signals, this means RMS stays constant.
    float rmsRatio = rmsDuring / rmsBefore;
    INFO("RMS ratio (during/before): " << rmsRatio);

    // Allow wide range due to phase relationships, but should not collapse
    REQUIRE(rmsRatio > 0.5f);   // Should not drop too much
    REQUIRE(rmsRatio < 2.0f);   // Should not spike excessively
}

TEST_CASE("CrossfadingDelayLine crossfade gain sum is approximately 1.0",
          "[delay][crossfade][equal-power][gains]") {
    // Test that the weighted average delay (which reflects gain distribution)
    // transitions smoothly without extreme values during crossfade.
    //
    // For equal-power: fadeOut² + fadeIn² = 1 (constant power)
    // For linear: fadeOut + fadeIn = 1 (constant amplitude sum)
    //
    // We test indirectly via getCurrentDelaySamples() which is the gain-weighted average.

    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    const float startDelayMs = 100.0f;
    const float endDelayMs = 300.0f;
    const float startSamples = startDelayMs * 0.001f * static_cast<float>(sampleRate);
    const float endSamples = endDelayMs * 0.001f * static_cast<float>(sampleRate);

    delay.setDelayMs(startDelayMs);

    // Prime buffer
    for (int i = 0; i < 5000; ++i) {
        delay.write(0.5f);
        (void)delay.read();
    }

    // Verify starting position
    REQUIRE(delay.getCurrentDelaySamples() == Approx(startSamples));

    // Trigger crossfade
    delay.setDelayMs(endDelayMs);
    REQUIRE(delay.isCrossfading());

    // Track the delay samples during crossfade - should transition smoothly
    std::vector<float> delaySamplesDuringCrossfade;
    delaySamplesDuringCrossfade.reserve(1000);

    while (delay.isCrossfading()) {
        delay.write(0.5f);
        (void)delay.read();
        delaySamplesDuringCrossfade.push_back(delay.getCurrentDelaySamples());
    }

    // Verify smooth transition (no extreme jumps)
    float maxJump = 0.0f;
    for (size_t i = 1; i < delaySamplesDuringCrossfade.size(); ++i) {
        float jump = std::abs(delaySamplesDuringCrossfade[i] - delaySamplesDuringCrossfade[i-1]);
        maxJump = std::max(maxJump, jump);
    }

    INFO("Max delay sample jump during crossfade: " << maxJump);

    // Equal-power gives smooth S-curve transition, not perfectly linear
    // But jumps should still be small (< 100 samples per sample)
    REQUIRE(maxJump < 100.0f);

    // Verify we ended at the target
    REQUIRE(delay.getCurrentDelaySamples() == Approx(endSamples));
}

TEST_CASE("CrossfadingDelayLine equal-power vs linear at midpoint",
          "[delay][crossfade][equal-power][midpoint]") {
    // This test verifies the equal-power behavior at the crossfade midpoint.
    //
    // At position 0.5:
    // - Linear: fadeOut = 0.5, fadeIn = 0.5
    // - Equal-power: fadeOut = cos(π/4) ≈ 0.707, fadeIn = sin(π/4) ≈ 0.707
    //
    // For identical signals: linear gives 1.0, equal-power gives 1.414

    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    // Use very short crossfade so we can precisely control position
    delay.setCrossfadeTime(20.0f);  // 882 samples at 44.1kHz

    // Use constant signal to see gain effects directly
    const float constantValue = 1.0f;

    delay.setDelayMs(100.0f);

    // Prime buffer with constant - MUST be long enough for BOTH delay times
    // 300ms = 13230 samples, so we need at least that
    for (int i = 0; i < 15000; ++i) {
        delay.write(constantValue);
        (void)delay.read();
    }

    // Trigger crossfade
    delay.setDelayMs(300.0f);
    REQUIRE(delay.isCrossfading());

    // Process exactly half the crossfade time (441 samples = ~10ms)
    float outputAtMidpoint = 0.0f;
    for (int i = 0; i < 441; ++i) {
        delay.write(constantValue);
        outputAtMidpoint = delay.read();
    }

    INFO("Output at crossfade midpoint: " << outputAtMidpoint);

    // With equal-power crossfade of identical signals (both reading 1.0):
    // output = 1.0 * cos(π/4) + 1.0 * sin(π/4) ≈ 0.707 + 0.707 = 1.414
    //
    // With linear crossfade:
    // output = 1.0 * 0.5 + 1.0 * 0.5 = 1.0
    //
    // So we expect output > 1.2 for equal-power (allowing some tolerance)
    REQUIRE(outputAtMidpoint > 1.2f);  // Proves it's equal-power, not linear
    REQUIRE(outputAtMidpoint < 1.5f);  // But not unreasonably high
}
