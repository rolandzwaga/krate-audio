// ==============================================================================
// Ring Modulator Unit Tests
// ==============================================================================
// Tests for the RingModulator Layer 2 DSP processor.
// Feature: 085-ring-mod-distortion
// Reference: specs/085-ring-mod-distortion/spec.md
//
// NOTE: This file must be in the -fno-fast-math block in CMakeLists.txt
// because it uses spectral analysis and floating-point comparisons that
// require IEEE 754 compliance.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <krate/dsp/processors/ring_modulator.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <test_signals.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Helper: Simple FFT magnitude (DFT at specific frequency bin)
// =============================================================================
namespace {

/// Compute the magnitude (in dB) of a specific frequency bin via DFT.
/// This is a direct DFT at a single bin, not a full FFT, but accurate for
/// measuring energy at a known frequency.
float magnitudeAtFrequency(const float* buffer, size_t numSamples,
                           float targetFreqHz, float sampleRate) {
    // Goertzel algorithm for single-bin DFT
    const float k = targetFreqHz * static_cast<float>(numSamples) / sampleRate;
    const float w = 2.0f * kPi * k / static_cast<float>(numSamples);
    const float coeff = 2.0f * std::cos(w);

    float s0 = 0.0f;
    float s1 = 0.0f;
    float s2 = 0.0f;

    for (size_t i = 0; i < numSamples; ++i) {
        s0 = buffer[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    const float real = s1 - s2 * std::cos(w);
    const float imag = s2 * std::sin(w);
    const float magnitude = std::sqrt(real * real + imag * imag) /
                            static_cast<float>(numSamples);

    // Convert to dB (floor at -120 dB)
    if (magnitude < 1e-6f) return -120.0f;
    return 20.0f * std::log10(magnitude);
}

} // anonymous namespace

// =============================================================================
// Lifecycle Tests (FR-008)
// =============================================================================

TEST_CASE("RingModulator lifecycle: prepare/reset/isPrepared", "[ring_modulator][lifecycle]") {
    RingModulator rm;

    SECTION("isPrepared returns false before prepare") {
        REQUIRE_FALSE(rm.isPrepared());
    }

    SECTION("isPrepared returns true after prepare") {
        rm.prepare(44100.0, 512);
        REQUIRE(rm.isPrepared());
    }

    SECTION("reset does not clear prepared state") {
        rm.prepare(44100.0, 512);
        rm.reset();
        REQUIRE(rm.isPrepared());
    }

    SECTION("re-prepare at different sample rate succeeds") {
        rm.prepare(44100.0, 512);
        rm.reset();
        rm.prepare(48000.0, 512);
        REQUIRE(rm.isPrepared());
    }
}

// =============================================================================
// Sine Carrier Output Range (FR-002)
// =============================================================================

TEST_CASE("RingModulator sine carrier output range [-1,+1]", "[ring_modulator][sine]") {
    RingModulator rm;
    // Set parameters BEFORE prepare so the smoother snaps to the correct freq
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f);
    rm.setAmplitude(1.0f);
    rm.prepare(44100.0, 512);

    // Fill buffer with full-scale sine at 1000 Hz
    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> buffer{};
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);

    // Warmup block to let smoother fully settle
    rm.processBlock(buffer.data(), kBlockSize);

    // Measurement block
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);
    rm.processBlock(buffer.data(), kBlockSize);

    float maxVal = 0.0f;
    for (float sample : buffer) {
        maxVal = std::max(maxVal, std::abs(sample));
    }

    // Ring mod output should not exceed 1.0 (input peak * carrier peak * amplitude)
    REQUIRE(maxVal <= 1.01f); // Small tolerance for floating-point
    REQUIRE(maxVal > 0.0f);   // Should produce non-zero output
}

// =============================================================================
// Sideband Correctness (SC-001)
// =============================================================================

TEST_CASE("RingModulator sideband correctness: 200 Hz carrier + 440 Hz input", "[ring_modulator][sideband][SC-001]") {
    // SC-001: 440 Hz sine input, 200 Hz sine carrier at full drive
    // Output must have peaks at 240 Hz and 640 Hz
    // 440 Hz fundamental must be suppressed by at least 60 dB relative to each sideband

    constexpr double kSampleRate = 44100.0;
    // Use N = 44100 so that freq resolution = 1 Hz exactly.
    // This ensures 200, 240, 440, 640 Hz all fall on exact DFT bins,
    // eliminating spectral leakage as a source of 440 Hz residual.
    constexpr size_t kBlockSize = 44100;
    constexpr float kCarrierFreq = 200.0f;
    constexpr float kInputFreq = 440.0f;

    RingModulator rm;
    // Set parameters BEFORE prepare so the smoother snaps to the correct freq
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(kCarrierFreq);
    rm.setAmplitude(1.0f);
    rm.prepare(kSampleRate, kBlockSize);

    // Allocate buffer on heap for larger block size
    std::vector<float> buffer(kBlockSize);

    // Warmup block to let oscillator reach steady state
    TestHelpers::generateSine(buffer.data(), kBlockSize, kInputFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Regenerate input for a clean measurement block
    TestHelpers::generateSine(buffer.data(), kBlockSize, kInputFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Measure spectral energy at key frequencies
    const float mag240 = magnitudeAtFrequency(buffer.data(), kBlockSize, 240.0f,
                                              static_cast<float>(kSampleRate));
    const float mag640 = magnitudeAtFrequency(buffer.data(), kBlockSize, 640.0f,
                                              static_cast<float>(kSampleRate));
    const float mag440 = magnitudeAtFrequency(buffer.data(), kBlockSize, kInputFreq,
                                              static_cast<float>(kSampleRate));

    // Each sideband must individually be at least 60 dB above the 440 Hz residual
    const float suppression240 = mag240 - mag440;
    const float suppression640 = mag640 - mag440;

    INFO("240 Hz magnitude: " << mag240 << " dB");
    INFO("640 Hz magnitude: " << mag640 << " dB");
    INFO("440 Hz magnitude: " << mag440 << " dB");
    INFO("Suppression (240 Hz - 440 Hz): " << suppression240 << " dB");
    INFO("Suppression (640 Hz - 440 Hz): " << suppression640 << " dB");

    REQUIRE(suppression240 >= 60.0f);
    REQUIRE(suppression640 >= 60.0f);

    // Both sidebands should have similar magnitude (within 1 dB)
    REQUIRE(std::abs(mag240 - mag640) < 1.0f);
}

// =============================================================================
// Mono processBlock Modifies Buffer (FR-001, FR-007)
// =============================================================================

TEST_CASE("RingModulator mono processBlock modifies buffer", "[ring_modulator][process]") {
    RingModulator rm;
    rm.prepare(44100.0, 512);
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f);
    rm.setAmplitude(1.0f);

    constexpr size_t kBlockSize = 512;
    std::array<float, kBlockSize> buffer{};
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);

    // Make a copy of the original
    std::array<float, kBlockSize> original{};
    std::copy(buffer.begin(), buffer.end(), original.begin());

    // Process -- skip first block to let smoother settle, use second
    rm.processBlock(buffer.data(), kBlockSize);
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);
    std::copy(buffer.begin(), buffer.end(), original.begin());
    rm.processBlock(buffer.data(), kBlockSize);

    // Buffer must be different from original
    bool different = false;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (buffer[i] != original[i]) {
            different = true;
            break;
        }
    }
    REQUIRE(different);
}

