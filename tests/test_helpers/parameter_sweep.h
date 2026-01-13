// ==============================================================================
// Test Helper: Parameter Sweep Utilities
// ==============================================================================
// Automated parameter range testing with artifact detection.
//
// This is TEST INFRASTRUCTURE, not production DSP code.
//
// Location: tests/test_helpers/parameter_sweep.h
// Namespace: Krate::DSP::TestUtils
//
// Reference: specs/055-artifact-detection/spec.md
// Requirements: FR-012, FR-013, FR-014, FR-015, FR-016, FR-024
// ==============================================================================

#pragma once

#include "artifact_detection.h"
#include "signal_metrics.h"

#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Krate {
namespace DSP {
namespace TestUtils {

// =============================================================================
// StepType Enumeration (FR-015)
// =============================================================================

/// @brief Parameter sweep step distribution type
enum class StepType {
    Linear,      ///< Evenly spaced steps
    Logarithmic  ///< Logarithmically spaced (for frequencies, etc.)
};

// =============================================================================
// ParameterSweepConfig (FR-012, FR-024)
// =============================================================================

/// @brief Configuration for parameter sweep testing
struct ParameterSweepConfig {
    std::string parameterName = "";     ///< Name for reporting
    float minValue = 0.0f;              ///< Minimum parameter value
    float maxValue = 1.0f;              ///< Maximum parameter value
    size_t numSteps = 10;               ///< Number of steps (1-1000)
    StepType stepType = StepType::Linear;  ///< Step distribution type
    bool checkForClicks = true;         ///< Enable click detection
    bool checkThd = false;              ///< Enable THD checking
    float thdThresholdPercent = 1.0f;   ///< THD threshold in percent
    float clickThreshold = 5.0f;        ///< Sigma threshold for clicks

    /// @brief Validate configuration
    [[nodiscard]] bool isValid() const noexcept {
        // NumSteps must be between 1 and 1000
        if (numSteps == 0 || numSteps > 1000) {
            return false;
        }

        // Min must be <= max
        if (minValue > maxValue) {
            return false;
        }

        // Logarithmic requires positive values
        if (stepType == StepType::Logarithmic && minValue <= 0.0f) {
            return false;
        }

        return true;
    }
};

// =============================================================================
// StepResult (FR-013)
// =============================================================================

/// @brief Result for a single parameter value in a sweep
struct StepResult {
    float parameterValue = 0.0f;        ///< Parameter value for this step
    bool passed = true;                 ///< Overall pass/fail
    size_t clicksDetected = 0;          ///< Number of clicks detected
    float thdPercent = 0.0f;            ///< Measured THD
    float snrDb = 0.0f;                 ///< Measured SNR
    std::string failureReason = "";     ///< Description if failed
};

// =============================================================================
// SweepResult (FR-014)
// =============================================================================

/// @brief Aggregate result of a parameter sweep
struct SweepResult {
    std::vector<StepResult> stepResults;    ///< Results for each step
    std::string parameterName = "";         ///< Parameter being tested

    /// @brief Check if any steps failed
    [[nodiscard]] bool hasFailed() const noexcept {
        for (const auto& step : stepResults) {
            if (!step.passed) {
                return true;
            }
        }
        return false;
    }

    /// @brief Get indices of failed steps
    [[nodiscard]] std::vector<size_t> getFailedSteps() const noexcept {
        std::vector<size_t> failed;
        for (size_t i = 0; i < stepResults.size(); ++i) {
            if (!stepResults[i].passed) {
                failed.push_back(i);
            }
        }
        return failed;
    }

