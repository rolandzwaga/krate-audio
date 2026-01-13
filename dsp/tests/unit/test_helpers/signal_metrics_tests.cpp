// ==============================================================================
// Unit Tests: Signal Quality Metrics
// ==============================================================================
// Tests for SNR, THD, crest factor, kurtosis, ZCR, and spectral flatness.
//
// Constitution Compliance:
// - Principle XII: Test-First Development (tests written FIRST)
// - Principle VIII: Testing Discipline
//
// Reference: specs/055-artifact-detection/spec.md
// Requirements: FR-005, FR-006, FR-007, FR-008, FR-010, FR-011
// Success Criteria: SC-003, SC-004
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "signal_metrics.h"
#include "test_signals.h"

#include <array>
#include <cmath>
#include <vector>

using namespace Krate::DSP::TestUtils;
using Catch::Approx;

// =============================================================================
// T023: calculateSNR() Tests
// =============================================================================

TEST_CASE("SignalMetrics::calculateSNR - Signal-to-Noise Ratio", "[signal-metrics][snr]") {
    SECTION("SC-003: SNR accuracy within 0.5 dB for known reference") {
        // Generate reference sine wave
        std::vector<float> reference(4096, 0.0f);
        TestHelpers::generateSine(reference.data(), reference.size(), 440.0f, 44100.0f, 0.5f);

        // Create signal with known noise level
        // Add noise at -40 dB relative to signal
        // Signal power = 0.5^2 / 2 = 0.125 (RMS of sine = amplitude / sqrt(2))
        // Noise power should be signal_power * 10^(-40/10) = 0.125 * 0.0001 = 0.0000125
        std::vector<float> signal = reference;
        TestHelpers::generateWhiteNoise(signal.data(), signal.size(), 42);

        // Scale noise to desired level
        const float signalRms = 0.5f / std::sqrt(2.0f);
        const float desiredNoiseDb = -40.0f;
        const float noiseScale = signalRms * std::pow(10.0f, desiredNoiseDb / 20.0f);

        std::vector<float> noise(4096, 0.0f);
        TestHelpers::generateWhiteNoise(noise.data(), noise.size(), 42);

        // Normalize noise and scale
        float noiseRms = 0.0f;
        for (float n : noise) {
            noiseRms += n * n;
        }
        noiseRms = std::sqrt(noiseRms / static_cast<float>(noise.size()));

        for (size_t i = 0; i < signal.size(); ++i) {
            signal[i] = reference[i] + (noise[i] / noiseRms) * noiseScale;
        }

        const float snr = SignalMetrics::calculateSNR(
            signal.data(), reference.data(), signal.size());

        // Expected SNR is approximately 40 dB
        INFO("Measured SNR: " << snr << " dB, expected ~40 dB");
        REQUIRE(snr == Approx(40.0f).margin(0.5f));
    }

    SECTION("pure signal vs itself has very high SNR") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.5f);

        const float snr = SignalMetrics::calculateSNR(
            signal.data(), signal.data(), signal.size());

        // Identical signals should have very high SNR (limited by floating point precision)
        REQUIRE(snr > 100.0f);
    }

    SECTION("SNR with known noise level") {
        std::vector<float> reference(4096, 0.0f);
        TestHelpers::generateSine(reference.data(), reference.size(), 1000.0f, 44100.0f, 1.0f);

        // Add small noise
        std::vector<float> signal = reference;
        for (size_t i = 0; i < signal.size(); ++i) {
            signal[i] += 0.001f * (static_cast<float>(i % 100) / 100.0f - 0.5f);
        }

        const float snr = SignalMetrics::calculateSNR(
            signal.data(), reference.data(), signal.size());

        // Should be high but not infinite
        REQUIRE(snr > 40.0f);
        REQUIRE(snr < 200.0f);
    }
}

// =============================================================================
// T024: calculateTHD() Tests
// =============================================================================

TEST_CASE("SignalMetrics::calculateTHD - Total Harmonic Distortion", "[signal-metrics][thd]") {
    SECTION("SC-004: THD accuracy within 1% for known harmonic content") {
        // Generate a signal with known harmonics
        // Fundamental + 2nd harmonic at 10% = THD = 10%
        std::vector<float> signal(4096, 0.0f);
        const float sampleRate = 44100.0f;
        const float fundamental = 1000.0f;

        // Fundamental at amplitude 1.0
        TestHelpers::generateSine(signal.data(), signal.size(), fundamental, sampleRate, 1.0f);

        // Add 2nd harmonic at 10% amplitude (0.1)
        std::vector<float> harmonic(4096, 0.0f);
        TestHelpers::generateSine(harmonic.data(), harmonic.size(), fundamental * 2.0f, sampleRate, 0.1f);

        for (size_t i = 0; i < signal.size(); ++i) {
            signal[i] += harmonic[i];
        }

        const float thd = SignalMetrics::calculateTHD(
            signal.data(), signal.size(), fundamental, sampleRate);

        INFO("Measured THD: " << thd << "%, expected ~10%");
        REQUIRE(thd == Approx(10.0f).margin(1.0f));
    }

    SECTION("pure sine wave has low THD") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 1000.0f, 44100.0f, 0.5f);

        const float thd = SignalMetrics::calculateTHD(
            signal.data(), signal.size(), 1000.0f, 44100.0f);

        // Pure sine should have very low THD (< 0.1%)
        REQUIRE(thd < 0.1f);
    }

    SECTION("hard clipper at 4x drive shows THD > 10%") {
        // Input amplitude = 4.0, hard clipped to [-1, 1]
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 1000.0f, 44100.0f, 4.0f);

        // Apply hard clipping
        for (auto& s : signal) {
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;
        }

        const float thd = SignalMetrics::calculateTHD(
            signal.data(), signal.size(), 1000.0f, 44100.0f);

        INFO("Hard clipper THD: " << thd << "%");
        REQUIRE(thd > 10.0f);
    }
}