// =============================================================================
// Amplitude=0 Produces Silence (FR-005 scenario 4)
// =============================================================================

TEST_CASE("RingModulator amplitude=0 produces silence", "[ring_modulator][amplitude]") {
    RingModulator rm;
    rm.prepare(44100.0, 512);
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f);
    rm.setAmplitude(0.0f);

    constexpr size_t kBlockSize = 512;
    std::array<float, kBlockSize> buffer{};
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);

    rm.processBlock(buffer.data(), kBlockSize);

    float maxVal = 0.0f;
    for (float sample : buffer) {
        maxVal = std::max(maxVal, std::abs(sample));
    }
    REQUIRE(maxVal < 1e-6f);
}

// =============================================================================
// Amplitude=1 Output Does Not Exceed Input Peak (FR-005 scenario 5)
// =============================================================================

TEST_CASE("RingModulator amplitude=1 output does not exceed input peak", "[ring_modulator][amplitude]") {
    RingModulator rm;
    // Set parameters BEFORE prepare so the smoother snaps to the correct freq
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f);
    rm.setAmplitude(1.0f);
    rm.prepare(44100.0, 512);

    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> buffer{};
    const float inputPeak = 0.8f;
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f, inputPeak);

    // Warmup block to let smoother fully settle
    rm.processBlock(buffer.data(), kBlockSize);
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f, inputPeak);
    rm.processBlock(buffer.data(), kBlockSize);

    float maxVal = 0.0f;
    for (float sample : buffer) {
        maxVal = std::max(maxVal, std::abs(sample));
    }

    // Ring mod output peak should not exceed input peak
    // (carrier peak is 1.0, amplitude is 1.0, so output peak <= input peak)
    REQUIRE(maxVal <= inputPeak + 0.01f); // Small tolerance
}

// =============================================================================
// Real-Time Safety: No Exceptions in processBlock (FR-010)
// =============================================================================

TEST_CASE("RingModulator processBlock does not throw", "[ring_modulator][realtime]") {
    RingModulator rm;
    rm.prepare(44100.0, 512);
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f);
    rm.setAmplitude(1.0f);

    constexpr size_t kBlockSize = 512;
    std::array<float, kBlockSize> buffer{};
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);

    REQUIRE_NOTHROW(rm.processBlock(buffer.data(), kBlockSize));
}

// =============================================================================
// Re-Prepare Test: No Transient on Re-Prepare (FR-023, C2)
// =============================================================================

TEST_CASE("RingModulator re-prepare produces no transient", "[ring_modulator][lifecycle][reprepare]") {
    RingModulator rm;

    // First prepare at 44100 Hz
    rm.prepare(44100.0, 512);
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f);
    rm.setAmplitude(1.0f);

    // Process a block to get into steady state
    constexpr size_t kBlockSize = 512;
    std::array<float, kBlockSize> buffer{};
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);
    rm.processBlock(buffer.data(), kBlockSize);

    // Reset and re-prepare at 48000 Hz
    rm.reset();
    rm.prepare(48000.0, 512);

    // Process first block after re-prepare
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 48000.0f);
    rm.processBlock(buffer.data(), kBlockSize);

    // Check that no sample has an abnormally large value (transient)
    // The ring mod output should be bounded by input amplitude * carrier amplitude
    float maxVal = 0.0f;
    bool hasNaN = false;
    bool hasInf = false;
    for (float sample : buffer) {
        if (detail::isNaN(sample)) hasNaN = true;
        if (detail::isInf(sample)) hasInf = true;
        maxVal = std::max(maxVal, std::abs(sample));
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    // Output should not exceed 1.0 (input peak * carrier peak * amplitude)
    REQUIRE(maxVal <= 1.01f);
}

// =============================================================================
// Guard Rails: No NaN or Inf (Safety)
// =============================================================================

TEST_CASE("RingModulator output contains no NaN or Inf", "[ring_modulator][safety]") {
    RingModulator rm;
    rm.prepare(44100.0, 512);
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f);
    rm.setAmplitude(1.0f);

    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> buffer{};

    SECTION("normal input") {
        TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);
        rm.processBlock(buffer.data(), kBlockSize);

        bool hasNaN = false;
        bool hasInf = false;
        for (float sample : buffer) {
            if (detail::isNaN(sample)) hasNaN = true;
            if (detail::isInf(sample)) hasInf = true;
        }
        REQUIRE_FALSE(hasNaN);
        REQUIRE_FALSE(hasInf);
    }

    SECTION("extreme input values") {
        std::fill(buffer.begin(), buffer.end(), 100.0f);
        rm.processBlock(buffer.data(), kBlockSize);

        bool hasNaN = false;
        bool hasInf = false;
        for (float sample : buffer) {
            if (detail::isNaN(sample)) hasNaN = true;
            if (detail::isInf(sample)) hasInf = true;
        }
        REQUIRE_FALSE(hasNaN);
        REQUIRE_FALSE(hasInf);
    }

    SECTION("zero input") {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        rm.processBlock(buffer.data(), kBlockSize);

        bool hasNaN = false;
        bool hasInf = false;
        float maxVal = 0.0f;
        for (float sample : buffer) {
            if (detail::isNaN(sample)) hasNaN = true;
            if (detail::isInf(sample)) hasInf = true;
            maxVal = std::max(maxVal, std::abs(sample));
        }
        REQUIRE_FALSE(hasNaN);
        REQUIRE_FALSE(hasInf);
        REQUIRE(maxVal < 1e-6f); // Zero input should produce zero output
    }
}

// =============================================================================
// User Story 2: Note-Tracking Carrier Frequency (FR-003, FR-004, FR-016)
// =============================================================================