    /// @brief Get contiguous failing parameter ranges
    /// @return Vector of (start, end) value pairs
    [[nodiscard]] std::vector<std::pair<float, float>> getFailingRanges() const noexcept {
        std::vector<std::pair<float, float>> ranges;

        if (stepResults.empty()) {
            return ranges;
        }

        bool inFailRange = false;
        float rangeStart = 0.0f;
        float rangeEnd = 0.0f;

        for (size_t i = 0; i < stepResults.size(); ++i) {
            if (!stepResults[i].passed) {
                if (!inFailRange) {
                    // Start new range
                    inFailRange = true;
                    rangeStart = stepResults[i].parameterValue;
                }
                rangeEnd = stepResults[i].parameterValue;
            } else {
                if (inFailRange) {
                    // End current range
                    ranges.push_back({rangeStart, rangeEnd});
                    inFailRange = false;
                }
            }
        }

        // Close any open range at end
        if (inFailRange) {
            ranges.push_back({rangeStart, rangeEnd});
        }

        return ranges;
    }
};

// =============================================================================
// Parameter Value Generation (FR-015)
// =============================================================================

/// @brief Generate parameter values for a sweep
/// @param config Sweep configuration
/// @return Vector of parameter values to test
[[nodiscard]] inline std::vector<float> generateParameterValues(
    const ParameterSweepConfig& config
) noexcept {
    std::vector<float> values;
    values.reserve(config.numSteps);

    if (config.numSteps == 1) {
        values.push_back(config.minValue);
        return values;
    }

    switch (config.stepType) {
        case StepType::Linear: {
            const float step = (config.maxValue - config.minValue) /
                              static_cast<float>(config.numSteps - 1);
            for (size_t i = 0; i < config.numSteps; ++i) {
                values.push_back(config.minValue + step * static_cast<float>(i));
            }
            break;
        }

        case StepType::Logarithmic: {
            // Log spacing: min * (max/min)^(i/(n-1))
            const float logRatio = std::log(config.maxValue / config.minValue);
            for (size_t i = 0; i < config.numSteps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(config.numSteps - 1);
                values.push_back(config.minValue * std::exp(logRatio * t));
            }
            break;
        }
    }

    return values;
}

// =============================================================================
// Main Sweep Function (FR-016)
// =============================================================================

/// @brief Run a parameter sweep with artifact detection
/// @tparam ParameterSetter Function type: void(float)
/// @tparam SignalGenerator Function type: std::vector<float>()
/// @tparam Processor Function type: std::vector<float>(const std::vector<float>&)
/// @param config Sweep configuration
/// @param setParameter Function to set parameter value
/// @param generateSignal Function to generate test signal
/// @param processSignal Function to process signal through DSP
/// @param sampleRate Sample rate for detection
/// @return Sweep result with all step results
template<typename ParameterSetter, typename SignalGenerator, typename Processor>
[[nodiscard]] SweepResult runParameterSweep(
    const ParameterSweepConfig& config,
    ParameterSetter&& setParameter,
    SignalGenerator&& generateSignal,
    Processor&& processSignal,
    float sampleRate
) {
    SweepResult result;
    result.parameterName = config.parameterName;

    if (!config.isValid()) {
        return result;
    }

    // Generate parameter values
    auto paramValues = generateParameterValues(config);

    // Configure click detector if needed
    ClickDetector clickDetector({
        .sampleRate = sampleRate,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = config.clickThreshold,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    });

    if (config.checkForClicks) {
        clickDetector.prepare();
    }

    // Run sweep
    for (float paramValue : paramValues) {
        // Set parameter
        setParameter(paramValue);

        // Generate input signal
        auto input = generateSignal();
        if (input.empty()) {
            continue;
        }

        // Process signal
        auto output = processSignal(input);
        if (output.empty()) {
            continue;
        }

        // Initialize step result
        StepResult stepResult;
        stepResult.parameterValue = paramValue;
        stepResult.passed = true;

        // Check for clicks
        if (config.checkForClicks) {
            auto clicks = clickDetector.detect(output.data(), output.size());
            stepResult.clicksDetected = clicks.size();

            if (!clicks.empty()) {
                stepResult.passed = false;
                stepResult.failureReason = "Detected " + std::to_string(clicks.size()) +
                                          " click(s) at parameter value " +
                                          std::to_string(paramValue);
            }
        }

        // Check THD
        if (config.checkThd) {
            // Assume fundamental at 1000 Hz for THD test
            // (in real usage, this would be configurable)
            stepResult.thdPercent = SignalMetrics::calculateTHD(
                output.data(), output.size(), 1000.0f, sampleRate);

            if (stepResult.thdPercent > config.thdThresholdPercent) {
                stepResult.passed = false;
                if (!stepResult.failureReason.empty()) {
                    stepResult.failureReason += "; ";
                }
                stepResult.failureReason += "THD " + std::to_string(stepResult.thdPercent) +
                                           "% exceeds threshold " +
                                           std::to_string(config.thdThresholdPercent) + "%";
            }
        }

        result.stepResults.push_back(stepResult);
    }

    return result;
}

} // namespace TestUtils
} // namespace DSP
} // namespace Krate
