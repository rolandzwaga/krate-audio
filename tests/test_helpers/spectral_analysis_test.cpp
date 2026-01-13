// ==============================================================================
// Unit Tests: Spectral Analysis Test Utilities
// ==============================================================================
// Tests for FFT-based aliasing measurement utilities.
//
// Constitution Principle XII: Test-First Development
// - Tests written BEFORE implementation
//
// Reference: specs/054-spectral-test-utils/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <spectral_analysis.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;

// ==============================================================================
// Test Tags
// ==============================================================================
// [spectral]   - All spectral analysis tests
// [test_utils] - Test utility infrastructure
// [US1]        - User Story 1: Helper functions
// [US2]        - User Story 2: Bin collection
// [US3]        - User Story 3: Main measurement
// [US4]        - User Story 4: Comparison utility

// ==============================================================================
// Placeholder test to verify build works
// ==============================================================================

TEST_CASE("Spectral analysis header compiles", "[spectral][test_utils]") {
    // This test just verifies the header can be included
    REQUIRE(true);
}

// ==============================================================================
// Phase 2: User Story 1 - Helper Functions (T006-T010)
// ==============================================================================

using namespace Krate::DSP::TestUtils;

// T006: frequencyToBin with known values
TEST_CASE("frequencyToBin returns correct bin for 1kHz at 44.1kHz FFT 2048", "[spectral][US1]") {
    // 1kHz at 44100Hz sample rate with FFT size 2048
    // Bin = round(freqHz * fftSize / sampleRate) = round(1000 * 2048 / 44100) = round(46.44) = 46
    const size_t bin = frequencyToBin(1000.0f, 44100.0f, 2048);
    REQUIRE(bin == 46);
}

// T007: frequencyToBin edge cases
TEST_CASE("frequencyToBin handles edge cases", "[spectral][US1]") {
    SECTION("DC (0 Hz) maps to bin 0") {
        REQUIRE(frequencyToBin(0.0f, 44100.0f, 2048) == 0);
    }

    SECTION("Nyquist maps to bin N/2") {
        // Nyquist = 22050 Hz, bin = round(22050 * 2048 / 44100) = round(1024) = 1024
        REQUIRE(frequencyToBin(22050.0f, 44100.0f, 2048) == 1024);
    }

    SECTION("Rounding works correctly") {
        // 1000 Hz: round(1000 * 2048 / 44100) = round(46.44) = 46
        REQUIRE(frequencyToBin(1000.0f, 44100.0f, 2048) == 46);
        // 1100 Hz: round(1100 * 2048 / 44100) = round(51.09) = 51
        REQUIRE(frequencyToBin(1100.0f, 44100.0f, 2048) == 51);
    }
}

// T008: calculateAliasedFrequency with spec example
TEST_CASE("calculateAliasedFrequency with spec example 5kHz harmonic 5 at 44.1kHz", "[spectral][US1]") {
    // From spec: harmonic 5 of 5kHz = 25kHz, which folds to 44100 - 25000 = 19100 Hz
    const float aliased = calculateAliasedFrequency(5000.0f, 5, 44100.0f);
    REQUIRE(aliased == Approx(19100.0f).margin(1.0f));
}

// T009: calculateAliasedFrequency for non-aliasing case
TEST_CASE("calculateAliasedFrequency returns original for non-aliasing harmonic", "[spectral][US1]") {
    // Harmonic 4 of 5kHz = 20kHz, which is below Nyquist (22050 Hz), no aliasing
    const float freq = calculateAliasedFrequency(5000.0f, 4, 44100.0f);
    REQUIRE(freq == Approx(20000.0f).margin(1.0f));
}

// T010: willAlias returns correct boolean
TEST_CASE("willAlias returns true for harmonic 5 false for harmonic 4", "[spectral][US1]") {
    // 5kHz fundamental at 44.1kHz
    // Harmonic 4: 20kHz < 22050 Hz (Nyquist) -> false
    // Harmonic 5: 25kHz > 22050 Hz (Nyquist) -> true
    REQUIRE(willAlias(5000.0f, 4, 44100.0f) == false);
    REQUIRE(willAlias(5000.0f, 5, 44100.0f) == true);
}

// ==============================================================================
// Phase 3: User Story 2 - Bin Collection Functions (T017-T022)
// ==============================================================================