TEST_CASE("RingModulator NoteTrack mode: ratio=1.0 yields DC + octave", "[ring_modulator][notetrack]") {
    // When ratio=1.0 and note=C4 (261.63 Hz), carrier=261.63 Hz.
    // Ring mod of input at 261.63 Hz with carrier at 261.63 Hz:
    //   difference = 0 Hz (DC), sum = 523.25 Hz (octave)
    // We verify that the sum sideband (523.25 Hz) is strong and the
    // original frequency is suppressed.

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 44100; // 1 Hz resolution
    constexpr float kNoteFreq = 261.63f; // C4

    RingModulator rm;
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::NoteTrack);
    rm.setNoteFrequency(kNoteFreq);
    rm.setRatio(1.0f);
    rm.setAmplitude(1.0f);
    rm.prepare(kSampleRate, kBlockSize);

    std::vector<float> buffer(kBlockSize);

    // Warmup block
    TestHelpers::generateSine(buffer.data(), kBlockSize, kNoteFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Measurement block
    TestHelpers::generateSine(buffer.data(), kBlockSize, kNoteFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // With carrier = input freq, we get DC (0 Hz) + sum at 2x freq
    const float magOctave = magnitudeAtFrequency(buffer.data(), kBlockSize,
                                                  kNoteFreq * 2.0f,
                                                  static_cast<float>(kSampleRate));
    const float magFundamental = magnitudeAtFrequency(buffer.data(), kBlockSize,
                                                       kNoteFreq,
                                                       static_cast<float>(kSampleRate));

    INFO("Octave (523.25 Hz) magnitude: " << magOctave << " dB");
    INFO("Fundamental (261.63 Hz) magnitude: " << magFundamental << " dB");

    // Octave sideband should be significantly stronger than the original fundamental
    REQUIRE(magOctave - magFundamental >= 40.0f);
    // The octave sideband should be at a reasonable level (above -20 dB)
    REQUIRE(magOctave > -20.0f);
}

TEST_CASE("RingModulator NoteTrack mode: ratio=2.0 with A4 yields 880 Hz carrier", "[ring_modulator][notetrack]") {
    // NoteTrack mode, note=440 Hz, ratio=2.0 => carrier=880 Hz
    // Input at 440 Hz: sidebands at 440 Hz (880-440) and 1320 Hz (880+440)

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 44100; // 1 Hz resolution
    constexpr float kNoteFreq = 440.0f;

    RingModulator rm;
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::NoteTrack);
    rm.setNoteFrequency(kNoteFreq);
    rm.setRatio(2.0f);
    rm.setAmplitude(1.0f);
    rm.prepare(kSampleRate, kBlockSize);

    std::vector<float> buffer(kBlockSize);

    // Warmup block
    TestHelpers::generateSine(buffer.data(), kBlockSize, kNoteFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Measurement block
    TestHelpers::generateSine(buffer.data(), kBlockSize, kNoteFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Carrier=880 Hz: difference = 880-440 = 440 Hz, sum = 880+440 = 1320 Hz
    const float mag440 = magnitudeAtFrequency(buffer.data(), kBlockSize, 440.0f,
                                              static_cast<float>(kSampleRate));
    const float mag1320 = magnitudeAtFrequency(buffer.data(), kBlockSize, 1320.0f,
                                               static_cast<float>(kSampleRate));

    INFO("440 Hz (difference sideband) magnitude: " << mag440 << " dB");
    INFO("1320 Hz (sum sideband) magnitude: " << mag1320 << " dB");

    // Both sidebands should have significant energy (above -20 dB)
    REQUIRE(mag440 > -20.0f);
    REQUIRE(mag1320 > -20.0f);

    // Sidebands should be roughly similar magnitude (within 3 dB)
    REQUIRE(std::abs(mag440 - mag1320) < 3.0f);
}

TEST_CASE("RingModulator NoteTrack mode: ratio=0.5 with A4 yields 220 Hz carrier", "[ring_modulator][notetrack]") {
    // NoteTrack mode, note=440 Hz, ratio=0.5 => carrier=220 Hz
    // Input at 440 Hz: sidebands at 220 Hz (440-220) and 660 Hz (440+220)

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 44100; // 1 Hz resolution
    constexpr float kNoteFreq = 440.0f;

    RingModulator rm;
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::NoteTrack);
    rm.setNoteFrequency(kNoteFreq);
    rm.setRatio(0.5f);
    rm.setAmplitude(1.0f);
    rm.prepare(kSampleRate, kBlockSize);

    std::vector<float> buffer(kBlockSize);

    // Warmup block
    TestHelpers::generateSine(buffer.data(), kBlockSize, kNoteFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Measurement block
    TestHelpers::generateSine(buffer.data(), kBlockSize, kNoteFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Carrier=220 Hz: difference = 440-220 = 220 Hz, sum = 440+220 = 660 Hz
    const float mag220 = magnitudeAtFrequency(buffer.data(), kBlockSize, 220.0f,
                                              static_cast<float>(kSampleRate));
    const float mag660 = magnitudeAtFrequency(buffer.data(), kBlockSize, 660.0f,
                                              static_cast<float>(kSampleRate));
    // The original 440 Hz should be suppressed
    const float mag440 = magnitudeAtFrequency(buffer.data(), kBlockSize, 440.0f,
                                              static_cast<float>(kSampleRate));

    INFO("220 Hz (difference sideband) magnitude: " << mag220 << " dB");
    INFO("660 Hz (sum sideband) magnitude: " << mag660 << " dB");
    INFO("440 Hz (suppressed fundamental) magnitude: " << mag440 << " dB");

    // Both sidebands should have significant energy
    REQUIRE(mag220 > -20.0f);
    REQUIRE(mag660 > -20.0f);

    // 440 Hz should be suppressed relative to sidebands
    REQUIRE(mag220 - mag440 >= 40.0f);
    REQUIRE(mag660 - mag440 >= 40.0f);
}

TEST_CASE("RingModulator Free mode ignores note frequency changes", "[ring_modulator][notetrack][free]") {
    // In Free mode, changing note frequency should have no effect on carrier

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 44100; // 1 Hz resolution
    constexpr float kCarrierFreq = 300.0f;
    constexpr float kInputFreq = 500.0f;

    RingModulator rm;
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(kCarrierFreq);
    rm.setAmplitude(1.0f);
    rm.prepare(kSampleRate, kBlockSize);

    // Process with note frequency at 440 Hz (should be ignored in Free mode)
    rm.setNoteFrequency(440.0f);

    std::vector<float> buffer(kBlockSize);

    // Warmup block
    TestHelpers::generateSine(buffer.data(), kBlockSize, kInputFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Measurement block
    TestHelpers::generateSine(buffer.data(), kBlockSize, kInputFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Expected sidebands: 500-300=200 Hz, 500+300=800 Hz
    const float mag200 = magnitudeAtFrequency(buffer.data(), kBlockSize, 200.0f,
                                              static_cast<float>(kSampleRate));
    const float mag800 = magnitudeAtFrequency(buffer.data(), kBlockSize, 800.0f,
                                              static_cast<float>(kSampleRate));
    const float mag500 = magnitudeAtFrequency(buffer.data(), kBlockSize, kInputFreq,
                                              static_cast<float>(kSampleRate));

    INFO("200 Hz magnitude: " << mag200 << " dB");
    INFO("800 Hz magnitude: " << mag800 << " dB");
    INFO("500 Hz (suppressed) magnitude: " << mag500 << " dB");

    // Sidebands should be strong, fundamental should be suppressed
    REQUIRE(mag200 > -20.0f);
    REQUIRE(mag800 > -20.0f);
    REQUIRE(mag200 - mag500 >= 40.0f);

    // Now change note frequency to 880 Hz -- should still produce same sidebands
    rm.setNoteFrequency(880.0f);

    // Warmup block to let any potential change settle
    TestHelpers::generateSine(buffer.data(), kBlockSize, kInputFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Measurement block
    TestHelpers::generateSine(buffer.data(), kBlockSize, kInputFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    const float mag200after = magnitudeAtFrequency(buffer.data(), kBlockSize, 200.0f,
                                                    static_cast<float>(kSampleRate));
    const float mag800after = magnitudeAtFrequency(buffer.data(), kBlockSize, 800.0f,
                                                    static_cast<float>(kSampleRate));

    INFO("200 Hz after note change: " << mag200after << " dB");
    INFO("800 Hz after note change: " << mag800after << " dB");

    // Sidebands should be virtually identical -- carrier is unaffected
    REQUIRE(std::abs(mag200 - mag200after) < 2.0f);
    REQUIRE(std::abs(mag800 - mag800after) < 2.0f);
}

TEST_CASE("RingModulator NoteTrack mode: non-integer ratio (1.37) produces inharmonic sidebands", "[ring_modulator][notetrack][inharmonic]") {
    // ratio=1.37, note=440 Hz => carrier = 602.8 Hz
    // Sidebands: 602.8-440 = 162.8 Hz, 602.8+440 = 1042.8 Hz

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 44100; // 1 Hz resolution
    constexpr float kNoteFreq = 440.0f;
    constexpr float kRatio = 1.37f;
    constexpr float kExpectedCarrier = kNoteFreq * kRatio; // 602.8 Hz
    constexpr float kExpectedDiff = kExpectedCarrier - kNoteFreq; // 162.8 Hz
    constexpr float kExpectedSum = kExpectedCarrier + kNoteFreq; // 1042.8 Hz

    RingModulator rm;
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::NoteTrack);
    rm.setNoteFrequency(kNoteFreq);
    rm.setRatio(kRatio);
    rm.setAmplitude(1.0f);
    rm.prepare(kSampleRate, kBlockSize);

    std::vector<float> buffer(kBlockSize);

    // Warmup block
    TestHelpers::generateSine(buffer.data(), kBlockSize, kNoteFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Measurement block
    TestHelpers::generateSine(buffer.data(), kBlockSize, kNoteFreq,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    const float magDiff = magnitudeAtFrequency(buffer.data(), kBlockSize,
                                               kExpectedDiff,
                                               static_cast<float>(kSampleRate));
    const float magSum = magnitudeAtFrequency(buffer.data(), kBlockSize,
                                              kExpectedSum,
                                              static_cast<float>(kSampleRate));
    const float magFundamental = magnitudeAtFrequency(buffer.data(), kBlockSize,
                                                       kNoteFreq,
                                                       static_cast<float>(kSampleRate));

    INFO("Difference sideband (" << kExpectedDiff << " Hz): " << magDiff << " dB");
    INFO("Sum sideband (" << kExpectedSum << " Hz): " << magSum << " dB");
    INFO("Fundamental (440 Hz): " << magFundamental << " dB");

    // Both inharmonic sidebands should have significant energy
    REQUIRE(magDiff > -20.0f);
    REQUIRE(magSum > -20.0f);

    // Fundamental should be suppressed relative to sidebands
    REQUIRE(magDiff - magFundamental >= 30.0f);
    REQUIRE(magSum - magFundamental >= 30.0f);
}

TEST_CASE("RingModulator freq mode switching is stable", "[ring_modulator][notetrack][switch]") {
    // Switching between Free and NoteTrack modes should not produce NaN, Inf,
    // or discontinuities

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    RingModulator rm;
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f);
    rm.setNoteFrequency(440.0f);
    rm.setRatio(2.0f);
    rm.setAmplitude(1.0f);
    rm.prepare(kSampleRate, kBlockSize);

    std::array<float, kBlockSize> buffer{};

    // Process in Free mode
    TestHelpers::generateSine(buffer.data(), kBlockSize, 440.0f,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Switch to NoteTrack mode mid-stream
    rm.setFreqMode(RingModFreqMode::NoteTrack);

    TestHelpers::generateSine(buffer.data(), kBlockSize, 440.0f,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Check for NaN/Inf
    bool hasNaN = false;
    bool hasInf = false;
    float maxVal = 0.0f;
    for (float sample : buffer) {
        if (detail::isNaN(sample)) hasNaN = true;
        if (detail::isInf(sample)) hasInf = true;
        maxVal = std::max(maxVal, std::abs(sample));
    }
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE(maxVal <= 1.01f);

    // Switch back to Free mode
    rm.setFreqMode(RingModFreqMode::Free);

    TestHelpers::generateSine(buffer.data(), kBlockSize, 440.0f,
                              static_cast<float>(kSampleRate));
    rm.processBlock(buffer.data(), kBlockSize);

    // Check again
    hasNaN = false;
    hasInf = false;
    maxVal = 0.0f;
    for (float sample : buffer) {
        if (detail::isNaN(sample)) hasNaN = true;
        if (detail::isInf(sample)) hasInf = true;
        maxVal = std::max(maxVal, std::abs(sample));
    }
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE(maxVal <= 1.01f);
}

// =============================================================================
// User Story 3: Carrier Waveform Selection (FR-002, FR-009)
// =============================================================================

TEST_CASE("RingModulator Triangle carrier produces output", "[ring_modulator][waveform][triangle]") {
    RingModulator rm;
    rm.setCarrierWaveform(RingModCarrierWaveform::Triangle);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f);
    rm.setAmplitude(1.0f);
    rm.prepare(44100.0, 512);

    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> buffer{};
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);

    // Warmup block
    rm.processBlock(buffer.data(), kBlockSize);

    // Measurement block
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);
    rm.processBlock(buffer.data(), kBlockSize);

    float maxVal = 0.0f;
    bool hasNaN = false;
    bool hasInf = false;
    for (float sample : buffer) {
        if (detail::isNaN(sample)) hasNaN = true;
        if (detail::isInf(sample)) hasInf = true;
        maxVal = std::max(maxVal, std::abs(sample));
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE(maxVal > 0.01f);   // Non-trivial output
    REQUIRE(maxVal <= 1.01f);  // Output bounded
}

TEST_CASE("RingModulator Sawtooth carrier produces output", "[ring_modulator][waveform][sawtooth]") {
    RingModulator rm;
    rm.setCarrierWaveform(RingModCarrierWaveform::Sawtooth);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f);
    rm.setAmplitude(1.0f);
    rm.prepare(44100.0, 512);

    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> buffer{};
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);

    // Warmup block
    rm.processBlock(buffer.data(), kBlockSize);

    // Measurement block
    TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);
    rm.processBlock(buffer.data(), kBlockSize);

    float maxVal = 0.0f;
    bool hasNaN = false;
    bool hasInf = false;
    for (float sample : buffer) {
        if (detail::isNaN(sample)) hasNaN = true;
        if (detail::isInf(sample)) hasInf = true;
        maxVal = std::max(maxVal, std::abs(sample));
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE(maxVal > 0.01f);   // Non-trivial output
    REQUIRE(maxVal <= 1.01f);  // Output bounded
}

TEST_CASE("RingModulator Square carrier produces odd-harmonic sidebands", "[ring_modulator][waveform][square]") {
    // A square wave carrier should produce a spectrally richer output than a sine
    // carrier because its odd harmonics create additional sideband pairs.
    // We verify this by comparing the total spectral energy at multiple frequencies
    // between square and sine carriers.

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 44100; // 1 Hz resolution
    constexpr float kCarrierFreq = 200.0f;
    constexpr float kInputFreq = 440.0f;
    constexpr auto kSampleRateF = static_cast<float>(kSampleRate);

    // First: measure with SINE carrier as baseline
    RingModulator rmSine;
    rmSine.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rmSine.setFreqMode(RingModFreqMode::Free);
    rmSine.setFrequency(kCarrierFreq);
    rmSine.setAmplitude(1.0f);
    rmSine.prepare(kSampleRate, kBlockSize);

    std::vector<float> bufSine(kBlockSize);
    TestHelpers::generateSine(bufSine.data(), kBlockSize, kInputFreq, kSampleRateF);
    rmSine.processBlock(bufSine.data(), kBlockSize);
    TestHelpers::generateSine(bufSine.data(), kBlockSize, kInputFreq, kSampleRateF);
    rmSine.processBlock(bufSine.data(), kBlockSize);

    // Sine carrier should have energy only at 240 Hz and 640 Hz sidebands
    const float sineMag160 = magnitudeAtFrequency(bufSine.data(), kBlockSize, 160.0f, kSampleRateF);
    const float sineMag1040 = magnitudeAtFrequency(bufSine.data(), kBlockSize, 1040.0f, kSampleRateF);

    // Second: measure with SQUARE carrier
    RingModulator rmSquare;
    rmSquare.setCarrierWaveform(RingModCarrierWaveform::Square);
    rmSquare.setFreqMode(RingModFreqMode::Free);
    rmSquare.setFrequency(kCarrierFreq);
    rmSquare.setAmplitude(1.0f);
    rmSquare.prepare(kSampleRate, kBlockSize);

    std::vector<float> bufSquare(kBlockSize);
    TestHelpers::generateSine(bufSquare.data(), kBlockSize, kInputFreq, kSampleRateF);
    rmSquare.processBlock(bufSquare.data(), kBlockSize);
    TestHelpers::generateSine(bufSquare.data(), kBlockSize, kInputFreq, kSampleRateF);
    rmSquare.processBlock(bufSquare.data(), kBlockSize);

    // Square carrier should have energy at 240 Hz and 640 Hz (fundamental sidebands)
    const float sqMag240 = magnitudeAtFrequency(bufSquare.data(), kBlockSize, 240.0f, kSampleRateF);
    const float sqMag640 = magnitudeAtFrequency(bufSquare.data(), kBlockSize, 640.0f, kSampleRateF);

    // Square carrier 3rd harmonic (600 Hz) produces sidebands at 160 Hz and 1040 Hz
    const float sqMag160 = magnitudeAtFrequency(bufSquare.data(), kBlockSize, 160.0f, kSampleRateF);
    const float sqMag1040 = magnitudeAtFrequency(bufSquare.data(), kBlockSize, 1040.0f, kSampleRateF);

    INFO("Square 240 Hz: " << sqMag240 << " dB");
    INFO("Square 640 Hz: " << sqMag640 << " dB");
    INFO("Square 160 Hz (3rd harm sideband): " << sqMag160 << " dB");
    INFO("Square 1040 Hz (3rd harm sideband): " << sqMag1040 << " dB");
    INFO("Sine 160 Hz (should be noise floor): " << sineMag160 << " dB");
    INFO("Sine 1040 Hz (should be noise floor): " << sineMag1040 << " dB");

    // Fundamental sidebands should have strong energy
    REQUIRE(sqMag240 > -20.0f);
    REQUIRE(sqMag640 > -20.0f);

    // The square carrier should produce more spectral energy at the 3rd-harmonic
    // sideband frequencies than a sine carrier does. The square's odd harmonics
    // create additional sidebands that a pure sine carrier cannot.
    // Square 3rd harmonic sidebands should be above the sine noise floor at
    // those same frequencies.
    REQUIRE(sqMag160 > sineMag160);
    REQUIRE(sqMag1040 > sineMag1040);
}

TEST_CASE("RingModulator Noise carrier produces broadband output", "[ring_modulator][waveform][noise]") {
    RingModulator rm;
    rm.setCarrierWaveform(RingModCarrierWaveform::Noise);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f); // Should be ignored for noise
    rm.setAmplitude(1.0f);
    rm.prepare(44100.0, 512);

    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> buffer{};
    TestHelpers::generateSine(buffer.data(), kBlockSize, 440.0f, 44100.0f);

    // Warmup block
    rm.processBlock(buffer.data(), kBlockSize);

    // Measurement block
    TestHelpers::generateSine(buffer.data(), kBlockSize, 440.0f, 44100.0f);
    rm.processBlock(buffer.data(), kBlockSize);

    // Check that the output is non-silent
    float maxVal = 0.0f;
    float rms = 0.0f;
    bool hasNaN = false;
    bool hasInf = false;
    for (size_t i = 0; i < kBlockSize; ++i) {
        const float s = buffer[i];
        if (detail::isNaN(s)) hasNaN = true;
        if (detail::isInf(s)) hasInf = true;
        maxVal = std::max(maxVal, std::abs(s));
        rms += s * s;
    }
    rms = std::sqrt(rms / static_cast<float>(kBlockSize));

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE(maxVal > 0.01f);  // Non-silent
    REQUIRE(rms > 0.001f);    // Significant broadband energy

    // Check broadband: measure at several frequencies and confirm spread
    const float mag200 = magnitudeAtFrequency(buffer.data(), kBlockSize, 200.0f, 44100.0f);
    const float mag1000 = magnitudeAtFrequency(buffer.data(), kBlockSize, 1000.0f, 44100.0f);
    const float mag3000 = magnitudeAtFrequency(buffer.data(), kBlockSize, 3000.0f, 44100.0f);

    INFO("200 Hz: " << mag200 << " dB");
    INFO("1000 Hz: " << mag1000 << " dB");
    INFO("3000 Hz: " << mag3000 << " dB");

    // All frequencies should have some energy (broadband)
    REQUIRE(mag200 > -80.0f);
    REQUIRE(mag1000 > -80.0f);
    REQUIRE(mag3000 > -80.0f);
}

TEST_CASE("RingModulator Noise carrier ignores setFrequency calls", "[ring_modulator][waveform][noise][FR-009]") {
    // FR-009: When the carrier waveform is Noise, frequency mode and ratio
    // parameters MUST be ignored.

    constexpr size_t kBlockSize = 4096;

    // Process with frequency = 100 Hz
    RingModulator rm1;
    rm1.setCarrierWaveform(RingModCarrierWaveform::Noise);
    rm1.setFreqMode(RingModFreqMode::Free);
    rm1.setFrequency(100.0f);
    rm1.setAmplitude(1.0f);
    rm1.prepare(44100.0, 512);

    std::array<float, kBlockSize> buf1{};
    TestHelpers::generateSine(buf1.data(), kBlockSize, 440.0f, 44100.0f);
    rm1.processBlock(buf1.data(), kBlockSize);

    // Process with frequency = 10000 Hz (should produce same statistical behavior)
    RingModulator rm2;
    rm2.setCarrierWaveform(RingModCarrierWaveform::Noise);
    rm2.setFreqMode(RingModFreqMode::Free);
    rm2.setFrequency(10000.0f);
    rm2.setAmplitude(1.0f);
    rm2.prepare(44100.0, 512);

    std::array<float, kBlockSize> buf2{};
    TestHelpers::generateSine(buf2.data(), kBlockSize, 440.0f, 44100.0f);
    rm2.processBlock(buf2.data(), kBlockSize);

    // Both should produce non-silent output
    float rms1 = 0.0f;
    float rms2 = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        rms1 += buf1[i] * buf1[i];
        rms2 += buf2[i] * buf2[i];
    }
    rms1 = std::sqrt(rms1 / static_cast<float>(kBlockSize));
    rms2 = std::sqrt(rms2 / static_cast<float>(kBlockSize));

    INFO("RMS at 100 Hz freq setting: " << rms1);
    INFO("RMS at 10000 Hz freq setting: " << rms2);

    // Both should produce non-trivial output
    REQUIRE(rms1 > 0.001f);
    REQUIRE(rms2 > 0.001f);

    // The noise output should NOT be deterministically different based on frequency
    // (noise is untimed). However, since noise is random, we can only assert
    // that both produce broadband output at similar statistical levels.
    // The RMS values should be in the same ballpark (within 20 dB).
    if (rms1 > 0.0f && rms2 > 0.0f) {
        const float rmsRatio = std::abs(20.0f * std::log10(rms1 / rms2));
        INFO("RMS ratio (dB): " << rmsRatio);
        REQUIRE(rmsRatio < 20.0f);
    }
}

TEST_CASE("RingModulator waveform switching mid-stream produces no NaN or Inf", "[ring_modulator][waveform][switch]") {
    RingModulator rm;
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f);
    rm.setAmplitude(1.0f);
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.prepare(44100.0, 512);

    constexpr size_t kBlockSize = 512;
    std::array<float, kBlockSize> buffer{};

    // Cycle through all waveforms, processing a block after each switch
    const RingModCarrierWaveform waveforms[] = {
        RingModCarrierWaveform::Sine,
        RingModCarrierWaveform::Triangle,
        RingModCarrierWaveform::Sawtooth,
        RingModCarrierWaveform::Square,
        RingModCarrierWaveform::Noise,
        RingModCarrierWaveform::Sine, // Switch back to sine
    };

    bool hasNaN = false;
    bool hasInf = false;

    for (auto wf : waveforms) {
        rm.setCarrierWaveform(wf);

        TestHelpers::generateSine(buffer.data(), kBlockSize, 1000.0f, 44100.0f);
        rm.processBlock(buffer.data(), kBlockSize);

        for (float sample : buffer) {
            if (detail::isNaN(sample)) hasNaN = true;
            if (detail::isInf(sample)) hasInf = true;
        }
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
}

TEST_CASE("RingModulator carrier output in range [-1,+1] for all waveforms", "[ring_modulator][waveform][range]") {
    constexpr size_t kBlockSize = 4096;

    const RingModCarrierWaveform waveforms[] = {
        RingModCarrierWaveform::Sine,
        RingModCarrierWaveform::Triangle,
        RingModCarrierWaveform::Sawtooth,
        RingModCarrierWaveform::Square,
        RingModCarrierWaveform::Noise,
    };

    for (auto wf : waveforms) {
        RingModulator rm;
        rm.setCarrierWaveform(wf);
        rm.setFreqMode(RingModFreqMode::Free);
        rm.setFrequency(440.0f);
        rm.setAmplitude(1.0f);
        rm.prepare(44100.0, 512);

        std::array<float, kBlockSize> buffer{};

        // Use unit-amplitude input so output = input * carrier * amplitude
        // With amplitude=1 and |input|<=1, output should be bounded by |carrier|
        std::fill(buffer.begin(), buffer.end(), 1.0f);

        // Warmup block
        rm.processBlock(buffer.data(), kBlockSize);

        // Measurement block
        std::fill(buffer.begin(), buffer.end(), 1.0f);
        rm.processBlock(buffer.data(), kBlockSize);

        float maxVal = 0.0f;
        bool hasNaN = false;
        bool hasInf = false;
        for (float sample : buffer) {
            if (detail::isNaN(sample)) hasNaN = true;
            if (detail::isInf(sample)) hasInf = true;
            maxVal = std::max(maxVal, std::abs(sample));
        }

        INFO("Waveform: " << static_cast<int>(wf));
        REQUIRE_FALSE(hasNaN);
        REQUIRE_FALSE(hasInf);
        // Output = input (1.0) * carrier * amplitude (1.0)
        // Carrier should be in [-1, +1], so |output| <= 1.0
        REQUIRE(maxVal <= 1.01f); // Small tolerance for floating-point
    }
}

// =============================================================================
// Stereo Spread Tests (FR-006, FR-007, Phase 6)
// =============================================================================

TEST_CASE("RingModulator stereo spread=0 produces identical L and R", "[ring_modulator][stereo][spread]") {
    // FR-006: stereo spread 0% => left and right outputs are identical
    constexpr size_t kBlockSize = 4096;
    constexpr float kSampleRate = 44100.0f;
    constexpr float kCarrierFreq = 500.0f;
    constexpr float kInputFreq = 440.0f;

    RingModulator rm;
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(kCarrierFreq);
    rm.setAmplitude(1.0f);
    rm.setStereoSpread(0.0f);
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.prepare(kSampleRate, 512);

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    // Fill both channels with the same sine input
    TestHelpers::generateSine(left.data(), kBlockSize, kInputFreq, kSampleRate);
    for (size_t i = 0; i < kBlockSize; ++i) {
        right[i] = left[i];
    }

    rm.processBlock(left.data(), right.data(), kBlockSize);

    // With spread=0, L and R should be identical
    float maxDiff = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        maxDiff = std::max(maxDiff, std::abs(left[i] - right[i]));
    }

    INFO("Max L/R difference with spread=0: " << maxDiff);
    REQUIRE(maxDiff < 1e-6f);
}

TEST_CASE("RingModulator stereo spread=1 produces different L and R carrier frequencies", "[ring_modulator][stereo][spread]") {
    // FR-006: stereo spread 100% with 500 Hz carrier => L=450 Hz, R=550 Hz
    constexpr size_t kBlockSize = 8192;
    constexpr float kSampleRate = 44100.0f;
    constexpr float kCarrierFreq = 500.0f;
    constexpr float kInputFreq = 440.0f;

    RingModulator rm;
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(kCarrierFreq);
    rm.setAmplitude(1.0f);
    rm.setStereoSpread(1.0f);
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.prepare(kSampleRate, 512);

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    // Warmup to let smoothers settle
    TestHelpers::generateSine(left.data(), kBlockSize, kInputFreq, kSampleRate);
    for (size_t i = 0; i < kBlockSize; ++i) right[i] = left[i];
    rm.processBlock(left.data(), right.data(), kBlockSize);

    // Measurement block
    TestHelpers::generateSine(left.data(), kBlockSize, kInputFreq, kSampleRate);
    for (size_t i = 0; i < kBlockSize; ++i) right[i] = left[i];
    rm.processBlock(left.data(), right.data(), kBlockSize);

    // L and R should NOT be identical with spread=1
    float maxDiff = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        maxDiff = std::max(maxDiff, std::abs(left[i] - right[i]));
    }

    INFO("Max L/R difference with spread=1: " << maxDiff);
    REQUIRE(maxDiff > 0.01f); // Should be significantly different

    // Verify expected sideband frequencies:
    // Left carrier = 500 - 50 = 450 Hz => sidebands at 440-450 = -10 (DC area), 440+450 = 890
    // Right carrier = 500 + 50 = 550 Hz => sidebands at 440-550 = -110 => 110, 440+550 = 990
    // Check that left has energy at 890 Hz and right has energy at 990 Hz
    const float leftMag890 = magnitudeAtFrequency(left.data(), kBlockSize, 890.0f, kSampleRate);
    const float rightMag990 = magnitudeAtFrequency(right.data(), kBlockSize, 990.0f, kSampleRate);

    INFO("Left mag at 890 Hz (expected sideband): " << leftMag890 << " dB");
    INFO("Right mag at 990 Hz (expected sideband): " << rightMag990 << " dB");

    // Both channels should have significant energy at their respective sidebands
    REQUIRE(leftMag890 > -40.0f);
    REQUIRE(rightMag990 > -40.0f);

    // Cross-check: left should have less energy at 990 Hz than right
    const float leftMag990 = magnitudeAtFrequency(left.data(), kBlockSize, 990.0f, kSampleRate);
    const float rightMag890 = magnitudeAtFrequency(right.data(), kBlockSize, 890.0f, kSampleRate);

    INFO("Left mag at 990 Hz (not expected): " << leftMag990 << " dB");
    INFO("Right mag at 890 Hz (not expected): " << rightMag890 << " dB");

    // The sideband at the "wrong" frequency should be much weaker
    REQUIRE(leftMag990 < leftMag890 - 10.0f); // At least 10 dB difference
    REQUIRE(rightMag890 < rightMag990 - 10.0f);
}

TEST_CASE("RingModulator stereo spread max offset is +/-50 Hz", "[ring_modulator][stereo][spread][kMaxSpreadOffsetHz]") {
    // FR-006: max offset at spread=1.0 is kMaxSpreadOffsetHz = 50 Hz
    // Verify by checking sideband locations match expected carrier frequencies

    constexpr size_t kBlockSize = 16384; // Large block for frequency resolution
    constexpr float kSampleRate = 44100.0f;
    constexpr float kCarrierFreq = 1000.0f;
    constexpr float kInputFreq = 440.0f;

    RingModulator rm;
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(kCarrierFreq);
    rm.setAmplitude(1.0f);
    rm.setStereoSpread(1.0f);
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.prepare(kSampleRate, 512);

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    // Warmup
    TestHelpers::generateSine(left.data(), kBlockSize, kInputFreq, kSampleRate);
    for (size_t i = 0; i < kBlockSize; ++i) right[i] = left[i];
    rm.processBlock(left.data(), right.data(), kBlockSize);

    // Measurement
    TestHelpers::generateSine(left.data(), kBlockSize, kInputFreq, kSampleRate);
    for (size_t i = 0; i < kBlockSize; ++i) right[i] = left[i];
    rm.processBlock(left.data(), right.data(), kBlockSize);

    // Left carrier = 1000 - 50 = 950 Hz
    // Left sidebands: 440 + 950 = 1390 Hz, |440 - 950| = 510 Hz
    const float leftSum = magnitudeAtFrequency(left.data(), kBlockSize, 1390.0f, kSampleRate);
    const float leftDiff = magnitudeAtFrequency(left.data(), kBlockSize, 510.0f, kSampleRate);

    // Right carrier = 1000 + 50 = 1050 Hz
    // Right sidebands: 440 + 1050 = 1490 Hz, |440 - 1050| = 610 Hz
    const float rightSum = magnitudeAtFrequency(right.data(), kBlockSize, 1490.0f, kSampleRate);
    const float rightDiff = magnitudeAtFrequency(right.data(), kBlockSize, 610.0f, kSampleRate);

    INFO("Left sum sideband at 1390 Hz: " << leftSum << " dB");
    INFO("Left diff sideband at 510 Hz: " << leftDiff << " dB");
    INFO("Right sum sideband at 1490 Hz: " << rightSum << " dB");
    INFO("Right diff sideband at 610 Hz: " << rightDiff << " dB");

    // All four sidebands should have significant energy
    REQUIRE(leftSum > -40.0f);
    REQUIRE(leftDiff > -40.0f);
    REQUIRE(rightSum > -40.0f);
    REQUIRE(rightDiff > -40.0f);

    // Verify the left does NOT have energy at right's sideband frequencies (and vice versa)
    // This confirms the offset is actually +/-50 Hz and not some other value
    const float leftAtRightSum = magnitudeAtFrequency(left.data(), kBlockSize, 1490.0f, kSampleRate);
    const float rightAtLeftSum = magnitudeAtFrequency(right.data(), kBlockSize, 1390.0f, kSampleRate);

    INFO("Left at right's sum sideband (1490 Hz): " << leftAtRightSum << " dB");
    INFO("Right at left's sum sideband (1390 Hz): " << rightAtLeftSum << " dB");

    // Cross-channel sidebands should be significantly weaker
    REQUIRE(leftAtRightSum < leftSum - 10.0f);
    REQUIRE(rightAtLeftSum < rightSum - 10.0f);
}

TEST_CASE("RingModulator stereo processBlock with Noise carrier produces broadband output on both channels", "[ring_modulator][stereo][spread][noise]") {
    // FR-006 + FR-009: Noise carrier with stereo spread still produces
    // broadband output on both channels
    constexpr size_t kBlockSize = 4096;
    constexpr float kSampleRate = 44100.0f;

    RingModulator rm;
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(1000.0f); // Ignored for noise
    rm.setAmplitude(1.0f);
    rm.setStereoSpread(1.0f);
    rm.setCarrierWaveform(RingModCarrierWaveform::Noise);
    rm.prepare(kSampleRate, 512);

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    // Fill with sine input
    TestHelpers::generateSine(left.data(), kBlockSize, 440.0f, kSampleRate);
    for (size_t i = 0; i < kBlockSize; ++i) right[i] = left[i];

    rm.processBlock(left.data(), right.data(), kBlockSize);

    // Both channels should have non-silent output
    float rmsL = 0.0f;
    float rmsR = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        rmsL += left[i] * left[i];
        rmsR += right[i] * right[i];
    }
    rmsL = std::sqrt(rmsL / static_cast<float>(kBlockSize));
    rmsR = std::sqrt(rmsR / static_cast<float>(kBlockSize));

    INFO("Left RMS: " << rmsL);
    INFO("Right RMS: " << rmsR);

    REQUIRE(rmsL > 0.001f);
    REQUIRE(rmsR > 0.001f);

    // Noise carriers should produce decorrelated output (different L and R)
    // even though input was the same, since we use separate noise oscillators
    // with different seeds
    float maxDiff = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        maxDiff = std::max(maxDiff, std::abs(left[i] - right[i]));
    }
    INFO("Max L/R difference (noise): " << maxDiff);
    REQUIRE(maxDiff > 0.01f); // Noise L and R should be decorrelated

    // Check no NaN or Inf
    bool hasNaN = false;
    bool hasInf = false;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (detail::isNaN(left[i]) || detail::isNaN(right[i])) hasNaN = true;
        if (detail::isInf(left[i]) || detail::isInf(right[i])) hasInf = true;
    }
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
}

