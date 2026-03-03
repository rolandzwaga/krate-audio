// ==============================================================================
// Layer 2: DSP Processor - Spectral Freeze Oscillator (API Contract)
// ==============================================================================
// Captures a single FFT frame and continuously resynthesizes it, creating
// frozen spectral drones from any audio input. Features freeze/unfreeze,
// pitch shift via bin shifting, spectral tilt, formant shift, and coherent
// phase advancement with overlap-add IFFT resynthesis.
//
// This is an API CONTRACT file -- defines the public interface only.
// Implementation details in the actual header.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept process, allocation in prepare())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 2 (depends on Layer 0-1)
// - Principle X: DSP Constraints (COLA windows, proper overlap)
// - Principle XII: Test-First Development
//
// Reference: specs/030-spectral-freeze-oscillator/spec.md
// ==============================================================================

#pragma once

#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Spectral freeze oscillator that captures and resynthesizes FFT frames.
///
/// Captures a single FFT frame's magnitude and phase spectrum from an audio
/// input, then continuously outputs a stable drone by advancing phase coherently
/// on each synthesis hop. Supports pitch shifting (bin shifting), spectral tilt
/// (brightness control), and formant shifting (spectral envelope manipulation).
///
/// @par Layer: 2 (processors/)
/// @par Dependencies: FFT, SpectralBuffer, FormantPreserver, Window, spectral_utils
///
/// @par Memory Model
/// All buffers allocated in prepare(). Processing is allocation-free.
///
/// @par Thread Safety
/// Single-threaded. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// - prepare(): NOT real-time safe (allocates memory)
/// - All other methods: Real-time safe (noexcept, no allocations)
///
/// @par Usage
/// @code
/// SpectralFreezeOscillator osc;
/// osc.prepare(44100.0, 2048);
///
/// // Feed audio and freeze at desired moment
/// osc.freeze(audioBlock, blockSize);
///
/// // Generate output
/// std::vector<float> output(512);
/// osc.processBlock(output.data(), 512);
///
/// // Modify frozen spectrum
/// osc.setPitchShift(7.0f);       // Perfect fifth up
/// osc.setSpectralTilt(-3.0f);    // Darken
/// osc.setFormantShift(-12.0f);   // Lower formants
///
/// // Release
/// osc.unfreeze();  // Crossfades to silence over one hop
/// @endcode
class SpectralFreezeOscillator {
public:
    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor. Must call prepare() before processing.
    SpectralFreezeOscillator() noexcept;

    /// @brief Destructor.
    ~SpectralFreezeOscillator() noexcept;

    // Non-copyable, movable
    SpectralFreezeOscillator(const SpectralFreezeOscillator&) = delete;
    SpectralFreezeOscillator& operator=(const SpectralFreezeOscillator&) = delete;
    SpectralFreezeOscillator(SpectralFreezeOscillator&&) noexcept;
    SpectralFreezeOscillator& operator=(SpectralFreezeOscillator&&) noexcept;

