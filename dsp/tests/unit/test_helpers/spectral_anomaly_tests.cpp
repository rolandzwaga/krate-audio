// ==============================================================================
// Unit Tests: Spectral Anomaly Detection
// ==============================================================================
// Tests for spectral flatness-based artifact detection.
//
// Constitution Compliance:
// - Principle XII: Test-First Development (tests written FIRST)
// - Principle VIII: Testing Discipline
//
// Reference: specs/055-artifact-detection/spec.md
// Requirements: FR-010, FR-024
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "artifact_detection.h"
#include "test_signals.h"

#include <array>
#include <cmath>
#include <vector>

using namespace Krate::DSP::TestUtils;
using Catch::Approx;

// =============================================================================
// T044: SpectralAnomalyConfig Validation Tests
// =============================================================================

TEST_CASE("SpectralAnomalyConfig::isValid - validates configuration", "[spectral-anomaly][config]") {
    SECTION("default config is valid") {
        SpectralAnomalyConfig config{};
        REQUIRE(config.isValid());
    }

    SECTION("valid custom config") {
        SpectralAnomalyConfig config{
            .sampleRate = 48000.0f,
            .fftSize = 1024,
            .hopSize = 512,
            .flatnessThreshold = 0.5f
        };
        REQUIRE(config.isValid());
    }

    SECTION("invalid fftSize - not power of 2") {
        SpectralAnomalyConfig config{};
        config.fftSize = 500;
        REQUIRE_FALSE(config.isValid());
    }

    SECTION("invalid flatnessThreshold - above 1") {
        SpectralAnomalyConfig config{};
        config.flatnessThreshold = 1.5f;
        REQUIRE_FALSE(config.isValid());
    }

    SECTION("invalid flatnessThreshold - negative") {
        SpectralAnomalyConfig config{};
        config.flatnessThreshold = -0.1f;
        REQUIRE_FALSE(config.isValid());
    }
}

// =============================================================================
// T045: SpectralAnomalyDetector Basic Detection Tests
// =============================================================================

TEST_CASE("SpectralAnomalyDetector - basic functionality", "[spectral-anomaly][detect]") {
    SpectralAnomalyConfig config{
        .sampleRate = 44100.0f,
        .fftSize = 512,
        .hopSize = 256,
        .flatnessThreshold = 0.5f
    };

    SpectralAnomalyDetector detector(config);
    detector.prepare();

    SECTION("pure sine has low flatness - no detections") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

        auto detections = detector.detect(signal.data(), signal.size());

        // Pure sine should have very low flatness (< 0.1)
        // With threshold at 0.5, no frames should be detected
        REQUIRE(detections.empty());
    }

    SECTION("white noise has high flatness - many detections") {
        // Lower threshold to detect white noise
        SpectralAnomalyConfig noiseConfig{
            .sampleRate = 44100.0f,
            .fftSize = 512,
            .hopSize = 256,
            .flatnessThreshold = 0.5f
        };
        SpectralAnomalyDetector noiseDetector(noiseConfig);
        noiseDetector.prepare();

        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateWhiteNoise(signal.data(), signal.size(), 42);

        auto detections = noiseDetector.detect(signal.data(), signal.size());

        // White noise should have high flatness (> 0.8)
        // Most frames should be detected
        INFO("Detections on white noise: " << detections.size());
        REQUIRE(detections.size() > 5);
    }

    SECTION("signal with click shows elevated flatness in affected frame") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

        // Insert click
        signal[2000] = 1.5f;

        // Use low threshold to detect click effect
        SpectralAnomalyConfig clickConfig{
            .sampleRate = 44100.0f,
            .fftSize = 512,
            .hopSize = 256,
            .flatnessThreshold = 0.15f  // Lower threshold to catch click
        };
        SpectralAnomalyDetector clickDetector(clickConfig);
        clickDetector.prepare();

        auto detections = clickDetector.detect(signal.data(), signal.size());

        INFO("Detections with click: " << detections.size());
        // Should detect at least one anomaly frame containing the click
        // (frame around sample 2000)
        bool foundNearClick = false;
        for (const auto& d : detections) {
            // Frame at sample 2000 would be around frame 7-8 with hopSize 256
            if (d.frameIndex >= 6 && d.frameIndex <= 10) {
                foundNearClick = true;
                break;
            }
        }
        REQUIRE(foundNearClick);
    }
}

