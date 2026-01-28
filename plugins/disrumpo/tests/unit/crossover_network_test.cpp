// ==============================================================================
// CrossoverNetwork Unit Tests
// ==============================================================================
// Tests for the N-band crossover network (1-8 bands).
// Per spec.md FR-001 to FR-014 and SC-001.
//
// Constitution Principle XII: Test-First Development
// These tests MUST fail before implementation.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/crossover_network.h"
#include "dsp/band_state.h"

#include <array>
#include <cmath>
#include <numeric>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// =============================================================================
// Test Helpers
// =============================================================================

/// @brief Generate sine wave samples
static void generateSine(float* buffer, size_t numSamples, float freq, double sampleRate) {
    const double twoPi = 6.283185307179586;
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
// Basic Functionality Tests
// =============================================================================

TEST_CASE("CrossoverNetwork 1 band passes input unchanged", "[crossover][US1]") {
    // FR-014: For 1 band configuration, process() MUST pass input directly
    Disrumpo::CrossoverNetwork network;
    network.prepare(44100.0, 1);

    std::array<float, Disrumpo::kMaxBands> bands{};

    SECTION("single sample passthrough") {
        network.process(1.0f, bands);
        REQUIRE(bands[0] == 1.0f);
    }

    SECTION("negative sample passthrough") {
        network.process(-0.5f, bands);
        REQUIRE(bands[0] == -0.5f);
    }

    SECTION("zero passthrough") {
        network.process(0.0f, bands);
        REQUIRE(bands[0] == 0.0f);
    }
}

TEST_CASE("CrossoverNetwork 2 bands split signal", "[crossover][US1]") {
    // FR-012, FR-013: Cascaded band splitting
    Disrumpo::CrossoverNetwork network;
    network.prepare(44100.0, 2);

    // Let the filter settle
    std::array<float, Disrumpo::kMaxBands> bands{};
    for (int i = 0; i < 1000; ++i) {
        network.process(1.0f, bands);
    }

    SECTION("low + high sums to input (DC)") {
        // After settling, DC should pass through perfectly
        float sum = bands[0] + bands[1];
        REQUIRE_THAT(sum, WithinAbs(1.0f, 0.01f));
    }
}

TEST_CASE("CrossoverNetwork 4 bands sum to flat response", "[crossover][US1][flat]") {
    // SC-001: Band summation produces flat frequency response within +/-0.1 dB
    Disrumpo::CrossoverNetwork network;
    network.prepare(44100.0, 4);

    std::array<float, Disrumpo::kMaxBands> bands{};

    // Test with DC (easiest case)
    SECTION("DC input sums to flat") {
        // Let filter settle
        for (int i = 0; i < 2000; ++i) {
            network.process(1.0f, bands);
        }

        float sum = bands[0] + bands[1] + bands[2] + bands[3];
        float errorDb = std::abs(linearToDb(sum / 1.0f));
        REQUIRE(errorDb < 0.1f);
    }

    // Test at various frequencies
    // SC-001: Band summation produces flat frequency response within +/-0.1 dB
    // D'Appolito allpass compensation ensures phase coherence across all bands.
    SECTION("1kHz sine sums to flat") {
        constexpr size_t kNumSamples = 8192;  // More samples for better settling
        std::array<float, kNumSamples> input{};
        generateSine(input.data(), kNumSamples, 1000.0f, 44100.0);

        // Calculate input RMS of second half only
        float inputRMS = calculateRMS(input.data() + kNumSamples / 2, kNumSamples / 2);

        // Process through crossover
        std::array<float, kNumSamples> summed{};
        for (size_t i = 0; i < kNumSamples; ++i) {
            network.process(input[i], bands);
            summed[i] = bands[0] + bands[1] + bands[2] + bands[3];
        }

        // Measure RMS of second half (after transient settles)
        float outputRMS = calculateRMS(summed.data() + kNumSamples / 2, kNumSamples / 2);

        float errorDb = std::abs(linearToDb(outputRMS / inputRMS));
        REQUIRE(errorDb < 0.1f);  // SC-001: +/-0.1dB flatness with allpass compensation
    }
}

TEST_CASE("CrossoverNetwork 8 bands configuration works", "[crossover][US1]") {
    // FR-002: Support configurable band count from 1 to 8 bands
    // SC-001: Band summation produces flat frequency response within +/-0.1 dB
    Disrumpo::CrossoverNetwork network;
    network.prepare(44100.0, 8);

    REQUIRE(network.getBandCount() == 8);

    std::array<float, Disrumpo::kMaxBands> bands{};

    // Process DC and verify sum is flat
    // Use more settling time for 8 bands (7 cascaded filters + allpasses)
    for (int i = 0; i < 4000; ++i) {
        network.process(1.0f, bands);
    }

    float sum = 0.0f;
    for (int i = 0; i < 8; ++i) {
        sum += bands[i];
    }

    float errorDb = std::abs(linearToDb(sum / 1.0f));
    // SC-001: +/-0.1dB flatness with D'Appolito allpass compensation
    REQUIRE(errorDb < 0.1f);
}

// =============================================================================
// Band Count Change Tests (FR-011a, FR-011b)
// =============================================================================

TEST_CASE("CrossoverNetwork band count increase preserves existing crossovers", "[crossover][US1]") {
    // FR-011a: When band count increases, existing crossover positions MUST be preserved
    // The crossover frequency should exist somewhere in the new configuration
    Disrumpo::CrossoverNetwork network;
    network.prepare(44100.0, 2);

    // Set a specific crossover frequency
    network.setCrossoverFrequency(0, 500.0f);
    float originalFreq = network.getCrossoverFrequency(0);

    // Increase to 4 bands
    network.setBandCount(4);

    // Original crossover frequency should exist somewhere in the new configuration
    // (it may have moved to a different index after sorting)
    bool found = false;
    for (int i = 0; i < network.getBandCount() - 1; ++i) {
        float freq = network.getCrossoverFrequency(i);
        if (std::abs(freq - originalFreq) < 1.0f) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("CrossoverNetwork band count decrease preserves lowest crossovers", "[crossover][US1]") {
    // FR-011b: When band count decreases, preserve lowest N-1 crossover frequencies
    Disrumpo::CrossoverNetwork network;
    network.prepare(44100.0, 4);

    // Set specific crossover frequencies
    network.setCrossoverFrequency(0, 200.0f);
    network.setCrossoverFrequency(1, 1000.0f);
    network.setCrossoverFrequency(2, 5000.0f);

    float freq0 = network.getCrossoverFrequency(0);
    float freq1 = network.getCrossoverFrequency(1);

    // Decrease to 3 bands (should keep lowest 2 crossovers)
    network.setBandCount(3);

    REQUIRE_THAT(network.getCrossoverFrequency(0), WithinAbs(freq0, 1.0f));
    REQUIRE_THAT(network.getCrossoverFrequency(1), WithinAbs(freq1, 1.0f));
}

// =============================================================================
// Logarithmic Frequency Distribution Tests (FR-009)
// =============================================================================

TEST_CASE("CrossoverNetwork uses logarithmic default frequency distribution", "[crossover][US1]") {
    // FR-009: Crossover frequencies redistribute logarithmically across 20Hz-20kHz
    Disrumpo::CrossoverNetwork network;
    network.prepare(44100.0, 4);

    // For 4 bands, we have 3 crossovers
    // Logarithmic distribution from 20Hz to 20kHz:
    // log10(20) = 1.301, log10(20000) = 4.301
    // step = 3.0 / 4 = 0.75
    // f0 = 10^(1.301 + 0.75) = 10^2.051 = ~112 Hz
    // f1 = 10^(1.301 + 1.5) = 10^2.801 = ~632 Hz
    // f2 = 10^(1.301 + 2.25) = 10^3.551 = ~3556 Hz

    float f0 = network.getCrossoverFrequency(0);
    float f1 = network.getCrossoverFrequency(1);
    float f2 = network.getCrossoverFrequency(2);

    // Verify logarithmic spacing: ratios should be approximately equal
    float ratio1 = f1 / f0;
    float ratio2 = f2 / f1;

    // Ratios should be within 10% of each other for logarithmic spacing
    REQUIRE_THAT(ratio1, WithinRel(ratio2, 0.1f));

    // Verify frequencies are in valid range
    REQUIRE(f0 > Disrumpo::kMinCrossoverHz);
    REQUIRE(f2 < Disrumpo::kMaxCrossoverHz);
    REQUIRE(f0 < f1);
    REQUIRE(f1 < f2);
}

// =============================================================================
// Sample Rate Tests (SC-007)
// =============================================================================

TEST_CASE("CrossoverNetwork flat response at all sample rates", "[crossover][US1][samplerate]") {
    // SC-007: Flat response verified at 44.1kHz, 48kHz, 96kHz, 192kHz
    const std::array<double, 4> sampleRates = {44100.0, 48000.0, 96000.0, 192000.0};

    for (double sr : sampleRates) {
        INFO("Testing at sample rate: " << sr);

        Disrumpo::CrossoverNetwork network;
        network.prepare(sr, 4);

        std::array<float, Disrumpo::kMaxBands> bands{};

        // Let filter settle with DC
        for (int i = 0; i < 4000; ++i) {
            network.process(1.0f, bands);
        }

        float sum = bands[0] + bands[1] + bands[2] + bands[3];
        float errorDb = std::abs(linearToDb(sum / 1.0f));

        REQUIRE(errorDb < 0.1f);
    }
}

// =============================================================================
// Prepare and Reset Tests
// =============================================================================

TEST_CASE("CrossoverNetwork prepare initializes correctly", "[crossover][US1]") {
    // FR-003: CrossoverNetwork MUST expose prepare(sampleRate, numBands)
    Disrumpo::CrossoverNetwork network;

    SECTION("prepares with valid parameters") {
        network.prepare(44100.0, 4);
        REQUIRE(network.getBandCount() == 4);
        REQUIRE(network.isPrepared());
    }

    SECTION("clamps band count to valid range") {
        network.prepare(44100.0, 0);  // Below minimum
        REQUIRE(network.getBandCount() >= Disrumpo::kMinBands);

        network.prepare(44100.0, 10); // Above maximum
        REQUIRE(network.getBandCount() <= Disrumpo::kMaxBands);
    }
}

TEST_CASE("CrossoverNetwork reset clears filter states", "[crossover][US1]") {
    // FR-004: CrossoverNetwork MUST expose reset() to clear all filter states
    Disrumpo::CrossoverNetwork network;
    network.prepare(44100.0, 2);

    std::array<float, Disrumpo::kMaxBands> bands{};

    // Process some samples
    for (int i = 0; i < 1000; ++i) {
        network.process(1.0f, bands);
    }

    // Reset
    network.reset();

    // After reset, processing zero input should produce near-zero output
    // (filters are cleared)
    for (int i = 0; i < 100; ++i) {
        network.process(0.0f, bands);
    }

    float totalEnergy = 0.0f;
    for (int i = 0; i < network.getBandCount(); ++i) {
        totalEnergy += bands[i] * bands[i];
    }

    REQUIRE(totalEnergy < 0.0001f);
}

// =============================================================================
// Crossover Frequency Tests
// =============================================================================

TEST_CASE("CrossoverNetwork setCrossoverFrequency clamps to valid range", "[crossover][US1]") {
    Disrumpo::CrossoverNetwork network;
    network.prepare(44100.0, 4);

    SECTION("clamps below minimum") {
        network.setCrossoverFrequency(0, 5.0f); // Below 20Hz
        REQUIRE(network.getCrossoverFrequency(0) >= Disrumpo::kMinCrossoverHz);
    }

    SECTION("clamps above maximum") {
        network.setCrossoverFrequency(0, 25000.0f); // Above 20kHz
        // Should clamp to Nyquist * 0.45
        REQUIRE(network.getCrossoverFrequency(0) <= 44100.0f * 0.45f);
    }
}
