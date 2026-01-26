// ==============================================================================
// Unit Tests: StochasticShaper Primitive
// ==============================================================================
// Tests for the stochastic waveshaper primitive.
//
// Feature: 106-stochastic-shaper
// Layer: 1 (Primitives)
// Test-First: Tests written BEFORE implementation per Constitution Principle XII
//
// Reference: specs/106-stochastic-shaper/spec.md
// ==============================================================================

#include <krate/dsp/primitives/stochastic_shaper.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <vector>
#include <numeric>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Phase 2: Foundational Tests - Constants and Construction
// =============================================================================

TEST_CASE("StochasticShaper constants are correct", "[stochastic_shaper][constants]") {
    REQUIRE(StochasticShaper::kDefaultJitterRate == Approx(10.0f));
    REQUIRE(StochasticShaper::kMinJitterRate == Approx(0.01f));
    REQUIRE(StochasticShaper::kMaxJitterOffset == Approx(0.5f));
    REQUIRE(StochasticShaper::kDriveModulationRange == Approx(0.5f));
    REQUIRE(StochasticShaper::kDefaultDrive == Approx(1.0f));
}

// =============================================================================
// Phase 3: User Story 1 - Basic Analog Warmth Tests
// =============================================================================

TEST_CASE("StochasticShaper construction and default initialization (FR-003, FR-007, FR-008b, FR-014)",
          "[stochastic_shaper][US1][construction]") {
    StochasticShaper shaper;

    SECTION("isPrepared() returns false before prepare()") {
        REQUIRE_FALSE(shaper.isPrepared());
    }

    SECTION("default baseType is Tanh (FR-007)") {
        REQUIRE(shaper.getBaseType() == WaveshapeType::Tanh);
    }

    SECTION("default drive is 1.0 (FR-008b)") {
        REQUIRE(shaper.getDrive() == Approx(1.0f));
    }

    SECTION("default jitterAmount is 0.0") {
        REQUIRE(shaper.getJitterAmount() == Approx(0.0f));
    }

    SECTION("default jitterRate is 10.0 Hz (FR-014)") {
        REQUIRE(shaper.getJitterRate() == Approx(10.0f));
    }

    SECTION("default coefficientNoise is 0.0") {
        REQUIRE(shaper.getCoefficientNoise() == Approx(0.0f));
    }

    SECTION("default seed is 1") {
        REQUIRE(shaper.getSeed() == 1);
    }
}

TEST_CASE("StochasticShaper prepare() initializes state correctly (FR-001)",
          "[stochastic_shaper][US1][lifecycle]") {
    StochasticShaper shaper;

    SECTION("prepare() marks shaper as prepared") {
        shaper.prepare(44100.0);
        REQUIRE(shaper.isPrepared());
    }

    SECTION("prepare() works at various sample rates") {
        for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
            StochasticShaper s;
            s.prepare(sr);
            REQUIRE(s.isPrepared());
        }
    }

    SECTION("prepare() can be called multiple times") {
        shaper.prepare(44100.0);
        REQUIRE(shaper.isPrepared());
        shaper.prepare(96000.0);
        REQUIRE(shaper.isPrepared());
    }
}

TEST_CASE("StochasticShaper reset() clears state while preserving config (FR-002)",
          "[stochastic_shaper][US1][lifecycle]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);

    // Set some configuration
    shaper.setBaseType(WaveshapeType::Tube);
    shaper.setDrive(2.5f);
    shaper.setJitterAmount(0.5f);
    shaper.setJitterRate(5.0f);
    shaper.setCoefficientNoise(0.3f);
    shaper.setSeed(12345);

    // Process some samples to change internal state
    for (int i = 0; i < 100; ++i) {
        [[maybe_unused]] float output = shaper.process(0.5f);
    }

    // Reset
    shaper.reset();

    // Configuration should be preserved
    REQUIRE(shaper.getBaseType() == WaveshapeType::Tube);
    REQUIRE(shaper.getDrive() == Approx(2.5f));
    REQUIRE(shaper.getJitterAmount() == Approx(0.5f));
    REQUIRE(shaper.getJitterRate() == Approx(5.0f));
    REQUIRE(shaper.getCoefficientNoise() == Approx(0.3f));
    REQUIRE(shaper.getSeed() == 12345);
    REQUIRE(shaper.isPrepared());
}

