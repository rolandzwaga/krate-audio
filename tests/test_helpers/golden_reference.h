// ==============================================================================
// Test Helper: Golden Reference Utilities
// ==============================================================================
// Golden reference comparison and A/B testing utilities for DSP validation.
//
// This is TEST INFRASTRUCTURE, not production DSP code.
//
// Location: tests/test_helpers/golden_reference.h
// Namespace: Krate::DSP::TestUtils
//
// Reference: specs/055-artifact-detection/spec.md
// Requirements: FR-017, FR-018, FR-019, FR-020, FR-021, FR-022, FR-023, FR-024
// ==============================================================================

#pragma once

#include "artifact_detection.h"
#include "signal_metrics.h"

#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace Krate {
namespace DSP {
namespace TestUtils {

// =============================================================================
// GoldenReferenceConfig (FR-024)
// =============================================================================

/// @brief Configuration for golden reference comparison
struct GoldenReferenceConfig {
    float sampleRate = 44100.0f;            ///< Sample rate in Hz
    float snrThresholdDb = 60.0f;           ///< Minimum acceptable SNR (dB)
    float maxClickAmplitude = 0.1f;         ///< Maximum acceptable click amplitude
    float thdThresholdPercent = 1.0f;       ///< Maximum acceptable THD (%)
    float maxCrestFactorDb = 20.0f;         ///< Maximum acceptable crest factor (dB)
    size_t maxClickCount = 0;               ///< Maximum acceptable click count

    /// @brief Validate configuration
    [[nodiscard]] bool isValid() const noexcept {
        if (sampleRate < 22050.0f || sampleRate > 192000.0f) {
            return false;
        }
        if (snrThresholdDb < 10.0f || snrThresholdDb > 200.0f) {
            return false;
        }
        return true;
    }
};

// =============================================================================
// GoldenComparisonResult (FR-017)
// =============================================================================

/// @brief Result of comparing signal with golden reference
struct GoldenComparisonResult {
    bool passed = true;                     ///< Overall pass/fail
    float snrDb = 0.0f;                     ///< Signal-to-Noise Ratio (dB)
    float thdPercent = 0.0f;                ///< Total Harmonic Distortion (%)
    float crestFactorDb = 0.0f;             ///< Crest factor (dB)
    size_t clicksDetected = 0;              ///< Number of clicks detected
    float maxClickAmplitude = 0.0f;         ///< Largest click amplitude
    std::vector<std::string> failureReasons;///< Descriptions of failures

    /// @brief Check if all metrics are valid (no NaN)
    [[nodiscard]] bool isValid() const noexcept {
        return std::isfinite(snrDb) &&
               std::isfinite(thdPercent) &&
               std::isfinite(crestFactorDb) &&
               std::isfinite(maxClickAmplitude);
    }
};

// =============================================================================
// ABTestResult (FR-020)
// =============================================================================

/// @brief Result of A/B comparison between two processors
struct ABTestResult {
    float snrDifferenceDb = 0.0f;           ///< SNR difference (A - B in dB)
    float thdDifferencePercent = 0.0f;      ///< THD difference (A - B in %)
    int clickCountDifference = 0;           ///< Click count difference (A - B)
    size_t clickCountA = 0;                 ///< Clicks in processor A output
    size_t clickCountB = 0;                 ///< Clicks in processor B output
    float snrA = 0.0f;                      ///< SNR of A
    float snrB = 0.0f;                      ///< SNR of B

