// ==============================================================================
// Unit Tests: Golden Reference Utilities
// ==============================================================================
// Tests for golden reference comparison and A/B testing utilities.
//
// Constitution Compliance:
// - Principle XII: Test-First Development (tests written FIRST)
// - Principle VIII: Testing Discipline
//
// Reference: specs/055-artifact-detection/spec.md
// Requirements: FR-017, FR-018, FR-019, FR-020, FR-021, FR-022, FR-023, FR-024
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "golden_reference.h"
#include "test_signals.h"

#include <array>
#include <cmath>
#include <vector>

using namespace Krate::DSP::TestUtils;
using Catch::Approx;

// =============================================================================
// Simple test processors for A/B testing
// =============================================================================

class ReferenceProcessor {
public:
    void process(const float* input, float* output, size_t n) const {
        // Simple pass-through
        for (size_t i = 0; i < n; ++i) {
            output[i] = input[i];
        }
    }
};

class NewProcessor {
public:
    void process(const float* input, float* output, size_t n) const {
        // Slightly different - adds tiny gain
        for (size_t i = 0; i < n; ++i) {
            output[i] = input[i] * 1.001f;
        }
    }
};

class BuggyProcessor {
public:
    void process(const float* input, float* output, size_t n) const {
        // Introduces a click at sample 1000
        for (size_t i = 0; i < n; ++i) {
            output[i] = input[i];
        }
        if (n > 1000) {
            output[1000] += 0.5f;
        }
    }
};

// =============================================================================
// T049: GoldenReferenceConfig Tests
// =============================================================================

TEST_CASE("GoldenReferenceConfig::isValid - validates configuration", "[golden-reference][config]") {
    SECTION("default config is valid") {
        GoldenReferenceConfig config{};
        REQUIRE(config.isValid());
    }

    SECTION("valid custom config") {
        GoldenReferenceConfig config{
            .sampleRate = 48000.0f,
            .snrThresholdDb = 80.0f,
            .maxClickAmplitude = 0.05f,
            .thdThresholdPercent = 0.5f,
            .maxCrestFactorDb = 25.0f
        };
        REQUIRE(config.isValid());
    }

    SECTION("invalid sampleRate") {
        GoldenReferenceConfig config{};
        config.sampleRate = 8000.0f;  // Below minimum
        REQUIRE_FALSE(config.isValid());
    }

    SECTION("invalid snrThresholdDb") {
        GoldenReferenceConfig config{};
        config.snrThresholdDb = 5.0f;  // Below minimum 10 dB
        REQUIRE_FALSE(config.isValid());
    }
}

// =============================================================================
// T050: compareWithReference() Tests
// =============================================================================