// =============================================================================
// T046: computeFlatnessTrack() Tests
// =============================================================================

TEST_CASE("SpectralAnomalyDetector::computeFlatnessTrack - flatness over time", "[spectral-anomaly][track]") {
    SpectralAnomalyConfig config{
        .sampleRate = 44100.0f,
        .fftSize = 512,
        .hopSize = 256,
        .flatnessThreshold = 0.5f
    };

    SpectralAnomalyDetector detector(config);
    detector.prepare();

    SECTION("pure sine flatness track is uniformly low") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

        auto flatnessTrack = detector.computeFlatnessTrack(signal.data(), signal.size());

        REQUIRE(flatnessTrack.size() > 0);

        // All flatness values should be low (< 0.1 for pure sine)
        for (float f : flatnessTrack) {
            INFO("Flatness: " << f);
            REQUIRE(f < 0.15f);
        }
    }

    SECTION("white noise flatness track is uniformly high") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateWhiteNoise(signal.data(), signal.size(), 42);

        auto flatnessTrack = detector.computeFlatnessTrack(signal.data(), signal.size());

        REQUIRE(flatnessTrack.size() > 0);

        // All flatness values should be high (> 0.7 for white noise)
        for (float f : flatnessTrack) {
            INFO("Flatness: " << f);
            REQUIRE(f > 0.6f);
        }
    }

    SECTION("flatness track shows spike at click location") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

        // Insert click
        signal[2000] = 2.0f;

        auto flatnessTrack = detector.computeFlatnessTrack(signal.data(), signal.size());

        REQUIRE(flatnessTrack.size() > 0);

        // Find the frame with highest flatness
        size_t maxFrame = 0;
        float maxFlatness = 0.0f;
        for (size_t i = 0; i < flatnessTrack.size(); ++i) {
            if (flatnessTrack[i] > maxFlatness) {
                maxFlatness = flatnessTrack[i];
                maxFrame = i;
            }
        }

        INFO("Max flatness: " << maxFlatness << " at frame " << maxFrame);

        // The max flatness should be near the click location
        // Sample 2000 / hopSize 256 = frame ~7-8
        REQUIRE(maxFrame >= 6);
        REQUIRE(maxFrame <= 10);
    }
}

// =============================================================================
// T047: Edge Cases
// =============================================================================

TEST_CASE("SpectralAnomalyDetector - edge cases", "[spectral-anomaly][edge-cases]") {
    SpectralAnomalyConfig config{
        .sampleRate = 44100.0f,
        .fftSize = 512,
        .hopSize = 256,
        .flatnessThreshold = 0.5f
    };

    SpectralAnomalyDetector detector(config);
    detector.prepare();

    SECTION("empty buffer returns empty results") {
        auto detections = detector.detect(nullptr, 0);
        REQUIRE(detections.empty());
    }

    SECTION("buffer smaller than FFT size returns empty results") {
        std::vector<float> signal(256, 0.0f);  // Smaller than fftSize
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

        auto detections = detector.detect(signal.data(), signal.size());
        REQUIRE(detections.empty());
    }

    SECTION("silence returns empty results") {
        std::vector<float> signal(4096, 0.0f);

        auto detections = detector.detect(signal.data(), signal.size());
        REQUIRE(detections.empty());
    }
}

// =============================================================================
// T048: SpectralAnomalyDetection struct
// =============================================================================

TEST_CASE("SpectralAnomalyDetection - struct fields", "[spectral-anomaly][struct]") {
    SECTION("fields are initialized correctly") {
        SpectralAnomalyDetection detection{
            .frameIndex = 10,
            .timeSeconds = 0.058f,  // 10 * 256 / 44100
            .flatness = 0.75f
        };

        REQUIRE(detection.frameIndex == 10);
        REQUIRE(detection.timeSeconds == Approx(0.058f));
        REQUIRE(detection.flatness == Approx(0.75f));
    }
}