    /// @brief Check if outputs are equivalent within tolerances
    /// @param snrToleranceDb Maximum SNR difference (dB)
    /// @param thdTolerancePercent Maximum THD difference (%)
    /// @param clickTolerance Maximum click count difference
    [[nodiscard]] bool equivalent(
        float snrToleranceDb,
        float thdTolerancePercent,
        int clickTolerance
    ) const noexcept {
        return std::abs(snrDifferenceDb) <= snrToleranceDb &&
               std::abs(thdDifferencePercent) <= thdTolerancePercent &&
               std::abs(clickCountDifference) <= clickTolerance;
    }
};

// =============================================================================
// compareWithReference() (FR-017, FR-018, FR-019)
// =============================================================================

/// @brief Compare a signal with a golden reference
/// @param signal Processed signal to test
/// @param reference Golden reference signal
/// @param numSamples Number of samples
/// @param config Comparison configuration
/// @return Comparison result
[[nodiscard]] inline GoldenComparisonResult compareWithReference(
    const float* signal,
    const float* reference,
    size_t numSamples,
    const GoldenReferenceConfig& config
) {
    GoldenComparisonResult result;

    if (signal == nullptr || reference == nullptr || numSamples == 0) {
        result.passed = false;
        result.failureReasons.push_back("Invalid input pointers or size");
        return result;
    }

    // Calculate SNR
    result.snrDb = SignalMetrics::calculateSNR(signal, reference, numSamples);

    // Calculate THD (assume fundamental at 1kHz for now)
    result.thdPercent = SignalMetrics::calculateTHD(
        signal, numSamples, 1000.0f, config.sampleRate);

    // Calculate crest factor
    result.crestFactorDb = SignalMetrics::calculateCrestFactorDb(signal, numSamples);

    // Detect clicks in the difference signal
    std::vector<float> difference(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        difference[i] = signal[i] - reference[i];
    }

    ClickDetectorConfig clickConfig{
        .sampleRate = config.sampleRate,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f,
        .energyThresholdDb = -80.0f,
        .mergeGap = 5
    };
    ClickDetector clickDetector(clickConfig);
    clickDetector.prepare();

    auto clicks = clickDetector.detect(difference.data(), difference.size());
    result.clicksDetected = clicks.size();

    // Find max click amplitude
    result.maxClickAmplitude = 0.0f;
    for (const auto& click : clicks) {
        float absAmp = std::abs(click.amplitude);
        if (absAmp > result.maxClickAmplitude) {
            result.maxClickAmplitude = absAmp;
        }
    }

    // Check against thresholds
    if (result.snrDb < config.snrThresholdDb) {
        result.passed = false;
        result.failureReasons.push_back(
            "SNR " + std::to_string(result.snrDb) + " dB below threshold " +
            std::to_string(config.snrThresholdDb) + " dB");
    }

    if (result.clicksDetected > config.maxClickCount) {
        result.passed = false;
        result.failureReasons.push_back(
            "Detected " + std::to_string(result.clicksDetected) +
            " clicks, maximum allowed: " + std::to_string(config.maxClickCount));
    }

    if (result.maxClickAmplitude > config.maxClickAmplitude) {
        result.passed = false;
        result.failureReasons.push_back(
            "Max click amplitude " + std::to_string(result.maxClickAmplitude) +
            " exceeds threshold " + std::to_string(config.maxClickAmplitude));
    }

    if (result.thdPercent > config.thdThresholdPercent) {
        result.passed = false;
        result.failureReasons.push_back(
            "THD " + std::to_string(result.thdPercent) +
            "% exceeds threshold " + std::to_string(config.thdThresholdPercent) + "%");
    }

    if (result.crestFactorDb > config.maxCrestFactorDb) {
        result.passed = false;
        result.failureReasons.push_back(
            "Crest factor " + std::to_string(result.crestFactorDb) +
            " dB exceeds threshold " + std::to_string(config.maxCrestFactorDb) + " dB");
    }

    return result;
}

// =============================================================================
// abCompare() (FR-020, FR-021, FR-022, FR-023)
// =============================================================================

/// @brief Compare outputs of two processors (A/B test)
/// @tparam SignalGenerator Function type: std::vector<float>()
/// @tparam ProcessorA Function type: std::vector<float>(const std::vector<float>&)
/// @tparam ProcessorB Function type: std::vector<float>(const std::vector<float>&)
/// @param generateSignal Function to generate test signal
/// @param processA Function to process through processor A
/// @param processB Function to process through processor B
/// @param sampleRate Sample rate for analysis
/// @return A/B comparison result
template<typename SignalGenerator, typename ProcessorA, typename ProcessorB>
[[nodiscard]] ABTestResult abCompare(
    SignalGenerator&& generateSignal,
    ProcessorA&& processA,
    ProcessorB&& processB,
    float sampleRate
) {
    ABTestResult result;

    // Generate test signal
    auto input = generateSignal();
    if (input.empty()) {
        return result;
    }

    // Process through both
    auto outputA = processA(input);
    auto outputB = processB(input);

    if (outputA.empty() || outputB.empty() || outputA.size() != outputB.size()) {
        return result;
    }

    const size_t numSamples = outputA.size();

    // Calculate SNR for both (relative to input)
    result.snrA = SignalMetrics::calculateSNR(outputA.data(), input.data(), numSamples);
    result.snrB = SignalMetrics::calculateSNR(outputB.data(), input.data(), numSamples);
    result.snrDifferenceDb = result.snrA - result.snrB;

    // Calculate THD for both
    float thdA = SignalMetrics::calculateTHD(outputA.data(), numSamples, 440.0f, sampleRate);
    float thdB = SignalMetrics::calculateTHD(outputB.data(), numSamples, 440.0f, sampleRate);
    result.thdDifferencePercent = thdA - thdB;

    // Detect clicks in both
    ClickDetectorConfig clickConfig{
        .sampleRate = sampleRate,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(clickConfig);
    detector.prepare();

    auto clicksA = detector.detect(outputA.data(), numSamples);
    detector.reset();
    auto clicksB = detector.detect(outputB.data(), numSamples);

    result.clickCountA = clicksA.size();
    result.clickCountB = clicksB.size();
    result.clickCountDifference = static_cast<int>(result.clickCountA) -
                                  static_cast<int>(result.clickCountB);

    return result;
}

} // namespace TestUtils
} // namespace DSP
} // namespace Krate
