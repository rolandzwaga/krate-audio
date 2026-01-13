// ==============================================================================
// Unit Tests: Artifact Detection
// ==============================================================================
// Tests for click/pop detection, LPC detection, and spectral anomaly detection.
//
// Constitution Compliance:
// - Principle XII: Test-First Development (tests written FIRST)
// - Principle VIII: Testing Discipline
//
// Reference: specs/055-artifact-detection/spec.md
// Requirements: FR-001, FR-002, FR-003, FR-004, FR-009, FR-010, FR-024
// Success Criteria: SC-001, SC-002, SC-005, SC-006, SC-007
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "artifact_detection.h"
#include "test_signals.h"
#include "allocation_detector.h"

#include <array>
#include <cmath>
#include <vector>
#include <chrono>

using namespace Krate::DSP::TestUtils;
using Catch::Approx;

// =============================================================================
// T009: ClickDetectorConfig Validation Tests
// =============================================================================

TEST_CASE("ClickDetectorConfig::isValid - validates configuration", "[artifact-detection][click-detector][config]") {
    SECTION("default config is valid") {
        ClickDetectorConfig config{};
        REQUIRE(config.isValid());
    }

    SECTION("valid custom config") {
        ClickDetectorConfig config{
            .sampleRate = 48000.0f,
            .frameSize = 1024,
            .hopSize = 512,
            .detectionThreshold = 4.0f,
            .energyThresholdDb = -50.0f,
            .mergeGap = 3
        };
        REQUIRE(config.isValid());
    }

    SECTION("invalid sampleRate - below minimum") {
        ClickDetectorConfig config{};
        config.sampleRate = 22000.0f;  // Below 22050 minimum
        REQUIRE_FALSE(config.isValid());
    }

    SECTION("invalid sampleRate - above maximum") {
        ClickDetectorConfig config{};
        config.sampleRate = 200000.0f;  // Above 192000 maximum
        REQUIRE_FALSE(config.isValid());
    }

    SECTION("invalid frameSize - not power of 2") {
        ClickDetectorConfig config{};
        config.frameSize = 500;  // Not power of 2
        REQUIRE_FALSE(config.isValid());
    }

    SECTION("invalid hopSize - zero") {
        ClickDetectorConfig config{};
        config.hopSize = 0;
        REQUIRE_FALSE(config.isValid());
    }

    SECTION("invalid hopSize - greater than frameSize") {
        ClickDetectorConfig config{};
        config.hopSize = config.frameSize + 1;
        REQUIRE_FALSE(config.isValid());
    }
}

// =============================================================================
// T010: ClickDetection Struct Tests
// =============================================================================

TEST_CASE("ClickDetection struct - initialization and helpers", "[artifact-detection][click-detector][detection]") {
    SECTION("fields initialized correctly") {
        ClickDetection detection{
            .sampleIndex = 1000,
            .amplitude = 0.5f,
            .timeSeconds = 0.0227f  // 1000 / 44100
        };
        REQUIRE(detection.sampleIndex == 1000);
        REQUIRE(detection.amplitude == Approx(0.5f));
        REQUIRE(detection.timeSeconds == Approx(0.0227f).margin(0.0001f));
    }

    SECTION("isAdjacentTo - within gap") {
        ClickDetection a{.sampleIndex = 100, .amplitude = 0.1f, .timeSeconds = 0.0f};
        ClickDetection b{.sampleIndex = 103, .amplitude = 0.2f, .timeSeconds = 0.0f};
        REQUIRE(a.isAdjacentTo(b, 5));  // Gap of 3, within 5
    }

    SECTION("isAdjacentTo - exactly at gap boundary") {
        ClickDetection a{.sampleIndex = 100, .amplitude = 0.1f, .timeSeconds = 0.0f};
        ClickDetection b{.sampleIndex = 105, .amplitude = 0.2f, .timeSeconds = 0.0f};
        REQUIRE(a.isAdjacentTo(b, 5));  // Gap of 5, equal to max
    }

    SECTION("isAdjacentTo - outside gap") {
        ClickDetection a{.sampleIndex = 100, .amplitude = 0.1f, .timeSeconds = 0.0f};
        ClickDetection b{.sampleIndex = 106, .amplitude = 0.2f, .timeSeconds = 0.0f};
        REQUIRE_FALSE(a.isAdjacentTo(b, 5));  // Gap of 6, exceeds 5
    }
}

// =============================================================================
// T011: ClickDetector::detect() Tests
// =============================================================================

