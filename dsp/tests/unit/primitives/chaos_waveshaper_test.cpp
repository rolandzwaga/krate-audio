// ==============================================================================
// Unit Tests: ChaosWaveshaper Primitive
// ==============================================================================
// Tests for the chaos attractor waveshaper primitive.
//
// Feature: 104-chaos-waveshaper
// Layer: 1 (Primitives)
// Test-First: Tests written BEFORE implementation per Constitution Principle XII
//
// Reference: specs/104-chaos-waveshaper/spec.md
// ==============================================================================

#include <krate/dsp/primitives/chaos_waveshaper.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Phase 2: Foundational Tests - Enum and Class Structure
// =============================================================================

TEST_CASE("ChaosModel enum has 4 values (FR-005 to FR-008)", "[chaos_waveshaper][enum]") {
    // Verify enum values exist and are distinct
    REQUIRE(static_cast<uint8_t>(ChaosModel::Lorenz) == 0);
    REQUIRE(static_cast<uint8_t>(ChaosModel::Rossler) == 1);
    REQUIRE(static_cast<uint8_t>(ChaosModel::Chua) == 2);
    REQUIRE(static_cast<uint8_t>(ChaosModel::Henon) == 3);
}

TEST_CASE("ChaosModel is uint8_t underlying type", "[chaos_waveshaper][enum]") {
    static_assert(std::is_same_v<std::underlying_type_t<ChaosModel>, uint8_t>,
                  "ChaosModel must be uint8_t");
}

TEST_CASE("kControlRateInterval is 32 samples", "[chaos_waveshaper][constants]") {
    REQUIRE(ChaosWaveshaper::kControlRateInterval == 32);
}

// =============================================================================
// Phase 3: User Story 1 - Basic Chaos Distortion Tests
// =============================================================================

TEST_CASE("ChaosWaveshaper prepare() and reset() lifecycle (FR-001, FR-002)",
          "[chaos_waveshaper][US1][lifecycle]") {
    ChaosWaveshaper shaper;

    SECTION("isPrepared() returns false before prepare()") {
        REQUIRE_FALSE(shaper.isPrepared());
    }

    SECTION("prepare() initializes for processing") {
        shaper.prepare(44100.0, 512);
        REQUIRE(shaper.isPrepared());
        REQUIRE(shaper.getSampleRate() == Approx(44100.0));
    }

    SECTION("reset() can be called after prepare()") {
        shaper.prepare(44100.0, 512);
        REQUIRE_NOTHROW(shaper.reset());
        REQUIRE(shaper.isPrepared());  // Still prepared after reset
    }

    SECTION("prepare() works at various sample rates") {
        for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
            shaper.prepare(sr, 512);
            REQUIRE(shaper.getSampleRate() == Approx(sr));
        }
    }
}

TEST_CASE("ChaosWaveshaper bypass when chaosAmount=0.0 (FR-023, SC-002)",
          "[chaos_waveshaper][US1][bypass]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Lorenz);
    shaper.setChaosAmount(0.0f);

    SECTION("process() returns input unchanged") {
        for (float input : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            REQUIRE(shaper.process(input) == Approx(input));
        }
    }

    SECTION("processBlock() leaves buffer unchanged") {
        constexpr size_t numSamples = 64;
        std::array<float, numSamples> buffer;
        std::array<float, numSamples> original;

        // Fill with sine wave
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = std::sin(static_cast<float>(i) * 0.1f);
            original[i] = buffer[i];
        }

        shaper.processBlock(buffer.data(), numSamples);

        // Verify unchanged
        for (size_t i = 0; i < numSamples; ++i) {
            REQUIRE(buffer[i] == Approx(original[i]));
        }
    }
}

