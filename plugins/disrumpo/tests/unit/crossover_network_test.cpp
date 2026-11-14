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

#include "../test_support/audit_metrics.h"

// FFT and noise generation for FR-033 pink noise test
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/processors/noise_generator.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/core/random.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <numeric>
#include <vector>

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

TEST_CASE("CrossoverNetwork 4 bands configuration works", "[crossover][US1]") {
    // FR-002: Support configurable band count from 1 to 4 bands
    // SC-001: Band summation produces flat frequency response within +/-0.1 dB
    Disrumpo::CrossoverNetwork network;
    network.prepare(44100.0, 4);

    REQUIRE(network.getBandCount() == 4);

    std::array<float, Disrumpo::kMaxBands> bands{};

    // Process DC and verify sum is flat
    for (int i = 0; i < 4000; ++i) {
        network.process(1.0f, bands);
    }

    float sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
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

// Audit D-023 (fix plan T2.08): an increase kept the running crossovers but let the ones
// joining the chain resume the filter state they froze with when they were last used, and
// glide from a stale frequency. Inputs are silent while the network runs 2 bands, so a
// crossover that starts clean matches a freshly prepared reference network.
TEST_CASE("CrossoverNetwork crossovers joining on a band-count increase start clean at their frequency",
          "[crossover][D-023]") {
    constexpr double kSr = 48000.0;
    const auto sine = [](size_t n) {
        std::vector<float> x(n);
        for (size_t i = 0; i < n; ++i) {
            x[i] = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / kSr));
        }
        return x;
    };
    const auto in = sine(4800);
    std::array<float, Disrumpo::kMaxBands> bands{};

    Disrumpo::CrossoverNetwork network;
    network.prepare(kSr, 4);
    for (float x : in) network.process(x, bands);   // every crossover builds up state
    network.setBandCount(2);
    network.setCrossoverFrequency(2, 8000.0f);         // stored while crossover 2 is idle
    for (size_t i = 0; i < 48000; ++i) network.process(0.0f, bands);
    network.setBandCount(4);

    Disrumpo::CrossoverNetwork reference;
    reference.prepare(kSr, 4);
    for (int c = 0; c < 3; ++c) reference.setCrossoverFrequency(c, network.getCrossoverFrequency(c));
    reference.prepare(kSr, 4);  // snap the reference to those frequencies, clean state

    std::array<float, Disrumpo::kMaxBands> refBands{};
    double maxDiff = 0.0;
    for (float x : in) {
        network.process(x, bands);
        reference.process(x, refBands);
        for (size_t b = 0; b < 4; ++b) {
            maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(bands[b] - refBands[b])));
        }
    }
    INFO("crossovers after 2 -> 4: " << network.getCrossoverFrequency(0) << " / " << network.getCrossoverFrequency(1)
         << " / " << network.getCrossoverFrequency(2) << " Hz; max band difference vs a clean network " << maxDiff);
    REQUIRE(maxDiff < 1e-5);
}