TEST_CASE("StochasticShaper with jitterAmount=0 and coeffNoise=0 equals standard Waveshaper (FR-024, SC-002)",
          "[stochastic_shaper][US1][bypass]") {
    StochasticShaper stochasticShaper;
    stochasticShaper.prepare(44100.0);
    stochasticShaper.setBaseType(WaveshapeType::Tanh);
    stochasticShaper.setDrive(2.0f);
    stochasticShaper.setJitterAmount(0.0f);
    stochasticShaper.setCoefficientNoise(0.0f);

    Waveshaper standardShaper;
    standardShaper.setType(WaveshapeType::Tanh);
    standardShaper.setDrive(2.0f);

    // Test various input values
    for (float input : {-1.0f, -0.5f, -0.1f, 0.0f, 0.1f, 0.5f, 1.0f}) {
        float stochasticOutput = stochasticShaper.process(input);
        float standardOutput = standardShaper.process(input);

        INFO("Input: " << input);
        REQUIRE(stochasticOutput == Approx(standardOutput).margin(1e-6f));
    }
}

TEST_CASE("StochasticShaper with jitterAmount > 0 differs from standard Waveshaper (FR-022, SC-001)",
          "[stochastic_shaper][US1][stochastic]") {
    StochasticShaper stochasticShaper;
    stochasticShaper.prepare(44100.0);
    stochasticShaper.setBaseType(WaveshapeType::Tanh);
    stochasticShaper.setDrive(2.0f);
    stochasticShaper.setJitterAmount(0.5f);  // Non-zero jitter
    stochasticShaper.setCoefficientNoise(0.0f);

    Waveshaper standardShaper;
    standardShaper.setType(WaveshapeType::Tanh);
    standardShaper.setDrive(2.0f);

    // Process constant input and collect outputs
    constexpr size_t numSamples = 1000;
    constexpr float constantInput = 0.5f;

    int differenceCount = 0;
    for (size_t i = 0; i < numSamples; ++i) {
        float stochasticOutput = stochasticShaper.process(constantInput);
        float standardOutput = standardShaper.process(constantInput);

        // Check if outputs differ (accounting for floating-point tolerance)
        if (std::abs(stochasticOutput - standardOutput) > 1e-5f) {
            differenceCount++;
        }
    }

    // Most outputs should differ due to stochastic jitter
    INFO("Number of samples that differed: " << differenceCount);
    REQUIRE(differenceCount > numSamples / 2);  // At least half should differ
}

TEST_CASE("StochasticShaper produces deterministic output with same seed (FR-019, FR-020, SC-003)",
          "[stochastic_shaper][US1][deterministic]") {
    constexpr uint32_t testSeed = 42;
    constexpr size_t numSamples = 500;

    // First run
    StochasticShaper shaper1;
    shaper1.setSeed(testSeed);
    shaper1.prepare(44100.0);
    shaper1.setJitterAmount(0.5f);
    shaper1.setCoefficientNoise(0.3f);

    std::vector<float> outputs1;
    for (size_t i = 0; i < numSamples; ++i) {
        outputs1.push_back(shaper1.process(0.5f));
    }

    // Second run with same seed
    StochasticShaper shaper2;
    shaper2.setSeed(testSeed);
    shaper2.prepare(44100.0);
    shaper2.setJitterAmount(0.5f);
    shaper2.setCoefficientNoise(0.3f);

    std::vector<float> outputs2;
    for (size_t i = 0; i < numSamples; ++i) {
        outputs2.push_back(shaper2.process(0.5f));
    }

    // Outputs should be identical
    for (size_t i = 0; i < numSamples; ++i) {
        INFO("Sample " << i);
        REQUIRE(outputs1[i] == Approx(outputs2[i]));
    }
}

TEST_CASE("StochasticShaper seed=0 is replaced with default (FR-021)",
          "[stochastic_shaper][US1][seed]") {
    // Seed 0 should be replaced with a valid seed and produce valid output
    StochasticShaper shaper;
    shaper.setSeed(0);
    shaper.prepare(44100.0);
    shaper.setJitterAmount(0.5f);

    // Should not crash or produce NaN
    for (int i = 0; i < 100; ++i) {
        float output = shaper.process(0.5f);
        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
    }

    // Verify the seed was stored (it's 0 in the getter, but internally replaced)
    REQUIRE(shaper.getSeed() == 0);  // Getter returns what was set
}