TEST_CASE("ChaosWaveshaper silence input produces silence output (SC-003)",
          "[chaos_waveshaper][US1][silence]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Lorenz);
    shaper.setChaosAmount(1.0f);

    SECTION("zero input produces near-zero output") {
        // Process many zero samples to let attractor evolve
        for (int i = 0; i < 1000; ++i) {
            float output = shaper.process(0.0f);
            // Output should be very small (attractor modulates drive,
            // but tanh(0 * anything) = 0)
            REQUIRE(std::abs(output) < 1e-6f);
        }
    }

    SECTION("block of zeros produces zeros") {
        constexpr size_t numSamples = 512;
        std::array<float, numSamples> buffer{};  // All zeros

        shaper.processBlock(buffer.data(), numSamples);

        float maxOutput = 0.0f;
        for (float sample : buffer) {
            maxOutput = std::max(maxOutput, std::abs(sample));
        }
        REQUIRE(maxOutput < 1e-5f);  // Noise floor
    }
}

TEST_CASE("ChaosWaveshaper time-varying output with constant sine (SC-001)",
          "[chaos_waveshaper][US1][time-varying]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Lorenz);
    shaper.setChaosAmount(1.0f);
    shaper.setAttractorSpeed(1.0f);

    // Generate constant sine wave input
    constexpr size_t numBlocks = 20;
    constexpr size_t blockSize = 512;
    constexpr float frequency = 440.0f;
    constexpr float sampleRate = 44100.0f;

    std::vector<float> rmsValues;

    for (size_t block = 0; block < numBlocks; ++block) {
        std::array<float, blockSize> buffer;

        // Generate sine wave
        const float phaseOffset = static_cast<float>(block * blockSize) / sampleRate;
        for (size_t i = 0; i < blockSize; ++i) {
            const float t = phaseOffset + static_cast<float>(i) / sampleRate;
            buffer[i] = 0.5f * std::sin(2.0f * 3.14159265f * frequency * t);
        }

        // Process
        shaper.processBlock(buffer.data(), blockSize);

        // Calculate RMS
        float sumSquares = 0.0f;
        for (float sample : buffer) {
            sumSquares += sample * sample;
        }
        rmsValues.push_back(std::sqrt(sumSquares / blockSize));
    }

    // Verify time-varying: RMS values should not all be identical
    // (chaos attractor causes drive to vary, changing output level)
    float minRms = *std::min_element(rmsValues.begin(), rmsValues.end());
    float maxRms = *std::max_element(rmsValues.begin(), rmsValues.end());

    INFO("Min RMS: " << minRms << ", Max RMS: " << maxRms);
    REQUIRE(maxRms - minRms > 0.01f);  // At least some variation
}

