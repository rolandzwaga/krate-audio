// ==============================================================================
// API Contract: RingModulator (Layer 2 DSP Processor)
// ==============================================================================
// This file defines the public API contract for the RingModulator class.
// Implementation will be in dsp/include/krate/dsp/processors/ring_modulator.h
//
// Layer 2: Depends on Layer 0 (math_constants, db_utils) and
//          Layer 1 (PolyBlepOscillator, NoiseOscillator, OnePoleSmoother)
//
// Reference: specs/085-ring-mod-distortion/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// Carrier waveform selection for the ring modulator.
enum class RingModCarrierWaveform : uint8_t {
    Sine = 0,       ///< Gordon-Smith magic circle (2 muls + 2 adds)
    Triangle = 1,   ///< Band-limited via PolyBlepOscillator
    Sawtooth = 2,   ///< Band-limited via PolyBlepOscillator
    Square = 3,     ///< Band-limited via PolyBlepOscillator
    Noise = 4       ///< White noise via NoiseOscillator
};

/// Frequency mode for the ring modulator carrier.
enum class RingModFreqMode : uint8_t {
    Free = 0,       ///< Carrier frequency set directly in Hz
    NoteTrack = 1   ///< Carrier frequency = noteFrequency * ratio
};

// =============================================================================
// RingModulator Class API Contract
// =============================================================================

class RingModulator {
public:
    // =========================================================================
    // Constants (FR-006, FR-023, FR-024)
    // =========================================================================

    static constexpr float kMinFreqHz = 0.1f;         ///< Min carrier freq
    static constexpr float kMaxFreqHz = 20000.0f;     ///< Max carrier freq
    static constexpr float kMinRatio = 0.25f;          ///< Min carrier ratio
    static constexpr float kMaxRatio = 16.0f;          ///< Max carrier ratio
    static constexpr float kMaxSpreadOffsetHz = 50.0f; ///< Max stereo spread
    static constexpr float kSmoothingTimeMs = 5.0f;    ///< Freq smoother time
    static constexpr int kRenormInterval = 1024;        ///< Sine renorm interval

    // =========================================================================
    // Lifecycle (FR-008)
    // =========================================================================

    RingModulator() noexcept = default;
    ~RingModulator() = default;

    // Non-copyable (owns unique_ptr-like sub-components conceptually)
    // but all members are value types, so actually copyable.
    // Keep default copy/move for simplicity.

    /// @brief Initialize all sub-components for given sample rate (FR-008).
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size (unused, for API consistency)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all oscillator and smoother state (FR-008).
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-002 through FR-006)
    // =========================================================================

    /// @brief Set carrier waveform (FR-002).
    void setCarrierWaveform(RingModCarrierWaveform waveform) noexcept;

    /// @brief Set frequency mode (FR-003).
    void setFreqMode(RingModFreqMode mode) noexcept;

    /// @brief Set carrier frequency in Hz for Free mode (FR-003).
    /// @param hz Clamped to [0.1, 20000]
    void setFrequency(float hz) noexcept;

    /// @brief Set note frequency from voice (FR-016).
    /// @param hz Voice note frequency in Hz
    void setNoteFrequency(float hz) noexcept;

    /// @brief Set carrier-to-note frequency ratio (FR-004).
    /// @param ratio Clamped to [0.25, 16.0]
    void setRatio(float ratio) noexcept;

    /// @brief Set carrier amplitude / drive (FR-005).
    /// @param amplitude Clamped to [0.0, 1.0]
    void setAmplitude(float amplitude) noexcept;

    /// @brief Set stereo spread amount (FR-006).
    /// @param spread Clamped to [0.0, 1.0]
    void setStereoSpread(float spread) noexcept;

    // =========================================================================
    // Processing (FR-007, FR-010)
    // =========================================================================

    /// @brief Process mono block in-place (FR-007).
    /// output[n] = input[n] * carrier[n] * amplitude
    /// @param buffer Input/output buffer
    /// @param numSamples Number of samples
    void processBlock(float* buffer, size_t numSamples) noexcept;

    /// @brief Process stereo block in-place (FR-007).
    /// Left uses center_freq - spread_offset, right uses center_freq + spread_offset.
    /// @param left Left channel buffer
    /// @param right Right channel buffer
    /// @param numSamples Number of samples
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    [[nodiscard]] bool isPrepared() const noexcept;
};

} // namespace DSP
} // namespace Krate