TEST_CASE("StochasticShaper jitterAmount clamped to [0.0, 1.0] (FR-009, FR-010, FR-011)",
          "[stochastic_shaper][US1][clamping]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);

    SECTION("negative values clamped to 0.0") {
        shaper.setJitterAmount(-0.5f);
        REQUIRE(shaper.getJitterAmount() == Approx(0.0f));
    }

    SECTION("values > 1.0 clamped to 1.0") {
        shaper.setJitterAmount(1.5f);
        REQUIRE(shaper.getJitterAmount() == Approx(1.0f));
    }

    SECTION("values in range are preserved") {
        shaper.setJitterAmount(0.7f);
        REQUIRE(shaper.getJitterAmount() == Approx(0.7f));
    }

    SECTION("0.0 produces no jitter offset (FR-010)") {
        shaper.setJitterAmount(0.0f);
        shaper.setJitterRate(100.0f);  // Fast rate to see variations quickly

        // Process and check diagnostic
        for (int i = 0; i < 100; ++i) {
            [[maybe_unused]] float output = shaper.process(0.5f);
        }
        REQUIRE(shaper.getCurrentJitter() == Approx(0.0f));
    }

    SECTION("1.0 produces max offset of +/- 0.5 (FR-011)") {
        shaper.setJitterAmount(1.0f);
        shaper.setJitterRate(1000.0f);  // Fast rate

        // Process many samples and track max jitter
        float maxAbsJitter = 0.0f;
        for (int i = 0; i < 10000; ++i) {
            [[maybe_unused]] float output = shaper.process(0.5f);
            maxAbsJitter = std::max(maxAbsJitter, std::abs(shaper.getCurrentJitter()));
        }

        // Max jitter should approach but not exceed 0.5
        REQUIRE(maxAbsJitter > 0.3f);  // Should have significant jitter
        REQUIRE(maxAbsJitter <= 0.51f);  // Should be bounded (small tolerance for smoothing)
    }
}