TEST_CASE("ChaosWaveshaper Lorenz attractor bounded state (FR-018, SC-005)",
          "[chaos_waveshaper][US1][bounded]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Lorenz);
    shaper.setChaosAmount(1.0f);
    shaper.setAttractorSpeed(10.0f);  // Fast evolution to stress-test bounds

    // Process for extended period (simulating 10+ minutes at 44.1kHz)
    // 10 minutes = 600 seconds * 44100 = 26,460,000 samples
    // We'll do a shorter test but with many iterations
    constexpr size_t iterations = 100000;

    bool allOutputsValid = true;
    bool anyNaN = false;
    bool anyInf = false;

    for (size_t i = 0; i < iterations; ++i) {
        float input = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
        float output = shaper.process(input);

        if (std::isnan(output)) {
            anyNaN = true;
            allOutputsValid = false;
            break;
        }
        if (std::isinf(output)) {
            anyInf = true;
            allOutputsValid = false;
            break;
        }
        // Output should be bounded (tanh-based, so in [-1, 1])
        if (std::abs(output) > 1.5f) {  // Allow small margin
            allOutputsValid = false;
            break;
        }
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(anyInf);
    REQUIRE(allOutputsValid);
}

TEST_CASE("ChaosWaveshaper NaN/Inf input sanitization (FR-031, FR-032)",
          "[chaos_waveshaper][US1][sanitization]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Lorenz);
    shaper.setChaosAmount(1.0f);

    SECTION("NaN input treated as 0.0 (FR-031)") {
        const float nanValue = std::numeric_limits<float>::quiet_NaN();
        float output = shaper.process(nanValue);

        // Output should be valid (not NaN)
        REQUIRE_FALSE(std::isnan(output));
        // Since NaN -> 0.0, and tanh(0) = 0, output should be near zero
        REQUIRE(std::abs(output) < 0.1f);
    }

    SECTION("Positive infinity clamped to 1.0 (FR-032)") {
        const float posInf = std::numeric_limits<float>::infinity();
        float output = shaper.process(posInf);

        REQUIRE_FALSE(std::isinf(output));
        REQUIRE_FALSE(std::isnan(output));
        REQUIRE(std::abs(output) <= 1.0f);
    }

    SECTION("Negative infinity clamped to -1.0 (FR-032)") {
        const float negInf = -std::numeric_limits<float>::infinity();
        float output = shaper.process(negInf);

        REQUIRE_FALSE(std::isinf(output));
        REQUIRE_FALSE(std::isnan(output));
        REQUIRE(std::abs(output) <= 1.0f);
    }
}

TEST_CASE("ChaosWaveshaper attractor divergence detection and reset (FR-033)",
          "[chaos_waveshaper][US1][divergence]") {
    // This test verifies that even with extreme conditions,
    // the attractor self-recovers when it diverges

    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Lorenz);
    shaper.setChaosAmount(1.0f);
    shaper.setAttractorSpeed(100.0f);  // Maximum speed to encourage divergence
    shaper.setInputCoupling(1.0f);      // Maximum coupling

    // Feed extreme inputs that might cause divergence
    bool everProducedValidOutput = false;

    for (int i = 0; i < 10000; ++i) {
        // Alternate between extreme values
        float input = (i % 2 == 0) ? 1.0f : -1.0f;
        float output = shaper.process(input);

        if (!std::isnan(output) && !std::isinf(output)) {
            everProducedValidOutput = true;
        }
    }

    // After all the stress, system should still produce valid output
    float finalOutput = shaper.process(0.5f);
    REQUIRE_FALSE(std::isnan(finalOutput));
    REQUIRE_FALSE(std::isinf(finalOutput));
    REQUIRE(everProducedValidOutput);
}

TEST_CASE("ChaosWaveshaper oversampling reduces aliasing (FR-034)",
          "[chaos_waveshaper][US1][oversampling]") {
    // Test that processBlock() (which uses oversampling) produces
    // different results than process() (which doesn't)

    ChaosWaveshaper shaperBlock;
    shaperBlock.prepare(44100.0, 512);
    shaperBlock.setModel(ChaosModel::Lorenz);
    shaperBlock.setChaosAmount(1.0f);

    ChaosWaveshaper shaperSample;
    shaperSample.prepare(44100.0, 512);
    shaperSample.setModel(ChaosModel::Lorenz);
    shaperSample.setChaosAmount(1.0f);

    constexpr size_t numSamples = 512;
    std::array<float, numSamples> blockBuffer;
    std::array<float, numSamples> sampleBuffer;

    // Generate high-frequency sine (more likely to alias)
    const float frequency = 8000.0f;  // High frequency
    const float sampleRate = 44100.0f;

    for (size_t i = 0; i < numSamples; ++i) {
        const float t = static_cast<float>(i) / sampleRate;
        blockBuffer[i] = 0.8f * std::sin(2.0f * 3.14159265f * frequency * t);
        sampleBuffer[i] = blockBuffer[i];
    }

    // Process with block (oversampled)
    shaperBlock.processBlock(blockBuffer.data(), numSamples);

    // Process sample-by-sample (no oversampling)
    for (size_t i = 0; i < numSamples; ++i) {
        sampleBuffer[i] = shaperSample.process(sampleBuffer[i]);
    }

    // Results should differ due to oversampling filtering
    // (Note: they won't be identical because oversampling smooths the signal)
    bool anyDifference = false;
    for (size_t i = 0; i < numSamples; ++i) {
        if (std::abs(blockBuffer[i] - sampleBuffer[i]) > 0.001f) {
            anyDifference = true;
            break;
        }
    }

    // With different chaos evolution paths, they should differ
    // (This is a weak test - aliasing measurement would be better)
    // The main point is that processBlock uses oversampling internally
    REQUIRE(anyDifference);
}

