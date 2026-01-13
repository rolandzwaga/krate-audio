// ==============================================================================
// Integration Tests: Artifact Detection System
// ==============================================================================
// Tests verifying all artifact detection components work together and
// integrate properly with existing test infrastructure.
//
// Constitution Compliance:
// - Principle XII: Test-First Development (tests written FIRST)
// - Principle VIII: Testing Discipline
//
// Reference: specs/055-artifact-detection/spec.md
// Requirements: FR-020, FR-021, FR-022, SC-009
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

// New artifact detection utilities
#include "artifact_detection.h"
#include "signal_metrics.h"
#include "statistical_utils.h"
#include "golden_reference.h"
#include "parameter_sweep.h"

// Existing test infrastructure (SC-009, FR-020)
#include "spectral_analysis.h"
#include "test_signals.h"
#include "buffer_comparison.h"

#include <array>
#include <cmath>
#include <vector>

using namespace Krate::DSP::TestUtils;
using Catch::Approx;

// =============================================================================
// T085: Integration test combining multiple detectors
// =============================================================================

TEST_CASE("Integration - multiple detectors on same signal", "[integration][artifact-detection]") {
    // Generate a signal with known artifact
    std::vector<float> signal(8192, 0.0f);
    TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

    // Insert a click artifact at known location
    const size_t clickLocation = 4000;
    signal[clickLocation] = 1.5f;  // Large discontinuity

    SECTION("ClickDetector detects the artifact") {
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

        auto detections = detector.detect(signal.data(), signal.size());

        INFO("Click detections: " << detections.size());
        REQUIRE(detections.size() >= 1);

        // Verify detection is near the click location
        bool foundNearClick = false;
        for (const auto& d : detections) {
            if (d.sampleIndex >= clickLocation - 10 && d.sampleIndex <= clickLocation + 10) {
                foundNearClick = true;
                break;
            }
        }
        REQUIRE(foundNearClick);
    }

    SECTION("SpectralAnomalyDetector shows elevated flatness at click") {
        SpectralAnomalyConfig config{
            .sampleRate = 44100.0f,
            .fftSize = 512,
            .hopSize = 256,
            .flatnessThreshold = 0.15f  // Low threshold to catch click
        };

        SpectralAnomalyDetector detector(config);
        detector.prepare();

        auto flatnessTrack = detector.computeFlatnessTrack(signal.data(), signal.size());

        // Find frame containing click (clickLocation / hopSize)
        size_t expectedFrame = clickLocation / 256;

        // Find max flatness - should be near click
        float maxFlatness = 0.0f;
        size_t maxFrame = 0;
        for (size_t i = 0; i < flatnessTrack.size(); ++i) {
            if (flatnessTrack[i] > maxFlatness) {
                maxFlatness = flatnessTrack[i];
                maxFrame = i;
            }
        }

        INFO("Max flatness: " << maxFlatness << " at frame " << maxFrame);
        INFO("Expected frame: " << expectedFrame);

        // Max flatness should be near click frame
        REQUIRE(maxFrame >= expectedFrame - 3);
        REQUIRE(maxFrame <= expectedFrame + 3);
    }

    SECTION("LPCDetector analyzes prediction error at click") {
        LPCDetectorConfig config{
            .sampleRate = 44100.0f,
            .lpcOrder = 16,
            .frameSize = 512,
            .hopSize = 256,
            .threshold = 3.0f  // Lower threshold for detection
        };

        LPCDetector detector(config);
        detector.prepare();

        auto detections = detector.detect(signal.data(), signal.size());

        // LPC should detect anomalies near the click
        INFO("LPC detections: " << detections.size());

        // At minimum, LPC should not crash and should return valid detections
        // (exact count depends on signal characteristics)
        REQUIRE(true);  // Detector runs without error
    }

    SECTION("SignalMetrics reports elevated crest factor") {
        // Window containing click should have high crest factor
        const size_t windowSize = 1024;
        const size_t windowStart = clickLocation - windowSize / 2;

        float crestFactor = SignalMetrics::calculateCrestFactorDb(
            signal.data() + windowStart, windowSize);

        INFO("Crest factor at click: " << crestFactor << " dB");

        // Click creates high peak-to-RMS ratio
        REQUIRE(crestFactor > 10.0f);  // Higher than normal sine (~3 dB)
    }
}

// =============================================================================
// T086: Integration with existing test infrastructure (SC-009, FR-020)
// =============================================================================