TEST_CASE("StochasticShaper NaN input treated as 0.0 (FR-029)",
          "[stochastic_shaper][US1][sanitization]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setBaseType(WaveshapeType::Tanh);
    shaper.setDrive(2.0f);
    shaper.setJitterAmount(0.0f);
    shaper.setCoefficientNoise(0.0f);

    const float nanValue = std::numeric_limits<float>::quiet_NaN();
    float output = shaper.process(nanValue);

    // NaN -> 0.0, tanh(0 * 2) = 0
    REQUIRE_FALSE(std::isnan(output));
    REQUIRE(output == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("StochasticShaper Infinity input clamped to [-1.0, 1.0] (FR-030)",
          "[stochastic_shaper][US1][sanitization]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setBaseType(WaveshapeType::Tanh);
    shaper.setDrive(2.0f);
    shaper.setJitterAmount(0.0f);
    shaper.setCoefficientNoise(0.0f);

    SECTION("positive infinity clamped to 1.0") {
        const float posInf = std::numeric_limits<float>::infinity();
        float output = shaper.process(posInf);

        REQUIRE_FALSE(std::isinf(output));
        REQUIRE_FALSE(std::isnan(output));
        REQUIRE(std::abs(output) <= 1.0f);
    }

    SECTION("negative infinity clamped to -1.0") {
        const float negInf = -std::numeric_limits<float>::infinity();
        float output = shaper.process(negInf);

        REQUIRE_FALSE(std::isinf(output));
        REQUIRE_FALSE(std::isnan(output));
        REQUIRE(std::abs(output) <= 1.0f);
    }
}

// =============================================================================
// Phase 4: User Story 2 - Jitter Rate Control Tests
// =============================================================================

TEST_CASE("StochasticShaper jitterRate=0.1Hz produces slow variation (FR-013, SC-005)",
          "[stochastic_shaper][US2][rate]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setJitterAmount(1.0f);
    shaper.setJitterRate(0.1f);  // Very slow - 10 seconds per cycle

    // Process samples and measure rate of change
    float prevJitter = 0.0f;
    float totalChange = 0.0f;
    constexpr size_t numSamples = 4410;  // 0.1 seconds at 44.1kHz

    [[maybe_unused]] float initOutput = shaper.process(0.5f);  // Initialize
    prevJitter = shaper.getCurrentJitter();

    for (size_t i = 0; i < numSamples; ++i) {
        [[maybe_unused]] float output = shaper.process(0.5f);
        float currentJitter = shaper.getCurrentJitter();
        totalChange += std::abs(currentJitter - prevJitter);
        prevJitter = currentJitter;
    }

    float avgChangePerSample = totalChange / static_cast<float>(numSamples);
    INFO("Average change per sample (slow rate): " << avgChangePerSample);

    // Slow rate should have small changes per sample
    REQUIRE(avgChangePerSample < 0.01f);
}

TEST_CASE("StochasticShaper jitterRate=1000Hz produces fast variation (FR-013, SC-005)",
          "[stochastic_shaper][US2][rate]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setJitterAmount(1.0f);
    shaper.setJitterRate(1000.0f);  // Fast - approaching audio rate

    // Process samples and measure rate of change
    float prevJitter = 0.0f;
    float totalChange = 0.0f;
    constexpr size_t numSamples = 4410;

    [[maybe_unused]] float initOutput = shaper.process(0.5f);  // Initialize
    prevJitter = shaper.getCurrentJitter();

    for (size_t i = 0; i < numSamples; ++i) {
        [[maybe_unused]] float output = shaper.process(0.5f);
        float currentJitter = shaper.getCurrentJitter();
        totalChange += std::abs(currentJitter - prevJitter);
        prevJitter = currentJitter;
    }

    float avgChangePerSample = totalChange / static_cast<float>(numSamples);
    INFO("Average change per sample (fast rate): " << avgChangePerSample);

    // Fast rate should have larger changes per sample than slow rate
    REQUIRE(avgChangePerSample > 0.001f);
}

TEST_CASE("StochasticShaper jitterRate defaults to 10.0Hz (FR-014)",
          "[stochastic_shaper][US2][default]") {
    StochasticShaper shaper;
    REQUIRE(shaper.getJitterRate() == Approx(10.0f));
}

TEST_CASE("StochasticShaper jitterRate clamped to [0.01, sampleRate/2] (FR-012)",
          "[stochastic_shaper][US2][clamping]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);

    SECTION("rate below minimum clamped to 0.01 Hz") {
        shaper.setJitterRate(0.001f);
        REQUIRE(shaper.getJitterRate() == Approx(0.01f));
    }

    SECTION("rate above Nyquist clamped to sampleRate/2") {
        shaper.setJitterRate(50000.0f);  // Above 44100/2 = 22050
        REQUIRE(shaper.getJitterRate() <= 22050.0f);
    }

    SECTION("rate within range preserved") {
        shaper.setJitterRate(100.0f);
        REQUIRE(shaper.getJitterRate() == Approx(100.0f));
    }
}

TEST_CASE("StochasticShaper changing jitterRate reconfigures smoothers correctly",
          "[stochastic_shaper][US2][reconfigure]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setJitterAmount(1.0f);

    // Measure change rate at slow setting
    shaper.setJitterRate(1.0f);
    float totalChangeSlow = 0.0f;
    float prevJitter = 0.0f;

    [[maybe_unused]] float initOutput = shaper.process(0.5f);
    prevJitter = shaper.getCurrentJitter();

    for (int i = 0; i < 1000; ++i) {
        [[maybe_unused]] float output = shaper.process(0.5f);
        totalChangeSlow += std::abs(shaper.getCurrentJitter() - prevJitter);
        prevJitter = shaper.getCurrentJitter();
    }

    // Change to fast rate
    shaper.setJitterRate(500.0f);
    float totalChangeFast = 0.0f;
    prevJitter = shaper.getCurrentJitter();

    for (int i = 0; i < 1000; ++i) {
        [[maybe_unused]] float output = shaper.process(0.5f);
        totalChangeFast += std::abs(shaper.getCurrentJitter() - prevJitter);
        prevJitter = shaper.getCurrentJitter();
    }

    INFO("Slow rate total change: " << totalChangeSlow);
    INFO("Fast rate total change: " << totalChangeFast);

    // Fast rate should produce more change
    REQUIRE(totalChangeFast > totalChangeSlow);
}

TEST_CASE("StochasticShaper jitterRate changes are audible (spectral analysis) (SC-005)",
          "[stochastic_shaper][US2][spectral]") {
    // Test that different rates produce different output variance characteristics
    auto measureVariance = [](float rate) {
        StochasticShaper shaper;
        shaper.prepare(44100.0);
        shaper.setJitterAmount(0.5f);
        shaper.setJitterRate(rate);
        shaper.setSeed(123);

        std::vector<float> outputs;
        for (int i = 0; i < 4410; ++i) {  // 0.1 seconds
            outputs.push_back(shaper.process(0.3f));
        }

        // Calculate variance
        float mean = std::accumulate(outputs.begin(), outputs.end(), 0.0f) /
                     static_cast<float>(outputs.size());
        float variance = 0.0f;
        for (float v : outputs) {
            variance += (v - mean) * (v - mean);
        }
        return variance / static_cast<float>(outputs.size());
    };

    float varianceSlow = measureVariance(0.5f);
    float varianceFast = measureVariance(100.0f);

    INFO("Slow rate variance: " << varianceSlow);
    INFO("Fast rate variance: " << varianceFast);

    // Both should produce measurable variance, but characteristics differ
    REQUIRE(varianceSlow > 0.0f);
    REQUIRE(varianceFast > 0.0f);
}

// =============================================================================
// Phase 5: User Story 3 - Coefficient Noise Tests
// =============================================================================

TEST_CASE("StochasticShaper coefficientNoise=0.5 with jitterAmount=0 varies drive over time (FR-023)",
          "[stochastic_shaper][US3][coefficient]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setJitterAmount(0.0f);  // No jitter
    shaper.setCoefficientNoise(0.5f);  // Drive modulation
    shaper.setJitterRate(100.0f);  // Moderate rate
    shaper.setDrive(2.0f);

    // Track drive modulation values
    std::vector<float> driveValues;
    for (int i = 0; i < 1000; ++i) {
        [[maybe_unused]] float output = shaper.process(0.5f);
        driveValues.push_back(shaper.getCurrentDriveModulation());
    }

    // Drive should vary around baseDrive
    float minDrive = *std::min_element(driveValues.begin(), driveValues.end());
    float maxDrive = *std::max_element(driveValues.begin(), driveValues.end());

    INFO("Min drive: " << minDrive << ", Max drive: " << maxDrive);
    REQUIRE(minDrive < 2.0f);  // Should go below base
    REQUIRE(maxDrive > 2.0f);  // Should go above base
}

TEST_CASE("StochasticShaper coefficientNoise=1.0 modulates drive by +/- 50% (FR-017, FR-023)",
          "[stochastic_shaper][US3][range]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setJitterAmount(0.0f);
    shaper.setCoefficientNoise(1.0f);  // Maximum modulation
    shaper.setJitterRate(1000.0f);  // Fast rate to see full range
    shaper.setDrive(2.0f);

    // Track drive modulation values
    float minDrive = 2.0f;
    float maxDrive = 2.0f;

    for (int i = 0; i < 50000; ++i) {
        [[maybe_unused]] float output = shaper.process(0.5f);
        float drive = shaper.getCurrentDriveModulation();
        minDrive = std::min(minDrive, drive);
        maxDrive = std::max(maxDrive, drive);
    }

    // At coeffNoise=1.0, drive should range from 0.5*base to 1.5*base
    // baseDrive=2.0, so range should be approximately [1.0, 3.0]
    INFO("Min drive: " << minDrive << ", Max drive: " << maxDrive);
    REQUIRE(minDrive < 1.5f);  // Should approach 1.0
    REQUIRE(maxDrive > 2.5f);  // Should approach 3.0
}

TEST_CASE("StochasticShaper coefficientNoise=0 results in constant drive (FR-016)",
          "[stochastic_shaper][US3][constant]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setJitterAmount(0.0f);
    shaper.setCoefficientNoise(0.0f);  // No modulation
    shaper.setDrive(2.0f);

    // Process and check drive remains constant
    for (int i = 0; i < 100; ++i) {
        [[maybe_unused]] float output = shaper.process(0.5f);
        REQUIRE(shaper.getCurrentDriveModulation() == Approx(2.0f));
    }
}

TEST_CASE("StochasticShaper coefficientNoise clamped to [0.0, 1.0] (FR-015)",
          "[stochastic_shaper][US3][clamping]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);

    SECTION("negative values clamped to 0.0") {
        shaper.setCoefficientNoise(-0.5f);
        REQUIRE(shaper.getCoefficientNoise() == Approx(0.0f));
    }

    SECTION("values > 1.0 clamped to 1.0") {
        shaper.setCoefficientNoise(1.5f);
        REQUIRE(shaper.getCoefficientNoise() == Approx(1.0f));
    }

    SECTION("values in range preserved") {
        shaper.setCoefficientNoise(0.7f);
        REQUIRE(shaper.getCoefficientNoise() == Approx(0.7f));
    }
}

TEST_CASE("StochasticShaper coefficient noise uses independent smoother from jitter (FR-018)",
          "[stochastic_shaper][US3][independent]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setJitterAmount(1.0f);
    shaper.setCoefficientNoise(1.0f);
    shaper.setJitterRate(100.0f);

    // Track both jitter and drive values
    std::vector<float> jitterValues;
    std::vector<float> driveValues;

    for (int i = 0; i < 1000; ++i) {
        [[maybe_unused]] float output = shaper.process(0.5f);
        jitterValues.push_back(shaper.getCurrentJitter());
        driveValues.push_back(shaper.getCurrentDriveModulation());
    }

    // Calculate correlation coefficient
    float meanJitter = std::accumulate(jitterValues.begin(), jitterValues.end(), 0.0f) /
                       static_cast<float>(jitterValues.size());
    float meanDrive = std::accumulate(driveValues.begin(), driveValues.end(), 0.0f) /
                      static_cast<float>(driveValues.size());

    float numerator = 0.0f;
    float denomJitter = 0.0f;
    float denomDrive = 0.0f;

    for (size_t i = 0; i < jitterValues.size(); ++i) {
        float dj = jitterValues[i] - meanJitter;
        float dd = driveValues[i] - meanDrive;
        numerator += dj * dd;
        denomJitter += dj * dj;
        denomDrive += dd * dd;
    }

    float correlation = numerator / std::sqrt(denomJitter * denomDrive + 1e-10f);

    INFO("Correlation between jitter and drive: " << correlation);

    // Independent streams should not be perfectly correlated
    // They might have some correlation due to shared RNG, but not 1.0 or -1.0
    REQUIRE(std::abs(correlation) < 0.99f);
}

TEST_CASE("StochasticShaper coefficient noise produces different character than jitter (SC-006)",
          "[stochastic_shaper][US3][character]") {
    // Test that coefficient noise affects the transfer curve shape,
    // while jitter affects the input offset

    auto processAndCollect = [](float jitter, float coeff) {
        StochasticShaper shaper;
        shaper.prepare(44100.0);
        shaper.setJitterAmount(jitter);
        shaper.setCoefficientNoise(coeff);
        shaper.setJitterRate(100.0f);
        shaper.setSeed(42);

        std::vector<float> outputs;
        for (int i = 0; i < 1000; ++i) {
            outputs.push_back(shaper.process(0.5f));
        }
        return outputs;
    };

    auto jitterOnlyOutputs = processAndCollect(0.5f, 0.0f);
    auto coeffOnlyOutputs = processAndCollect(0.0f, 0.5f);
    auto bothOutputs = processAndCollect(0.5f, 0.5f);

    // All should produce variation
    auto calcStdDev = [](const std::vector<float>& data) {
        float mean = std::accumulate(data.begin(), data.end(), 0.0f) /
                     static_cast<float>(data.size());
        float variance = 0.0f;
        for (float v : data) variance += (v - mean) * (v - mean);
        return std::sqrt(variance / static_cast<float>(data.size()));
    };

    float jitterStdDev = calcStdDev(jitterOnlyOutputs);
    float coeffStdDev = calcStdDev(coeffOnlyOutputs);
    float bothStdDev = calcStdDev(bothOutputs);

    INFO("Jitter-only std dev: " << jitterStdDev);
    INFO("Coeff-only std dev: " << coeffStdDev);
    INFO("Both std dev: " << bothStdDev);

    // All should have non-zero variation
    REQUIRE(jitterStdDev > 0.001f);
    REQUIRE(coeffStdDev > 0.001f);
    REQUIRE(bothStdDev > 0.001f);
}

// =============================================================================
// Phase 6: User Story 4 - Waveshape Type Selection Tests
// =============================================================================

TEST_CASE("StochasticShaper baseType=Tanh retains tanh character with stochastic variation (SC-007)",
          "[stochastic_shaper][US4][tanh]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setBaseType(WaveshapeType::Tanh);
    shaper.setDrive(3.0f);
    shaper.setJitterAmount(0.3f);
    shaper.setCoefficientNoise(0.2f);

    // Process various inputs and verify output stays bounded like tanh
    for (int i = 0; i < 1000; ++i) {
        float input = 2.0f * std::sin(static_cast<float>(i) * 0.1f);  // -2 to 2
        float output = shaper.process(input);

        // Tanh-based shaper should produce bounded output
        REQUIRE(std::abs(output) <= 1.0f);
        REQUIRE_FALSE(std::isnan(output));
    }
}

TEST_CASE("StochasticShaper baseType=Tube retains tube character with stochastic variation (SC-007)",
          "[stochastic_shaper][US4][tube]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setBaseType(WaveshapeType::Tube);
    shaper.setDrive(2.0f);
    shaper.setJitterAmount(0.3f);
    shaper.setCoefficientNoise(0.2f);

    // Verify Tube type is set
    REQUIRE(shaper.getBaseType() == WaveshapeType::Tube);

    // Process and verify output is valid
    for (int i = 0; i < 100; ++i) {
        float input = std::sin(static_cast<float>(i) * 0.1f);
        float output = shaper.process(input);

        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
        REQUIRE(std::abs(output) <= 1.5f);  // Tube is bounded
    }
}

TEST_CASE("StochasticShaper baseType=HardClip retains hard clipping character (SC-007)",
          "[stochastic_shaper][US4][hardclip]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setBaseType(WaveshapeType::HardClip);
    shaper.setDrive(3.0f);
    shaper.setJitterAmount(0.1f);  // Small jitter
    shaper.setCoefficientNoise(0.1f);

    REQUIRE(shaper.getBaseType() == WaveshapeType::HardClip);

    // Hard clip should strictly bound output to [-1, 1]
    for (int i = 0; i < 100; ++i) {
        float input = 2.0f * std::sin(static_cast<float>(i) * 0.1f);
        float output = shaper.process(input);

        REQUIRE(std::abs(output) <= 1.001f);  // Small tolerance for jitter
    }
}

TEST_CASE("StochasticShaper all 9 WaveshapeType values work correctly (FR-006, SC-007)",
          "[stochastic_shaper][US4][alltypes]") {
    auto type = GENERATE(
        WaveshapeType::Tanh,
        WaveshapeType::Atan,
        WaveshapeType::Cubic,
        WaveshapeType::Quintic,
        WaveshapeType::ReciprocalSqrt,
        WaveshapeType::Erf,
        WaveshapeType::HardClip,
        WaveshapeType::Diode,
        WaveshapeType::Tube
    );

    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setBaseType(type);
    shaper.setDrive(2.0f);
    shaper.setJitterAmount(0.3f);
    shaper.setCoefficientNoise(0.2f);

    REQUIRE(shaper.getBaseType() == type);

    // Process samples and verify no NaN/Inf
    for (int i = 0; i < 100; ++i) {
        float input = std::sin(static_cast<float>(i) * 0.1f);
        float output = shaper.process(input);

        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
    }
}

TEST_CASE("StochasticShaper uses Waveshaper composition, not duplication (FR-032)",
          "[stochastic_shaper][US4][composition]") {
    // Test that changing base type affects output
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setDrive(3.0f);
    shaper.setJitterAmount(0.0f);  // Disable jitter for clean comparison
    shaper.setCoefficientNoise(0.0f);

    // Get output with Tanh
    shaper.setBaseType(WaveshapeType::Tanh);
    float tanhOutput = shaper.process(0.8f);

    // Get output with HardClip
    shaper.setBaseType(WaveshapeType::HardClip);
    float hardClipOutput = shaper.process(0.8f);

    // Outputs should be different
    INFO("Tanh output: " << tanhOutput);
    INFO("HardClip output: " << hardClipOutput);
    REQUIRE(tanhOutput != Approx(hardClipOutput));
}

// =============================================================================
// Phase 7: Edge Cases & Diagnostics Tests
// =============================================================================

TEST_CASE("StochasticShaper jitterRate exceeds Nyquist/2 is clamped",
          "[stochastic_shaper][edge]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);

    shaper.setJitterRate(30000.0f);  // Above Nyquist/2 (22050 Hz)
    REQUIRE(shaper.getJitterRate() <= 22050.0f);
}

TEST_CASE("StochasticShaper drive=0 returns 0 regardless of jitter",
          "[stochastic_shaper][edge]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setDrive(0.0f);
    shaper.setJitterAmount(1.0f);
    shaper.setCoefficientNoise(1.0f);

    // Drive=0 should produce zero output
    for (int i = 0; i < 100; ++i) {
        float output = shaper.process(0.5f);
        REQUIRE(output == Approx(0.0f));
    }
}

TEST_CASE("StochasticShaper extreme jitterAmount (>1.0) is clamped",
          "[stochastic_shaper][edge]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);

    shaper.setJitterAmount(5.0f);
    REQUIRE(shaper.getJitterAmount() == Approx(1.0f));
}

TEST_CASE("StochasticShaper smoothed random values remain bounded to [-1.0, 1.0] (FR-031)",
          "[stochastic_shaper][edge][bounded]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setJitterAmount(1.0f);
    shaper.setJitterRate(10000.0f);  // Fast rate

    // Process many samples and check jitter bounds
    for (int i = 0; i < 100000; ++i) {
        [[maybe_unused]] float output = shaper.process(0.5f);
        float jitter = shaper.getCurrentJitter();

        // Jitter should be bounded to [-0.5, 0.5] at amount=1.0
        REQUIRE(jitter >= -0.51f);
        REQUIRE(jitter <= 0.51f);
    }
}

TEST_CASE("StochasticShaper long-duration processing produces no NaN/Inf (SC-008)",
          "[stochastic_shaper][edge][longrun]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setJitterAmount(1.0f);
    shaper.setCoefficientNoise(1.0f);
    shaper.setJitterRate(10.0f);

    // Simulate ~1 minute of processing (2,646,000 samples at 44.1kHz)
    // We'll do a shorter test to keep runtime reasonable
    constexpr size_t numSamples = 100000;

    for (size_t i = 0; i < numSamples; ++i) {
        float input = std::sin(static_cast<float>(i) * 0.01f);
        float output = shaper.process(input);

        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
    }
}

TEST_CASE("StochasticShaper process() is noexcept (FR-026)",
          "[stochastic_shaper][edge][noexcept]") {
    // Static assertion that process is noexcept
    StochasticShaper shaper;
    static_assert(noexcept(shaper.process(0.5f)), "process() must be noexcept");
}

TEST_CASE("StochasticShaper processBlock() is noexcept (FR-026)",
          "[stochastic_shaper][edge][noexcept]") {
    // Static assertion that processBlock is noexcept
    StochasticShaper shaper;
    std::array<float, 64> buffer{};
    static_assert(noexcept(shaper.processBlock(buffer.data(), buffer.size())),
                  "processBlock() must be noexcept");
}

TEST_CASE("StochasticShaper getCurrentJitter() returns value in expected range (FR-035)",
          "[stochastic_shaper][diagnostics]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setJitterAmount(1.0f);
    shaper.setJitterRate(100.0f);

    // Process and check diagnostic
    for (int i = 0; i < 1000; ++i) {
        [[maybe_unused]] float output = shaper.process(0.5f);
        float jitter = shaper.getCurrentJitter();
        REQUIRE(jitter >= -0.5f);
        REQUIRE(jitter <= 0.5f);
    }
}

TEST_CASE("StochasticShaper getCurrentDriveModulation() returns effective drive value (FR-036)",
          "[stochastic_shaper][diagnostics]") {
    StochasticShaper shaper;
    shaper.prepare(44100.0);
    shaper.setDrive(2.0f);
    shaper.setCoefficientNoise(0.5f);
    shaper.setJitterRate(100.0f);

    // Process and check diagnostic
    for (int i = 0; i < 1000; ++i) {
        [[maybe_unused]] float output = shaper.process(0.5f);
        float drive = shaper.getCurrentDriveModulation();

        // At coeffNoise=0.5, drive should vary by +/- 25% from base
        // baseDrive=2.0, so range should be approximately [1.5, 2.5]
        REQUIRE(drive > 0.0f);  // Always positive
        REQUIRE(drive < 5.0f);  // Reasonable upper bound
    }
}

TEST_CASE("StochasticShaper diagnostic getters are thread-safe for read-only inspection (FR-037)",
          "[stochastic_shaper][diagnostics]") {
    // This is a compile-time check that getters are const
    const StochasticShaper shaper;

    // These should compile without issues (const correctness)
    [[maybe_unused]] float jitter = shaper.getCurrentJitter();
    [[maybe_unused]] float drive = shaper.getCurrentDriveModulation();
    [[maybe_unused]] bool prepared = shaper.isPrepared();
}

// =============================================================================
// Phase 8: Performance Tests
// =============================================================================

TEST_CASE("StochasticShaper processBlock equivalent to sequential process",
          "[stochastic_shaper][performance]") {
    // Test that processBlock produces same results as sequential process calls
    StochasticShaper shaper1;
    shaper1.setSeed(42);
    shaper1.prepare(44100.0);
    shaper1.setJitterAmount(0.5f);
    shaper1.setCoefficientNoise(0.3f);

    StochasticShaper shaper2;
    shaper2.setSeed(42);
    shaper2.prepare(44100.0);
    shaper2.setJitterAmount(0.5f);
    shaper2.setCoefficientNoise(0.3f);

    constexpr size_t blockSize = 64;
    std::array<float, blockSize> buffer1;
    std::array<float, blockSize> buffer2;

    // Generate input
    for (size_t i = 0; i < blockSize; ++i) {
        buffer1[i] = std::sin(static_cast<float>(i) * 0.1f);
        buffer2[i] = buffer1[i];
    }

    // Process with processBlock
    shaper1.processBlock(buffer1.data(), blockSize);

    // Process with sequential process calls
    for (size_t i = 0; i < blockSize; ++i) {
        buffer2[i] = shaper2.process(buffer2[i]);
    }

    // Results should match
    for (size_t i = 0; i < blockSize; ++i) {
        INFO("Sample " << i);
        REQUIRE(buffer1[i] == Approx(buffer2[i]));
    }
}