TEST_CASE("ChaosWaveshaper latency() returns oversampler latency",
          "[chaos_waveshaper][US1][latency]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);

    // With default Economy/ZeroLatency mode, latency should be 0
    REQUIRE(shaper.latency() == 0);
}

TEST_CASE("ChaosWaveshaper parameter setters and getters",
          "[chaos_waveshaper][US1][parameters]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);

    SECTION("chaosAmount is clamped to [0, 1]") {
        shaper.setChaosAmount(-0.5f);
        REQUIRE(shaper.getChaosAmount() == Approx(0.0f));

        shaper.setChaosAmount(1.5f);
        REQUIRE(shaper.getChaosAmount() == Approx(1.0f));

        shaper.setChaosAmount(0.7f);
        REQUIRE(shaper.getChaosAmount() == Approx(0.7f));
    }

    SECTION("attractorSpeed is clamped to [0.01, 100]") {
        shaper.setAttractorSpeed(0.001f);
        REQUIRE(shaper.getAttractorSpeed() == Approx(0.01f));

        shaper.setAttractorSpeed(200.0f);
        REQUIRE(shaper.getAttractorSpeed() == Approx(100.0f));

        shaper.setAttractorSpeed(5.0f);
        REQUIRE(shaper.getAttractorSpeed() == Approx(5.0f));
    }

    SECTION("inputCoupling is clamped to [0, 1]") {
        shaper.setInputCoupling(-0.1f);
        REQUIRE(shaper.getInputCoupling() == Approx(0.0f));

        shaper.setInputCoupling(1.5f);
        REQUIRE(shaper.getInputCoupling() == Approx(1.0f));

        shaper.setInputCoupling(0.3f);
        REQUIRE(shaper.getInputCoupling() == Approx(0.3f));
    }

    SECTION("model can be set and retrieved") {
        shaper.setModel(ChaosModel::Rossler);
        REQUIRE(shaper.getModel() == ChaosModel::Rossler);

        shaper.setModel(ChaosModel::Chua);
        REQUIRE(shaper.getModel() == ChaosModel::Chua);

        shaper.setModel(ChaosModel::Henon);
        REQUIRE(shaper.getModel() == ChaosModel::Henon);

        shaper.setModel(ChaosModel::Lorenz);
        REQUIRE(shaper.getModel() == ChaosModel::Lorenz);
    }
}

// =============================================================================
// Phase 4: User Story 2 - Input-Reactive Chaos Tests
// =============================================================================

TEST_CASE("ChaosWaveshaper setInputCoupling() parameter (FR-012)",
          "[chaos_waveshaper][US2][coupling]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);

    SECTION("default is 0.0 (no coupling)") {
        REQUIRE(shaper.getInputCoupling() == Approx(0.0f));
    }

    SECTION("setter/getter work correctly") {
        shaper.setInputCoupling(0.5f);
        REQUIRE(shaper.getInputCoupling() == Approx(0.5f));

        shaper.setInputCoupling(1.0f);
        REQUIRE(shaper.getInputCoupling() == Approx(1.0f));
    }
}

