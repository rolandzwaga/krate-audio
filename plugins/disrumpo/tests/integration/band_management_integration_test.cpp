// ==============================================================================
// Band Management Integration Tests
// ==============================================================================
// IT-001 to IT-005: End-to-end band management tests
// Tests full signal path through crossover → bands → summation.
//
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/crossover_network.h"
#include "dsp/band_processor.h"
#include "dsp/band_state.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

using Catch::Matchers::WithinAbs;

// =============================================================================
// Test Helpers
// =============================================================================

/// @brief Generate sine wave samples
static void generateSine(float* buffer, size_t numSamples, float freq, double sampleRate) {
    constexpr double twoPi = 6.283185307179586;
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = static_cast<float>(std::sin(twoPi * freq * static_cast<double>(i) / sampleRate));
    }
}

/// @brief Calculate RMS of a buffer
static float calculateRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    double sumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSq += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

/// @brief Convert linear to dB
static float linearToDb(float linear) {
    if (linear <= 0.0f) return -144.0f;
    return 20.0f * std::log10(linear);
}

// =============================================================================
// IT-001: Crossover Flat Response Test (without BandProcessor)
// =============================================================================

TEST_CASE("IT-001: Crossover flat frequency response", "[integration][band-management]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kNumSamples = 16384;
    constexpr int kNumBands = 4;

    // Setup crossover network
    Disrumpo::CrossoverNetwork crossover;
    crossover.prepare(kSampleRate, kNumBands);

    // Generate test signal (1kHz sine)
    std::array<float, kNumSamples> input{};
    std::array<float, kNumSamples> output{};
    std::array<float, Disrumpo::kMaxBands> bands{};

    generateSine(input.data(), kNumSamples, 1000.0f, kSampleRate);

    // Process through crossover only (no BandProcessor - pure crossover test)
    for (size_t i = 0; i < kNumSamples; ++i) {
        crossover.process(input[i], bands);

        // Sum all bands directly
        float sum = 0.0f;
        for (int b = 0; b < kNumBands; ++b) {
            sum += bands[b];
        }
        output[i] = sum;
    }

    SECTION("output is not silent") {
        const size_t measureStart = kNumSamples * 3 / 4;
        float outputRMS = calculateRMS(output.data() + measureStart, kNumSamples / 4);
        REQUIRE(outputRMS > 0.1f);
    }

    SECTION("flat frequency response (SC-001)") {
        const size_t measureStart = kNumSamples * 3 / 4;
        const size_t measureLen = kNumSamples / 4;
        float inputRMS = calculateRMS(input.data() + measureStart, measureLen);
        float outputRMS = calculateRMS(output.data() + measureStart, measureLen);
        float errorDb = std::abs(linearToDb(outputRMS / inputRMS));
        INFO("Input RMS: " << inputRMS << ", Output RMS: " << outputRMS << ", Error: " << errorDb << " dB");
        REQUIRE(errorDb < 0.1f);  // SC-001: +/-0.1dB
    }
}

// =============================================================================
// IT-002: Varying Band Count Test
// =============================================================================

TEST_CASE("IT-002: Audio processing with varying band counts", "[integration][band-management]") {
    constexpr double kSampleRate = 44100.0;

    Disrumpo::CrossoverNetwork crossover;
    std::array<float, Disrumpo::kMaxBands> bands{};

    for (int numBands = 1; numBands <= Disrumpo::kMaxBands; ++numBands) {
        DYNAMIC_SECTION("band count = " << numBands) {
            crossover.prepare(kSampleRate, numBands);

            // Let filters settle with DC (more iterations for more bands)
            const int settleIterations = 2000 + numBands * 500;
            for (int i = 0; i < settleIterations; ++i) {
                crossover.process(1.0f, bands);
            }

            float sum = 0.0f;
            for (int b = 0; b < numBands; ++b) {
                sum += bands[b];
            }

            float errorDb = std::abs(linearToDb(sum));
            INFO("Band count: " << numBands << ", sum: " << sum << ", error: " << errorDb << " dB");
            REQUIRE(errorDb < 0.1f);  // SC-001 compliance
        }
    }
}

// =============================================================================
// IT-003: Dynamic Band Count Change Test
// =============================================================================

TEST_CASE("IT-003: Band count change maintains output stability", "[integration][band-management]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kSettleTime = 4000;

    Disrumpo::CrossoverNetwork crossover;
    crossover.prepare(kSampleRate, 4);

    std::array<float, Disrumpo::kMaxBands> bands{};

    // Let filters settle with 4 bands
    for (size_t i = 0; i < kSettleTime; ++i) {
        crossover.process(1.0f, bands);
    }

    float sumBefore = 0.0f;
    for (int b = 0; b < 4; ++b) {
        sumBefore += bands[b];
    }

    // Change to 3 bands (test band count decrease)
    crossover.setBandCount(3);

    // Let new configuration settle
    for (size_t i = 0; i < kSettleTime; ++i) {
        crossover.process(1.0f, bands);
    }

    float sumAfter = 0.0f;
    for (int b = 0; b < 3; ++b) {
        sumAfter += bands[b];
    }

    // Both should be near unity (DC input)
    REQUIRE(std::abs(sumBefore - 1.0f) < 0.1f);
    REQUIRE(std::abs(sumAfter - 1.0f) < 0.1f);
}