// =============================================================================
// T025: calculateCrestFactor() Tests
// =============================================================================

TEST_CASE("SignalMetrics::calculateCrestFactor - Peak-to-RMS Ratio", "[signal-metrics][crest-factor]") {
    SECTION("sine wave crest factor is approximately 3 dB") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 1.0f);

        const float crestDb = SignalMetrics::calculateCrestFactorDb(
            signal.data(), signal.size());

        // Sine wave: peak = 1.0, RMS = 1/sqrt(2) ~= 0.707
        // Crest factor = peak / RMS = sqrt(2) ~= 1.414
        // In dB: 20 * log10(sqrt(2)) ~= 3.01 dB
        REQUIRE(crestDb == Approx(3.01f).margin(0.1f));
    }

    SECTION("square wave crest factor is approximately 0 dB") {
        // Square wave: peak = RMS = amplitude
        std::vector<float> signal(4096, 0.0f);
        for (size_t i = 0; i < signal.size(); ++i) {
            signal[i] = (i % 100 < 50) ? 1.0f : -1.0f;
        }

        const float crestDb = SignalMetrics::calculateCrestFactorDb(
            signal.data(), signal.size());

        // Square wave: peak = 1.0, RMS = 1.0
        // Crest factor = 1.0, in dB = 0 dB
        REQUIRE(crestDb == Approx(0.0f).margin(0.1f));
    }

    SECTION("window with click shows crest factor > 20 dB") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.01f);

        // Insert a click with amplitude 1.0
        signal[1000] = 1.0f;

        const float crestDb = SignalMetrics::calculateCrestFactorDb(
            signal.data(), signal.size());

        // With a single large spike and small background signal, crest factor should be very high
        INFO("Crest factor with click: " << crestDb << " dB");
        REQUIRE(crestDb > 20.0f);
    }
}

// =============================================================================
// T026: calculateKurtosis() Tests
// =============================================================================

TEST_CASE("SignalMetrics::calculateKurtosis - Distribution Shape", "[signal-metrics][kurtosis]") {
    SECTION("excess kurtosis approximately 0 for normal-like distribution") {
        // Generate approximately normal distribution using sum of uniform distributions
        // (Central Limit Theorem)
        std::vector<float> signal(10000, 0.0f);
        for (int j = 0; j < 12; ++j) {
            std::vector<float> uniform(10000, 0.0f);
            TestHelpers::generateWhiteNoise(uniform.data(), uniform.size(), static_cast<uint32_t>(42 + j));
            for (size_t i = 0; i < signal.size(); ++i) {
                signal[i] += uniform[i];
            }
        }
        // Scale by sqrt(12) / 12 to get approximately unit variance
        for (auto& s : signal) {
            s /= std::sqrt(12.0f);
        }

        const float kurtosis = SignalMetrics::calculateKurtosis(
            signal.data(), signal.size());

        // Excess kurtosis of normal distribution is 0
        INFO("Kurtosis of pseudo-normal: " << kurtosis);
        REQUIRE(kurtosis == Approx(0.0f).margin(0.5f));
    }

    SECTION("high kurtosis for impulsive signals") {
        std::vector<float> signal(10000, 0.0f);
        // Mostly zeros with a few spikes (heavy-tailed)
        signal[1000] = 10.0f;
        signal[3000] = -10.0f;
        signal[5000] = 10.0f;
        signal[7000] = -10.0f;

        const float kurtosis = SignalMetrics::calculateKurtosis(
            signal.data(), signal.size());

        // Impulsive signals have very high kurtosis (heavy tails)
        INFO("Kurtosis of impulsive signal: " << kurtosis);
        REQUIRE(kurtosis > 10.0f);
    }

    SECTION("uniform distribution has kurtosis approximately -1.2") {
        std::vector<float> signal(10000, 0.0f);
        TestHelpers::generateWhiteNoise(signal.data(), signal.size(), 42);

        const float kurtosis = SignalMetrics::calculateKurtosis(
            signal.data(), signal.size());

        // Uniform distribution excess kurtosis = -6/5 = -1.2
        INFO("Kurtosis of uniform: " << kurtosis);
        REQUIRE(kurtosis == Approx(-1.2f).margin(0.2f));
    }
}

// =============================================================================
// T027: calculateZCR() Tests
// =============================================================================