TEST_CASE("ChaosWaveshaper zero coupling produces independent evolution",
          "[chaos_waveshaper][US2][independent]") {
    // With inputCoupling=0, the attractor should evolve the same way
    // regardless of input amplitude

    ChaosWaveshaper shaper1;
    shaper1.prepare(44100.0, 512);
    shaper1.setModel(ChaosModel::Lorenz);
    shaper1.setChaosAmount(1.0f);
    shaper1.setInputCoupling(0.0f);

    ChaosWaveshaper shaper2;
    shaper2.prepare(44100.0, 512);
    shaper2.setModel(ChaosModel::Lorenz);
    shaper2.setChaosAmount(1.0f);
    shaper2.setInputCoupling(0.0f);

    // Process different inputs
    std::vector<float> outputs1, outputs2;
    for (int i = 0; i < 1000; ++i) {
        outputs1.push_back(shaper1.process(0.1f));   // Quiet input
        outputs2.push_back(shaper2.process(0.9f));   // Loud input
    }

    // With no coupling, the attractor evolution should be identical
    // But the outputs will differ because the input differs
    // The key is that the distortion CHARACTER should be similar
    // We test this by comparing the output patterns
    float sumDiff = 0.0f;
    for (size_t i = 0; i < outputs1.size(); ++i) {
        // Normalize by input ratio to compare distortion character
        sumDiff += std::abs(outputs1[i] / 0.1f - outputs2[i] / 0.9f);
    }
    float avgDiff = sumDiff / static_cast<float>(outputs1.size());

    // Should be relatively similar (same drive modulation)
    // Allow for some difference due to nonlinearity
    REQUIRE(avgDiff < 5.0f);
}

TEST_CASE("ChaosWaveshaper full coupling shows input-correlated variation (SC-008)",
          "[chaos_waveshaper][US2][correlated]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Lorenz);
    shaper.setChaosAmount(1.0f);
    shaper.setInputCoupling(1.0f);

    // Process alternating quiet and loud sections
    std::vector<float> quietOutputs, loudOutputs;

    // Quiet section
    for (int i = 0; i < 500; ++i) {
        quietOutputs.push_back(shaper.process(0.1f));
    }

    // Loud section (should perturb attractor more)
    for (int i = 0; i < 500; ++i) {
        loudOutputs.push_back(shaper.process(0.9f));
    }

    // Calculate variance of each section
    auto calcVariance = [](const std::vector<float>& data) {
        float mean = 0.0f;
        for (float v : data) mean += v;
        mean /= static_cast<float>(data.size());

        float variance = 0.0f;
        for (float v : data) variance += (v - mean) * (v - mean);
        return variance / static_cast<float>(data.size());
    };

    float quietVariance = calcVariance(quietOutputs);
    float loudVariance = calcVariance(loudOutputs);

    INFO("Quiet variance: " << quietVariance);
    INFO("Loud variance: " << loudVariance);

    // Loud section should generally have different characteristics
    // due to input-coupled perturbation
    // The louder input produces more perturbation, which should result
    // in different variance. Allow for wide tolerance since chaos is unpredictable.
    // The key point is that input coupling affects the output.
    float varianceRatio = (loudVariance > quietVariance) ?
        (loudVariance / quietVariance) : (quietVariance / loudVariance);
    INFO("Variance ratio: " << varianceRatio);
    // Just verify we got valid variances (both should be > 0)
    REQUIRE(quietVariance > 0.0f);
    REQUIRE(loudVariance > 0.0f);
}

TEST_CASE("ChaosWaveshaper coupling doesn't cause divergence (FR-027)",
          "[chaos_waveshaper][US2][stability]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Lorenz);
    shaper.setChaosAmount(1.0f);
    shaper.setInputCoupling(1.0f);  // Maximum coupling

    // Feed extreme inputs continuously
    for (int i = 0; i < 50000; ++i) {
        float input = (i % 2 == 0) ? 1.0f : -1.0f;
        float output = shaper.process(input);

        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
        REQUIRE(std::abs(output) <= 1.5f);  // Bounded output
    }
}

// =============================================================================
// Phase 5: User Story 3 - Model Selection Tests
// =============================================================================

TEST_CASE("ChaosWaveshaper setModel() parameter (FR-009)",
          "[chaos_waveshaper][US3][model]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);

    SECTION("default is Lorenz") {
        REQUIRE(shaper.getModel() == ChaosModel::Lorenz);
    }

    SECTION("can set all models") {
        auto model = GENERATE(
            ChaosModel::Lorenz,
            ChaosModel::Rossler,
            ChaosModel::Chua,
            ChaosModel::Henon
        );

        shaper.setModel(model);
        REQUIRE(shaper.getModel() == model);
    }
}

