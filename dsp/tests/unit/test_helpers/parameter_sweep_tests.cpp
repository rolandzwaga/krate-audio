// ==============================================================================
// Unit Tests: Parameter Sweep Utilities
// ==============================================================================
// Tests for automated parameter range testing with artifact detection.
//
// Constitution Compliance:
// - Principle XII: Test-First Development (tests written FIRST)
// - Principle VIII: Testing Discipline
//
// Reference: specs/055-artifact-detection/spec.md
// Requirements: FR-012, FR-013, FR-014, FR-015, FR-016, FR-024
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "parameter_sweep.h"
#include "test_signals.h"

#include <array>
#include <cmath>
#include <vector>

using namespace Krate::DSP::TestUtils;
using Catch::Approx;

// =============================================================================
// Simple test processor for parameter sweep tests
// =============================================================================

class TestGainProcessor {
public:
    void setGain(float gain) { gain_ = gain; }
    void setDelay(float delay) { delay_ = delay; }

    float process(float input) const {
        return input * gain_;
    }

    void process(const float* input, float* output, size_t n) const {
        for (size_t i = 0; i < n; ++i) {
            output[i] = input[i] * gain_;
        }
    }

private:
    float gain_ = 1.0f;
    float delay_ = 0.0f;
};

class TestClippingProcessor {
public:
    void setDrive(float drive) { drive_ = drive; }

    void process(const float* input, float* output, size_t n) const {
        for (size_t i = 0; i < n; ++i) {
            float s = input[i] * drive_;
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;
            output[i] = s;
        }
    }

private:
    float drive_ = 1.0f;
};

// =============================================================================
// T034: ParameterSweepConfig Tests
// =============================================================================

TEST_CASE("ParameterSweepConfig - validation", "[parameter-sweep][config]") {
    SECTION("default config is valid") {
        ParameterSweepConfig config{};
        REQUIRE(config.isValid());
    }

    SECTION("custom valid config") {
        ParameterSweepConfig config{
            .parameterName = "Gain",
            .minValue = 0.0f,
            .maxValue = 2.0f,
            .numSteps = 20,
            .stepType = StepType::Linear,
            .checkForClicks = true,
            .checkThd = false
        };
        REQUIRE(config.isValid());
    }

    SECTION("invalid numSteps - zero") {
        ParameterSweepConfig config{};
        config.numSteps = 0;
        REQUIRE_FALSE(config.isValid());
    }

    SECTION("invalid numSteps - too high") {
        ParameterSweepConfig config{};
        config.numSteps = 2000;  // Max is 1000
        REQUIRE_FALSE(config.isValid());
    }

    SECTION("invalid range - min > max") {
        ParameterSweepConfig config{};
        config.minValue = 10.0f;
        config.maxValue = 5.0f;
        REQUIRE_FALSE(config.isValid());
    }

    SECTION("logarithmic with zero min is invalid") {
        ParameterSweepConfig config{};
        config.minValue = 0.0f;
        config.maxValue = 100.0f;
        config.stepType = StepType::Logarithmic;
        REQUIRE_FALSE(config.isValid());
    }
}

// =============================================================================
// T035: generateParameterValues() Tests
// =============================================================================

TEST_CASE("generateParameterValues - linear steps", "[parameter-sweep][generate]") {
    SECTION("10 linear steps from 0 to 1") {
        ParameterSweepConfig config{
            .parameterName = "Test",
            .minValue = 0.0f,
            .maxValue = 1.0f,
            .numSteps = 11,  // 0, 0.1, 0.2, ..., 1.0
            .stepType = StepType::Linear
        };

        auto values = generateParameterValues(config);

        REQUIRE(values.size() == 11);
        REQUIRE(values[0] == Approx(0.0f));
        REQUIRE(values[5] == Approx(0.5f));
        REQUIRE(values[10] == Approx(1.0f));
    }

    SECTION("linear steps are evenly spaced") {
        ParameterSweepConfig config{
            .parameterName = "Test",
            .minValue = -10.0f,
            .maxValue = 10.0f,
            .numSteps = 5,
            .stepType = StepType::Linear
        };

        auto values = generateParameterValues(config);

        REQUIRE(values.size() == 5);
        // Expected: -10, -5, 0, 5, 10
        REQUIRE(values[0] == Approx(-10.0f));
        REQUIRE(values[1] == Approx(-5.0f));
        REQUIRE(values[2] == Approx(0.0f));
        REQUIRE(values[3] == Approx(5.0f));
        REQUIRE(values[4] == Approx(10.0f));
    }
}