TEST_CASE("RingModulator stereo spread with NoteTrack mode applies spread around note-tracked center", "[ring_modulator][stereo][spread][notetrack]") {
    // FR-006 + FR-003: NoteTrack mode with spread should offset around
    // the note-tracked carrier frequency (noteFreq * ratio)
    constexpr size_t kBlockSize = 16384;
    constexpr float kSampleRate = 44100.0f;
    constexpr float kNoteFreq = 440.0f;
    constexpr float kRatio = 2.0f;
    constexpr float kInputFreq = 300.0f; // Use different input freq to avoid confusion

    // Center carrier = 440 * 2 = 880 Hz
    // With spread=1.0: L carrier = 880 - 50 = 830 Hz, R carrier = 880 + 50 = 930 Hz
    // Left sidebands:  300 + 830 = 1130 Hz, |300 - 830| = 530 Hz
    // Right sidebands: 300 + 930 = 1230 Hz, |300 - 930| = 630 Hz

    RingModulator rm;
    rm.setFreqMode(RingModFreqMode::NoteTrack);
    rm.setNoteFrequency(kNoteFreq);
    rm.setRatio(kRatio);
    rm.setAmplitude(1.0f);
    rm.setStereoSpread(1.0f);
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.prepare(kSampleRate, 512);

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    // Warmup block to settle smoothers
    TestHelpers::generateSine(left.data(), kBlockSize, kInputFreq, kSampleRate);
    for (size_t i = 0; i < kBlockSize; ++i) right[i] = left[i];
    rm.processBlock(left.data(), right.data(), kBlockSize);

    // Measurement block
    TestHelpers::generateSine(left.data(), kBlockSize, kInputFreq, kSampleRate);
    for (size_t i = 0; i < kBlockSize; ++i) right[i] = left[i];
    rm.processBlock(left.data(), right.data(), kBlockSize);

    // Left channel: expect sidebands at 1130 Hz and 530 Hz
    const float leftSumMag = magnitudeAtFrequency(left.data(), kBlockSize, 1130.0f, kSampleRate);
    const float leftDiffMag = magnitudeAtFrequency(left.data(), kBlockSize, 530.0f, kSampleRate);

    // Right channel: expect sidebands at 1230 Hz and 630 Hz
    const float rightSumMag = magnitudeAtFrequency(right.data(), kBlockSize, 1230.0f, kSampleRate);
    const float rightDiffMag = magnitudeAtFrequency(right.data(), kBlockSize, 630.0f, kSampleRate);

    INFO("Left sum sideband at 1130 Hz: " << leftSumMag << " dB");
    INFO("Left diff sideband at 530 Hz: " << leftDiffMag << " dB");
    INFO("Right sum sideband at 1230 Hz: " << rightSumMag << " dB");
    INFO("Right diff sideband at 630 Hz: " << rightDiffMag << " dB");

    // All sidebands should have significant energy
    REQUIRE(leftSumMag > -40.0f);
    REQUIRE(leftDiffMag > -40.0f);
    REQUIRE(rightSumMag > -40.0f);
    REQUIRE(rightDiffMag > -40.0f);

    // Cross-check: channels should differ at their respective sideband frequencies
    const float leftAt1230 = magnitudeAtFrequency(left.data(), kBlockSize, 1230.0f, kSampleRate);
    const float rightAt1130 = magnitudeAtFrequency(right.data(), kBlockSize, 1130.0f, kSampleRate);

    INFO("Left at right's sum sideband (1230 Hz): " << leftAt1230 << " dB");
    INFO("Right at left's sum sideband (1130 Hz): " << rightAt1130 << " dB");

    // Cross-channel sidebands should be weaker
    REQUIRE(leftAt1230 < leftSumMag - 10.0f);
    REQUIRE(rightAt1130 < rightSumMag - 10.0f);
}