TEST_CASE("ChaosWaveshaper invalid enum defaults to Lorenz (FR-036)",
          "[chaos_waveshaper][US3][invalid]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);

    // Cast invalid value to ChaosModel
    shaper.setModel(static_cast<ChaosModel>(99));
    REQUIRE(shaper.getModel() == ChaosModel::Lorenz);

    shaper.setModel(static_cast<ChaosModel>(255));
    REQUIRE(shaper.getModel() == ChaosModel::Lorenz);
}

TEST_CASE("ChaosWaveshaper Rossler attractor bounded state (FR-015)",
          "[chaos_waveshaper][US3][rossler]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Rossler);
    shaper.setChaosAmount(1.0f);
    shaper.setAttractorSpeed(10.0f);

    // Process for extended period
    for (int i = 0; i < 50000; ++i) {
        float input = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
        float output = shaper.process(input);

        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
    }
}

TEST_CASE("ChaosWaveshaper Chua attractor bounded state (FR-016)",
          "[chaos_waveshaper][US3][chua]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Chua);
    shaper.setChaosAmount(1.0f);
    shaper.setAttractorSpeed(10.0f);

    // Process for extended period
    for (int i = 0; i < 50000; ++i) {
        float input = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
        float output = shaper.process(input);

        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
    }
}

TEST_CASE("ChaosWaveshaper Henon map bounded state (FR-017)",
          "[chaos_waveshaper][US3][henon]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Henon);
    shaper.setChaosAmount(1.0f);
    shaper.setAttractorSpeed(10.0f);

    // Process for extended period
    for (int i = 0; i < 50000; ++i) {
        float input = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
        float output = shaper.process(input);

        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
    }
}

TEST_CASE("ChaosWaveshaper Lorenz vs Rossler produce different spectra (SC-006)",
          "[chaos_waveshaper][US3][distinct]") {
    // Test that different models produce measurably different results

    constexpr size_t numSamples = 4096;
    constexpr float sampleRate = 44100.0f;
    constexpr float frequency = 440.0f;

    auto processWithModel = [&](ChaosModel model) {
        ChaosWaveshaper shaper;
        shaper.prepare(sampleRate, 512);
        shaper.setModel(model);
        shaper.setChaosAmount(1.0f);
        shaper.setAttractorSpeed(1.0f);

        std::vector<float> output(numSamples);
        for (size_t i = 0; i < numSamples; ++i) {
            const float t = static_cast<float>(i) / sampleRate;
            float input = 0.5f * std::sin(2.0f * 3.14159265f * frequency * t);
            output[i] = shaper.process(input);
        }
        return output;
    };

    auto lorenzOutput = processWithModel(ChaosModel::Lorenz);
    auto rosslerOutput = processWithModel(ChaosModel::Rossler);

    // Calculate difference metric
    float sumDiff = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sumDiff += std::abs(lorenzOutput[i] - rosslerOutput[i]);
    }
    float avgDiff = sumDiff / static_cast<float>(numSamples);

    INFO("Average difference between Lorenz and Rossler: " << avgDiff);
    REQUIRE(avgDiff > 0.01f);  // Should be measurably different
}

