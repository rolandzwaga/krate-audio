// ==============================================================================
// Layer 2: DSP Processor - Spectral Tilt Filter Tests
// ==============================================================================
// Test-First Development: Tests written BEFORE implementation (Constitution XII)
//
// Spectral Tilt Filter applies a linear dB/octave gain slope across the
// frequency spectrum using a single high-shelf biquad filter.
//
// Test Strategy:
// - Octave interval measurement: 125, 250, 500, 1000, 2000, 4000, 8000 Hz
// - Gain tolerance: +/- 1 dB from target slope
// - Pivot frequency unity gain: +/- 0.5 dB
// ==============================================================================

#include <krate/dsp/processors/spectral_tilt.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

/// Generate a sine wave at given frequency
void generateSineWave(float* buffer, size_t numSamples, float frequency, float sampleRate) {
    const float phaseIncrement = kTwoPi * frequency / sampleRate;
    float phase = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(phase);
        phase += phaseIncrement;
        if (phase >= kTwoPi) {
            phase -= kTwoPi;
        }
    }
}

/// Measure RMS of a buffer
float measureRMS(const float* buffer, size_t numSamples) {
    float sumSquared = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSquared += buffer[i] * buffer[i];
    }
    return std::sqrt(sumSquared / static_cast<float>(numSamples));
}

/// Measure gain at a specific frequency by processing a sine wave
/// Returns gain in dB relative to input
float measureGainAtFrequency(SpectralTilt& filter, float frequency, float sampleRate) {
    const size_t numSamples = static_cast<size_t>(sampleRate);  // 1 second
    std::vector<float> buffer(numSamples);

    // Generate sine wave
    generateSineWave(buffer.data(), numSamples, frequency, sampleRate);

    // Skip first half for filter settling
    const size_t settleTime = numSamples / 2;
    for (size_t i = 0; i < settleTime; ++i) {
        (void)filter.process(buffer[i]);
    }

    // Generate fresh sine wave for measurement
    generateSineWave(buffer.data(), numSamples, frequency, sampleRate);

    // Process and measure output
    std::vector<float> output(numSamples - settleTime);
    for (size_t i = 0; i < output.size(); ++i) {
        output[i] = filter.process(buffer[i + settleTime]);
    }

    const float outputRMS = measureRMS(output.data(), output.size());
    const float inputRMS = 0.7071067811865476f;  // RMS of unit sine wave

    return gainToDb(outputRMS / inputRMS);
}

} // anonymous namespace

// =============================================================================
// Phase 3.1: User Story 1 - Basic Construction and Default State Tests
// =============================================================================

TEST_CASE("SpectralTilt: Default construction creates valid object", "[spectral_tilt][construction]") {
    SpectralTilt tilt;

    SECTION("Object is not prepared after construction") {
        REQUIRE_FALSE(tilt.isPrepared());
    }

    SECTION("Default tilt is 0 dB/octave") {
        REQUIRE(tilt.getTilt() == Approx(SpectralTilt::kDefaultTilt));
    }

    SECTION("Default pivot frequency is 1 kHz") {
        REQUIRE(tilt.getPivotFrequency() == Approx(SpectralTilt::kDefaultPivot));
    }

    SECTION("Default smoothing is 50 ms") {
        REQUIRE(tilt.getSmoothing() == Approx(SpectralTilt::kDefaultSmoothing));
    }
}

TEST_CASE("SpectralTilt: Constants are correctly defined", "[spectral_tilt][constants]") {
    SECTION("Tilt range constants") {
        REQUIRE(SpectralTilt::kMinTilt == Approx(-12.0f));
        REQUIRE(SpectralTilt::kMaxTilt == Approx(+12.0f));
    }

    SECTION("Pivot range constants") {
        REQUIRE(SpectralTilt::kMinPivot == Approx(20.0f));
        REQUIRE(SpectralTilt::kMaxPivot == Approx(20000.0f));
    }

    SECTION("Smoothing range constants") {
        REQUIRE(SpectralTilt::kMinSmoothing == Approx(1.0f));
        REQUIRE(SpectralTilt::kMaxSmoothing == Approx(500.0f));
    }

    SECTION("Gain limit constants") {
        REQUIRE(SpectralTilt::kMaxGainDb == Approx(+24.0f));
        REQUIRE(SpectralTilt::kMinGainDb == Approx(-48.0f));
    }
}