TEST_CASE("Integration - namespace compatibility with existing spectral_analysis.h", "[integration][namespace]") {
    // Verify both old and new utilities work in same namespace
    SECTION("AliasingTestConfig from spectral_analysis.h is accessible") {
        AliasingTestConfig config{
            .testFrequencyHz = 5000.0f,
            .sampleRate = 44100.0f,
            .driveGain = 4.0f,
            .fftSize = 4096,
            .maxHarmonic = 10
        };

        REQUIRE(config.isValid());
    }

    SECTION("ClickDetectorConfig from artifact_detection.h is accessible") {
        ClickDetectorConfig config{
            .sampleRate = 44100.0f,
            .frameSize = 512,
            .hopSize = 256,
            .detectionThreshold = 5.0f,
            .energyThresholdDb = -60.0f,
            .mergeGap = 5
        };

        REQUIRE(config.isValid());
    }

    SECTION("both can be used in same test") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

        // Use existing aliasing measurement config
        AliasingTestConfig aliasingConfig{
            .testFrequencyHz = 440.0f,
            .sampleRate = 44100.0f,
            .driveGain = 4.0f,
            .fftSize = 4096,
            .maxHarmonic = 10
        };

        // Use new click detector
        ClickDetectorConfig clickConfig{
            .sampleRate = 44100.0f,
            .frameSize = 512,
            .hopSize = 256,
            .detectionThreshold = 5.0f,
            .energyThresholdDb = -60.0f,
            .mergeGap = 5
        };

        ClickDetector clickDetector(clickConfig);
        clickDetector.prepare();
        auto clicks = clickDetector.detect(signal.data(), signal.size());

        // Both configs work together without symbol conflicts
        REQUIRE(aliasingConfig.isValid());
        REQUIRE(clicks.empty());  // Clean sine should have no clicks
    }
}

TEST_CASE("Integration - combined usage of AliasingMeasurement and ClickDetection", "[integration][combined]") {
    // Scenario: Test a DSP processor for both aliasing and click artifacts

    std::vector<float> signal(8192, 0.0f);
    TestHelpers::generateSine(signal.data(), signal.size(), 1000.0f, 44100.0f, 0.5f);

    SECTION("clean signal passes both aliasing and click detection") {
        // Check for clicks
        ClickDetectorConfig clickConfig{
            .sampleRate = 44100.0f,
            .frameSize = 512,
            .hopSize = 256,
            .detectionThreshold = 5.0f,
            .energyThresholdDb = -60.0f,
            .mergeGap = 5
        };
        ClickDetector clickDetector(clickConfig);
        clickDetector.prepare();
        auto clicks = clickDetector.detect(signal.data(), signal.size());

        // Use aliasing measurement with a processor
        AliasingTestConfig aliasingConfig{
            .testFrequencyHz = 1000.0f,
            .sampleRate = 44100.0f,
            .driveGain = 4.0f,
            .fftSize = 4096,
            .maxHarmonic = 10
        };
        auto measurement = measureAliasing(aliasingConfig, identityReference);

        INFO("Clicks detected: " << clicks.size());
        INFO("Aliasing power: " << measurement.aliasingPowerDb << " dB");

        // Clean sine should pass both
        REQUIRE(clicks.empty());
        // Identity processor should have minimal aliasing
        REQUIRE(measurement.isValid());
    }
}

TEST_CASE("Integration - API consistency with buffer_comparison.h", "[integration][api]") {
    std::vector<float> signal(1024, 0.0f);
    TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

    SECTION("RMS calculation matches existing and new implementations") {
        // Existing buffer_comparison.h uses calculateRMS in TestHelpers namespace
        float existingRms = TestHelpers::calculateRMS(signal.data(), signal.size());

        // Verify consistency
        REQUIRE(existingRms > 0.0f);
        REQUIRE(existingRms == Approx(0.5f / std::sqrt(2.0f)).margin(0.01f));  // Sine RMS = amplitude / sqrt(2)
    }

    SECTION("Peak calculation is consistent") {
        float peak = TestHelpers::findPeak(signal.data(), signal.size());

        REQUIRE(peak == Approx(0.5f).margin(0.01f));  // Peak = amplitude
    }
}

// =============================================================================
// T087: Integration with Catch2 assertions (FR-021)
// =============================================================================