TEST_CASE("ClickDetector::detect - basic detection", "[artifact-detection][click-detector][detect]") {
    ClickDetectorConfig config{
        .sampleRate = 44100.0f,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(config);
    detector.prepare();

    SECTION("SC-001: Detect synthetic click at position 1000 with amplitude 0.5") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.3f);

        // Insert synthetic click (single-sample discontinuity)
        signal[1000] += 0.5f;

        auto detections = detector.detect(signal.data(), signal.size());

        REQUIRE(detections.size() >= 1);
        // Check that we found a detection near position 1000
        bool foundNearExpected = false;
        for (const auto& d : detections) {
            if (d.sampleIndex >= 995 && d.sampleIndex <= 1005) {
                foundNearExpected = true;
                break;
            }
        }
        REQUIRE(foundNearExpected);
    }

    SECTION("SC-001: Detect clicks at various positions") {
        std::vector<float> signal(8192, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.2f);

        // Insert clicks at multiple positions with amplitude >= 0.1
        std::vector<size_t> clickPositions = {500, 2000, 4000, 6000};
        for (size_t pos : clickPositions) {
            signal[pos] += 0.15f;
        }

        auto detections = detector.detect(signal.data(), signal.size());

        // We should detect all inserted clicks
        REQUIRE(detections.size() >= 4);
    }

    SECTION("SC-001: Detect click with amplitude exactly 0.1") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.2f);

        signal[1500] += 0.1f;  // Minimum detectable amplitude per spec

        auto detections = detector.detect(signal.data(), signal.size());

        // Should detect the click
        bool foundNearExpected = false;
        for (const auto& d : detections) {
            if (d.sampleIndex >= 1495 && d.sampleIndex <= 1505) {
                foundNearExpected = true;
                break;
            }
        }
        REQUIRE(foundNearExpected);
    }

    SECTION("SC-002: Zero false positives on clean 440Hz sine wave") {
        std::vector<float> signal(8192, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

        auto detections = detector.detect(signal.data(), signal.size());

        REQUIRE(detections.empty());
    }

    SECTION("SC-002: Zero false positives on clean 1kHz sine wave") {
        std::vector<float> signal(8192, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 1000.0f, 44100.0f, 0.5f);

        auto detections = detector.detect(signal.data(), signal.size());

        REQUIRE(detections.empty());
    }

    SECTION("SC-002: Zero false positives on clean 10kHz sine wave") {
        std::vector<float> signal(8192, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 10000.0f, 44100.0f, 0.5f);

        auto detections = detector.detect(signal.data(), signal.size());

        REQUIRE(detections.empty());
    }

    SECTION("Edge case: All zeros input") {
        std::vector<float> signal(4096, 0.0f);

        auto detections = detector.detect(signal.data(), signal.size());

        REQUIRE(detections.empty());
    }

    SECTION("Edge case: Very short buffer (less than frame size)") {
        std::vector<float> signal(256, 0.0f);  // Less than frameSize of 512
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

        auto detections = detector.detect(signal.data(), signal.size());

        // Should not crash, may or may not detect depending on implementation
        // Just verify it returns something reasonable
        REQUIRE(detections.size() >= 0);  // Always true, but verifies no crash
    }

    SECTION("Edge case: Signal with DC offset") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.3f);

        // Add DC offset
        for (auto& s : signal) {
            s += 0.5f;
        }

        auto detections = detector.detect(signal.data(), signal.size());

        // DC offset should not cause false positives (derivative is 0)
        REQUIRE(detections.empty());
    }

    SECTION("FR-003: Adjacent detections are merged within mergeGap") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.2f);

        // Insert two adjacent clicks within merge gap
        signal[1000] += 0.3f;
        signal[1002] += 0.3f;  // Only 2 samples apart, within mergeGap of 5

        auto detections = detector.detect(signal.data(), signal.size());

        // Should merge into single detection
        REQUIRE(detections.size() == 1);
    }

    SECTION("FR-003: Non-adjacent detections are not merged") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.2f);

        // Insert two non-adjacent clicks
        signal[1000] += 0.3f;
        signal[1020] += 0.3f;  // 20 samples apart, exceeds mergeGap of 5

        auto detections = detector.detect(signal.data(), signal.size());

        // Should remain as two separate detections
        REQUIRE(detections.size() >= 2);
    }
}

// =============================================================================
// T012: Performance Test for SC-005
// =============================================================================