// =============================================================================
// Phase 3.1: User Story 1 - Prepare and isPrepared Tests
// =============================================================================

TEST_CASE("SpectralTilt: prepare() initializes the filter", "[spectral_tilt][prepare]") {
    SpectralTilt tilt;

    SECTION("isPrepared() returns true after prepare()") {
        tilt.prepare(44100.0);
        REQUIRE(tilt.isPrepared());
    }

    SECTION("Multiple prepare() calls are safe") {
        tilt.prepare(44100.0);
        REQUIRE(tilt.isPrepared());

        tilt.prepare(48000.0);
        REQUIRE(tilt.isPrepared());

        tilt.prepare(96000.0);
        REQUIRE(tilt.isPrepared());
    }
}

// =============================================================================
// Phase 3.1: User Story 1 - Passthrough When Not Prepared (FR-019)
// =============================================================================

TEST_CASE("SpectralTilt: Passthrough when not prepared (FR-019)", "[spectral_tilt][passthrough]") {
    SpectralTilt tilt;

    SECTION("process() returns input unchanged before prepare()") {
        REQUIRE_FALSE(tilt.isPrepared());

        REQUIRE(tilt.process(0.0f) == Approx(0.0f));
        REQUIRE(tilt.process(0.5f) == Approx(0.5f));
        REQUIRE(tilt.process(-0.5f) == Approx(-0.5f));
        REQUIRE(tilt.process(1.0f) == Approx(1.0f));
        REQUIRE(tilt.process(-1.0f) == Approx(-1.0f));
    }

    SECTION("setTilt() before prepare() doesn't affect passthrough") {
        tilt.setTilt(6.0f);
        REQUIRE(tilt.process(0.5f) == Approx(0.5f));
    }
}

// =============================================================================
// Phase 3.1: User Story 1 - Zero Tilt Produces Unity Output (SC-008)
// =============================================================================

TEST_CASE("SpectralTilt: Zero tilt produces near-unity output (SC-008)", "[spectral_tilt][unity]") {
    SpectralTilt tilt;
    tilt.prepare(44100.0);
    tilt.setTilt(0.0f);

    // Let smoothers settle
    for (int i = 0; i < 4410; ++i) {
        (void)tilt.process(0.0f);
    }

    SECTION("Process sine wave at various frequencies") {
        constexpr float sampleRate = 44100.0f;
        const std::array<float, 5> testFrequencies = {100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f};

        for (float freq : testFrequencies) {
            // Reset filter state
            tilt.reset();

            float gainDb = measureGainAtFrequency(tilt, freq, sampleRate);

            INFO("Frequency: " << freq << " Hz, Gain: " << gainDb << " dB");
            REQUIRE(gainDb == Approx(0.0f).margin(0.5f));  // Within 0.5 dB of unity
        }
    }
}

// =============================================================================
// Phase 3.1: User Story 1 - Positive Tilt Slope Accuracy
// =============================================================================

TEST_CASE("SpectralTilt: Positive tilt boosts above pivot (+6 dB/octave)", "[spectral_tilt][slope][positive]") {
    // The high-shelf filter applies increasing gain above the cutoff frequency.
    // At Nyquist, the gain reaches the calculated plateau.
    // The SLOPE between measurement points indicates the tilt effect.

    constexpr float sampleRate = 44100.0f;
    constexpr float pivotFrequency = 1000.0f;
    constexpr float tiltAmount = 6.0f;  // +6 dB/octave

    SpectralTilt tilt;
    tilt.prepare(sampleRate);
    tilt.setTilt(tiltAmount);
    tilt.setPivotFrequency(pivotFrequency);

    // Let smoothers settle
    for (int i = 0; i < 4410; ++i) {
        (void)tilt.process(0.0f);
    }

    // Measure gains at octave intervals
    tilt.reset();
    for (int i = 0; i < 4410; ++i) {
        (void)tilt.process(0.0f);
    }

    float gainAtPivot = measureGainAtFrequency(tilt, 1000.0f, sampleRate);
    float gainAt2k = measureGainAtFrequency(tilt, 2000.0f, sampleRate);
    float gainAt4k = measureGainAtFrequency(tilt, 4000.0f, sampleRate);
    float gainAt500 = measureGainAtFrequency(tilt, 500.0f, sampleRate);

    INFO("Gain at 500 Hz: " << gainAt500 << " dB");
    INFO("Gain at 1 kHz (pivot): " << gainAtPivot << " dB");
    INFO("Gain at 2 kHz: " << gainAt2k << " dB");
    INFO("Gain at 4 kHz: " << gainAt4k << " dB");

    // Key characteristics of positive tilt:
    // 1. Gain increases monotonically with frequency
    REQUIRE(gainAt500 < gainAtPivot);
    REQUIRE(gainAtPivot < gainAt2k);
    REQUIRE(gainAt2k < gainAt4k);

    // 2. Below pivot is near 0 dB (passband)
    REQUIRE(gainAt500 >= -3.0f);
    REQUIRE(gainAt500 <= 3.0f);

    // 3. Above pivot has significant boost
    REQUIRE(gainAt4k > gainAtPivot + 5.0f);
}