TEST_CASE("generateParameterValues - logarithmic steps", "[parameter-sweep][generate]") {
    SECTION("logarithmic from 1 to 1000") {
        ParameterSweepConfig config{
            .parameterName = "Freq",
            .minValue = 1.0f,
            .maxValue = 1000.0f,
            .numSteps = 4,
            .stepType = StepType::Logarithmic
        };

        auto values = generateParameterValues(config);

        REQUIRE(values.size() == 4);
        // Expected: 1, 10, 100, 1000 (powers of 10)
        REQUIRE(values[0] == Approx(1.0f).margin(0.01f));
        REQUIRE(values[1] == Approx(10.0f).margin(0.1f));
        REQUIRE(values[2] == Approx(100.0f).margin(1.0f));
        REQUIRE(values[3] == Approx(1000.0f).margin(10.0f));
    }

    SECTION("logarithmic covers audio frequency range") {
        ParameterSweepConfig config{
            .parameterName = "Freq",
            .minValue = 20.0f,
            .maxValue = 20000.0f,
            .numSteps = 10,
            .stepType = StepType::Logarithmic
        };

        auto values = generateParameterValues(config);

        REQUIRE(values.size() == 10);
        REQUIRE(values[0] == Approx(20.0f));
        REQUIRE(values[values.size() - 1] == Approx(20000.0f).margin(100.0f));

        // Verify logarithmic spacing: ratios between consecutive values should be constant
        const float ratio = values[1] / values[0];
        for (size_t i = 2; i < values.size(); ++i) {
            const float thisRatio = values[i] / values[i - 1];
            REQUIRE(thisRatio == Approx(ratio).margin(ratio * 0.05f));
        }
    }
}

// =============================================================================
// T036: SweepResult Tests
// =============================================================================

TEST_CASE("SweepResult - accessors", "[parameter-sweep][result]") {
    SECTION("hasFailed identifies any failed step") {
        SweepResult result;
        result.stepResults.push_back({.parameterValue = 0.5f, .passed = true, .clicksDetected = 0});
        result.stepResults.push_back({.parameterValue = 1.0f, .passed = false, .clicksDetected = 3});
        result.stepResults.push_back({.parameterValue = 1.5f, .passed = true, .clicksDetected = 0});

        REQUIRE(result.hasFailed());
    }

    SECTION("hasFailed returns false when all pass") {
        SweepResult result;
        result.stepResults.push_back({.parameterValue = 0.5f, .passed = true, .clicksDetected = 0});
        result.stepResults.push_back({.parameterValue = 1.0f, .passed = true, .clicksDetected = 0});

        REQUIRE_FALSE(result.hasFailed());
    }

    SECTION("getFailedSteps returns failed step indices") {
        SweepResult result;
        result.stepResults.push_back({.parameterValue = 0.5f, .passed = true, .clicksDetected = 0});
        result.stepResults.push_back({.parameterValue = 1.0f, .passed = false, .clicksDetected = 2});
        result.stepResults.push_back({.parameterValue = 1.5f, .passed = true, .clicksDetected = 0});
        result.stepResults.push_back({.parameterValue = 2.0f, .passed = false, .clicksDetected = 1});

        auto failed = result.getFailedSteps();

        REQUIRE(failed.size() == 2);
        REQUIRE(failed[0] == 1);
        REQUIRE(failed[1] == 3);
    }

    SECTION("getFailingRanges identifies contiguous failures") {
        SweepResult result;
        result.stepResults.push_back({.parameterValue = 0.0f, .passed = true, .clicksDetected = 0});
        result.stepResults.push_back({.parameterValue = 0.2f, .passed = false, .clicksDetected = 1});
        result.stepResults.push_back({.parameterValue = 0.4f, .passed = false, .clicksDetected = 2});
        result.stepResults.push_back({.parameterValue = 0.6f, .passed = false, .clicksDetected = 1});
        result.stepResults.push_back({.parameterValue = 0.8f, .passed = true, .clicksDetected = 0});
        result.stepResults.push_back({.parameterValue = 1.0f, .passed = false, .clicksDetected = 1});

        auto ranges = result.getFailingRanges();

        REQUIRE(ranges.size() == 2);
        // First range: steps 1-3 (values 0.2-0.6)
        REQUIRE(ranges[0].first == Approx(0.2f));
        REQUIRE(ranges[0].second == Approx(0.6f));
        // Second range: step 5 (value 1.0)
        REQUIRE(ranges[1].first == Approx(1.0f));
        REQUIRE(ranges[1].second == Approx(1.0f));
    }
}

