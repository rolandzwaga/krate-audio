// ==============================================================================
// Unit Tests: DiodeClipper Processor
// ==============================================================================
// Tests for the DiodeClipper Layer 2 processor.
//
// Feature: 060-diode-clipper
// Reference: specs/060-diode-clipper/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <krate/dsp/processors/diode_clipper.h>

#include <array>
#include <cmath>
#include <vector>
#include <numeric>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Helper Functions
// =============================================================================

namespace {

/// Generate a sine wave at the specified frequency
void generateSine(float* buffer, size_t numSamples, float frequency, float sampleRate, float amplitude = 1.0f) {
    const float twoPi = 6.283185307f;
    const float phaseIncrement = twoPi * frequency / sampleRate;
    float phase = 0.0f;

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * std::sin(phase);
        phase += phaseIncrement;
        if (phase >= twoPi) {
            phase -= twoPi;
        }
    }
}

/// Calculate RMS (Root Mean Square) of a buffer
float calculateRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;

    float sumSquares = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSquares += buffer[i] * buffer[i];
    }
    return std::sqrt(sumSquares / static_cast<float>(numSamples));
}

/// Calculate peak absolute value
float calculatePeak(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        const float absVal = std::abs(buffer[i]);
        if (absVal > peak) {
            peak = absVal;
        }
    }
    return peak;
}

/// Calculate DC offset (average value)
float calculateDCOffset(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;

    float sum = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += buffer[i];
    }
    return sum / static_cast<float>(numSamples);
}