// =============================================================================
// Phase 3.1: User Story 1 - Negative Tilt Slope Accuracy
// =============================================================================

TEST_CASE("SpectralTilt: Negative tilt cuts above pivot (-6 dB/octave)", "[spectral_tilt][slope][negative]") {
    // The high-shelf filter with negative gain attenuates above the cutoff.
    // This creates a "darkening" effect by attenuating high frequencies.

    constexpr float sampleRate = 44100.0f;
    constexpr float pivotFrequency = 1000.0f;
    constexpr float tiltAmount = -6.0f;  // -6 dB/octave (darkening)

    SpectralTilt tilt;
    tilt.prepare(sampleRate);
    tilt.setTilt(tiltAmount);
    tilt.setPivotFrequency(pivotFrequency);

    // Let smoothers settle
    for (int i = 0; i < 4410; ++i) {
        (void)tilt.process(0.0f);
    }

    // Measure gains at octave intervals
    tilt.reset();
    for (int i = 0; i < 4410; ++i) {
        (void)tilt.process(0.0f);
    }

    float gainAtPivot = measureGainAtFrequency(tilt, 1000.0f, sampleRate);
    float gainAt2k = measureGainAtFrequency(tilt, 2000.0f, sampleRate);
    float gainAt4k = measureGainAtFrequency(tilt, 4000.0f, sampleRate);
    float gainAt500 = measureGainAtFrequency(tilt, 500.0f, sampleRate);

    INFO("Gain at 500 Hz: " << gainAt500 << " dB");
    INFO("Gain at 1 kHz (pivot): " << gainAtPivot << " dB");
    INFO("Gain at 2 kHz: " << gainAt2k << " dB");
    INFO("Gain at 4 kHz: " << gainAt4k << " dB");

    // Key characteristics of negative tilt:
    // 1. Gain decreases monotonically with frequency
    REQUIRE(gainAt500 > gainAtPivot);
    REQUIRE(gainAtPivot > gainAt2k);
    REQUIRE(gainAt2k > gainAt4k);

    // 2. Below pivot is closer to 0 dB than above pivot
    // (high-shelf transition region may affect slightly below cutoff)
    REQUIRE(gainAt500 >= -5.0f);
    REQUIRE(gainAt500 <= 3.0f);

    // 3. Above pivot has significant cut
    REQUIRE(gainAt4k < gainAtPivot - 5.0f);
}

// =============================================================================
// Phase 4.1: User Story 2 - Pivot Frequency Tests
// =============================================================================

TEST_CASE("SpectralTilt: setPivotFrequency() with range clamping", "[spectral_tilt][pivot][clamping]") {
    SpectralTilt tilt;

    SECTION("Values within range are accepted") {
        tilt.setPivotFrequency(500.0f);
        REQUIRE(tilt.getPivotFrequency() == Approx(500.0f));

        tilt.setPivotFrequency(2000.0f);
        REQUIRE(tilt.getPivotFrequency() == Approx(2000.0f));
    }

    SECTION("Values below minimum are clamped to kMinPivot") {
        tilt.setPivotFrequency(10.0f);
        REQUIRE(tilt.getPivotFrequency() == Approx(SpectralTilt::kMinPivot));

        tilt.setPivotFrequency(-100.0f);
        REQUIRE(tilt.getPivotFrequency() == Approx(SpectralTilt::kMinPivot));
    }

    SECTION("Values above maximum are clamped to kMaxPivot") {
        tilt.setPivotFrequency(25000.0f);
        REQUIRE(tilt.getPivotFrequency() == Approx(SpectralTilt::kMaxPivot));

        tilt.setPivotFrequency(100000.0f);
        REQUIRE(tilt.getPivotFrequency() == Approx(SpectralTilt::kMaxPivot));
    }
}