    /// @brief Allocate all internal buffers and initialize state (FR-001).
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param fftSize FFT size (power of 2, 256-8192). Default: 2048.
    ///        Non-power-of-2 values are clamped to nearest valid size.
    ///
    /// @pre sampleRate > 0
    /// @post isPrepared() == true
    /// @post isFrozen() == false (any previous freeze is cleared)
    ///
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, size_t fftSize = 2048) noexcept;

    /// @brief Clear all internal buffers and state without deallocating (FR-002).
    ///
    /// Clears frozen state, phase accumulators, and output buffer.
    /// Configuration (sample rate, FFT size) is preserved.
    ///
    /// @pre isPrepared() == true (otherwise no-op)
    /// @post isFrozen() == false
    ///
    /// @note Real-time safe
    void reset() noexcept;

    /// @brief Check if prepare() has been called successfully (FR-003).
    /// @return true if ready for processing
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Freeze / Unfreeze (FR-004, FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Capture spectral content from audio block (FR-004).
    ///
    /// Performs STFT analysis (Hann window + FFT) on the input block and
    /// stores magnitude and phase spectrum. If blockSize < fftSize, the
    /// block is zero-padded. Subsequent processBlock() calls produce output
    /// from the frozen spectrum.
    ///
    /// @param inputBlock Audio samples to analyze
    /// @param blockSize Number of samples in inputBlock
    ///
    /// @pre isPrepared() == true
    /// @pre inputBlock != nullptr
    ///
    /// @note Real-time safe (uses pre-allocated buffers)
    /// @note Calling freeze() while already frozen overwrites the capture
    void freeze(const float* inputBlock, size_t blockSize) noexcept;

    /// @brief Release frozen state and fade to silence (FR-005).
    ///
    /// Initiates a linear crossfade to zero over one hop duration
    /// (fftSize/4 samples). After crossfade, processBlock() outputs silence.
    ///
    /// @pre isFrozen() == true (otherwise no-op)
    ///
    /// @note Real-time safe
    void unfreeze() noexcept;

    /// @brief Check if oscillator is in frozen state (FR-006).
    /// @return true if frozen and producing output (or unfreezing)
    [[nodiscard]] bool isFrozen() const noexcept;

    // =========================================================================
    // Processing (FR-008 to FR-011)
    // =========================================================================

    /// @brief Generate output samples from frozen spectrum (FR-011).
    ///
    /// Uses coherent phase advancement (FR-008, FR-009) with IFFT + overlap-add
    /// synthesis (FR-010). Handles arbitrary block sizes via internal ring buffer.
    ///
    /// Output behavior:
    /// - Not prepared: zeros (FR-028)
    /// - Not frozen: zeros (FR-027)
    /// - Frozen: continuous resynthesized audio
    /// - Unfreezing: fading to silence
    ///
    /// @param output Destination buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate
    ///
    /// @note Real-time safe: noexcept, no allocations (FR-023, FR-024)
    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Parameters (FR-012 to FR-022)
    // =========================================================================

    /// @brief Set pitch shift in semitones (FR-012).
    ///
    /// Shifts all frequency bins by the pitch ratio 2^(semitones/12).
    /// Applied on next synthesis frame boundary.
    ///
    /// @param semitones Pitch shift [-24, +24]. Clamped to range.
    ///
    /// @note Real-time safe
    void setPitchShift(float semitones) noexcept;

    /// @brief Get current pitch shift in semitones.
    [[nodiscard]] float getPitchShift() const noexcept;

    /// @brief Set spectral tilt in dB/octave (FR-016).
    ///
    /// Applies multiplicative gain slope to magnitude spectrum.
    /// Positive = brighter, negative = darker.
    /// Applied on next synthesis frame boundary.
    ///
    /// @param dbPerOctave Tilt amount [-24, +24]. Clamped to range.
    ///
    /// @note Real-time safe
    void setSpectralTilt(float dbPerOctave) noexcept;

    /// @brief Get current spectral tilt in dB/octave.
    [[nodiscard]] float getSpectralTilt() const noexcept;

    /// @brief Set formant shift in semitones (FR-019).
    ///
    /// Shifts spectral envelope independently of pitch.
    /// Uses cepstral analysis for envelope extraction (FR-021).
    /// Applied on next synthesis frame boundary.
    ///
    /// @param semitones Formant shift [-24, +24]. Clamped to range.
    ///
    /// @note Real-time safe
    void setFormantShift(float semitones) noexcept;

    /// @brief Get current formant shift in semitones.
    [[nodiscard]] float getFormantShift() const noexcept;

    // =========================================================================
    // Query (FR-026)
    // =========================================================================

    /// @brief Get processing latency in samples (FR-026).
    ///
    /// Latency equals fftSize (one full analysis window).
    ///
    /// @return Latency in samples, or 0 if not prepared
    [[nodiscard]] size_t getLatencySamples() const noexcept;

    /// @brief Get configured FFT size.
    /// @return FFT size, or 0 if not prepared
    [[nodiscard]] size_t fftSize() const noexcept;

    /// @brief Get hop size (fftSize/4).
    /// @return Hop size, or 0 if not prepared
    [[nodiscard]] size_t hopSize() const noexcept;
};

} // namespace DSP
} // namespace Krate