// =============================================================================
// T037: runParameterSweep() Tests
// =============================================================================

TEST_CASE("runParameterSweep - basic functionality", "[parameter-sweep][run]") {
    SECTION("clean gain processor passes all steps") {
        TestGainProcessor processor;

        ParameterSweepConfig config{
            .parameterName = "Gain",
            .minValue = 0.0f,
            .maxValue = 1.0f,
            .numSteps = 5,
            .stepType = StepType::Linear,
            .checkForClicks = true,
            .checkThd = false
        };

        // Setter function
        auto setter = [&processor](float value) {
            processor.setGain(value);
        };

        // Generator function
        auto generator = []() {
            std::vector<float> signal(2048, 0.0f);
            TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);
            return signal;
        };

        // Processor function
        auto processFn = [&processor](const std::vector<float>& input) {
            std::vector<float> output(input.size());
            processor.process(input.data(), output.data(), input.size());
            return output;
        };

        auto result = runParameterSweep(config, setter, generator, processFn, 44100.0f);

        REQUIRE(result.stepResults.size() == 5);
        REQUIRE_FALSE(result.hasFailed());
    }

    SECTION("clipping processor with high drive has failures") {
        TestClippingProcessor processor;

        ParameterSweepConfig config{
            .parameterName = "Drive",
            .minValue = 1.0f,
            .maxValue = 10.0f,
            .numSteps = 5,
            .stepType = StepType::Linear,
            .checkForClicks = false,  // Clipping doesn't create clicks
            .checkThd = true,
            .thdThresholdPercent = 5.0f  // Fail if THD > 5%
        };

        auto setter = [&processor](float value) {
            processor.setDrive(value);
        };

        auto generator = []() {
            std::vector<float> signal(2048, 0.0f);
            TestHelpers::generateSine(signal.data(), signal.size(), 1000.0f, 44100.0f, 0.5f);
            return signal;
        };

        auto processFn = [&processor](const std::vector<float>& input) {
            std::vector<float> output(input.size());
            processor.process(input.data(), output.data(), input.size());
            return output;
        };

        auto result = runParameterSweep(config, setter, generator, processFn, 44100.0f);

        REQUIRE(result.stepResults.size() == 5);
        // At high drive values, clipping should cause THD > 5%
        // Drive values: 1, 3.25, 5.5, 7.75, 10
        // At drive >= 2, 0.5 * drive >= 1.0, causing clipping
        INFO("Testing drive sweep results");
        // Low drive values should pass, high should fail
        // Drive 1.0 -> peak 0.5, no clipping
        // Drive 3.25 -> peak 1.625, clipping
        // etc.
        REQUIRE(result.hasFailed());
    }
}

// =============================================================================
// T038: Edge Cases
// =============================================================================

TEST_CASE("ParameterSweep - edge cases", "[parameter-sweep][edge-cases]") {
    SECTION("single step sweep") {
        TestGainProcessor processor;

        ParameterSweepConfig config{
            .parameterName = "Gain",
            .minValue = 1.0f,
            .maxValue = 1.0f,  // Same as min
            .numSteps = 1,
            .stepType = StepType::Linear,
            .checkForClicks = true,
            .checkThd = false
        };

        auto setter = [&processor](float value) {
            processor.setGain(value);
        };

        auto generator = []() {
            std::vector<float> signal(1024, 0.0f);
            TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);
            return signal;
        };

        auto processFn = [&processor](const std::vector<float>& input) {
            std::vector<float> output(input.size());
            processor.process(input.data(), output.data(), input.size());
            return output;
        };

        auto result = runParameterSweep(config, setter, generator, processFn, 44100.0f);

        REQUIRE(result.stepResults.size() == 1);
        REQUIRE_FALSE(result.hasFailed());
    }

    SECTION("getFailingRanges on empty results returns empty") {
        SweepResult result;
        auto ranges = result.getFailingRanges();
        REQUIRE(ranges.empty());
    }

    SECTION("getFailingRanges on all-pass returns empty") {
        SweepResult result;
        result.stepResults.push_back({.parameterValue = 0.5f, .passed = true, .clicksDetected = 0});
        result.stepResults.push_back({.parameterValue = 1.0f, .passed = true, .clicksDetected = 0});

        auto ranges = result.getFailingRanges();
        REQUIRE(ranges.empty());
    }
}