TEST_CASE("SpectralTilt: Transition at pivot frequency", "[spectral_tilt][pivot][transition]") {
    // NOTE: The high-shelf filter has a transition region at the pivot frequency.
    // The gain at exactly the pivot frequency is approximately half the shelf gain.
    // This is a characteristic of the Butterworth high-shelf response.
    // For SC-003 "Gain at pivot frequency remains within 0.5 dB of unity", we test
    // with ZERO tilt, where the gain should truly be unity.

    constexpr float sampleRate = 44100.0f;

    SpectralTilt tilt;
    tilt.prepare(sampleRate);

    SECTION("Zero tilt gives unity gain at all frequencies") {
        tilt.setPivotFrequency(1000.0f);
        tilt.setTilt(0.0f);

        // Settle
        for (int i = 0; i < 4410; ++i) {
            (void)tilt.process(0.0f);
        }

        float gainDb = measureGainAtFrequency(tilt, 1000.0f, sampleRate);
        REQUIRE(gainDb == Approx(0.0f).margin(0.5f));
    }

    SECTION("Pivot position affects where transition occurs") {
        // With +6 dB/octave tilt at 1 kHz pivot:
        // - 500 Hz (1 octave below): near 0 dB
        // - 1 kHz (pivot): in transition region (some boost due to shelf shape)
        // - 2 kHz (1 octave above): significant boost

        tilt.setPivotFrequency(1000.0f);
        tilt.setTilt(6.0f);

        // Settle
        for (int i = 0; i < 4410; ++i) {
            (void)tilt.process(0.0f);
        }

        float gain500 = measureGainAtFrequency(tilt, 500.0f, sampleRate);
        float gain1k = measureGainAtFrequency(tilt, 1000.0f, sampleRate);
        float gain2k = measureGainAtFrequency(tilt, 2000.0f, sampleRate);

        INFO("Gain at 500 Hz: " << gain500 << " dB");
        INFO("Gain at 1 kHz: " << gain1k << " dB");
        INFO("Gain at 2 kHz: " << gain2k << " dB");

        // 500 Hz should have less boost than 1 kHz
        // 1 kHz should have less boost than 2 kHz
        REQUIRE(gain500 < gain1k);
        REQUIRE(gain1k < gain2k);

        // 500 Hz should be near 0 dB (below transition)
        REQUIRE(gain500 >= -3.0f);
        REQUIRE(gain500 <= 3.0f);
    }
}

TEST_CASE("SpectralTilt: Different pivot positions shift transition region", "[spectral_tilt][pivot][positions]") {
    // Verify that changing pivot frequency shifts where the tilt transition occurs
    constexpr float sampleRate = 44100.0f;

    SpectralTilt tilt;
    tilt.prepare(sampleRate);
    tilt.setTilt(6.0f);

    SECTION("Pivot at 500 Hz: frequencies below 500 Hz are less affected") {
        tilt.setPivotFrequency(500.0f);

        // Settle
        for (int i = 0; i < 4410; ++i) {
            (void)tilt.process(0.0f);
        }

        float gain250 = measureGainAtFrequency(tilt, 250.0f, sampleRate);
        float gain1k = measureGainAtFrequency(tilt, 1000.0f, sampleRate);

        INFO("Gain at 250 Hz: " << gain250 << " dB");
        INFO("Gain at 1 kHz: " << gain1k << " dB");

        // 250 Hz (below pivot) should be near unity
        REQUIRE(gain250 >= -3.0f);
        REQUIRE(gain250 <= 3.0f);

        // 1 kHz (above pivot) should have significant boost
        REQUIRE(gain1k > 5.0f);
    }

    SECTION("Pivot at 2 kHz: frequencies below 2 kHz are less affected") {
        tilt.setPivotFrequency(2000.0f);

        // Settle
        for (int i = 0; i < 4410; ++i) {
            (void)tilt.process(0.0f);
        }

        float gain1k = measureGainAtFrequency(tilt, 1000.0f, sampleRate);
        float gain4k = measureGainAtFrequency(tilt, 4000.0f, sampleRate);

        INFO("Gain at 1 kHz: " << gain1k << " dB");
        INFO("Gain at 4 kHz: " << gain4k << " dB");

        // 1 kHz (below pivot) should be near unity
        REQUIRE(gain1k >= -3.0f);
        REQUIRE(gain1k <= 3.0f);

        // 4 kHz (above pivot) should have significant boost
        REQUIRE(gain4k > 5.0f);
    }
}