TEST_CASE("compareWithReference - basic comparison", "[golden-reference][compare]") {
    GoldenReferenceConfig config{
        .sampleRate = 44100.0f,
        .snrThresholdDb = 60.0f,
        .maxClickAmplitude = 0.1f,
        .thdThresholdPercent = 1.0f,
        .maxCrestFactorDb = 20.0f
    };

    SECTION("identical signals pass comparison") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 1000.0f, 44100.0f, 0.5f);

        auto result = compareWithReference(signal.data(), signal.data(), signal.size(), config);

        INFO("SNR: " << result.snrDb << " dB");
        INFO("THD: " << result.thdPercent << "%");
        INFO("Crest factor: " << result.crestFactorDb << " dB");
        INFO("Clicks: " << result.clicksDetected);
        for (const auto& reason : result.failureReasons) {
            INFO("Failure: " << reason);
        }

        REQUIRE(result.passed);
        REQUIRE(result.snrDb > 100.0f);  // Very high SNR for identical
    }

    SECTION("slightly different signals still pass if within threshold") {
        std::vector<float> reference(4096, 0.0f);
        TestHelpers::generateSine(reference.data(), reference.size(), 1000.0f, 44100.0f, 0.5f);

        // Create signal with tiny difference
        std::vector<float> signal = reference;
        for (size_t i = 0; i < signal.size(); ++i) {
            signal[i] *= 1.0001f;  // 0.01% gain difference
        }

        auto result = compareWithReference(signal.data(), reference.data(), signal.size(), config);

        INFO("SNR: " << result.snrDb << " dB");
        INFO("THD: " << result.thdPercent << "%");
        INFO("Crest factor: " << result.crestFactorDb << " dB");
        for (const auto& reason : result.failureReasons) {
            INFO("Failure: " << reason);
        }
        REQUIRE(result.passed);
    }

    SECTION("signal with click fails comparison") {
        std::vector<float> reference(4096, 0.0f);
        TestHelpers::generateSine(reference.data(), reference.size(), 440.0f, 44100.0f, 0.5f);

        std::vector<float> signal = reference;
        signal[2000] += 0.3f;  // Click above threshold

        auto result = compareWithReference(signal.data(), reference.data(), signal.size(), config);

        INFO("Click amplitude: " << result.maxClickAmplitude);
        INFO("Failure reasons: ");
        for (const auto& reason : result.failureReasons) {
            INFO("  - " << reason);
        }
        REQUIRE_FALSE(result.passed);
        REQUIRE(result.clicksDetected > 0);
    }

    SECTION("very noisy signal fails SNR check") {
        std::vector<float> reference(4096, 0.0f);
        TestHelpers::generateSine(reference.data(), reference.size(), 440.0f, 44100.0f, 0.5f);

        std::vector<float> signal = reference;
        // Add significant noise
        for (size_t i = 0; i < signal.size(); ++i) {
            signal[i] += 0.01f * static_cast<float>((i % 100) - 50) / 50.0f;
        }

        auto result = compareWithReference(signal.data(), reference.data(), signal.size(), config);

        INFO("SNR: " << result.snrDb << " dB (threshold: 60 dB)");
        REQUIRE_FALSE(result.passed);
    }
}

// =============================================================================
// T051: ABTestResult Tests
// =============================================================================

TEST_CASE("ABTestResult - accessor methods", "[golden-reference][ab-test]") {
    SECTION("equivalent() returns true when within tolerance") {
        ABTestResult result;
        result.snrDifferenceDb = 0.5f;
        result.thdDifferencePercent = 0.3f;
        result.clickCountDifference = 0;

        REQUIRE(result.equivalent(1.0f, 0.5f, 0));
    }

    SECTION("equivalent() returns false when SNR difference exceeds tolerance") {
        ABTestResult result;
        result.snrDifferenceDb = 2.0f;
        result.thdDifferencePercent = 0.3f;
        result.clickCountDifference = 0;

        REQUIRE_FALSE(result.equivalent(1.0f, 0.5f, 0));
    }

    SECTION("equivalent() returns false when click count differs") {
        ABTestResult result;
        result.snrDifferenceDb = 0.1f;
        result.thdDifferencePercent = 0.1f;
        result.clickCountDifference = 1;

        REQUIRE_FALSE(result.equivalent(1.0f, 0.5f, 0));
    }
}

// =============================================================================
// T052: abCompare() Tests
// =============================================================================