TEST_CASE("SignalMetrics::calculateZCR - Zero-Crossing Rate", "[signal-metrics][zcr]") {
    SECTION("ZCR increases with frequency") {
        std::vector<float> signal1k(4096, 0.0f);
        std::vector<float> signal10k(4096, 0.0f);
        TestHelpers::generateSine(signal1k.data(), signal1k.size(), 1000.0f, 44100.0f, 0.5f);
        TestHelpers::generateSine(signal10k.data(), signal10k.size(), 10000.0f, 44100.0f, 0.5f);

        const float zcr1k = SignalMetrics::calculateZCR(signal1k.data(), signal1k.size());
        const float zcr10k = SignalMetrics::calculateZCR(signal10k.data(), signal10k.size());

        INFO("ZCR 1kHz: " << zcr1k << ", ZCR 10kHz: " << zcr10k);
        REQUIRE(zcr10k > zcr1k * 5.0f);  // 10x frequency = ~10x ZCR
    }

    SECTION("ZCR approximately 0 for DC signal") {
        std::vector<float> signal(4096, 0.5f);  // DC at 0.5

        const float zcr = SignalMetrics::calculateZCR(signal.data(), signal.size());

        REQUIRE(zcr == Approx(0.0f).margin(0.001f));
    }

    SECTION("ZCR for 1kHz sine at 44.1kHz") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 1000.0f, 44100.0f, 0.5f);

        const float zcr = SignalMetrics::calculateZCR(signal.data(), signal.size());

        // ZCR for sine wave = 2 * frequency / sampleRate
        // For 1kHz at 44.1kHz: 2 * 1000 / 44100 ~= 0.0453
        INFO("ZCR for 1kHz sine: " << zcr);
        REQUIRE(zcr == Approx(0.0453f).margin(0.005f));
    }
}

// =============================================================================
// T028: calculateSpectralFlatness() Tests
// =============================================================================

TEST_CASE("SignalMetrics::calculateSpectralFlatness - Spectral Shape", "[signal-metrics][spectral-flatness]") {
    SECTION("pure sine has flatness less than 0.1") {
        std::vector<float> signal(1024, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 1000.0f, 44100.0f, 0.5f);

        const float flatness = SignalMetrics::calculateSpectralFlatness(
            signal.data(), signal.size(), 44100.0f);

        INFO("Spectral flatness of sine: " << flatness);
        REQUIRE(flatness < 0.1f);
    }

    SECTION("white noise has flatness approaching 1.0") {
        std::vector<float> signal(4096, 0.0f);
        TestHelpers::generateWhiteNoise(signal.data(), signal.size(), 42);

        const float flatness = SignalMetrics::calculateSpectralFlatness(
            signal.data(), signal.size(), 44100.0f);

        INFO("Spectral flatness of white noise: " << flatness);
        REQUIRE(flatness > 0.8f);
    }

    SECTION("signal with click has elevated flatness") {
        std::vector<float> signal(1024, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f, 0.2f);

        // Insert click
        signal[512] = 1.0f;

        const float flatness = SignalMetrics::calculateSpectralFlatness(
            signal.data(), signal.size(), 44100.0f);

        // Click adds broadband energy, increasing flatness
        INFO("Spectral flatness with click: " << flatness);
        REQUIRE(flatness > 0.3f);
    }
}

// =============================================================================
// T029: measureQuality() Aggregate Function Tests
// =============================================================================

TEST_CASE("SignalMetrics::measureQuality - Aggregate metrics", "[signal-metrics][quality]") {
    SECTION("SignalQualityMetrics isValid() returns true for valid metrics") {
        SignalQualityMetrics metrics{
            .snrDb = 60.0f,
            .thdPercent = 0.5f,
            .thdDb = -46.0f,
            .crestFactorDb = 3.0f,
            .kurtosis = -1.2f
        };
        REQUIRE(metrics.isValid());
    }

    SECTION("SignalQualityMetrics isValid() returns false for NaN") {
        SignalQualityMetrics metrics{
            .snrDb = std::numeric_limits<float>::quiet_NaN(),
            .thdPercent = 0.5f,
            .thdDb = -46.0f,
            .crestFactorDb = 3.0f,
            .kurtosis = -1.2f
        };
        REQUIRE_FALSE(metrics.isValid());
    }

    SECTION("measureQuality computes all metrics") {
        std::vector<float> signal(4096, 0.0f);
        std::vector<float> reference(4096, 0.0f);
        TestHelpers::generateSine(signal.data(), signal.size(), 1000.0f, 44100.0f, 0.5f);
        TestHelpers::generateSine(reference.data(), reference.size(), 1000.0f, 44100.0f, 0.5f);

        const auto metrics = SignalMetrics::measureQuality(
            signal.data(), reference.data(), signal.size(), 1000.0f, 44100.0f);

        REQUIRE(metrics.isValid());
        REQUIRE(metrics.snrDb > 50.0f);      // Should be high (identical signals)
        REQUIRE(metrics.thdPercent < 1.0f);   // Should be low (pure sine)
        REQUIRE(metrics.crestFactorDb == Approx(3.01f).margin(0.5f));
    }
}