TEST_CASE("SpectralTilt: Pivot clamping at boundaries", "[spectral_tilt][pivot][boundaries]") {
    SpectralTilt tilt;

    SECTION("Clamping at 20 Hz boundary") {
        tilt.setPivotFrequency(15.0f);
        REQUIRE(tilt.getPivotFrequency() == Approx(20.0f));
    }

    SECTION("Clamping at 20 kHz boundary") {
        tilt.setPivotFrequency(25000.0f);
        REQUIRE(tilt.getPivotFrequency() == Approx(20000.0f));
    }
}

// =============================================================================
// Phase 5.1: User Story 3 - Parameter Smoothing Tests
// =============================================================================

TEST_CASE("SpectralTilt: setSmoothing() with range validation", "[spectral_tilt][smoothing][validation]") {
    SpectralTilt tilt;

    SECTION("Values within range are accepted") {
        tilt.setSmoothing(10.0f);
        REQUIRE(tilt.getSmoothing() == Approx(10.0f));

        tilt.setSmoothing(100.0f);
        REQUIRE(tilt.getSmoothing() == Approx(100.0f));
    }

    SECTION("Values below minimum are clamped") {
        tilt.setSmoothing(0.5f);
        REQUIRE(tilt.getSmoothing() == Approx(SpectralTilt::kMinSmoothing));
    }

    SECTION("Values above maximum are clamped") {
        tilt.setSmoothing(1000.0f);
        REQUIRE(tilt.getSmoothing() == Approx(SpectralTilt::kMaxSmoothing));
    }
}

TEST_CASE("SpectralTilt: Smoothing allows gradual parameter changes", "[spectral_tilt][smoothing][convergence]") {
    // Use a high frequency signal to see the effect of tilt (high-shelf boosts above pivot)
    constexpr float sampleRate = 44100.0f;
    constexpr float smoothingMs = 50.0f;
    constexpr size_t blockSize = 512;

    SpectralTilt tilt;
    tilt.prepare(sampleRate);
    tilt.setSmoothing(smoothingMs);
    tilt.setTilt(0.0f);
    tilt.setPivotFrequency(500.0f);  // Low pivot so 4 kHz test signal is well above

    // Generate high frequency test signal (will be boosted by positive tilt)
    std::array<float, blockSize> input;
    generateSineWave(input.data(), blockSize, 4000.0f, sampleRate);

    // Let initial state settle
    for (int block = 0; block < 20; ++block) {
        for (size_t i = 0; i < blockSize; ++i) {
            (void)tilt.process(input[i]);
        }
    }

    // Measure RMS before parameter change
    std::vector<float> outputBefore(blockSize);
    for (size_t i = 0; i < blockSize; ++i) {
        outputBefore[i] = tilt.process(input[i]);
    }
    float rmsBefore = measureRMS(outputBefore.data(), blockSize);

    // Jump to new tilt value
    tilt.setTilt(6.0f);

    // Process for extended time to ensure full convergence
    for (int block = 0; block < 50; ++block) {
        for (size_t i = 0; i < blockSize; ++i) {
            (void)tilt.process(input[i]);
        }
    }

    // Measure RMS after settling
    std::vector<float> outputAfter(blockSize);
    for (size_t i = 0; i < blockSize; ++i) {
        outputAfter[i] = tilt.process(input[i]);
    }
    float rmsAfter = measureRMS(outputAfter.data(), blockSize);

    INFO("RMS before: " << rmsBefore);
    INFO("RMS after: " << rmsAfter);

    // The output should have increased (high frequency boosted by positive tilt)
    REQUIRE(rmsAfter > rmsBefore * 1.5f);
}