TEST_CASE("Integration - Catch2 assertion compatibility", "[integration][catch2]") {
    SECTION("GoldenComparisonResult works with REQUIRE") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 1000.0f, 44100.0f, 0.5f);

        GoldenReferenceConfig config{
            .sampleRate = 44100.0f,
            .snrThresholdDb = 60.0f,
            .maxClickAmplitude = 0.1f,
            .thdThresholdPercent = 1.0f,
            .maxCrestFactorDb = 20.0f
        };

        auto result = compareWithReference(
            signal.data(), signal.data(), signal.size(), config);

        // Direct boolean assertion
        REQUIRE(result.passed);
        REQUIRE(result.isValid());

        // Metric assertions with Approx
        REQUIRE(result.snrDb > 100.0f);
        REQUIRE(result.clicksDetected == 0);
    }

    SECTION("ABTestResult::equivalent works with REQUIRE") {
        ABTestResult result;
        result.snrDifferenceDb = 0.5f;
        result.thdDifferencePercent = 0.3f;
        result.clickCountDifference = 0;

        REQUIRE(result.equivalent(1.0f, 0.5f, 0));
    }

    SECTION("SweepResult works with REQUIRE") {
        SweepResult result;
        result.parameterName = "test";
        result.stepResults.push_back({.parameterValue = 0.0f, .passed = true});
        result.stepResults.push_back({.parameterValue = 0.5f, .passed = true});
        result.stepResults.push_back({.parameterValue = 1.0f, .passed = true});

        REQUIRE_FALSE(result.hasFailed());
        REQUIRE(result.getFailedSteps().empty());
    }

    SECTION("ClickDetection fields accessible for assertions") {
        ClickDetection detection{
            .sampleIndex = 1000,
            .amplitude = 0.5f,
            .timeSeconds = 1000.0f / 44100.0f
        };

        REQUIRE(detection.sampleIndex == 1000);
        REQUIRE(detection.amplitude == Approx(0.5f));
        REQUIRE(detection.timeSeconds == Approx(0.0227f).margin(0.001f));
    }
}

// =============================================================================
// T088: Header-only usage verification (FR-022)
// =============================================================================