// =============================================================================
// Performance Benchmark (SC-002, Phase 10)
// =============================================================================

TEST_CASE("RingModulator performance: sine carrier < 0.3% CPU at 44.1 kHz",
          "[ring_modulator][.perf]") {
    // SC-002: Ring Modulator processor MUST consume less than 0.3% CPU per voice
    // at 44.1 kHz sample rate with any carrier waveform.
    //
    // Methodology: Process 10,000 blocks of 512 samples with sine carrier,
    // measure wall-clock time, compute CPU% = (elapsed / realTimeDuration) * 100.

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;
    constexpr int kNumBlocks = 10000;

    RingModulator rm;
    rm.setCarrierWaveform(RingModCarrierWaveform::Sine);
    rm.setFreqMode(RingModFreqMode::Free);
    rm.setFrequency(440.0f);
    rm.setAmplitude(1.0f);
    rm.prepare(kSampleRate, kBlockSize);

    // Prepare input buffer with a sine wave
    std::array<float, kBlockSize> buffer{};
    TestHelpers::generateSine(buffer.data(), kBlockSize, 440.0f,
                              static_cast<float>(kSampleRate));

    // Warmup: process a few blocks to stabilize caches and smoothers
    for (int i = 0; i < 100; ++i) {
        rm.processBlock(buffer.data(), kBlockSize);
        // Regenerate input each time (processBlock modifies in-place)
        TestHelpers::generateSine(buffer.data(), kBlockSize, 440.0f,
                                  static_cast<float>(kSampleRate));
    }

    // Measurement
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < kNumBlocks; ++i) {
        rm.processBlock(buffer.data(), kBlockSize);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsedMs =
        std::chrono::duration<double, std::milli>(end - start).count();

    // Compute real-time duration of the audio processed
    double totalSamples =
        static_cast<double>(kNumBlocks) * static_cast<double>(kBlockSize);
    double totalRealTimeMs = (totalSamples / kSampleRate) * 1000.0;

    double cpuPercent = (elapsedMs / totalRealTimeMs) * 100.0;

    INFO("Processed " << kNumBlocks << " blocks of " << kBlockSize << " samples");
    INFO("Elapsed: " << elapsedMs << " ms for " << totalRealTimeMs
                     << " ms of audio");
    INFO("CPU usage: " << cpuPercent << "%");

    // SC-002 target: < 0.3% on reference hardware
    // Use a generous threshold for CI/test machines that may be slower
    REQUIRE(cpuPercent < 0.3);
}