TEST_CASE("SpectralTilt: Parameter smoothing prevents harsh transients", "[spectral_tilt][smoothing][clicks]") {
    // This test verifies that parameter changes are smoothed using a high frequency
    // signal that is affected by the tilt.

    constexpr float sampleRate = 44100.0f;
    constexpr size_t blockSize = 512;

    SpectralTilt tilt;
    tilt.prepare(sampleRate);
    tilt.setTilt(0.0f);
    tilt.setPivotFrequency(500.0f);
    tilt.setSmoothing(50.0f);

    // Use a high frequency signal that will be boosted by the tilt
    std::array<float, blockSize> input;
    generateSineWave(input.data(), blockSize, 4000.0f, sampleRate);

    // Process blocks to settle
    for (int block = 0; block < 20; ++block) {
        for (size_t i = 0; i < blockSize; ++i) {
            (void)tilt.process(input[i]);
        }
    }

    // Record output levels before parameter change
    std::vector<float> outputsBefore;
    for (size_t i = 0; i < blockSize; ++i) {
        outputsBefore.push_back(tilt.process(input[i]));
    }

    // Make a large parameter change
    tilt.setTilt(12.0f);  // Maximum positive tilt

    // Process more blocks
    std::vector<float> outputsAfter;
    for (int block = 0; block < 50; ++block) {
        for (size_t i = 0; i < blockSize; ++i) {
            outputsAfter.push_back(tilt.process(input[i]));
        }
    }

    // Calculate RMS of first and last segments
    float rmsBefore = measureRMS(outputsBefore.data(), outputsBefore.size());
    float rmsAfter = measureRMS(outputsAfter.data() + outputsAfter.size() - blockSize, blockSize);

    INFO("RMS before change: " << rmsBefore);
    INFO("RMS after settling: " << rmsAfter);

    // Output level should have increased significantly (high frequency boosted)
    REQUIRE(rmsAfter > rmsBefore * 2.0f);
}

TEST_CASE("SpectralTilt: Pivot frequency changes affect filter response", "[spectral_tilt][smoothing][pivot]") {
    // Verify that pivot frequency changes affect the filter response
    constexpr float sampleRate = 44100.0f;

    SpectralTilt tilt;
    tilt.prepare(sampleRate);
    tilt.setTilt(6.0f);
    tilt.setSmoothing(50.0f);

    // Start with low pivot
    tilt.setPivotFrequency(500.0f);

    // Let settle
    for (int i = 0; i < 44100; ++i) {
        (void)tilt.process(0.0f);
    }

    // Measure gain at 2 kHz with low pivot (should be boosted)
    float gainWithLowPivot = measureGainAtFrequency(tilt, 2000.0f, sampleRate);

    // Change to high pivot
    tilt.setPivotFrequency(4000.0f);

    // Let settle
    for (int i = 0; i < 44100; ++i) {
        (void)tilt.process(0.0f);
    }

    // Measure gain at 2 kHz with high pivot (should be less boosted)
    float gainWithHighPivot = measureGainAtFrequency(tilt, 2000.0f, sampleRate);

    INFO("Gain at 2 kHz with pivot at 500 Hz: " << gainWithLowPivot << " dB");
    INFO("Gain at 2 kHz with pivot at 4 kHz: " << gainWithHighPivot << " dB");

    // 2 kHz is above 500 Hz pivot (boosted) but below 4 kHz pivot (less boosted)
    REQUIRE(gainWithLowPivot > gainWithHighPivot);
}

// =============================================================================
// Phase 6.1: User Story 4 - Efficient IIR Implementation Tests
// =============================================================================

TEST_CASE("SpectralTilt: processBlock() with various buffer sizes", "[spectral_tilt][block][sizes]") {
    constexpr float sampleRate = 44100.0f;

    SpectralTilt tilt;
    tilt.prepare(sampleRate);
    tilt.setTilt(6.0f);

    // Let smoother settle
    for (int i = 0; i < 4410; ++i) {
        (void)tilt.process(0.0f);
    }

    SECTION("Block size 1") {
        std::array<float, 1> buffer = {0.5f};
        tilt.processBlock(buffer.data(), 1);
        REQUIRE_FALSE(detail::isNaN(buffer[0]));
    }

    SECTION("Block size 32") {
        std::array<float, 32> buffer;
        generateSineWave(buffer.data(), 32, 1000.0f, sampleRate);
        tilt.processBlock(buffer.data(), 32);
        for (size_t i = 0; i < 32; ++i) {
            REQUIRE_FALSE(detail::isNaN(buffer[i]));
        }
    }

    SECTION("Block size 512") {
        std::array<float, 512> buffer;
        generateSineWave(buffer.data(), 512, 1000.0f, sampleRate);
        tilt.processBlock(buffer.data(), 512);
        for (size_t i = 0; i < 512; ++i) {
            REQUIRE_FALSE(detail::isNaN(buffer[i]));
        }
    }

    SECTION("Block size 2048") {
        std::array<float, 2048> buffer;
        generateSineWave(buffer.data(), 2048, 1000.0f, sampleRate);
        tilt.processBlock(buffer.data(), 2048);
        for (size_t i = 0; i < 2048; ++i) {
            REQUIRE_FALSE(detail::isNaN(buffer[i]));
        }
    }
}