// =============================================================================
// IT-004: Band Gain Processing Test
// =============================================================================

TEST_CASE("IT-004: Per-band gain affects signal level", "[integration][band-management]") {
    constexpr double kSampleRate = 44100.0;

    Disrumpo::BandProcessor processor;
    processor.prepare(kSampleRate);

    // Test +6dB gain
    processor.setGainDb(6.0f);

    // Let smoother settle
    float lastLeft = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float left = 1.0f;
        float right = 1.0f;
        processor.process(left, right);
        lastLeft = left;
    }

    // +6dB gain with center pan should give:
    // leftGain = cos(PI/4) * 10^(6/20) = 0.707 * 2.0 = 1.414
    float expectedGain = std::cos(3.14159265f / 4.0f) * std::pow(10.0f, 6.0f / 20.0f);
    INFO("Expected gain: " << expectedGain << ", Actual: " << lastLeft);
    REQUIRE_THAT(lastLeft, WithinAbs(expectedGain, 0.1f));
}

// =============================================================================
// IT-005: Mute Processing Test
// =============================================================================

TEST_CASE("IT-005: Mute suppresses band output", "[integration][band-management]") {
    constexpr double kSampleRate = 44100.0;

    Disrumpo::BandProcessor processor;
    processor.prepare(kSampleRate);

    // Get unmuted output first (let smoother settle)
    float unmutedLevel = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float left = 1.0f;
        float right = 1.0f;
        processor.process(left, right);
        unmutedLevel = left;
    }

    // Now mute and let smoother settle
    processor.setMute(true);
    float mutedLevel = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float left = 1.0f;
        float right = 1.0f;
        processor.process(left, right);
        mutedLevel = left;
    }

    INFO("Unmuted level: " << unmutedLevel << ", Muted level: " << mutedLevel);
    REQUIRE(unmutedLevel > 0.5f);       // Should have signal (center pan = 0.707)
    REQUIRE(mutedLevel < 0.001f);       // Muted should be near zero
}

// =============================================================================
// IT-006: Full Signal Path (Stereo)
// =============================================================================

TEST_CASE("IT-006: Full stereo signal path", "[integration][band-management]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kNumSamples = 8192;
    constexpr int kNumBands = 4;

    // Setup L and R crossovers
    Disrumpo::CrossoverNetwork crossoverL;
    Disrumpo::CrossoverNetwork crossoverR;
    crossoverL.prepare(kSampleRate, kNumBands);
    crossoverR.prepare(kSampleRate, kNumBands);

    // Setup per-band processors on the heap (BandProcessor is large due to oversamplers/distortion)
    std::vector<std::unique_ptr<Disrumpo::BandProcessor>> bandProcessors;
    bandProcessors.reserve(kNumBands);
    for (int i = 0; i < kNumBands; ++i) {
        bandProcessors.push_back(std::make_unique<Disrumpo::BandProcessor>());
        bandProcessors.back()->prepare(kSampleRate);
    }

    // Generate stereo test signals
    std::array<float, kNumSamples> inputL{};
    std::array<float, kNumSamples> inputR{};
    generateSine(inputL.data(), kNumSamples, 1000.0f, kSampleRate);
    generateSine(inputR.data(), kNumSamples, 500.0f, kSampleRate);  // Different freq for R

    std::array<float, Disrumpo::kMaxBands> bandsL{};
    std::array<float, Disrumpo::kMaxBands> bandsR{};

    float sumL = 0.0f;
    float sumR = 0.0f;

    // Process through full chain
    for (size_t i = 0; i < kNumSamples; ++i) {
        crossoverL.process(inputL[i], bandsL);
        crossoverR.process(inputR[i], bandsR);

        float frameL = 0.0f;
        float frameR = 0.0f;
        for (int b = 0; b < kNumBands; ++b) {
            // BandProcessor processes L/R together (applies pan)
            float left = bandsL[b];
            float right = bandsR[b];
            bandProcessors[b]->process(left, right);
            frameL += left;
            frameR += right;
        }

        // Accumulate for final quarter
        if (i >= kNumSamples * 3 / 4) {
            sumL += frameL * frameL;
            sumR += frameR * frameR;
        }
    }

    // Calculate RMS for final quarter
    const size_t measureLen = kNumSamples / 4;
    float outputLRMS = std::sqrt(sumL / static_cast<float>(measureLen));
    float outputRRMS = std::sqrt(sumR / static_cast<float>(measureLen));

    INFO("Output L RMS: " << outputLRMS << ", Output R RMS: " << outputRRMS);

    // Both channels should have signal
    REQUIRE(outputLRMS > 0.1f);
    REQUIRE(outputRRMS > 0.1f);

    // Center pan should give equal L/R levels
    float ratio = outputLRMS / outputRRMS;
    REQUIRE_THAT(ratio, WithinAbs(1.0f, 0.2f));
}