// T017: AliasingTestConfig::isValid()
TEST_CASE("AliasingTestConfig isValid returns true for valid config", "[spectral][US2]") {
    AliasingTestConfig config{};  // Default config
    REQUIRE(config.isValid() == true);

    // Invalid: frequency above Nyquist
    AliasingTestConfig invalid1{.testFrequencyHz = 30000.0f, .sampleRate = 44100.0f};
    REQUIRE(invalid1.isValid() == false);

    // Invalid: zero sample rate
    AliasingTestConfig invalid2{.sampleRate = 0.0f};
    REQUIRE(invalid2.isValid() == false);

    // Invalid: non-power-of-2 FFT size
    AliasingTestConfig invalid3{.fftSize = 1000};
    REQUIRE(invalid3.isValid() == false);

    // Invalid: FFT size out of range
    AliasingTestConfig invalid4{.fftSize = 128};  // Below 256
    REQUIRE(invalid4.isValid() == false);
}

// T018: AliasingTestConfig::nyquist()
TEST_CASE("AliasingTestConfig nyquist returns sampleRate over 2", "[spectral][US2]") {
    AliasingTestConfig config{.sampleRate = 44100.0f};
    REQUIRE(config.nyquist() == Approx(22050.0f));

    AliasingTestConfig config96k{.sampleRate = 96000.0f};
    REQUIRE(config96k.nyquist() == Approx(48000.0f));
}

// T019: AliasingTestConfig::binResolution()
TEST_CASE("AliasingTestConfig binResolution returns sampleRate over fftSize", "[spectral][US2]") {
    AliasingTestConfig config{.sampleRate = 44100.0f, .fftSize = 2048};
    // 44100 / 2048 = 21.533...
    REQUIRE(config.binResolution() == Approx(21.533f).margin(0.01f));

    AliasingTestConfig config4096{.sampleRate = 44100.0f, .fftSize = 4096};
    // 44100 / 4096 = 10.767...
    REQUIRE(config4096.binResolution() == Approx(10.767f).margin(0.01f));
}

// T020: getHarmonicBins returns bins for harmonics below Nyquist
TEST_CASE("getHarmonicBins returns bins for harmonics 2-4 below Nyquist", "[spectral][US2]") {
    // 5kHz at 44.1kHz: harmonics 2-4 are 10kHz, 15kHz, 20kHz (all below Nyquist 22.05kHz)
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    auto bins = getHarmonicBins(config);

    // Should have bins for harmonics 2, 3, 4 (10kHz, 15kHz, 20kHz)
    REQUIRE(bins.size() == 3);

    // Harmonic 2: 10kHz -> bin round(10000 * 2048 / 44100) = round(464.4) = 464
    REQUIRE(bins[0] == 464);
    // Harmonic 3: 15kHz -> bin round(15000 * 2048 / 44100) = round(696.6) = 697
    REQUIRE(bins[1] == 697);
    // Harmonic 4: 20kHz -> bin round(20000 * 2048 / 44100) = round(928.8) = 929
    REQUIRE(bins[2] == 929);
}

// T021: getAliasedBins returns bins for harmonics above Nyquist folded back
TEST_CASE("getAliasedBins returns bins for harmonics 5-10 above Nyquist", "[spectral][US2]") {
    // 5kHz at 44.1kHz: harmonics 5+ alias
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    auto bins = getAliasedBins(config);

    // Should have bins for harmonics 5-10 (all aliased)
    // Harmonic 5: 25kHz -> 19.1kHz
    // Harmonic 6: 30kHz -> 14.1kHz
    // Harmonic 7: 35kHz -> 9.1kHz
    // Harmonic 8: 40kHz -> 4.1kHz
    // Harmonic 9: 45kHz -> 0.9kHz
    // Harmonic 10: 50kHz -> 5.9kHz
    REQUIRE(bins.size() == 6);
}

// T022: No overlap between harmonic and aliased bins
TEST_CASE("No overlap between harmonic bins and aliased bins", "[spectral][US2]") {
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    auto harmonicBins = getHarmonicBins(config);
    auto aliasedBins = getAliasedBins(config);

    // Check no bins appear in both sets
    for (size_t hbin : harmonicBins) {
        for (size_t abin : aliasedBins) {
            REQUIRE(hbin != abin);
        }
    }
}

// ==============================================================================
// Phase 4: User Story 3 - Main Measurement Function (T029-T036)
// ==============================================================================