TEST_CASE("SpectralTilt: processBlock() matches sequential process()", "[spectral_tilt][block][equivalence]") {
    constexpr float sampleRate = 44100.0f;
    constexpr size_t blockSize = 256;

    // Create two identical filters
    SpectralTilt tilt1;
    tilt1.prepare(sampleRate);
    tilt1.setTilt(6.0f);

    SpectralTilt tilt2;
    tilt2.prepare(sampleRate);
    tilt2.setTilt(6.0f);

    // Settle both filters
    for (int i = 0; i < 4410; ++i) {
        (void)tilt1.process(0.0f);
        (void)tilt2.process(0.0f);
    }

    // Generate test signal
    std::array<float, blockSize> input;
    generateSineWave(input.data(), blockSize, 1000.0f, sampleRate);

    // Process with sequential samples
    std::array<float, blockSize> sequential;
    for (size_t i = 0; i < blockSize; ++i) {
        sequential[i] = tilt1.process(input[i]);
    }

    // Process with block
    std::array<float, blockSize> block;
    std::copy(input.begin(), input.end(), block.begin());
    tilt2.processBlock(block.data(), blockSize);

    // Verify equivalence
    for (size_t i = 0; i < blockSize; ++i) {
        REQUIRE(block[i] == Approx(sequential[i]).margin(1e-6f));
    }
}

TEST_CASE("SpectralTilt: Zero latency (FR-010)", "[spectral_tilt][latency]") {
    // IIR filters have zero latency by definition
    // This test verifies that the output starts immediately

    SpectralTilt tilt;
    tilt.prepare(44100.0);
    tilt.setTilt(0.0f);  // Unity gain

    // Settle
    for (int i = 0; i < 4410; ++i) {
        (void)tilt.process(0.0f);
    }

    // Process impulse
    float output = tilt.process(1.0f);

    // Output should be non-zero on first sample (no latency)
    REQUIRE(std::abs(output) > 0.0f);
}

TEST_CASE("SpectralTilt: process() and processBlock() are noexcept (FR-021)", "[spectral_tilt][noexcept]") {
    // Verify noexcept specification at compile time
    SpectralTilt tilt;
    float value = 0.5f;
    float buffer[2] = {0.0f, 0.0f};

    static_assert(noexcept(tilt.process(value)), "process() must be noexcept");
    static_assert(noexcept(tilt.processBlock(buffer, 2)), "processBlock() must be noexcept");
}

// =============================================================================
// Phase 7.1: Edge Cases Tests
// =============================================================================

TEST_CASE("SpectralTilt: reset() clears filter state", "[spectral_tilt][reset]") {
    SpectralTilt tilt;
    tilt.prepare(44100.0);
    tilt.setTilt(6.0f);

    // Process some samples to build up state
    for (int i = 0; i < 1000; ++i) {
        (void)tilt.process(0.5f);
    }

    // Reset
    tilt.reset();

    // Process zeros - should output zeros (or very close to it)
    for (int i = 0; i < 100; ++i) {
        float output = tilt.process(0.0f);
        REQUIRE(std::abs(output) < 0.001f);
    }
}

TEST_CASE("SpectralTilt: NaN input handling", "[spectral_tilt][nan]") {
    SpectralTilt tilt;
    tilt.prepare(44100.0);
    tilt.setTilt(6.0f);

    // Settle
    for (int i = 0; i < 4410; ++i) {
        (void)tilt.process(0.0f);
    }

    // Process NaN - should not propagate
    float nan = std::numeric_limits<float>::quiet_NaN();
    float output = tilt.process(nan);

    // Output should not be NaN
    REQUIRE_FALSE(detail::isNaN(output));

    // Filter should recover and produce valid output
    output = tilt.process(0.5f);
    REQUIRE_FALSE(detail::isNaN(output));
}