// Audit D-023 (fix plan T2.08): repeated band-count changes keep every crossover at its
// index with its frequency (new ones land above the running ones here).
TEST_CASE("CrossoverNetwork band count 2->4->2->4 keeps each crossover at its index", "[crossover][D-023]") {
    Disrumpo::CrossoverNetwork network;
    network.prepare(48000.0, 2);
    const float f0 = network.getCrossoverFrequency(0);
    network.setBandCount(4);
    const std::array<float, 3> first{network.getCrossoverFrequency(0), network.getCrossoverFrequency(1),
                                     network.getCrossoverFrequency(2)};
    network.setBandCount(2);
    const float f0AtTwo = network.getCrossoverFrequency(0);
    network.setBandCount(4);
    const std::array<float, 3> second{network.getCrossoverFrequency(0), network.getCrossoverFrequency(1),
                                      network.getCrossoverFrequency(2)};
    std::printf("D-023 crossovers per index: 2 bands %.2f; 2->4 %.2f / %.2f / %.2f; 4->2 %.2f; 2->4 %.2f / %.2f / %.2f Hz\n",
                static_cast<double>(f0), static_cast<double>(first[0]), static_cast<double>(first[1]),
                static_cast<double>(first[2]), static_cast<double>(f0AtTwo), static_cast<double>(second[0]),
                static_cast<double>(second[1]), static_cast<double>(second[2]));
    REQUIRE(first[0] == f0);
    REQUIRE(f0AtTwo == f0);
    REQUIRE(second == first);
    REQUIRE(first[0] < first[1]);
    REQUIRE(first[1] < first[2]);
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
// Crossover Frequency Tests (User Story 5)
// =============================================================================

TEST_CASE("CrossoverNetwork setCrossoverFrequency clamps to valid range", "[crossover][US1][US5]") {
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

TEST_CASE("CrossoverNetwork manual crossover frequency adjustment", "[crossover][US5]") {
    // FR-035: Support manual crossover frequency adjustment
    Disrumpo::CrossoverNetwork network;
    network.prepare(44100.0, 4);

    SECTION("set crossover to specific frequency") {
        network.setCrossoverFrequency(0, 250.0f);
        REQUIRE_THAT(network.getCrossoverFrequency(0), WithinAbs(250.0f, 0.01f));

        network.setCrossoverFrequency(1, 1000.0f);
        REQUIRE_THAT(network.getCrossoverFrequency(1), WithinAbs(1000.0f, 0.01f));

        network.setCrossoverFrequency(2, 4000.0f);
        REQUIRE_THAT(network.getCrossoverFrequency(2), WithinAbs(4000.0f, 0.01f));
    }

    SECTION("manual values persist after band count increase") {
        // FR-011a: Existing crossovers preserved when increasing
        // Start with 2 bands, set crossover, then increase to 4 (max)
        network.setBandCount(2);
        network.setCrossoverFrequency(0, 250.0f);

        // Increase band count to max
        network.setBandCount(4);

        // Original crossover should still exist (may be at different indices after sorting)
        bool found250 = false;
        for (int i = 0; i < network.getBandCount() - 1; ++i) {
            float freq = network.getCrossoverFrequency(i);
            if (std::abs(freq - 250.0f) < 1.0f) found250 = true;
        }
        REQUIRE(found250);
    }

    SECTION("invalid index is silently ignored") {
        float originalFreq = network.getCrossoverFrequency(0);
        network.setCrossoverFrequency(-1, 500.0f);  // Invalid: negative
        network.setCrossoverFrequency(10, 500.0f);  // Invalid: beyond range
        // Should not crash, original value unchanged
        REQUIRE_THAT(network.getCrossoverFrequency(0), WithinAbs(originalFreq, 0.01f));
    }
}

// D-124 (fix plan T5.04): FR-BAND-002 keeps adjacent running crossovers at least
// kMinCrossoverSpacingOctaves (0.5 octave) apart, the rule the spectrum display's
// divider drag applies. The network holds crossover i >= f[i-1] * 2^0.5 and keeps
// the frequency it was given, so it returns there when the one below moves away.
TEST_CASE("CrossoverNetwork holds each crossover half an octave above the one below", "[crossover][US5][D-124]") {
    const float ratio = std::pow(2.0f, Disrumpo::kMinCrossoverSpacingOctaves);
    Disrumpo::CrossoverNetwork network;
    network.prepare(44100.0, 4);

    network.setCrossoverFrequency(0, 1000.0f);
    network.setCrossoverFrequency(1, 1200.0f);  // 0.26 octave above crossover 0
    const float f0 = network.getCrossoverFrequency(0);
    const float f1 = network.getCrossoverFrequency(1);
    const float f2 = network.getCrossoverFrequency(2);
    std::printf("D-124 crossovers set 1000 / 1200 Hz: running %.2f / %.2f / %.2f Hz (f0 * 2^0.5 = %.2f)\n",
                static_cast<double>(f0), static_cast<double>(f1), static_cast<double>(f2),
                static_cast<double>(f0 * ratio));
    REQUIRE(f0 == 1000.0f);
    REQUIRE(f1 >= f0 * ratio);
    REQUIRE_THAT(f1, WithinRel(1000.0f * ratio, 1e-6f));
    REQUIRE(f2 >= f1 * ratio);

    // The order the two values arrive in does not matter (a state load sets them one by one)
    Disrumpo::CrossoverNetwork reversed;
    reversed.prepare(44100.0, 4);
    reversed.setCrossoverFrequency(1, 1200.0f);
    reversed.setCrossoverFrequency(0, 1000.0f);
    REQUIRE(reversed.getCrossoverFrequency(1) == f1);

    // Crossover 0 moves away again: crossover 1 is back at the 1200 Hz it was given
    network.setCrossoverFrequency(0, 500.0f);
    REQUIRE(network.getCrossoverFrequency(1) == 1200.0f);
}

// Where the spacing meets the 20 kHz / 0.45 fs cap, the top running crossover
// takes the cap and each one below it is lowered to half an octave under the next.
TEST_CASE("CrossoverNetwork spacing below the frequency cap", "[crossover][US5][D-124]") {
    const float ratio = std::pow(2.0f, Disrumpo::kMinCrossoverSpacingOctaves);
    const float cap = 44100.0f * 0.45f;

    SECTION("four bands, every crossover set to 20 kHz") {
        Disrumpo::CrossoverNetwork network;
        network.prepare(44100.0, 4);
        for (int i = 0; i < 3; ++i) network.setCrossoverFrequency(i, 20000.0f);
        const float f[3] = {network.getCrossoverFrequency(0), network.getCrossoverFrequency(1),
                            network.getCrossoverFrequency(2)};
        std::printf("D-124 4 bands, all set to 20 kHz: running %.1f / %.1f / %.1f Hz (cap %.1f)\n",
                    static_cast<double>(f[0]), static_cast<double>(f[1]), static_cast<double>(f[2]),
                    static_cast<double>(cap));
        REQUIRE_THAT(f[2], WithinRel(cap, 1e-6f));
        REQUIRE_THAT(f[1], WithinRel(cap / ratio, 1e-6f));
        REQUIRE_THAT(f[0], WithinRel(cap / (ratio * ratio), 1e-6f));
        REQUIRE(f[1] >= f[0] * ratio * (1.0f - 1e-6f));
        REQUIRE(f[2] >= f[1] * ratio * (1.0f - 1e-6f));
    }

    SECTION("a band-count increase above a high crossover") {
        Disrumpo::CrossoverNetwork network;
        network.prepare(44100.0, 2);
        network.setCrossoverFrequency(0, 15000.0f);
        network.setCrossoverFrequency(1, 100.0f);  // idle at 2 bands: does not move crossover 0
        REQUIRE(network.getCrossoverFrequency(0) == 15000.0f);
        network.setBandCount(4);
        const float f[3] = {network.getCrossoverFrequency(0), network.getCrossoverFrequency(1),
                            network.getCrossoverFrequency(2)};
        std::printf("D-124 15 kHz at 2 bands, then 4 bands: running %.1f / %.1f / %.1f Hz\n",
                    static_cast<double>(f[0]), static_cast<double>(f[1]), static_cast<double>(f[2]));
        REQUIRE(f[2] <= cap);
        REQUIRE(f[1] >= f[0] * ratio * (1.0f - 1e-6f));
        REQUIRE(f[2] >= f[1] * ratio * (1.0f - 1e-6f));
        network.setBandCount(2);
        REQUIRE(network.getCrossoverFrequency(0) == 15000.0f);  // back where it was set
    }
}

// =============================================================================
// Pink Noise FFT Flat Response Test (FR-033)
// =============================================================================

TEST_CASE("CrossoverNetwork pink noise FFT flat response", "[crossover][US1][flat][FR-033]") {
    // FR-033: Use pink noise + FFT analysis to verify broadband flat frequency response
    // SC-001: Band summation produces flat frequency response within +/-0.1 dB
    //
    // This test uses pink noise (which has energy across ALL frequencies) and FFT
    // analysis to verify that the crossover summation is flat across the entire
    // audible spectrum, not just at a few discrete test frequencies.
    //
    // Methodology: Average power spectral density over multiple frames to reduce
    // variance from the stochastic pink noise signal. Compare total power in
    // octave bands between input and output.
    //
    // Note: Low frequencies (below 200 Hz) need very long settling times due to
    // the filter time constants. We use extended settling and more frames to
    // achieve reliable measurements.

    constexpr size_t kFFTSize = 8192;  // Larger FFT for better low-freq resolution
    constexpr size_t kNumBins = kFFTSize / 2 + 1;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kNumFrames = 16;  // More frames for better averaging
    constexpr size_t kBaseSettlingSamples = 32768;  // Base settling time

    // Test multiple band configurations
    const std::array<int, 3> bandCounts = {2, 3, 4};

    for (int numBands : bandCounts) {
        INFO("Testing " << numBands << " band configuration");

        Disrumpo::CrossoverNetwork network;
        network.prepare(kSampleRate, numBands);

        // Generate pink noise with deterministic seed
        Krate::DSP::Xorshift32 rng(42);
        Krate::DSP::PinkNoiseFilter pinkFilter;

        // Allocate buffers
        std::vector<float> window(kFFTSize);
        Krate::DSP::Window::generateHann(window.data(), kFFTSize);

        // Accumulated power spectra
        std::vector<double> inputPower(kNumBins, 0.0);
        std::vector<double> outputPower(kNumBins, 0.0);

        // Let crossover settle with pink noise
        // More bands = more cascaded filters = more settling time needed
        const size_t settlingSamples = kBaseSettlingSamples * static_cast<size_t>(numBands);
        std::array<float, Disrumpo::kMaxBands> bands{};
        for (size_t i = 0; i < settlingSamples; ++i) {
            float pink = pinkFilter.process(rng.nextFloat());
            network.process(pink, bands);
        }

        // FFT processor
        Krate::DSP::FFT fft;
        fft.prepare(kFFTSize);

        std::vector<Krate::DSP::Complex> spectrum(kNumBins);
        std::vector<float> frame(kFFTSize);
        std::vector<float> windowedFrame(kFFTSize);

        // Process multiple frames and accumulate power
        for (size_t frameIdx = 0; frameIdx < kNumFrames; ++frameIdx) {
            // Generate frame of pink noise and process
            std::vector<float> inputFrame(kFFTSize);
            std::vector<float> outputFrame(kFFTSize);

            for (size_t i = 0; i < kFFTSize; ++i) {
                float pink = pinkFilter.process(rng.nextFloat());
                inputFrame[i] = pink;

                network.process(pink, bands);
                float sum = 0.0f;
                for (int b = 0; b < numBands; ++b) {
                    sum += bands[b];
                }
                outputFrame[i] = sum;
            }

            // Window and FFT input
            for (size_t i = 0; i < kFFTSize; ++i) {
                windowedFrame[i] = inputFrame[i] * window[i];
            }
            fft.forward(windowedFrame.data(), spectrum.data());

            // Accumulate input power
            for (size_t bin = 0; bin < kNumBins; ++bin) {
                float mag = spectrum[bin].magnitude();
                inputPower[bin] += static_cast<double>(mag * mag);
            }

            // Window and FFT output
            for (size_t i = 0; i < kFFTSize; ++i) {
                windowedFrame[i] = outputFrame[i] * window[i];
            }
            fft.forward(windowedFrame.data(), spectrum.data());

            // Accumulate output power
            for (size_t bin = 0; bin < kNumBins; ++bin) {
                float mag = spectrum[bin].magnitude();
                outputPower[bin] += static_cast<double>(mag * mag);
            }
        }

        // Compare power in octave bands (more robust than per-bin)
        // Start from 300 Hz - lower frequencies have longer filter settling times
        // and crossover frequencies in typical multiband setups are above 100-200 Hz.
        // This range (300-6400 Hz) covers the critical frequencies for crossover verification.
        const std::array<float, 5> bandCenters = {300.0f, 600.0f, 1200.0f, 2400.0f, 4800.0f};

        float maxErrorDb = 0.0f;
        float worstFreq = 0.0f;

        for (float centerFreq : bandCenters) {
            // Skip bands above Nyquist/4 to avoid filter rolloff effects
            if (centerFreq > kSampleRate / 4.0f) continue;

            // Calculate bin range for this octave (centerFreq / sqrt(2) to centerFreq * sqrt(2))
            float lowFreq = centerFreq / std::numbers::sqrt2_v<float>;
            float highFreq = centerFreq * std::numbers::sqrt2_v<float>;

            size_t lowBin = static_cast<size_t>(lowFreq * kFFTSize / kSampleRate);
            size_t highBin = static_cast<size_t>(highFreq * kFFTSize / kSampleRate);

            // Clamp to valid range
            lowBin = std::max(lowBin, size_t(1));
            highBin = std::min(highBin, kNumBins - 1);

            // Sum power in this octave band
            double inputBandPower = 0.0;
            double outputBandPower = 0.0;

            for (size_t bin = lowBin; bin <= highBin; ++bin) {
                inputBandPower += inputPower[bin];
                outputBandPower += outputPower[bin];
            }

            // Calculate error in dB
            if (inputBandPower > 1e-12) {
                double ratio = outputBandPower / inputBandPower;
                float errorDb = static_cast<float>(std::abs(10.0 * std::log10(ratio)));

                if (errorDb > maxErrorDb) {
                    maxErrorDb = errorDb;
                    worstFreq = centerFreq;
                }
            }
        }

        INFO("Worst error: " << maxErrorDb << " dB at octave band " << worstFreq << " Hz");

        // SC-001: +/-0.1 dB flat response
        // With correct allpass Q=0.7071 (matching LR4 Butterworth Q), the crossover
        // achieves proper phase alignment and meets the 0.1 dB spec.
        REQUIRE(maxErrorDb < 0.1f);
    }
}

TEST_CASE("CrossoverNetwork pink noise FFT at multiple sample rates", "[crossover][US1][flat][FR-033][SC-007]") {
    // FR-033 + SC-007: Pink noise FFT verification at all sample rates
    // Ensures flat response across 44.1kHz, 48kHz, 96kHz, 192kHz

    constexpr size_t kFFTSize = 8192;
    constexpr size_t kNumBins = kFFTSize / 2 + 1;
    constexpr size_t kNumFrames = 16;
    constexpr double kSettlingTimeMs = 500.0;  // 500ms settling in real time

    const std::array<double, 4> sampleRates = {44100.0, 48000.0, 96000.0, 192000.0};

    for (double sampleRate : sampleRates) {
        INFO("Testing at sample rate: " << sampleRate);

        // Use 4 bands as representative configuration
        Disrumpo::CrossoverNetwork network;
        network.prepare(sampleRate, 4);

        // Generate pink noise
        Krate::DSP::Xorshift32 rng(42);
        Krate::DSP::PinkNoiseFilter pinkFilter;

        // Prepare window
        std::vector<float> window(kFFTSize);
        Krate::DSP::Window::generateHann(window.data(), kFFTSize);

        // Accumulated power spectra
        std::vector<double> inputPower(kNumBins, 0.0);
        std::vector<double> outputPower(kNumBins, 0.0);

        // Let filter settle - scale by sample rate to maintain consistent real-time settling
        const size_t settlingSamples = static_cast<size_t>(kSettlingTimeMs * sampleRate / 1000.0);
        std::array<float, Disrumpo::kMaxBands> bands{};
        for (size_t i = 0; i < settlingSamples; ++i) {
            float pink = pinkFilter.process(rng.nextFloat());
            network.process(pink, bands);
        }

        // FFT processor
        Krate::DSP::FFT fft;
        fft.prepare(kFFTSize);

        std::vector<Krate::DSP::Complex> spectrum(kNumBins);
        std::vector<float> windowedFrame(kFFTSize);

        // Process multiple frames
        for (size_t frameIdx = 0; frameIdx < kNumFrames; ++frameIdx) {
            std::vector<float> inputFrame(kFFTSize);
            std::vector<float> outputFrame(kFFTSize);

            for (size_t i = 0; i < kFFTSize; ++i) {
                float pink = pinkFilter.process(rng.nextFloat());
                inputFrame[i] = pink;

                network.process(pink, bands);
                outputFrame[i] = bands[0] + bands[1] + bands[2] + bands[3];
            }

            // Window and FFT input
            for (size_t i = 0; i < kFFTSize; ++i) {
                windowedFrame[i] = inputFrame[i] * window[i];
            }
            fft.forward(windowedFrame.data(), spectrum.data());

            for (size_t bin = 0; bin < kNumBins; ++bin) {
                float mag = spectrum[bin].magnitude();
                inputPower[bin] += static_cast<double>(mag * mag);
            }

            // Window and FFT output
            for (size_t i = 0; i < kFFTSize; ++i) {
                windowedFrame[i] = outputFrame[i] * window[i];
            }
            fft.forward(windowedFrame.data(), spectrum.data());

            for (size_t bin = 0; bin < kNumBins; ++bin) {
                float mag = spectrum[bin].magnitude();
                outputPower[bin] += static_cast<double>(mag * mag);
            }
        }

        // Compare octave bands (starting from 300 Hz for reliable settling)
        const std::array<float, 5> bandCenters = {300.0f, 600.0f, 1200.0f, 2400.0f, 4800.0f};

        float maxErrorDb = 0.0f;
        float worstFreq = 0.0f;

        for (float centerFreq : bandCenters) {
            // Skip bands above Nyquist/4
            if (centerFreq > sampleRate / 4.0) continue;

            float lowFreq = centerFreq / std::numbers::sqrt2_v<float>;
            float highFreq = centerFreq * std::numbers::sqrt2_v<float>;

            size_t lowBin = static_cast<size_t>(lowFreq * kFFTSize / sampleRate);
            size_t highBin = static_cast<size_t>(highFreq * kFFTSize / sampleRate);

            lowBin = std::max(lowBin, size_t(1));
            highBin = std::min(highBin, kNumBins - 1);

            double inputBandPower = 0.0;
            double outputBandPower = 0.0;

            for (size_t bin = lowBin; bin <= highBin; ++bin) {
                inputBandPower += inputPower[bin];
                outputBandPower += outputPower[bin];
            }

            if (inputBandPower > 1e-12) {
                double ratio = outputBandPower / inputBandPower;
                float errorDb = static_cast<float>(std::abs(10.0 * std::log10(ratio)));

                if (errorDb > maxErrorDb) {
                    maxErrorDb = errorDb;
                    worstFreq = centerFreq;
                }
            }
        }

        INFO("Max error: " << maxErrorDb << " dB at " << worstFreq << " Hz");
        // SC-001: +/-0.1 dB flat response with correct allpass Q
        REQUIRE(maxErrorDb < 0.1f);
    }
}

// The audit probe "crossover:network" (fix plan T6.04): the band sum's impulse response at
// the default logarithmic crossovers, read at 160 log-spaced frequencies 20 Hz - 20 kHz.
// After a 0.3 s pre-roll every band count is within SC-001 (+/-0.1 dB; baseline 0.000 /
// 0.002 / -0.004 dB). From the first sample after prepare() 4 bands stay within 0.5 dB
// (baseline 12.3 dB at 80 Hz while the crossover smoothers glided from 1 kHz, D-024).
TEST_CASE("CrossoverNetwork impulse response sums flat at the default crossovers, from the first sample",
          "[disrumpo][audit][crossover][SC-001][D-024]") {
    constexpr double kSr = 48000.0;
    constexpr size_t kPreRoll = 14400;
    constexpr size_t kIrLength = 32768;
    std::vector<double> freqs(160);
    for (size_t i = 0; i < freqs.size(); ++i) {
        freqs[i] = 20.0 * std::pow(1000.0, static_cast<double>(i) / static_cast<double>(freqs.size() - 1));
    }
    const auto worstDeviationDb = [&](int numBands, size_t impulseAt, double& atHz) {
        Disrumpo::CrossoverNetwork network;
        network.prepare(kSr, numBands);
        std::array<float, Disrumpo::kMaxBands> bands{};
        std::vector<float> h(kIrLength);
        for (size_t i = 0; i < impulseAt + kIrLength; ++i) {
            network.process(i == impulseAt ? 1.0f : 0.0f, bands);
            if (i < impulseAt) continue;
            float sum = 0.0f;
            for (int b = 0; b < numBands; ++b) sum += bands[static_cast<size_t>(b)];
            h[i - impulseAt] = sum;
        }
        double worst = 0.0;
        for (const double f : freqs) {
            const double d = Disrumpo::TestSupport::responseDb(h, f, kSr);
            if (std::fabs(d) > std::fabs(worst)) {
                worst = d;
                atHz = f;
            }
        }
        return worst;
    };
    for (int numBands = 2; numBands <= 4; ++numBands) {
        double atHz = 0.0;
        const double dev = worstDeviationDb(numBands, kPreRoll, atHz);
        std::printf("T6.04 CrossoverNetwork %d bands after 0.3 s: worst deviation %.4f dB at %.0f Hz\n", numBands, dev,
                    atHz);
        INFO(numBands << " bands after a 0.3 s pre-roll: worst deviation " << dev << " dB at " << atHz << " Hz");
        CHECK(std::fabs(dev) <= 0.1);
    }
    double atHz = 0.0;
    const double dev0 = worstDeviationDb(4, 0, atHz);
    std::printf("T6.04 CrossoverNetwork 4 bands from the first sample after prepare: worst deviation %.4f dB at %.0f Hz\n",
                dev0, atHz);
    INFO("4 bands, impulse at the first sample after prepare(): worst deviation " << dev0 << " dB at " << atHz << " Hz");
    CHECK(std::fabs(dev0) <= 0.5);
}

// =============================================================================
// Hidden A/B probe (crossover dedup step 3): new CrossoverNetwork vs the pre-dedup one
// =============================================================================

namespace Disrumpo {
namespace {

// Frozen copy of Disrumpo::CrossoverNetwork before crossover dedup step 3 (commit 12e3a217
// tree), generated for the A/B probe only: allpass Q settable (0.7071f as shipped).
// 4 kAllpassQ uses renamed.
class OldCrossoverNetwork {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxBands = 4;
    static constexpr int kMinBands = 1;
    static constexpr int kDefaultBands = 4;
    static constexpr float kDefaultSmoothingMs = 10.0f;

    /// Q for 2nd-order allpass to match LR4 phase response
    /// LR4 = two cascaded Butterworth (Q=0.7071) filters
    /// The allpass must have the same Q to match the phase curve
    /// Reference: KVR "N-band Linkwitz-Riley crossovers" discussion
    float allpassQ_ = 0.7071f;
    void setAllpassQForProbe(float q) noexcept { allpassQ_ = q; }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    OldCrossoverNetwork() noexcept = default;
    ~OldCrossoverNetwork() noexcept = default;

    // Non-copyable (contains filter state)
    OldCrossoverNetwork(const OldCrossoverNetwork&) = delete;
    OldCrossoverNetwork& operator=(const OldCrossoverNetwork&) = delete;

    // Move operations
    OldCrossoverNetwork(OldCrossoverNetwork&&) noexcept = default;
    OldCrossoverNetwork& operator=(OldCrossoverNetwork&&) noexcept = default;

    // =========================================================================
    // Initialization (FR-003, FR-004)
    // =========================================================================

    /// @brief Initialize for given sample rate and band count.
    /// @param sampleRate Sample rate in Hz
    /// @param numBands Number of bands (1-4)
    void prepare(double sampleRate, int numBands) noexcept {
        sampleRate_ = sampleRate;
        numBands_ = clampBandCount(numBands);
        prepared_ = true;

        // Logarithmic defaults only on the first prepare, and only for
        // crossovers nobody has set. A re-prepare (sample-rate or block-size
        // change) keeps the stored frequencies (D-046)
        if (!frequenciesInitialized_) {
            initializeLogarithmicDistribution();
        }

        // CrossoverLR4::prepare() snaps its frequency smoother to the stored
        // frequency, so set the frequency (clamped at the NEW rate, hence the
        // first prepare) and prepare again: no glide from the previous value
        for (int i = 0; i < kMaxBands - 1; ++i) {
            crossovers_[i].prepare(sampleRate);
            crossovers_[i].setCrossoverFrequency(crossoverFrequencies_[i]);
            crossovers_[i].prepare(sampleRate);
        }

        // Initialize D'Appolito allpass compensation
        updateAllpassCoefficients();
    }

    /// @brief Reset all filter states without reinitialization.
    void reset() noexcept {
        for (auto& crossover : crossovers_) {
            crossover.reset();
        }
        resetAllpasses();
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Change band count dynamically.
    /// Preserves existing crossover positions per FR-011a/FR-011b.
    /// @param numBands New number of bands (1-4)
    void setBandCount(int numBands) noexcept {
        const int newBandCount = clampBandCount(numBands);
        if (newBandCount == numBands_) {
            return;
        }

        const int oldBandCount = numBands_;
        numBands_ = newBandCount;

        // Redistribute crossovers per FR-011a/FR-011b
        redistributeCrossovers(oldBandCount, newBandCount);

        // Crossovers and allpasses joining the chain start clean at their frequency:
        // never the state they froze with when last used, nor a glide from a stale
        // frequency (D-023). CrossoverLR4::prepare only snaps the smoother, sets the
        // coefficients and clears the state: no allocation.
        if (newBandCount > oldBandCount && prepared_) {
            const int firstNew = std::max(oldBandCount - 1, 0);
            for (int i = firstNew; i < newBandCount - 1; ++i) {
                crossovers_[i].prepare(sampleRate_);
            }
            for (int band = 0; band < kMaxBands - 2; ++band) {
                for (int apIdx = std::max(band + 1, firstNew); apIdx < kMaxBands - 1; ++apIdx) {
                    allpasses_[band][apIdx - band - 1].reset();
                }
            }
            for (int i = firstNew; i < kMaxBands - 1; ++i) {
                dryAllpasses_[i].reset();
            }
        }
    }

    /// @brief Set crossover frequency for a specific split point.
    /// The value is stored for every index, including crossovers the current
    /// band count does not use, so a later band-count increase finds it (D-022).
    /// @param index Crossover index (0 to kMaxBands-2)
    /// @param hz Frequency in Hz
    void setCrossoverFrequency(int index, float hz) noexcept {
        if (index < 0 || index >= kMaxBands - 1) {
            return;
        }

        const float clamped = clampFrequency(hz);
        crossoverFrequencies_[index] = clamped;
        frequencyWasSet_[index] = true;
        crossovers_[index].setCrossoverFrequency(clamped);

        // Update allpass filters that use this crossover frequency
        // Crossover index i is used as allpass for bands 0..(i-1) and for the dry path
        if (prepared_) {
            const float srFloat = static_cast<float>(sampleRate_);
            dryAllpasses_[index].configure(
                Krate::DSP::FilterType::Allpass,
                clamped,
                allpassQ_,
                0.0f,
                srFloat
            );
            for (int band = 0; band < index; ++band) {
                const int apIdx = index - band - 1;
                allpasses_[band][apIdx].configure(
                    Krate::DSP::FilterType::Allpass,
                    clamped,
                    allpassQ_,
                    0.0f,
                    srFloat
                );
            }
        }
    }

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Get current band count.
    [[nodiscard]] int getBandCount() const noexcept {
        return numBands_;
    }

    /// @brief Get crossover frequency at index.
    [[nodiscard]] float getCrossoverFrequency(int index) const noexcept {
        if (index < 0 || index >= kMaxBands - 1) {
            return 1000.0f; // Default
        }
        return crossoverFrequencies_[index];
    }

    /// @brief Check if prepare() has been called.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Processing (FR-001a, FR-012, FR-013, FR-014, SC-001)
    // =========================================================================

    /// @brief Process single sample, output to band array.
    /// For 1 band: passes input directly to bands[0].
    /// For N bands: cascaded split with D'Appolito allpass compensation.
    /// SC-001: Achieves +/-0.1dB flat frequency response when bands summed.
    /// @param input Input sample
    /// @param bands Output array (uses first numBands_ elements)
    void process(float input, std::array<float, kMaxBands>& bands) noexcept {
        if (!prepared_) {
            // Output zeros if not prepared
            for (int i = 0; i < kMaxBands; ++i) {
                bands[i] = 0.0f;
            }
            return;
        }

        // FR-014: For 1 band, pass input directly
        if (numBands_ == 1) {
            bands[0] = input;
            return;
        }

        // FR-012: Cascaded band splitting with D'Appolito allpass compensation
        // Input -> Split1 -> (Band0, Remainder) -> Split2 -> (Band1, Remainder) -> ...
        //
        // Phase compensation pattern:
        // Band 0: LP(f0) * AP(f1) * AP(f2) * ... * AP(fN-2)
        // Band 1: HP(f0) * LP(f1) * AP(f2) * ... * AP(fN-2)
        // Band k: HP(f0..fk-1) * LP(fk) * AP(fk+1) * ... * AP(fN-2)
        // Band N-1: HP(f0) * HP(f1) * ... * HP(fN-2)

        float remainder = input;

        for (int i = 0; i < numBands_ - 1; ++i) {
            auto outputs = crossovers_[i].process(remainder);
            bands[i] = outputs.low;       // Low band goes to output
            remainder = outputs.high;     // High band continues down the chain
        }

        // Last band gets the final remainder (no allpass needed)
        bands[numBands_ - 1] = remainder;

        // Apply D'Appolito allpass compensation to each band
        // Band k needs allpasses at frequencies f[k+1], f[k+2], ..., f[N-2]
        for (int band = 0; band < numBands_ - 1; ++band) {
            // Apply allpasses for crossovers higher than this band's split
            for (int apIdx = band + 1; apIdx < numBands_ - 1; ++apIdx) {
                bands[band] = allpasses_[band][apIdx - band - 1].process(bands[band]);
            }
        }
    }

    /// @brief Give a dry (unsplit) sample the phase of the summed bands (D-018).
    /// The clean band sum is AP(f0) * AP(f1) * ... * AP(fN-2) * x (each LR4
    /// LP + HP pair is a 2nd-order allpass at its crossover), so a dry signal
    /// mixed with the wet sum must run through the same allpass chain or every
    /// crossover notches. Call once per sample, alongside process().
    /// @param x Dry input sample
    /// @return x through the allpasses at every active crossover frequency
    [[nodiscard]] float processDryCompensation(float x) noexcept {
        if (!prepared_ || numBands_ == 1) {
            return x;
        }
        for (int i = 0; i < numBands_ - 1; ++i) {
            x = dryAllpasses_[i].process(x);
        }
        return x;
    }

private:
    // =========================================================================
    // Helper Functions
    // =========================================================================

    /// @brief Clamp band count to valid range.
    [[nodiscard]] static int clampBandCount(int numBands) noexcept {
        if (numBands < kMinBands) return kMinBands;
        if (numBands > kMaxBands) return kMaxBands;
        return numBands;
    }

    /// @brief Clamp frequency to valid range.
    [[nodiscard]] float clampFrequency(float freq) const noexcept {
        const float maxFreq = static_cast<float>(sampleRate_) * 0.45f;
        if (freq < kMinCrossoverHz) return kMinCrossoverHz;
        if (freq > maxFreq) return maxFreq;
        if (freq > kMaxCrossoverHz) return kMaxCrossoverHz;
        return freq;
    }

    /// @brief Initialize crossover frequencies with logarithmic distribution.
    /// FR-009: Crossover frequencies redistribute logarithmically across 20Hz-20kHz
    /// Crossovers already set through setCrossoverFrequency() keep their value.
    void initializeLogarithmicDistribution() noexcept {
        if (numBands_ <= 1) {
            return;
        }

        // Logarithmic distribution from 20Hz to 20kHz
        const float logMin = std::log10(kMinCrossoverHz);      // ~1.301
        const float logMax = std::log10(kMaxCrossoverHz);      // ~4.301
        const float step = (logMax - logMin) / static_cast<float>(numBands_);

        for (int i = 0; i < numBands_ - 1; ++i) {
            if (frequencyWasSet_[i]) {
                continue;
            }
            const float logFreq = logMin + step * static_cast<float>(i + 1);
            const float freq = std::pow(10.0f, logFreq);
            crossoverFrequencies_[i] = clampFrequency(freq);
            crossovers_[i].setCrossoverFrequency(crossoverFrequencies_[i]);
        }
        frequenciesInitialized_ = true;
    }

    /// @brief Redistribute crossovers when band count changes.
    /// FR-011a: Preserve existing crossovers when increasing
    /// FR-011b: Preserve lowest N-1 crossovers when decreasing
    void redistributeCrossovers(int oldBandCount, int newBandCount) noexcept {
        if (newBandCount <= 1) {
            // No crossovers needed for 1 band
            return;
        }

        if (oldBandCount <= 1) {
            // Was 1 band, now more: logarithmic defaults only if no
            // distribution exists yet; otherwise keep the stored values (FR-011a)
            if (!frequenciesInitialized_) {
                initializeLogarithmicDistribution();
            }
            updateAllpassCoefficients();
            return;
        }

        if (newBandCount > oldBandCount) {
            // FR-011a: Increasing - preserve existing, fill the free indices
            redistributeIncreasing(oldBandCount, newBandCount);
            updateAllpassCoefficients();
        } else {
            // FR-011b: Decreasing - keep lowest N-1 crossovers
            // The existing frequencies stay in place, we just use fewer
            // Update allpasses for new band configuration
            updateAllpassCoefficients();
        }
    }

    /// @brief Fill the crossovers a band-count increase adds (FR-011a, D-023).
    /// Running crossovers keep their index and frequency, so crossover i stays the
    /// split the crossover parameter i sets and a later decrease returns to the same
    /// split. A joining crossover set explicitly while it was idle (state restore or
    /// automation) keeps that value. Each run of new ones splits the gap between the
    /// crossover below and the next set one above (else 20 kHz) at even log spacing.
    void redistributeIncreasing(int oldBandCount, int newBandCount) noexcept {
        const int used = newBandCount - 1;
        int i = std::max(oldBandCount - 1, 0);
        while (i < used) {
            if (frequencyWasSet_[i]) {
                ++i;
                continue;
            }
            int end = i;
            while (end < used && !frequencyWasSet_[end]) {
                ++end;
            }
            const float lower = (i == 0) ? kMinCrossoverHz : crossoverFrequencies_[i - 1];
            const float upper = (end < used) ? crossoverFrequencies_[end] : kMaxCrossoverHz;
            const auto parts = static_cast<float>(end - i + 1);
            for (int k = i; k < end; ++k) {
                const float frac = static_cast<float>(k - i + 1) / parts;
                crossoverFrequencies_[k] = clampFrequency(lower * std::pow(upper / lower, frac));
                crossovers_[k].setCrossoverFrequency(crossoverFrequencies_[k]);
            }
            i = end;
        }
    }

    // =========================================================================
    // Allpass Helpers
    // =========================================================================

    /// @brief Update all allpass filter coefficients.
    /// Called after crossover frequencies change.
    void updateAllpassCoefficients() noexcept {
        const float srFloat = static_cast<float>(sampleRate_);

        // For each band, configure allpasses at higher crossover frequencies
        // Band k needs allpasses at f[k+1], f[k+2], ..., f[N-2]
        for (int band = 0; band < kMaxBands - 2; ++band) {
            for (int apIdx = band + 1; apIdx < kMaxBands - 1; ++apIdx) {
                const float freq = crossoverFrequencies_[apIdx];
                allpasses_[band][apIdx - band - 1].configure(
                    Krate::DSP::FilterType::Allpass,
                    clampFrequency(freq),
                    allpassQ_,
                    0.0f,
                    srFloat
                );
            }
        }

        // Dry-path compensation: one allpass at every crossover frequency
        for (int i = 0; i < kMaxBands - 1; ++i) {
            dryAllpasses_[i].configure(
                Krate::DSP::FilterType::Allpass,
                clampFrequency(crossoverFrequencies_[i]),
                allpassQ_,
                0.0f,
                srFloat
            );
        }
    }

    /// @brief Reset all allpass filter states.
    void resetAllpasses() noexcept {
        for (auto& bandAllpasses : allpasses_) {
            for (auto& ap : bandAllpasses) {
                ap.reset();
            }
        }
        for (auto& ap : dryAllpasses_) {
            ap.reset();
        }
    }

    // =========================================================================
    // Members
    // =========================================================================

    double sampleRate_ = 44100.0;
    int numBands_ = kDefaultBands;
    bool prepared_ = false;

    // N-1 crossovers for N bands
    std::array<Krate::DSP::CrossoverLR4, kMaxBands - 1> crossovers_;

    // D'Appolito allpass compensation filters
    // allpasses_[band][apIdx] is the allpass for band 'band' at crossover 'band + apIdx + 1'
    // Band 0 needs up to 2 allpasses (at f1..f2 for 4 bands)
    // Band 1 needs up to 1 allpass (at f2)
    // Band 2 and 3 need no allpasses
    // Maximum allpasses per band = kMaxBands - 2 = 2
    std::array<std::array<Krate::DSP::Biquad, kMaxBands - 2>, kMaxBands - 2> allpasses_;

    // Dry-path allpasses: dryAllpasses_[i] at crossover i (processDryCompensation)
    std::array<Krate::DSP::Biquad, kMaxBands - 1> dryAllpasses_;

    // Target frequencies (for redistribution logic)
    std::array<float, kMaxBands - 1> crossoverFrequencies_ = {
        200.0f, 1500.0f, 6000.0f
    };

    // Crossovers given a value through setCrossoverFrequency()
    std::array<bool, kMaxBands - 1> frequencyWasSet_{};

    // True once a logarithmic distribution has been computed (first prepare
    // with more than one band); later prepares keep the stored frequencies
    bool frequenciesInitialized_ = false;
};

} // namespace
} // namespace Disrumpo

// Hidden A/B probe (crossover dedup step 3): the MultibandCrossover-based CrossoverNetwork
// against the frozen pre-dedup copy above, on 1 s of white noise from fresh instances, and
// on a 48 kHz run with a crossover move and band-count changes. The old copy runs with its
// shipped allpass Q 0.7071f and, as a control, with the new 1/sqrt(2): the control isolates
// the Q change (ruling 1) from any structural difference. Prints, asserts nothing.
TEST_CASE("CrossoverNetwork vs the pre-dedup network (A/B probe)", "[.][xover-dedup-network-ab-probe]") {
    struct Diff {
        double bandMax = 0.0;
        double sumMax = 0.0;
        double dryMax = 0.0;
        double sumPow = 0.0;
        double sumDiffPow = 0.0;
        double dryDiffPow = 0.0;
    };
    const auto run = [](Disrumpo::OldCrossoverNetwork& oldNet, Disrumpo::CrossoverNetwork& newNet,
                        const std::vector<float>& x, const auto& onSample) {
        Diff d;
        std::array<float, Disrumpo::kMaxBands> bo{};
        std::array<float, Disrumpo::kMaxBands> bn{};
        for (size_t i = 0; i < x.size(); ++i) {
            onSample(i);
            oldNet.process(x[i], bo);
            newNet.process(x[i], bn);
            const float dryOld = oldNet.processDryCompensation(x[i]);
            const float dryNew = newNet.processDryCompensation(x[i]);
            const int n = newNet.getBandCount();
            double so = 0.0;
            double sn = 0.0;
            for (int b = 0; b < n; ++b) {
                const auto k = static_cast<size_t>(b);
                d.bandMax = std::max(d.bandMax, static_cast<double>(std::fabs(bo[k] - bn[k])));
                so += static_cast<double>(bo[k]);
                sn += static_cast<double>(bn[k]);
            }
            d.sumMax = std::max(d.sumMax, std::fabs(so - sn));
            d.dryMax = std::max(d.dryMax, static_cast<double>(std::fabs(dryOld - dryNew)));
            d.sumPow += so * so;
            d.sumDiffPow += (so - sn) * (so - sn);
            d.dryDiffPow += static_cast<double>(dryOld - dryNew) * static_cast<double>(dryOld - dryNew);
        }
        return d;
    };
    const auto print = [](const char* label, const Diff& d) {
        std::printf("  %-44s max|band diff| %.3e  band sum: max|diff| %.3e, diff %7.1f dB re sum | dry: max|diff| "
                    "%.3e, diff %7.1f dB re sum\n",
                    label, d.bandMax, d.sumMax, 10.0 * std::log10(d.sumDiffPow / d.sumPow + 1e-300), d.dryMax,
                    10.0 * std::log10(d.dryDiffPow / d.sumPow + 1e-300));
    };

    std::printf("\n=== CrossoverNetwork new vs pre-dedup copy, 1 s white noise (+/-0.5), fresh instances ===\n");
    for (const double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
        std::vector<float> x(static_cast<size_t>(sr));
        Krate::DSP::Xorshift32 rng(1234);
        for (auto& v : x) v = 0.5f * rng.nextFloat();
        for (const int bands : {2, 3, 4}) {
            for (const bool controlQ : {false, true}) {
                Disrumpo::OldCrossoverNetwork oldNet;
                if (controlQ) oldNet.setAllpassQForProbe(Krate::DSP::MultibandCrossover<Disrumpo::kMaxBands>::kAllpassQ);
                oldNet.prepare(sr, bands);
                Disrumpo::CrossoverNetwork newNet;
                newNet.prepare(sr, bands);
                const Diff d = run(oldNet, newNet, x, [](size_t) {});
                std::array<char, 64> label{};
                std::snprintf(label.data(), label.size(), "%6.0f Hz, %d bands, old Q %s", sr, bands,
                              controlQ ? "1/sqrt(2) (control)" : "0.7071f (shipped)");
                print(label.data(), d);
            }
        }
    }

    std::printf("\n=== 48 kHz, 4 bands: crossover 1 -> 1.2 kHz at 0.25 s, 2 bands at 0.5 s, 4 bands at 0.75 s, "
                "re-prepare at 96 kHz between two 1 s runs ===\n");
    for (const bool controlQ : {false, true}) {
        Disrumpo::OldCrossoverNetwork oldNet;
        if (controlQ) oldNet.setAllpassQForProbe(Krate::DSP::MultibandCrossover<Disrumpo::kMaxBands>::kAllpassQ);
        Disrumpo::CrossoverNetwork newNet;
        oldNet.prepare(48000.0, 4);
        newNet.prepare(48000.0, 4);
        std::vector<float> x(48000);
        Krate::DSP::Xorshift32 rng(99);
        for (auto& v : x) v = 0.5f * rng.nextFloat();
        const Diff d1 = run(oldNet, newNet, x, [&](size_t i) {
            if (i == 12000) { oldNet.setCrossoverFrequency(1, 1200.0f); newNet.setCrossoverFrequency(1, 1200.0f); }
            if (i == 24000) { oldNet.setBandCount(2); newNet.setBandCount(2); }
            if (i == 36000) { oldNet.setBandCount(4); newNet.setBandCount(4); }
        });
        print(controlQ ? "changes, old Q 1/sqrt(2) (control)" : "changes, old Q 0.7071f (shipped)", d1);
        oldNet.prepare(96000.0, 4);
        newNet.prepare(96000.0, 4);
        std::vector<float> y(96000);
        for (auto& v : y) v = 0.5f * rng.nextFloat();
        const Diff d2 = run(oldNet, newNet, y, [](size_t) {});
        print(controlQ ? "after re-prepare, old Q 1/sqrt(2) (control)" : "after re-prepare, old Q 0.7071f", d2);
    }
    std::fflush(stdout);
    SUCCEED();
}