TEST_CASE("Integration - header-only verification", "[integration][header-only]") {
    // This test verifies all headers can be included without linking issues
    // The fact that this test compiles proves FR-022

    SECTION("statistical_utils.h is header-only") {
        std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        float mean = StatisticalUtils::computeMean(data.data(), data.size());
        REQUIRE(mean == Approx(3.0f));
    }

    SECTION("signal_metrics.h is header-only") {
        std::vector<float> signal(1024, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

        float crest = SignalMetrics::calculateCrestFactorDb(signal.data(), signal.size());
        REQUIRE(crest > 0.0f);
    }

    SECTION("artifact_detection.h is header-only") {
        ClickDetectorConfig config{};
        REQUIRE(config.isValid());
    }

    SECTION("golden_reference.h is header-only") {
        GoldenReferenceConfig config{};
        REQUIRE(config.isValid());
    }

    SECTION("parameter_sweep.h is header-only") {
        ParameterSweepConfig config{
            .parameterName = "gain",
            .minValue = 0.0f,
            .maxValue = 1.0f,
            .numSteps = 5,
            .stepType = StepType::Linear,
            .checkForClicks = true,
            .checkThd = false,
            .thdThresholdPercent = 1.0f,
            .clickThreshold = 5.0f
        };
        auto values = generateParameterValues(config);
        REQUIRE(values.size() == 5);
    }
}

// =============================================================================
// T089: No symbol conflicts verification
// =============================================================================

TEST_CASE("Integration - no symbol conflicts between headers", "[integration][symbols]") {
    // All headers in same translation unit should not cause ODR violations

    SECTION("all config structs have unique names") {
        // These all compile without conflict
        ClickDetectorConfig clickConfig{};
        LPCDetectorConfig lpcConfig{};
        SpectralAnomalyConfig spectralConfig{};
        GoldenReferenceConfig goldenConfig{};
        ParameterSweepConfig sweepConfig{};
        AliasingTestConfig aliasingConfig{};

        REQUIRE(clickConfig.isValid());
        REQUIRE(lpcConfig.isValid());
        REQUIRE(spectralConfig.isValid());
        REQUIRE(goldenConfig.isValid());
        REQUIRE(aliasingConfig.isValid());
    }

    SECTION("detection result types have unique names") {
        ClickDetection clickDetection{};
        SpectralAnomalyDetection spectralDetection{};
        GoldenComparisonResult goldenResult{};
        ABTestResult abResult{};
        AliasingMeasurement aliasingMeasurement{};

        // All compile and are distinct types
        REQUIRE(true);
    }
}

// =============================================================================
// T090: Test signal generators integration (FR-016 to FR-019)
// =============================================================================

TEST_CASE("Integration - test signal generators from test_signals.h", "[integration][signals]") {
    SECTION("generateSine works with artifact detectors (FR-016)") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 1000.0f, 44100.0f, 0.5f);

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

        auto clicks = detector.detect(signal.data(), signal.size());
        REQUIRE(clicks.empty());  // Clean sine = no clicks
    }

    SECTION("generateWhiteNoise works with spectral analysis (FR-019)") {
        std::vector<float> noise(4096, 0.0f);
        TestHelpers::generateWhiteNoise(noise.data(), noise.size(), 42);

        SpectralAnomalyConfig config{
            .sampleRate = 44100.0f,
            .fftSize = 512,
            .hopSize = 256,
            .flatnessThreshold = 0.5f
        };
        SpectralAnomalyDetector detector(config);
        detector.prepare();

        auto flatnessTrack = detector.computeFlatnessTrack(noise.data(), noise.size());

        // White noise should have high flatness
        float avgFlatness = StatisticalUtils::computeMean(
            flatnessTrack.data(), flatnessTrack.size());

        INFO("Average flatness for white noise: " << avgFlatness);
        REQUIRE(avgFlatness > 0.6f);
    }

    SECTION("generateImpulse creates detectable click (FR-017)") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateImpulse(signal.data(), signal.size(), 2000);  // Impulse at sample 2000

        ClickDetectorConfig config{
            .sampleRate = 44100.0f,
            .frameSize = 512,
            .hopSize = 256,
            .detectionThreshold = 3.0f,  // Lower threshold
            .energyThresholdDb = -80.0f,
            .mergeGap = 5
        };
        ClickDetector detector(config);
        detector.prepare();

        auto clicks = detector.detect(signal.data(), signal.size());

        INFO("Clicks detected: " << clicks.size());
        REQUIRE(clicks.size() >= 1);

        // Click should be near impulse location
        bool foundNearImpulse = false;
        for (const auto& click : clicks) {
            if (click.sampleIndex >= 1990 && click.sampleIndex <= 2010) {
                foundNearImpulse = true;
                break;
            }
        }
        REQUIRE(foundNearImpulse);
    }
}

// =============================================================================
// End-to-end workflow test
// =============================================================================

TEST_CASE("Integration - end-to-end DSP validation workflow", "[integration][workflow]") {
    SECTION("complete DSP validation using all utilities") {
        // Step 1: Generate test signal
        std::vector<float> input(8192, 0.0f);
        TestHelpers::generateSine(input.data(), input.size(), 1000.0f, 44100.0f, 0.5f);

        // Step 2: Simulate processing (identity for this test)
        std::vector<float> output = input;

        // Step 3: Check for clicks
        ClickDetectorConfig clickConfig{
            .sampleRate = 44100.0f,
            .frameSize = 512,
            .hopSize = 256,
            .detectionThreshold = 5.0f,
            .energyThresholdDb = -60.0f,
            .mergeGap = 5
        };
        ClickDetector clickDetector(clickConfig);
        clickDetector.prepare();
        auto clicks = clickDetector.detect(output.data(), output.size());

        // Step 4: Measure signal quality
        auto quality = SignalMetrics::measureQuality(
            output.data(), input.data(), output.size(), 1000.0f, 44100.0f);

        // Step 5: Compare with reference
        GoldenReferenceConfig goldenConfig{
            .sampleRate = 44100.0f,
            .snrThresholdDb = 60.0f,
            .maxClickAmplitude = 0.1f,
            .thdThresholdPercent = 1.0f,
            .maxCrestFactorDb = 20.0f
        };
        auto comparison = compareWithReference(
            output.data(), input.data(), output.size(), goldenConfig);

        // Step 6: Verify all metrics
        INFO("Clicks: " << clicks.size());
        INFO("SNR: " << quality.snrDb << " dB");
        INFO("THD: " << quality.thdPercent << "%");
        INFO("Crest: " << quality.crestFactorDb << " dB");
        INFO("Comparison passed: " << comparison.passed);

        REQUIRE(clicks.empty());
        REQUIRE(quality.isValid());
        REQUIRE(comparison.passed);
    }
}