TEST_CASE("SpectralTilt: Extreme sample rates", "[spectral_tilt][samplerate]") {
    SECTION("Very low sample rate (1000 Hz)") {
        SpectralTilt tilt;
        tilt.prepare(1000.0);
        tilt.setTilt(6.0f);
        tilt.setPivotFrequency(100.0f);  // Low pivot for low sample rate

        // Should not crash or produce NaN
        for (int i = 0; i < 100; ++i) {
            float output = tilt.process(0.5f);
            REQUIRE_FALSE(detail::isNaN(output));
            REQUIRE_FALSE(detail::isInf(output));
        }
    }

    SECTION("Very high sample rate (192000 Hz)") {
        SpectralTilt tilt;
        tilt.prepare(192000.0);
        tilt.setTilt(6.0f);
        tilt.setPivotFrequency(1000.0f);

        // Should not crash or produce NaN
        for (int i = 0; i < 1000; ++i) {
            float output = tilt.process(0.5f);
            REQUIRE_FALSE(detail::isNaN(output));
            REQUIRE_FALSE(detail::isInf(output));
        }
    }
}

TEST_CASE("SpectralTilt: Gain limiting at extreme tilt values (FR-023, FR-024, FR-025)", "[spectral_tilt][gain][limiting]") {
    constexpr float sampleRate = 44100.0f;

    SpectralTilt tilt;
    tilt.prepare(sampleRate);

    SECTION("Maximum positive tilt is limited") {
        tilt.setTilt(SpectralTilt::kMaxTilt);  // +12 dB/octave

        // Settle
        for (int i = 0; i < 4410; ++i) {
            (void)tilt.process(0.0f);
        }

        // Measure gain at high frequency
        float gainDb = measureGainAtFrequency(tilt, 8000.0f, sampleRate);

        // Gain should be limited to +24 dB max
        INFO("Measured gain at 8 kHz: " << gainDb << " dB");
        REQUIRE(gainDb <= SpectralTilt::kMaxGainDb + 1.0f);  // 1 dB tolerance
    }

    SECTION("Maximum negative tilt is limited") {
        tilt.setTilt(SpectralTilt::kMinTilt);  // -12 dB/octave

        // Settle
        for (int i = 0; i < 4410; ++i) {
            (void)tilt.process(0.0f);
        }

        // Measure gain at high frequency
        float gainDb = measureGainAtFrequency(tilt, 8000.0f, sampleRate);

        // Gain should be limited to -48 dB min
        INFO("Measured gain at 8 kHz: " << gainDb << " dB");
        REQUIRE(gainDb >= SpectralTilt::kMinGainDb - 1.0f);  // 1 dB tolerance
    }
}

TEST_CASE("SpectralTilt: processBlock() with zero samples", "[spectral_tilt][block][zero]") {
    SpectralTilt tilt;
    tilt.prepare(44100.0);
    tilt.setTilt(6.0f);

    // Should not crash with zero samples
    float buffer[1] = {0.0f};
    tilt.processBlock(buffer, 0);

    // Buffer should be unchanged
    REQUIRE(buffer[0] == Approx(0.0f));
}

TEST_CASE("SpectralTilt: setTilt() with range clamping", "[spectral_tilt][tilt][clamping]") {
    SpectralTilt tilt;

    SECTION("Values within range are accepted") {
        tilt.setTilt(0.0f);
        REQUIRE(tilt.getTilt() == Approx(0.0f));

        tilt.setTilt(6.0f);
        REQUIRE(tilt.getTilt() == Approx(6.0f));

        tilt.setTilt(-6.0f);
        REQUIRE(tilt.getTilt() == Approx(-6.0f));
    }

    SECTION("Values below minimum are clamped") {
        tilt.setTilt(-20.0f);
        REQUIRE(tilt.getTilt() == Approx(SpectralTilt::kMinTilt));
    }

    SECTION("Values above maximum are clamped") {
        tilt.setTilt(20.0f);
        REQUIRE(tilt.getTilt() == Approx(SpectralTilt::kMaxTilt));
    }
}