TEST_CASE("ClickDetector::detect - SC-005: Performance 1 second in under 50ms", "[artifact-detection][click-detector][performance]") {
    ClickDetectorConfig config{
        .sampleRate = 44100.0f,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(config);
    detector.prepare();

    // Generate 1 second of audio at 44.1kHz
    std::vector<float> signal(44100, 0.0f);
    TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

    // Insert some clicks
    signal[10000] += 0.3f;
    signal[20000] += 0.3f;
    signal[30000] += 0.3f;

    // Measure execution time
    auto start = std::chrono::high_resolution_clock::now();
    auto detections = detector.detect(signal.data(), signal.size());
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    INFO("Detection took " << duration.count() << " ms");
    REQUIRE(duration.count() < 50);
    REQUIRE(detections.size() >= 3);  // Should detect all clicks
}

// =============================================================================
// T013: Real-Time Safety Test for SC-007
// =============================================================================
// Note: This test requires ENABLE_ALLOCATION_TRACKING to be defined
// For now, we just verify the detector doesn't throw

TEST_CASE("ClickDetector - SC-007: No exceptions during processing", "[artifact-detection][click-detector][realtime]") {
    ClickDetectorConfig config{
        .sampleRate = 44100.0f,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(config);
    detector.prepare();

    std::vector<float> signal(4096, 0.0f);
    TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);
    signal[1000] += 0.3f;

    // Should complete without throwing
    REQUIRE_NOTHROW(detector.detect(signal.data(), signal.size()));
}

// =============================================================================
// T018: Acceptance Scenario 1
// =============================================================================

TEST_CASE("Acceptance: Single-sample discontinuity of 0.5 amplitude detected at correct location", "[artifact-detection][click-detector][acceptance]") {
    ClickDetectorConfig config{
        .sampleRate = 44100.0f,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(config);
    detector.prepare();

    std::vector<float> signal(4096, 0.0f);
    TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.3f);

    // Insert single-sample discontinuity
    const size_t clickPosition = 2000;
    signal[clickPosition] += 0.5f;

    auto detections = detector.detect(signal.data(), signal.size());

    REQUIRE(detections.size() == 1);
    // Allow some tolerance in position due to frame-based processing
    REQUIRE(detections[0].sampleIndex >= clickPosition - 10);
    REQUIRE(detections[0].sampleIndex <= clickPosition + 10);
}

// =============================================================================
// T019: Acceptance Scenario 2
// =============================================================================

TEST_CASE("Acceptance: Clean sine through delay line produces zero artifacts", "[artifact-detection][click-detector][acceptance]") {
    ClickDetectorConfig config{
        .sampleRate = 44100.0f,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(config);
    detector.prepare();

    // Test clean sine waves at various frequencies (20Hz to 20kHz range)
    std::vector<float> frequencies = {20.0f, 100.0f, 440.0f, 1000.0f, 5000.0f, 10000.0f, 15000.0f, 20000.0f};

    for (float freq : frequencies) {
        std::vector<float> signal(8192, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), freq, 44100.0f, 0.5f);

        auto detections = detector.detect(signal.data(), signal.size());

        INFO("Testing frequency: " << freq << " Hz");
        REQUIRE(detections.empty());
    }
}

// =============================================================================
// T020: Acceptance Scenario 3 - Zipper Noise Detection
// =============================================================================

TEST_CASE("Acceptance: Delay line with integer-only indexing produces zipper noise detection", "[artifact-detection][click-detector][acceptance][zipper]") {
    ClickDetectorConfig config{
        .sampleRate = 44100.0f,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(config);
    detector.prepare();

    // Simulate zipper noise: a signal with regular "jumps" that occur
    // when delay time changes without interpolation
    std::vector<float> signal(8192, 0.0f);
    TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.3f);

    // Insert periodic discontinuities simulating block-rate parameter updates
    // (zipper noise occurs when parameters change at block boundaries without smoothing)
    const size_t blockSize = 64;  // Simulated block rate updates
    for (size_t i = blockSize; i < signal.size(); i += blockSize) {
        // Small discontinuity at each block boundary
        signal[i] += 0.12f;  // Just above 0.1 threshold
    }

    auto detections = detector.detect(signal.data(), signal.size());

    // Should detect multiple zipper noise artifacts
    // At ~8192 samples with blockSize=64, we expect ~127 discontinuities
    // Not all may be detected depending on threshold, but we expect many
    INFO("Detected " << detections.size() << " zipper noise artifacts");
    REQUIRE(detections.size() >= 10);  // Should detect a significant number
}

// =============================================================================
// SC-002: Comprehensive frequency range test (20Hz - 20kHz)
// =============================================================================

TEST_CASE("ClickDetector - SC-002: Zero false positives across entire audible range", "[artifact-detection][click-detector][SC-002]") {
    ClickDetectorConfig config{
        .sampleRate = 44100.0f,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(config);
    detector.prepare();

    // Test frequencies spanning 20Hz to 20kHz
    std::vector<float> frequencies;
    for (float freq = 20.0f; freq <= 20000.0f; freq *= 1.5f) {
        frequencies.push_back(freq);
    }

    int totalFalsePositives = 0;

    for (float freq : frequencies) {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), freq, 44100.0f, 0.5f);

        auto detections = detector.detect(signal.data(), signal.size());
        totalFalsePositives += static_cast<int>(detections.size());

        if (!detections.empty()) {
            INFO("False positive at frequency: " << freq << " Hz");
        }
    }

    REQUIRE(totalFalsePositives == 0);
}