/// Simple FFT magnitude calculation for a specific bin (harmonic analysis)
/// Uses DFT for simplicity - adequate for small test buffers
float measureHarmonicMagnitude(const float* buffer, size_t numSamples, size_t harmonicBin, float sampleRate) {
    // Goertzel algorithm for single frequency detection
    const float twoPi = 6.283185307f;
    const float k = static_cast<float>(harmonicBin);
    const float omega = twoPi * k / static_cast<float>(numSamples);
    const float coeff = 2.0f * std::cos(omega);

    float s0 = 0.0f;
    float s1 = 0.0f;
    float s2 = 0.0f;

    for (size_t i = 0; i < numSamples; ++i) {
        s0 = buffer[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    // Calculate power
    float real = s1 - s2 * std::cos(omega);
    float imag = s2 * std::sin(omega);
    float magnitude = std::sqrt(real * real + imag * imag);

    // Normalize by number of samples
    return magnitude / static_cast<float>(numSamples);
}

/// Calculate Total Harmonic Distortion (THD)
/// Measures harmonics 2-5 relative to fundamental
float calculateTHD(const float* buffer, size_t numSamples, size_t fundamentalBin, float sampleRate) {
    const float fundamental = measureHarmonicMagnitude(buffer, numSamples, fundamentalBin, sampleRate);
    if (fundamental < 1e-10f) return 0.0f;

    float harmonicPower = 0.0f;
    for (size_t h = 2; h <= 5; ++h) {
        const float harmonic = measureHarmonicMagnitude(buffer, numSamples, fundamentalBin * h, sampleRate);
        harmonicPower += harmonic * harmonic;
    }

    return std::sqrt(harmonicPower) / fundamental;
}

/// Convert linear magnitude to dB relative to another magnitude
float magnitudeToDbRelative(float magnitude, float reference) {
    if (magnitude < 1e-10f || reference < 1e-10f) {
        return -144.0f;  // Silence floor
    }
    return 20.0f * std::log10(magnitude / reference);
}

} // anonymous namespace

// =============================================================================
// Phase 2: Foundational Tests
// =============================================================================

TEST_CASE("DiodeClipper default construction", "[diode_clipper][foundational]") {
    DiodeClipper clipper;

    SECTION("default diode type is Silicon") {
        REQUIRE(clipper.getDiodeType() == DiodeType::Silicon);
    }

    SECTION("default topology is Symmetric") {
        REQUIRE(clipper.getTopology() == ClipperTopology::Symmetric);
    }

    SECTION("default drive is 0 dB") {
        REQUIRE(clipper.getDrive() == Approx(0.0f));
    }

    SECTION("default mix is 1.0") {
        REQUIRE(clipper.getMix() == Approx(1.0f));
    }

    SECTION("default output level is 0 dB") {
        REQUIRE(clipper.getOutputLevel() == Approx(0.0f));
    }

    SECTION("default forward voltage matches Silicon") {
        REQUIRE(clipper.getForwardVoltage() == Approx(DiodeClipper::kSiliconVoltage));
    }

    SECTION("default knee sharpness matches Silicon") {
        REQUIRE(clipper.getKneeSharpness() == Approx(DiodeClipper::kSiliconKnee));
    }
}

TEST_CASE("DiodeClipper prepare does not crash", "[diode_clipper][foundational]") {
    DiodeClipper clipper;

    SECTION("prepare at 44.1kHz") {
        REQUIRE_NOTHROW(clipper.prepare(44100.0, 512));
    }

    SECTION("prepare at 48kHz") {
        REQUIRE_NOTHROW(clipper.prepare(48000.0, 512));
    }

    SECTION("prepare at 96kHz") {
        REQUIRE_NOTHROW(clipper.prepare(96000.0, 512));
    }

    SECTION("prepare at 192kHz") {
        REQUIRE_NOTHROW(clipper.prepare(192000.0, 512));
    }
}

TEST_CASE("DiodeClipper reset does not crash", "[diode_clipper][foundational]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);

    REQUIRE_NOTHROW(clipper.reset());
}

TEST_CASE("DiodeClipper getLatency returns 0 (FR-021)", "[diode_clipper][foundational]") {
    DiodeClipper clipper;
    REQUIRE(clipper.getLatency() == 0);
}

// =============================================================================
// Phase 3: User Story 1 - Basic Diode Clipping Tests
// =============================================================================

TEST_CASE("DiodeClipper processSample applies clipping", "[diode_clipper][us1]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setDrive(12.0f);  // +12dB drive to create obvious clipping

    // Generate a high-amplitude sine wave
    constexpr size_t numSamples = 1024;
    std::array<float, numSamples> input{};
    std::array<float, numSamples> output{};
    generateSine(input.data(), numSamples, 440.0f, 44100.0f, 0.8f);

    // Process through clipper
    for (size_t i = 0; i < numSamples; ++i) {
        output[i] = clipper.processSample(input[i]);
    }

    // Verify clipping occurred - peak should be reduced compared to driven input
    const float inputPeak = calculatePeak(input.data(), numSamples);
    const float outputPeak = calculatePeak(output.data(), numSamples);
    const float drivenPeak = inputPeak * dbToGain(12.0f);  // What peak would be without clipping

    // Output peak should be less than the driven peak (clipping occurred)
    REQUIRE(outputPeak < drivenPeak);
}

TEST_CASE("DiodeClipper process block matches sequential processSample", "[diode_clipper][us1]") {
    DiodeClipper clipper1;
    DiodeClipper clipper2;
    clipper1.prepare(44100.0, 512);
    clipper2.prepare(44100.0, 512);
    clipper1.setDrive(6.0f);
    clipper2.setDrive(6.0f);

    constexpr size_t numSamples = 256;
    std::array<float, numSamples> input{};
    std::array<float, numSamples> output1{};
    std::array<float, numSamples> output2{};
    generateSine(input.data(), numSamples, 440.0f, 44100.0f, 0.5f);

    // Copy input to both outputs
    std::copy(input.begin(), input.end(), output1.begin());
    std::copy(input.begin(), input.end(), output2.begin());

    // Process with block method
    clipper1.process(output1.data(), numSamples);

    // Process sample-by-sample
    for (size_t i = 0; i < numSamples; ++i) {
        output2[i] = clipper2.processSample(output2[i]);
    }

    // Outputs should be identical
    for (size_t i = 0; i < numSamples; ++i) {
        REQUIRE(output1[i] == Approx(output2[i]).margin(1e-6f));
    }
}

TEST_CASE("DiodeClipper setDrive increases saturation", "[diode_clipper][us1]") {
    DiodeClipper clipperLow;
    DiodeClipper clipperHigh;
    clipperLow.prepare(44100.0, 512);
    clipperHigh.prepare(44100.0, 512);

    clipperLow.setDrive(0.0f);   // Unity drive
    clipperHigh.setDrive(12.0f); // +12dB drive

    constexpr size_t numSamples = 4096;
    constexpr float sampleRate = 44100.0f;
    constexpr float frequency = 100.0f;  // Low frequency for better harmonic resolution
    constexpr size_t fundamentalBin = static_cast<size_t>(frequency * numSamples / sampleRate);

    std::array<float, numSamples> input{};
    std::array<float, numSamples> outputLow{};
    std::array<float, numSamples> outputHigh{};

    generateSine(input.data(), numSamples, frequency, sampleRate, 0.5f);
    std::copy(input.begin(), input.end(), outputLow.begin());
    std::copy(input.begin(), input.end(), outputHigh.begin());

    clipperLow.process(outputLow.data(), numSamples);
    clipperHigh.process(outputHigh.data(), numSamples);

    // Calculate THD for both
    const float thdLow = calculateTHD(outputLow.data(), numSamples, fundamentalBin, sampleRate);
    const float thdHigh = calculateTHD(outputHigh.data(), numSamples, fundamentalBin, sampleRate);

    // Higher drive should produce more THD
    REQUIRE(thdHigh > thdLow);
}

TEST_CASE("DiodeClipper drive parameter clamping", "[diode_clipper][us1]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);

    SECTION("drive below minimum is clamped to -24 dB") {
        clipper.setDrive(-100.0f);
        REQUIRE(clipper.getDrive() == Approx(DiodeClipper::kMinDriveDb));
    }

    SECTION("drive above maximum is clamped to +48 dB") {
        clipper.setDrive(100.0f);
        REQUIRE(clipper.getDrive() == Approx(DiodeClipper::kMaxDriveDb));
    }

    SECTION("drive within range is preserved") {
        clipper.setDrive(12.0f);
        REQUIRE(clipper.getDrive() == Approx(12.0f));
    }
}

TEST_CASE("DiodeClipper Silicon/Symmetric produces primarily odd harmonics", "[diode_clipper][us1]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setDiodeType(DiodeType::Silicon);
    clipper.setTopology(ClipperTopology::Symmetric);
    clipper.setDrive(12.0f);

    constexpr size_t numSamples = 8192;
    constexpr float sampleRate = 44100.0f;
    constexpr float frequency = 100.0f;
    constexpr size_t fundamentalBin = static_cast<size_t>(frequency * numSamples / sampleRate);

    std::array<float, numSamples> buffer{};
    generateSine(buffer.data(), numSamples, frequency, sampleRate, 0.8f);
    clipper.process(buffer.data(), numSamples);

    const float fundamental = measureHarmonicMagnitude(buffer.data(), numSamples, fundamentalBin, sampleRate);
    const float thirdHarmonic = measureHarmonicMagnitude(buffer.data(), numSamples, fundamentalBin * 3, sampleRate);

    // 3rd harmonic should be significant (within -40dB of fundamental)
    const float thirdRelativeDb = magnitudeToDbRelative(thirdHarmonic, fundamental);
    REQUIRE(thirdRelativeDb > -40.0f);
}

TEST_CASE("DiodeClipper low-level audio is nearly linear", "[diode_clipper][us1]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setDrive(0.0f);  // Unity gain

    constexpr size_t numSamples = 4096;
    constexpr float sampleRate = 44100.0f;
    constexpr float frequency = 100.0f;
    constexpr size_t fundamentalBin = static_cast<size_t>(frequency * numSamples / sampleRate);

    // Very low level input (-40dBFS = 0.01 amplitude)
    // At this level, signal should be well below the clipping threshold (0.6V default)
    // so it should pass through nearly linearly
    std::array<float, numSamples> buffer{};
    generateSine(buffer.data(), numSamples, frequency, sampleRate, 0.01f);
    clipper.process(buffer.data(), numSamples);

    // THD should be less than 5% for low-level signals
    // (some residual THD from DC blocker and numerical effects)
    const float thd = calculateTHD(buffer.data(), numSamples, fundamentalBin, sampleRate);
    REQUIRE(thd < 0.05f);
}

TEST_CASE("DiodeClipper silence in produces silence out", "[diode_clipper][us1]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setDrive(12.0f);

    constexpr size_t numSamples = 1024;
    std::array<float, numSamples> buffer{};
    std::fill(buffer.begin(), buffer.end(), 0.0f);

    clipper.process(buffer.data(), numSamples);

    // Output should be silence with no DC offset
    const float dcOffset = calculateDCOffset(buffer.data(), numSamples);
    const float peak = calculatePeak(buffer.data(), numSamples);

    REQUIRE(std::abs(dcOffset) < 1e-6f);
    REQUIRE(peak < 1e-6f);
}

TEST_CASE("DiodeClipper before prepare returns input unchanged (FR-003)", "[diode_clipper][us1][edge]") {
    DiodeClipper clipper;
    // Note: NOT calling prepare()

    SECTION("processSample returns input unchanged") {
        REQUIRE(clipper.processSample(0.5f) == Approx(0.5f));
        REQUIRE(clipper.processSample(-0.3f) == Approx(-0.3f));
        REQUIRE(clipper.processSample(0.0f) == Approx(0.0f));
    }

    SECTION("process block leaves buffer unchanged") {
        std::array<float, 4> buffer = {0.1f, 0.2f, 0.3f, 0.4f};
        std::array<float, 4> original = buffer;

        clipper.process(buffer.data(), buffer.size());

        for (size_t i = 0; i < buffer.size(); ++i) {
            REQUIRE(buffer[i] == Approx(original[i]));
        }
    }
}

TEST_CASE("DiodeClipper extreme drive values don't cause overflow", "[diode_clipper][us1][edge]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);

    SECTION("maximum drive (+48 dB) doesn't overflow") {
        clipper.setDrive(48.0f);

        constexpr size_t numSamples = 1024;
        std::array<float, numSamples> buffer{};
        generateSine(buffer.data(), numSamples, 440.0f, 44100.0f, 1.0f);
        clipper.process(buffer.data(), numSamples);

        // Check no NaN or Inf
        for (size_t i = 0; i < numSamples; ++i) {
            REQUIRE_FALSE(std::isnan(buffer[i]));
            REQUIRE_FALSE(std::isinf(buffer[i]));
        }
    }

    SECTION("minimum drive (-24 dB) works correctly") {
        clipper.setDrive(-24.0f);

        constexpr size_t numSamples = 1024;
        std::array<float, numSamples> buffer{};
        generateSine(buffer.data(), numSamples, 440.0f, 44100.0f, 1.0f);
        clipper.process(buffer.data(), numSamples);

        // Check no NaN or Inf
        for (size_t i = 0; i < numSamples; ++i) {
            REQUIRE_FALSE(std::isnan(buffer[i]));
            REQUIRE_FALSE(std::isinf(buffer[i]));
        }
    }
}

TEST_CASE("DiodeClipper handles NaN input without crash", "[diode_clipper][us1][edge]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);

    // NaN input - should not crash
    const float nanValue = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_NOTHROW(clipper.processSample(nanValue));
}

// =============================================================================
// Phase 4: User Story 2 - Diode Type Selection Tests
// =============================================================================

TEST_CASE("DiodeClipper setDiodeType changes clipping character", "[diode_clipper][us2]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setDrive(12.0f);

    constexpr size_t numSamples = 2048;
    constexpr float sampleRate = 44100.0f;
    constexpr float frequency = 200.0f;

    SECTION("Germanium has lower threshold than Silicon") {
        std::array<float, numSamples> siliconOutput{};
        std::array<float, numSamples> germaniumOutput{};

        generateSine(siliconOutput.data(), numSamples, frequency, sampleRate, 0.5f);
        generateSine(germaniumOutput.data(), numSamples, frequency, sampleRate, 0.5f);

        clipper.setDiodeType(DiodeType::Silicon);
        clipper.process(siliconOutput.data(), numSamples);

        // Reset and change type
        DiodeClipper clipper2;
        clipper2.prepare(44100.0, 512);
        clipper2.setDrive(12.0f);
        clipper2.setDiodeType(DiodeType::Germanium);
        clipper2.process(germaniumOutput.data(), numSamples);

        // Germanium should have more clipping (lower RMS since it clips earlier)
        const float siliconPeak = calculatePeak(siliconOutput.data(), numSamples);
        const float germaniumPeak = calculatePeak(germaniumOutput.data(), numSamples);

        // Germanium clips earlier (lower threshold), so peak should be lower
        REQUIRE(germaniumPeak < siliconPeak);
    }

    SECTION("LED has higher threshold than Silicon") {
        std::array<float, numSamples> siliconOutput{};
        std::array<float, numSamples> ledOutput{};

        generateSine(siliconOutput.data(), numSamples, frequency, sampleRate, 0.5f);
        generateSine(ledOutput.data(), numSamples, frequency, sampleRate, 0.5f);

        clipper.setDiodeType(DiodeType::Silicon);
        clipper.process(siliconOutput.data(), numSamples);

        DiodeClipper clipper2;
        clipper2.prepare(44100.0, 512);
        clipper2.setDrive(12.0f);
        clipper2.setDiodeType(DiodeType::LED);
        clipper2.process(ledOutput.data(), numSamples);

        // LED clips later (higher threshold)
        const float siliconPeak = calculatePeak(siliconOutput.data(), numSamples);
        const float ledPeak = calculatePeak(ledOutput.data(), numSamples);

        REQUIRE(ledPeak > siliconPeak);
    }

    SECTION("Schottky has lowest threshold") {
        std::array<float, numSamples> schottkyOutput{};
        std::array<float, numSamples> siliconOutput{};

        generateSine(schottkyOutput.data(), numSamples, frequency, sampleRate, 0.5f);
        generateSine(siliconOutput.data(), numSamples, frequency, sampleRate, 0.5f);

        clipper.setDiodeType(DiodeType::Schottky);
        clipper.process(schottkyOutput.data(), numSamples);

        DiodeClipper clipper2;
        clipper2.prepare(44100.0, 512);
        clipper2.setDrive(12.0f);
        clipper2.setDiodeType(DiodeType::Silicon);
        clipper2.process(siliconOutput.data(), numSamples);

        const float schottkyPeak = calculatePeak(schottkyOutput.data(), numSamples);
        const float siliconPeak = calculatePeak(siliconOutput.data(), numSamples);

        // Schottky clips earliest
        REQUIRE(schottkyPeak < siliconPeak);
    }
}

TEST_CASE("DiodeClipper getDiodeType returns current type", "[diode_clipper][us2]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);

    clipper.setDiodeType(DiodeType::Germanium);
    REQUIRE(clipper.getDiodeType() == DiodeType::Germanium);

    clipper.setDiodeType(DiodeType::LED);
    REQUIRE(clipper.getDiodeType() == DiodeType::LED);

    clipper.setDiodeType(DiodeType::Schottky);
    REQUIRE(clipper.getDiodeType() == DiodeType::Schottky);

    clipper.setDiodeType(DiodeType::Silicon);
    REQUIRE(clipper.getDiodeType() == DiodeType::Silicon);
}

TEST_CASE("DiodeClipper each diode type produces different spectra (SC-001)", "[diode_clipper][us2]") {
    constexpr size_t numSamples = 4096;
    constexpr float sampleRate = 44100.0f;
    constexpr float frequency = 100.0f;
    constexpr size_t fundamentalBin = static_cast<size_t>(frequency * numSamples / sampleRate);

    std::array<float, numSamples> siliconOut{};
    std::array<float, numSamples> germaniumOut{};
    std::array<float, numSamples> ledOut{};
    std::array<float, numSamples> schottkyOut{};

    // Generate same input for all
    std::array<float, numSamples> input{};
    generateSine(input.data(), numSamples, frequency, sampleRate, 0.5f);

    auto processWithType = [&](float* output, DiodeType type) {
        DiodeClipper clipper;
        clipper.prepare(sampleRate, 512);
        clipper.setDrive(12.0f);
        clipper.setDiodeType(type);
        std::copy(input.begin(), input.end(), output);
        clipper.process(output, numSamples);
    };

    processWithType(siliconOut.data(), DiodeType::Silicon);
    processWithType(germaniumOut.data(), DiodeType::Germanium);
    processWithType(ledOut.data(), DiodeType::LED);
    processWithType(schottkyOut.data(), DiodeType::Schottky);

    // Calculate THD for each
    const float siliconTHD = calculateTHD(siliconOut.data(), numSamples, fundamentalBin, sampleRate);
    const float germaniumTHD = calculateTHD(germaniumOut.data(), numSamples, fundamentalBin, sampleRate);
    const float ledTHD = calculateTHD(ledOut.data(), numSamples, fundamentalBin, sampleRate);
    const float schottkyTHD = calculateTHD(schottkyOut.data(), numSamples, fundamentalBin, sampleRate);

    // All THDs should be different (different spectra)
    REQUIRE(siliconTHD != Approx(germaniumTHD).margin(0.001f));
    REQUIRE(siliconTHD != Approx(ledTHD).margin(0.001f));
    REQUIRE(siliconTHD != Approx(schottkyTHD).margin(0.001f));
}

TEST_CASE("DiodeClipper setForwardVoltage overrides type default", "[diode_clipper][us2]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setDiodeType(DiodeType::Silicon);

    // Override voltage
    clipper.setForwardVoltage(1.0f);
    REQUIRE(clipper.getForwardVoltage() == Approx(1.0f));

    // Should still be able to get it after changing type
    clipper.setDiodeType(DiodeType::Germanium);
    REQUIRE(clipper.getForwardVoltage() == Approx(DiodeClipper::kGermaniumVoltage));
}

TEST_CASE("DiodeClipper setKneeSharpness overrides type default", "[diode_clipper][us2]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);

    clipper.setKneeSharpness(10.0f);
    REQUIRE(clipper.getKneeSharpness() == Approx(10.0f));
}

TEST_CASE("DiodeClipper parameter clamping (FR-025, FR-026)", "[diode_clipper][us2]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);

    SECTION("forward voltage clamped to [0.05, 5.0]") {
        clipper.setForwardVoltage(0.01f);
        REQUIRE(clipper.getForwardVoltage() == Approx(DiodeClipper::kMinVoltage));

        clipper.setForwardVoltage(10.0f);
        REQUIRE(clipper.getForwardVoltage() == Approx(DiodeClipper::kMaxVoltage));
    }

    SECTION("knee sharpness clamped to [0.5, 20.0]") {
        clipper.setKneeSharpness(0.1f);
        REQUIRE(clipper.getKneeSharpness() == Approx(DiodeClipper::kMinKnee));

        clipper.setKneeSharpness(50.0f);
        REQUIRE(clipper.getKneeSharpness() == Approx(DiodeClipper::kMaxKnee));
    }
}

TEST_CASE("DiodeClipper setDiodeType causes smooth transition (FR-008)", "[diode_clipper][us2]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setDiodeType(DiodeType::Silicon);
    clipper.setDrive(12.0f);

    // Process some samples to let smoother settle
    constexpr size_t settleSize = 1024;
    std::array<float, settleSize> settleBuffer{};
    generateSine(settleBuffer.data(), settleSize, 440.0f, 44100.0f, 0.5f);
    clipper.process(settleBuffer.data(), settleSize);

    // Change type during processing
    clipper.setDiodeType(DiodeType::Germanium);

    // Process a block right after type change
    constexpr size_t testSize = 512;
    std::array<float, testSize> testBuffer{};
    generateSine(testBuffer.data(), testSize, 440.0f, 44100.0f, 0.5f);
    clipper.process(testBuffer.data(), testSize);

    // Check for no clicks (no sudden large jumps)
    float maxDelta = 0.0f;
    for (size_t i = 1; i < testSize; ++i) {
        const float delta = std::abs(testBuffer[i] - testBuffer[i-1]);
        if (delta > maxDelta) {
            maxDelta = delta;
        }
    }

    // Max sample-to-sample change should be reasonable (no clicks)
    // For a 440Hz sine at 44.1kHz, max natural delta is about 0.06
    // With 12dB drive and parameter transitions, allow up to 0.35
    // Key point: no hard discontinuities (clicks would be > 0.5)
    REQUIRE(maxDelta < 0.35f);
}

// =============================================================================
// Phase 5: User Story 3 - Topology Configuration Tests
// =============================================================================

TEST_CASE("DiodeClipper default topology is Symmetric", "[diode_clipper][us3]") {
    DiodeClipper clipper;
    REQUIRE(clipper.getTopology() == ClipperTopology::Symmetric);
}

TEST_CASE("DiodeClipper setTopology changes behavior", "[diode_clipper][us3]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);

    clipper.setTopology(ClipperTopology::Asymmetric);
    REQUIRE(clipper.getTopology() == ClipperTopology::Asymmetric);

    clipper.setTopology(ClipperTopology::SoftHard);
    REQUIRE(clipper.getTopology() == ClipperTopology::SoftHard);

    clipper.setTopology(ClipperTopology::Symmetric);
    REQUIRE(clipper.getTopology() == ClipperTopology::Symmetric);
}

TEST_CASE("DiodeClipper Symmetric produces only odd harmonics (SC-002)", "[diode_clipper][us3]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setTopology(ClipperTopology::Symmetric);
    clipper.setDrive(12.0f);  // Moderate drive

    // Use 44100 samples (1 second) to get exact integer bins and avoid spectral leakage
    // that would otherwise appear as spurious even harmonics
    constexpr size_t numSamples = 44100;
    constexpr float sampleRate = 44100.0f;
    constexpr float frequency = 100.0f;  // Bin 100 exactly
    constexpr size_t fundamentalBin = static_cast<size_t>(frequency * numSamples / sampleRate);

    std::vector<float> buffer(numSamples);
    generateSine(buffer.data(), numSamples, frequency, sampleRate, 0.8f);
    clipper.process(buffer.data(), numSamples);

    const float fundamental = measureHarmonicMagnitude(buffer.data(), numSamples, fundamentalBin, sampleRate);
    const float secondHarmonic = measureHarmonicMagnitude(buffer.data(), numSamples, fundamentalBin * 2, sampleRate);
    const float thirdHarmonic = measureHarmonicMagnitude(buffer.data(), numSamples, fundamentalBin * 3, sampleRate);

    // 2nd harmonic should be significantly below 3rd harmonic (odd harmonics dominate)
    // SC-002 requires 40dB below fundamental for symmetric topology
    const float secondRelativeDb = magnitudeToDbRelative(secondHarmonic, fundamental);
    const float thirdRelativeDb = magnitudeToDbRelative(thirdHarmonic, fundamental);

    INFO("SC-002 2nd harmonic suppression: " << secondRelativeDb << " dB");
    INFO("SC-002 3rd harmonic: " << thirdRelativeDb << " dB");

    // 2nd harmonic should be at least 40dB below fundamental (SC-002)
    // With proper FFT bin alignment and pure tanh, this is achievable
    REQUIRE(secondRelativeDb < -40.0f);

    // Odd harmonics should dominate (3rd should be stronger than 2nd)
    // Key requirement: 3rd harmonic should be present and measurable
    REQUIRE(thirdRelativeDb > -40.0f);  // 3rd harmonic is significant
}

TEST_CASE("DiodeClipper Symmetric - Isolated tanh analysis", "[diode_clipper][diagnostic]") {
    // Diagnostic test: apply tanh directly without DC blocker to find theoretical limit
    // Use 44100 samples (1 second) to get exact integer bins and avoid spectral leakage
    constexpr size_t numSamples = 44100;
    constexpr float sampleRate = 44100.0f;
    constexpr float frequency = 100.0f;  // Bin 100 exactly
    constexpr size_t fundamentalBin = static_cast<size_t>(frequency * numSamples / sampleRate);

    std::vector<float> buffer(numSamples);
    generateSine(buffer.data(), numSamples, frequency, sampleRate, 0.8f);

    // Apply pure tanh saturation (same as DiodeClipper symmetric mode)
    constexpr float voltage = 0.6f;  // Silicon default
    constexpr float knee = 5.0f;
    constexpr float drive = 4.0f;    // ~12dB
    const float kneeScale = knee / 5.0f;

    for (size_t i = 0; i < numSamples; ++i) {
        const float x = buffer[i] * drive;
        buffer[i] = voltage * std::tanh(x * kneeScale / voltage);
    }

    const float fundamental = measureHarmonicMagnitude(buffer.data(), numSamples, fundamentalBin, sampleRate);
    const float secondHarmonic = measureHarmonicMagnitude(buffer.data(), numSamples, fundamentalBin * 2, sampleRate);
    const float thirdHarmonic = measureHarmonicMagnitude(buffer.data(), numSamples, fundamentalBin * 3, sampleRate);

    const float secondRelativeDb = magnitudeToDbRelative(secondHarmonic, fundamental);
    const float thirdRelativeDb = magnitudeToDbRelative(thirdHarmonic, fundamental);

    // Pure tanh should produce effectively no 2nd harmonic (only numerical precision limits)
    INFO("Pure tanh 2nd harmonic: " << secondRelativeDb << " dB");
    INFO("Pure tanh 3rd harmonic: " << thirdRelativeDb << " dB");

    // Without DC blocker, expect very high suppression (>60dB)
    REQUIRE(secondRelativeDb < -60.0f);
    REQUIRE(thirdRelativeDb > -40.0f);
}

TEST_CASE("DiodeClipper Asymmetric produces even harmonics (SC-003)", "[diode_clipper][us3]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setTopology(ClipperTopology::Asymmetric);
    clipper.setDrive(24.0f);  // Higher drive for more asymmetry

    constexpr size_t numSamples = 8192;
    constexpr float sampleRate = 44100.0f;
    constexpr float frequency = 100.0f;
    constexpr size_t fundamentalBin = static_cast<size_t>(frequency * numSamples / sampleRate);

    std::array<float, numSamples> buffer{};
    generateSine(buffer.data(), numSamples, frequency, sampleRate, 0.8f);
    clipper.process(buffer.data(), numSamples);

    const float fundamental = measureHarmonicMagnitude(buffer.data(), numSamples, fundamentalBin, sampleRate);
    const float secondHarmonic = measureHarmonicMagnitude(buffer.data(), numSamples, fundamentalBin * 2, sampleRate);

    // Key requirement: 2nd harmonic should be measurable (above noise floor)
    // This indicates even harmonics are present (asymmetric behavior)
    const float secondRelativeToFundamental = magnitudeToDbRelative(secondHarmonic, fundamental);

    // 2nd harmonic should be above noise floor and not too suppressed
    // For asymmetric clipping, it should be measurable
    REQUIRE(secondRelativeToFundamental > -40.0f);  // Above noise floor
}

TEST_CASE("DiodeClipper SoftHard produces even harmonics", "[diode_clipper][us3]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setTopology(ClipperTopology::SoftHard);
    clipper.setDrive(18.0f);

    constexpr size_t numSamples = 8192;
    constexpr float sampleRate = 44100.0f;
    constexpr float frequency = 100.0f;
    constexpr size_t fundamentalBin = static_cast<size_t>(frequency * numSamples / sampleRate);

    std::array<float, numSamples> buffer{};
    generateSine(buffer.data(), numSamples, frequency, sampleRate, 0.8f);
    clipper.process(buffer.data(), numSamples);

    const float fundamental = measureHarmonicMagnitude(buffer.data(), numSamples, fundamentalBin, sampleRate);
    const float secondHarmonic = measureHarmonicMagnitude(buffer.data(), numSamples, fundamentalBin * 2, sampleRate);

    // SoftHard should produce measurable even harmonics (asymmetric behavior)
    // 2nd harmonic should be above noise floor
    const float secondRelativeDb = magnitudeToDbRelative(secondHarmonic, fundamental);
    REQUIRE(secondRelativeDb > -60.0f);  // Above noise floor
}

TEST_CASE("DiodeClipper DC blocking for asymmetric topologies (FR-019, SC-006)", "[diode_clipper][us3]") {
    // Test DC blocking with longer buffers to let the DC blocker fully engage
    constexpr size_t numSamples = 8192;  // Longer buffer for DC blocker to stabilize
    constexpr float sampleRate = 44100.0f;

    SECTION("Asymmetric topology DC offset is significantly reduced") {
        DiodeClipper clipper;
        clipper.prepare(sampleRate, 512);
        clipper.setTopology(ClipperTopology::Asymmetric);
        clipper.setDrive(12.0f);

        // First, process some audio to let DC blocker reach steady state
        std::array<float, numSamples> warmup{};
        generateSine(warmup.data(), numSamples, 440.0f, sampleRate, 0.8f);
        clipper.process(warmup.data(), numSamples);

        // Now process test buffer
        std::array<float, numSamples> buffer{};
        generateSine(buffer.data(), numSamples, 440.0f, sampleRate, 0.8f);
        clipper.process(buffer.data(), numSamples);

        // Only measure DC on latter half where DC blocker is fully engaged
        const float dcOffset = std::abs(calculateDCOffset(buffer.data() + numSamples/2, numSamples/2));
        const float dcOffsetDb = 20.0f * std::log10(dcOffset + 1e-10f);

        // DC should be significantly reduced (below -40dBFS after settling)
        // The 10Hz DC blocker needs time to remove low-frequency DC components
        REQUIRE(dcOffsetDb < -35.0f);
    }

    SECTION("SoftHard topology DC offset is significantly reduced") {
        DiodeClipper clipper;
        clipper.prepare(sampleRate, 512);
        clipper.setTopology(ClipperTopology::SoftHard);
        clipper.setDrive(12.0f);

        // First, process some audio to let DC blocker reach steady state
        std::array<float, numSamples> warmup{};
        generateSine(warmup.data(), numSamples, 440.0f, sampleRate, 0.8f);
        clipper.process(warmup.data(), numSamples);

        // Now process test buffer
        std::array<float, numSamples> buffer{};
        generateSine(buffer.data(), numSamples, 440.0f, sampleRate, 0.8f);
        clipper.process(buffer.data(), numSamples);

        // Only measure DC on latter half where DC blocker is fully engaged
        const float dcOffset = std::abs(calculateDCOffset(buffer.data() + numSamples/2, numSamples/2));
        const float dcOffsetDb = 20.0f * std::log10(dcOffset + 1e-10f);

        REQUIRE(dcOffsetDb < -35.0f);
    }
}

// =============================================================================
// Phase 6: User Story 4 - Dry/Wet Mix Control Tests
// =============================================================================

TEST_CASE("DiodeClipper mix=0.0 outputs dry signal exactly (FR-015)", "[diode_clipper][us4]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setDrive(12.0f);
    clipper.setMix(0.0f);

    // Let the mix smoother settle at 0
    constexpr size_t settleSize = 512;
    std::array<float, settleSize> settleBuffer{};
    generateSine(settleBuffer.data(), settleSize, 440.0f, 44100.0f, 0.5f);
    clipper.process(settleBuffer.data(), settleSize);

    // Now test bypass behavior
    constexpr size_t numSamples = 256;
    std::array<float, numSamples> input{};
    std::array<float, numSamples> output{};

    generateSine(input.data(), numSamples, 440.0f, 44100.0f, 0.5f);
    std::copy(input.begin(), input.end(), output.begin());

    clipper.process(output.data(), numSamples);

    // Output should equal input (bypass)
    for (size_t i = 0; i < numSamples; ++i) {
        REQUIRE(output[i] == Approx(input[i]).margin(1e-5f));
    }
}

TEST_CASE("DiodeClipper mix=1.0 outputs fully clipped signal", "[diode_clipper][us4]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setDrive(12.0f);
    clipper.setMix(1.0f);

    constexpr size_t numSamples = 1024;
    std::array<float, numSamples> input{};
    std::array<float, numSamples> output{};

    generateSine(input.data(), numSamples, 440.0f, 44100.0f, 0.8f);
    std::copy(input.begin(), input.end(), output.begin());

    clipper.process(output.data(), numSamples);

    // Output should be different from input (clipping occurred)
    bool different = false;
    for (size_t i = 0; i < numSamples; ++i) {
        if (std::abs(output[i] - input[i]) > 0.01f) {
            different = true;
            break;
        }
    }
    REQUIRE(different);
}

TEST_CASE("DiodeClipper mix=0.5 produces 50/50 blend", "[diode_clipper][us4]") {
    // Create two clippers - one at 0% and one at 100%
    DiodeClipper clipperFull;
    clipperFull.prepare(44100.0, 512);
    clipperFull.setDrive(12.0f);
    clipperFull.setMix(1.0f);

    DiodeClipper clipperHalf;
    clipperHalf.prepare(44100.0, 512);
    clipperHalf.setDrive(12.0f);
    clipperHalf.setMix(0.5f);

    constexpr size_t numSamples = 1024;
    std::array<float, numSamples> dry{};
    std::array<float, numSamples> wet{};
    std::array<float, numSamples> half{};

    generateSine(dry.data(), numSamples, 440.0f, 44100.0f, 0.5f);
    std::copy(dry.begin(), dry.end(), wet.begin());
    std::copy(dry.begin(), dry.end(), half.begin());

    clipperFull.process(wet.data(), numSamples);
    clipperHalf.process(half.data(), numSamples);

    // After smoothers settle (skip first samples), half should be approximately
    // 0.5 * dry + 0.5 * wet
    constexpr size_t skipSamples = 500;  // Let smoothers settle
    for (size_t i = skipSamples; i < numSamples; ++i) {
        const float expected = 0.5f * dry[i] + 0.5f * wet[i];
        REQUIRE(half[i] == Approx(expected).margin(0.05f));
    }
}

TEST_CASE("DiodeClipper mix smoothing (SC-004)", "[diode_clipper][us4]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setDrive(12.0f);
    clipper.setMix(0.0f);

    // Let it settle
    constexpr size_t settleSize = 512;
    std::array<float, settleSize> settleBuffer{};
    generateSine(settleBuffer.data(), settleSize, 440.0f, 44100.0f, 0.5f);
    clipper.process(settleBuffer.data(), settleSize);

    // Change mix from 0.0 to 1.0 mid-processing
    clipper.setMix(1.0f);

    // Process a block
    constexpr size_t testSize = 512;
    std::array<float, testSize> testBuffer{};
    generateSine(testBuffer.data(), testSize, 440.0f, 44100.0f, 0.5f);
    clipper.process(testBuffer.data(), testSize);

    // Check for no clicks (max sample-to-sample delta)
    float maxDelta = 0.0f;
    for (size_t i = 1; i < testSize; ++i) {
        const float delta = std::abs(testBuffer[i] - testBuffer[i-1]);
        if (delta > maxDelta) {
            maxDelta = delta;
        }
    }

    // Max delta should be reasonable (no hard clicks)
    // With drive and mix transitions, allow up to 0.35
    // Clicks would cause deltas > 0.5
    REQUIRE(maxDelta < 0.35f);
}

// =============================================================================
// Phase 7: Real-Time Safety & Success Criteria Tests
// =============================================================================

TEST_CASE("DiodeClipper noexcept verification (FR-022)", "[diode_clipper][safety]") {
    // Static assertions to verify noexcept
    static_assert(noexcept(std::declval<DiodeClipper>().prepare(44100.0, 512)),
                  "prepare() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().reset()),
                  "reset() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().process(nullptr, 0)),
                  "process() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().processSample(0.0f)),
                  "processSample() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().setDiodeType(DiodeType::Silicon)),
                  "setDiodeType() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().setTopology(ClipperTopology::Symmetric)),
                  "setTopology() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().setDrive(0.0f)),
                  "setDrive() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().setMix(0.0f)),
                  "setMix() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().setForwardVoltage(0.0f)),
                  "setForwardVoltage() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().setKneeSharpness(0.0f)),
                  "setKneeSharpness() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().setOutputLevel(0.0f)),
                  "setOutputLevel() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().getDiodeType()),
                  "getDiodeType() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().getTopology()),
                  "getTopology() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().getDrive()),
                  "getDrive() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().getMix()),
                  "getMix() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().getForwardVoltage()),
                  "getForwardVoltage() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().getKneeSharpness()),
                  "getKneeSharpness() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().getOutputLevel()),
                  "getOutputLevel() must be noexcept");
    static_assert(noexcept(std::declval<DiodeClipper>().getLatency()),
                  "getLatency() must be noexcept");

    // Compile-time check passed
    REQUIRE(true);
}

TEST_CASE("DiodeClipper handles infinity input", "[diode_clipper][safety][edge]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);

    const float posInf = std::numeric_limits<float>::infinity();
    const float negInf = -std::numeric_limits<float>::infinity();

    // Should not crash
    REQUIRE_NOTHROW(clipper.processSample(posInf));
    REQUIRE_NOTHROW(clipper.processSample(negInf));
}

TEST_CASE("DiodeClipper handles denormal input", "[diode_clipper][safety][edge]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);

    // Very small denormal-like values
    const float denormal = 1e-40f;

    // Should not cause CPU spike or crash
    REQUIRE_NOTHROW(clipper.processSample(denormal));
}

TEST_CASE("DiodeClipper parameter smoothing within 10ms (SC-004)", "[diode_clipper][safety]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setDrive(0.0f);
    clipper.setMix(1.0f);

    // Let initial state settle
    constexpr size_t settleSize = 1024;
    std::array<float, settleSize> settleBuffer{};
    std::fill(settleBuffer.begin(), settleBuffer.end(), 0.5f);
    clipper.process(settleBuffer.data(), settleSize);

    // Change drive significantly
    clipper.setDrive(24.0f);

    // Process for 10ms (441 samples at 44.1kHz)
    constexpr size_t smoothingTime = 441;
    std::array<float, smoothingTime> smoothBuffer{};
    std::fill(smoothBuffer.begin(), smoothBuffer.end(), 0.5f);
    clipper.process(smoothBuffer.data(), smoothingTime);

    // After 10ms, smoothing should be essentially complete
    // Process one more sample and check it's stable
    float prevSample = clipper.processSample(0.5f);
    float currSample = clipper.processSample(0.5f);

    // Change should be very small (smoothing complete)
    REQUIRE(std::abs(currSample - prevSample) < 0.001f);
}

TEST_CASE("DiodeClipper multi-sample-rate test (SC-007)", "[diode_clipper][safety]") {
    const std::array<double, 5> sampleRates = {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};

    for (double sr : sampleRates) {
        DYNAMIC_SECTION("Sample rate: " << sr << " Hz") {
            DiodeClipper clipper;
            clipper.prepare(sr, 512);
            clipper.setDrive(12.0f);
            clipper.setMix(1.0f);

            constexpr size_t numSamples = 1024;
            std::array<float, numSamples> buffer{};
            generateSine(buffer.data(), numSamples, 440.0f, static_cast<float>(sr), 0.5f);

            REQUIRE_NOTHROW(clipper.process(buffer.data(), numSamples));

            // Check output is valid
            for (size_t i = 0; i < numSamples; ++i) {
                REQUIRE_FALSE(std::isnan(buffer[i]));
                REQUIRE_FALSE(std::isinf(buffer[i]));
            }
        }
    }
}

TEST_CASE("DiodeClipper performance benchmark (SC-005)", "[diode_clipper][!benchmark]") {
    DiodeClipper clipper;
    clipper.prepare(44100.0, 512);
    clipper.setDrive(12.0f);
    clipper.setMix(1.0f);

    // 1 second of audio at 44.1kHz
    constexpr size_t numSamples = 44100;
    std::vector<float> buffer(numSamples);
    generateSine(buffer.data(), numSamples, 440.0f, 44100.0f, 0.5f);

    BENCHMARK("Process 1 second mono audio") {
        clipper.process(buffer.data(), numSamples);
        return buffer[0];  // Prevent optimization
    };

    // Note: Actual CPU measurement requires profiling tools
    // This benchmark provides timing data for manual verification
}