// T029: detail::toDb converts amplitude to dB correctly
TEST_CASE("detail::toDb converts amplitude to dB correctly", "[spectral][US3]") {
    using Krate::DSP::TestUtils::detail::toDb;

    // 1.0 amplitude = 0 dB
    REQUIRE(toDb(1.0f) == Approx(0.0f).margin(0.001f));

    // 0.1 amplitude = -20 dB
    REQUIRE(toDb(0.1f) == Approx(-20.0f).margin(0.1f));

    // 10.0 amplitude = +20 dB
    REQUIRE(toDb(10.0f) == Approx(20.0f).margin(0.1f));

    // 2.0 amplitude = ~6.02 dB
    REQUIRE(toDb(2.0f) == Approx(6.02f).margin(0.1f));
}

// T030: detail::toDb handles zero/epsilon correctly
TEST_CASE("detail::toDb handles zero and epsilon correctly", "[spectral][US3]") {
    using Krate::DSP::TestUtils::detail::toDb;

    // Zero should return floor dB (-200)
    REQUIRE(toDb(0.0f) == Approx(-200.0f).margin(1.0f));

    // Very small values should return floor dB
    REQUIRE(toDb(1e-11f) == Approx(-200.0f).margin(1.0f));
}

// T031: detail::sumBinPower computes RMS of specified bins
TEST_CASE("detail::sumBinPower computes RMS of specified bins", "[spectral][US3]") {
    using Krate::DSP::TestUtils::detail::sumBinPower;
    using Krate::DSP::Complex;

    // Create test spectrum with known magnitudes
    std::vector<Complex> spectrum(10);
    spectrum[2] = {3.0f, 0.0f};  // magnitude 3
    spectrum[5] = {4.0f, 0.0f};  // magnitude 4

    std::vector<size_t> bins = {2, 5};

    // Sum of squares: 9 + 16 = 25, sqrt = 5
    float power = sumBinPower(spectrum.data(), bins);
    REQUIRE(power == Approx(5.0f).margin(0.01f));
}

// T032: AliasingMeasurement::isValid returns true for valid, false for NaN
TEST_CASE("AliasingMeasurement isValid returns true for valid false for NaN", "[spectral][US3]") {
    AliasingMeasurement valid{
        .fundamentalPowerDb = -10.0f,
        .harmonicPowerDb = -30.0f,
        .aliasingPowerDb = -50.0f,
        .signalToAliasingDb = 40.0f
    };
    REQUIRE(valid.isValid() == true);

    AliasingMeasurement invalid{
        .fundamentalPowerDb = std::numeric_limits<float>::quiet_NaN(),
        .harmonicPowerDb = -30.0f,
        .aliasingPowerDb = -50.0f,
        .signalToAliasingDb = 40.0f
    };
    REQUIRE(invalid.isValid() == false);
}

// T033: AliasingMeasurement::aliasingReductionVs computes difference correctly
TEST_CASE("AliasingMeasurement aliasingReductionVs computes difference", "[spectral][US3]") {
    AliasingMeasurement test{.aliasingPowerDb = -50.0f};
    AliasingMeasurement reference{.aliasingPowerDb = -30.0f};

    // Reduction = reference.aliasingPowerDb - test.aliasingPowerDb
    // = -30 - (-50) = 20 dB improvement
    REQUIRE(test.aliasingReductionVs(reference) == Approx(20.0f).margin(0.01f));
}

// T034: measureAliasing with identity processor has low aliasing
TEST_CASE("measureAliasing with identity processor has low aliasing", "[spectral][US3]") {
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 1.0f,  // No clipping with identity
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    // Identity processor: y = x
    auto result = measureAliasing(config, [](float x) { return x; });

    REQUIRE(result.isValid());
    // With identity (no clipping), aliasing should be very low (noise floor)
    // Fundamental should be strong (positive dB or close to 0)
    INFO("Fundamental: " << result.fundamentalPowerDb << " dB");
    INFO("Aliasing: " << result.aliasingPowerDb << " dB");
    REQUIRE(result.fundamentalPowerDb > result.aliasingPowerDb + 40.0f);
}

// T035: measureAliasing with naive hardClip has measurable aliasing
TEST_CASE("measureAliasing with naive hardClip has measurable aliasing", "[spectral][US3]") {
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 4.0f,  // Induce clipping
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    // Naive hard clip
    auto result = measureAliasing(config, [](float x) {
        return std::clamp(x, -1.0f, 1.0f);
    });

    REQUIRE(result.isValid());
    // With hard clipping at drive 4.0, there should be measurable aliasing
    // aliasing should be higher than with identity
    INFO("Fundamental: " << result.fundamentalPowerDb << " dB");
    INFO("Harmonic: " << result.harmonicPowerDb << " dB");
    INFO("Aliasing: " << result.aliasingPowerDb << " dB");
    // Aliasing should be present (not at noise floor)
    REQUIRE(result.aliasingPowerDb > -100.0f);
}