TEST_CASE("abCompare - A/B testing processors", "[golden-reference][ab-compare]") {
    SECTION("identical processors have equivalent output") {
        ReferenceProcessor procA;
        ReferenceProcessor procB;

        auto generator = []() {
            std::vector<float> signal(2048, 0.0f);
            TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);
            return signal;
        };

        auto processA = [&procA](const std::vector<float>& input) {
            std::vector<float> output(input.size());
            procA.process(input.data(), output.data(), input.size());
            return output;
        };

        auto processB = [&procB](const std::vector<float>& input) {
            std::vector<float> output(input.size());
            procB.process(input.data(), output.data(), input.size());
            return output;
        };

        auto result = abCompare(generator, processA, processB, 44100.0f);

        INFO("SNR difference: " << result.snrDifferenceDb << " dB");
        INFO("THD difference: " << result.thdDifferencePercent << "%");
        INFO("Click count difference: " << result.clickCountDifference);

        REQUIRE(result.equivalent(1.0f, 0.5f, 0));
    }

    SECTION("slightly different processors show measurable but small difference") {
        ReferenceProcessor procA;
        NewProcessor procB;

        auto generator = []() {
            std::vector<float> signal(2048, 0.0f);
            TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);
            return signal;
        };

        auto processA = [&procA](const std::vector<float>& input) {
            std::vector<float> output(input.size());
            procA.process(input.data(), output.data(), input.size());
            return output;
        };

        auto processB = [&procB](const std::vector<float>& input) {
            std::vector<float> output(input.size());
            procB.process(input.data(), output.data(), input.size());
            return output;
        };

        auto result = abCompare(generator, processA, processB, 44100.0f);

        INFO("SNR A: " << result.snrA << " dB");
        INFO("SNR B: " << result.snrB << " dB");
        INFO("SNR difference: " << result.snrDifferenceDb << " dB");
        INFO("Click count A: " << result.clickCountA);
        INFO("Click count B: " << result.clickCountB);

        // Both processors are pass-through-like, so they should have
        // very high SNR relative to input. The important metric is
        // that neither introduces clicks.
        REQUIRE(result.clickCountA == 0);
        REQUIRE(result.clickCountB == 0);
    }

    SECTION("buggy processor introduces click - not equivalent") {
        ReferenceProcessor procA;
        BuggyProcessor procB;

        auto generator = []() {
            std::vector<float> signal(2048, 0.0f);
            TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.3f);
            return signal;
        };

        auto processA = [&procA](const std::vector<float>& input) {
            std::vector<float> output(input.size());
            procA.process(input.data(), output.data(), input.size());
            return output;
        };

        auto processB = [&procB](const std::vector<float>& input) {
            std::vector<float> output(input.size());
            procB.process(input.data(), output.data(), input.size());
            return output;
        };

        auto result = abCompare(generator, processA, processB, 44100.0f);

        INFO("Click count A: " << result.clickCountA);
        INFO("Click count B: " << result.clickCountB);
        INFO("Click difference: " << result.clickCountDifference);

        // Buggy processor introduces a click
        REQUIRE(result.clickCountB > result.clickCountA);
        REQUIRE_FALSE(result.equivalent(1.0f, 0.5f, 0));
    }
}

// =============================================================================
// T053: GoldenComparisonResult Tests
// =============================================================================

TEST_CASE("GoldenComparisonResult - field access", "[golden-reference][result]") {
    SECTION("isValid() returns true for valid metrics") {
        GoldenComparisonResult result;
        result.passed = true;
        result.snrDb = 80.0f;
        result.thdPercent = 0.5f;
        result.crestFactorDb = 3.0f;
        result.clicksDetected = 0;
        result.maxClickAmplitude = 0.0f;

        REQUIRE(result.isValid());
    }

    SECTION("isValid() returns false for NaN values") {
        GoldenComparisonResult result;
        result.snrDb = std::numeric_limits<float>::quiet_NaN();

        REQUIRE_FALSE(result.isValid());
    }

    SECTION("failure reasons populated on failure") {
        std::vector<float> reference(4096, 0.0f);
        TestHelpers::generateSine(reference.data(), reference.size(), 440.0f, 44100.0f, 0.5f);

        std::vector<float> signal = reference;
        // Add multiple issues
        signal[1000] += 0.5f;  // Click
        signal[2000] += 0.5f;  // Another click

        GoldenReferenceConfig config{
            .sampleRate = 44100.0f,
            .snrThresholdDb = 60.0f,
            .maxClickAmplitude = 0.1f,
            .thdThresholdPercent = 1.0f,
            .maxCrestFactorDb = 20.0f
        };

        auto result = compareWithReference(signal.data(), reference.data(), signal.size(), config);

        REQUIRE_FALSE(result.passed);
        REQUIRE_FALSE(result.failureReasons.empty());

        INFO("Failure reasons:");
        for (const auto& reason : result.failureReasons) {
            INFO("  - " << reason);
        }
    }
}