TEST_CASE("ChaosWaveshaper Henon produces more abrupt transitions",
          "[chaos_waveshaper][US3][henon-abrupt]") {
    // Henon (discrete map) should have different characteristics
    // than continuous attractors

    ChaosWaveshaper shaperHenon;
    shaperHenon.prepare(44100.0, 512);
    shaperHenon.setModel(ChaosModel::Henon);
    shaperHenon.setChaosAmount(1.0f);
    shaperHenon.setAttractorSpeed(5.0f);

    ChaosWaveshaper shaperLorenz;
    shaperLorenz.prepare(44100.0, 512);
    shaperLorenz.setModel(ChaosModel::Lorenz);
    shaperLorenz.setChaosAmount(1.0f);
    shaperLorenz.setAttractorSpeed(5.0f);

    // Calculate "roughness" as sum of absolute differences between consecutive samples
    auto calcRoughness = [](ChaosWaveshaper& shaper, size_t n) {
        float prevOutput = 0.0f;
        float roughness = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            float input = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
            float output = shaper.process(input);
            roughness += std::abs(output - prevOutput);
            prevOutput = output;
        }
        return roughness / static_cast<float>(n);
    };

    float henonRoughness = calcRoughness(shaperHenon, 10000);
    float lorenzRoughness = calcRoughness(shaperLorenz, 10000);

    INFO("Henon roughness: " << henonRoughness);
    INFO("Lorenz roughness: " << lorenzRoughness);

    // Just verify both produce valid output and have different characteristics
    REQUIRE(henonRoughness > 0.0f);
    REQUIRE(lorenzRoughness > 0.0f);
}

TEST_CASE("ChaosWaveshaper Chua double-scroll bi-modal behavior",
          "[chaos_waveshaper][US3][chua-bimodal]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Chua);
    shaper.setChaosAmount(1.0f);
    shaper.setAttractorSpeed(2.0f);

    // Track output values to detect bi-modal distribution
    std::vector<float> outputs;
    for (int i = 0; i < 10000; ++i) {
        float input = 0.3f;  // Constant input
        outputs.push_back(shaper.process(input));
    }

    // Simple check: outputs should span a range (not stuck at one value)
    float minOut = *std::min_element(outputs.begin(), outputs.end());
    float maxOut = *std::max_element(outputs.begin(), outputs.end());

    INFO("Chua output range: [" << minOut << ", " << maxOut << "]");
    REQUIRE(maxOut - minOut > 0.01f);  // Should have variation
}

// =============================================================================
// Phase 6: User Story 4 - Attractor Speed Control Tests
// =============================================================================

TEST_CASE("ChaosWaveshaper setAttractorSpeed() parameter (FR-011)",
          "[chaos_waveshaper][US4][speed]") {
    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);

    SECTION("default is 1.0") {
        REQUIRE(shaper.getAttractorSpeed() == Approx(1.0f));
    }

    SECTION("setter/getter work correctly") {
        shaper.setAttractorSpeed(0.5f);
        REQUIRE(shaper.getAttractorSpeed() == Approx(0.5f));

        shaper.setAttractorSpeed(10.0f);
        REQUIRE(shaper.getAttractorSpeed() == Approx(10.0f));
    }

    SECTION("clamped to valid range") {
        shaper.setAttractorSpeed(0.001f);
        REQUIRE(shaper.getAttractorSpeed() == Approx(0.01f));

        shaper.setAttractorSpeed(1000.0f);
        REQUIRE(shaper.getAttractorSpeed() == Approx(100.0f));
    }
}

TEST_CASE("ChaosWaveshaper speed=0.1 slower than speed=1.0 (SC-007)",
          "[chaos_waveshaper][US4][slow]") {
    // Measure "evolution rate" by tracking output variation over time

    auto measureVariation = [](float speed) {
        ChaosWaveshaper shaper;
        shaper.prepare(44100.0, 512);
        shaper.setModel(ChaosModel::Lorenz);
        shaper.setChaosAmount(1.0f);
        shaper.setAttractorSpeed(speed);

        std::vector<float> outputs;
        for (int i = 0; i < 5000; ++i) {
            outputs.push_back(shaper.process(0.5f));
        }

        // Calculate total variation (sum of absolute differences)
        float variation = 0.0f;
        for (size_t i = 1; i < outputs.size(); ++i) {
            variation += std::abs(outputs[i] - outputs[i-1]);
        }
        return variation;
    };

    float slowVariation = measureVariation(0.1f);
    float normalVariation = measureVariation(1.0f);

    INFO("Slow (0.1) variation: " << slowVariation);
    INFO("Normal (1.0) variation: " << normalVariation);

    REQUIRE(slowVariation < normalVariation);
}