// T036: measureAliasing result isValid returns true
TEST_CASE("measureAliasing result isValid returns true for valid processing", "[spectral][US3]") {
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 4.0f,
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    auto result = measureAliasing(config, [](float x) {
        return std::clamp(x, -1.0f, 1.0f);
    });

    REQUIRE(result.isValid() == true);
}

// ==============================================================================
// Phase 5: User Story 4 - Comparison Utility (T044-T050)
// ==============================================================================

// T044: AliasingComparison fields
TEST_CASE("AliasingComparison stores correct fields", "[spectral][US4]") {
    AliasingComparison comp{
        .referenceAliasing = -30.0f,
        .testedAliasing = -50.0f,
        .reductionDb = 20.0f
    };

    REQUIRE(comp.referenceAliasing == Approx(-30.0f));
    REQUIRE(comp.testedAliasing == Approx(-50.0f));
    REQUIRE(comp.reductionDb == Approx(20.0f));
}

// T045: AliasingComparison::meetsThreshold
TEST_CASE("AliasingComparison meetsThreshold returns correct result", "[spectral][US4]") {
    AliasingComparison good{.reductionDb = 15.0f};
    AliasingComparison bad{.reductionDb = 8.0f};

    // 12 dB threshold
    REQUIRE(good.meetsThreshold(12.0f) == true);
    REQUIRE(bad.meetsThreshold(12.0f) == false);
}

// T046: compareAliasing computes correct comparison
TEST_CASE("compareAliasing computes correct comparison", "[spectral][US4]") {
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 4.0f,
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    // Reference: naive hard clip
    auto reference = [](float x) { return std::clamp(x, -1.0f, 1.0f); };

    // Tested: soft clip approximation (tanh-like)
    auto tested = [](float x) { return std::tanh(x); };

    auto result = compareAliasing(config, reference, tested);

    // tanh should have less aliasing than hard clip
    INFO("Reference aliasing: " << result.referenceAliasing << " dB");
    INFO("Tested aliasing: " << result.testedAliasing << " dB");
    INFO("Reduction: " << result.reductionDb << " dB");

    // Soft clip should reduce aliasing compared to hard clip
    REQUIRE(result.reductionDb > 0.0f);
}

// T047: compareAliasing with identical processors returns ~0 dB reduction
TEST_CASE("compareAliasing with identical processors returns near zero reduction", "[spectral][US4]") {
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 4.0f,
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    auto hardClip = [](float x) { return std::clamp(x, -1.0f, 1.0f); };

    auto result = compareAliasing(config, hardClip, hardClip);

    // Same processor should give ~0 dB difference
    REQUIRE(std::abs(result.reductionDb) < 0.1f);
}

// T048: hardClipReference function works as expected
TEST_CASE("hardClipReference function produces expected output", "[spectral][US4]") {
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 4.0f,
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    auto result = measureAliasing(config, hardClipReference);

    REQUIRE(result.isValid());
    // Hard clip should have measurable aliasing
    REQUIRE(result.aliasingPowerDb > -100.0f);
}

// T049: identityReference function works as expected
TEST_CASE("identityReference function produces expected output", "[spectral][US4]") {
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 1.0f,  // No clipping
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    auto result = measureAliasing(config, identityReference);

    REQUIRE(result.isValid());
    // Identity should have low aliasing compared to hard clip
    // The aliasing bins will still pick up spectral leakage and noise floor
    // so we can't expect extremely low values
    REQUIRE(result.aliasingPowerDb < result.fundamentalPowerDb - 30.0f);
}

// T050: End-to-end test with known aliasing difference
TEST_CASE("End-to-end comparison shows tanh reduces aliasing vs hard clip", "[spectral][US4]") {
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 4.0f,
        .fftSize = 2048,
        .maxHarmonic = 10
    };

    // tanh vs hard clip
    auto result = compareAliasing(
        config,
        hardClipReference,
        [](float x) { return std::tanh(x); }
    );

    INFO("Hard clip aliasing: " << result.referenceAliasing << " dB");
    INFO("tanh aliasing: " << result.testedAliasing << " dB");
    INFO("Reduction: " << result.reductionDb << " dB");

    // tanh should provide some aliasing reduction (soft vs hard clip)
    // The exact amount depends on test frequency and drive
    REQUIRE(result.reductionDb > 0.0f);
}