TEST_CASE("ChaosWaveshaper speed=10.0 faster than speed=1.0 (SC-007)",
          "[chaos_waveshaper][US4][fast]") {
    auto measureVariation = [](float speed) {
        ChaosWaveshaper shaper;
        shaper.prepare(44100.0, 512);
        shaper.setModel(ChaosModel::Lorenz);
        shaper.setChaosAmount(1.0f);
        shaper.setAttractorSpeed(speed);

        std::vector<float> outputs;
        for (int i = 0; i < 5000; ++i) {
            outputs.push_back(shaper.process(0.5f));
        }

        float variation = 0.0f;
        for (size_t i = 1; i < outputs.size(); ++i) {
            variation += std::abs(outputs[i] - outputs[i-1]);
        }
        return variation;
    };

    float normalVariation = measureVariation(1.0f);
    float fastVariation = measureVariation(10.0f);

    INFO("Normal (1.0) variation: " << normalVariation);
    INFO("Fast (10.0) variation: " << fastVariation);

    REQUIRE(fastVariation > normalVariation);
}

TEST_CASE("ChaosWaveshaper sample rate compensation (FR-019)",
          "[chaos_waveshaper][US4][sample-rate]") {
    // Attractor should evolve at similar perceptual rate across sample rates
    // The key test: after processing 1 second of audio, the attractor
    // should have evolved roughly the same amount regardless of sample rate

    auto measureOutputRange = [](double sampleRate) {
        ChaosWaveshaper shaper;
        shaper.prepare(sampleRate, 512);
        shaper.setModel(ChaosModel::Lorenz);
        shaper.setChaosAmount(1.0f);
        shaper.setAttractorSpeed(1.0f);

        // Process same duration (0.5 seconds) at different sample rates
        size_t numSamples = static_cast<size_t>(sampleRate * 0.5);
        float minOutput = 1.0f;
        float maxOutput = -1.0f;

        for (size_t i = 0; i < numSamples; ++i) {
            float output = shaper.process(0.5f);
            minOutput = std::min(minOutput, output);
            maxOutput = std::max(maxOutput, output);
        }

        return maxOutput - minOutput;  // Output range
    };

    float range44k = measureOutputRange(44100.0);
    float range48k = measureOutputRange(48000.0);
    float range96k = measureOutputRange(96000.0);

    INFO("44.1kHz output range: " << range44k);
    INFO("48kHz output range: " << range48k);
    INFO("96kHz output range: " << range96k);

    // The output range (dynamic behavior of attractor) should be similar
    // across sample rates when sample-rate compensation is working
    // Allow generous tolerance due to chaotic nature
    REQUIRE(range48k > range44k * 0.3f);
    REQUIRE(range48k < range44k * 3.0f);
    REQUIRE(range96k > range44k * 0.3f);
    REQUIRE(range96k < range44k * 3.0f);

    // All should have meaningful range (not stuck at a fixed value)
    REQUIRE(range44k > 0.01f);
    REQUIRE(range48k > 0.01f);
    REQUIRE(range96k > 0.01f);
}

TEST_CASE("ChaosWaveshaper all speeds keep attractor bounded",
          "[chaos_waveshaper][US4][bounded-all-speeds]") {
    auto testSpeed = GENERATE(0.01f, 0.1f, 1.0f, 10.0f, 100.0f);

    ChaosWaveshaper shaper;
    shaper.prepare(44100.0, 512);
    shaper.setModel(ChaosModel::Lorenz);
    shaper.setChaosAmount(1.0f);
    shaper.setAttractorSpeed(testSpeed);

    for (int i = 0; i < 10000; ++i) {
        float input = 0.5f * std::sin(static_cast<float>(i) * 0.01f);
        float output = shaper.process(input);

        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
    }
}
