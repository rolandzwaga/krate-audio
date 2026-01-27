// ==============================================================================
// API Contract: Spectral Morph Filter
// ==============================================================================
// This file defines the public API contract for SpectralMorphFilter.
// Implementation must conform exactly to these signatures.
//
// Spec: 080-spectral-morph-filter
// Layer: 2 (Processors)
// Location: dsp/include/krate/dsp/processors/spectral_morph_filter.h
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Phase source selection for spectral morphing
/// @note FR-005: System MUST provide setPreservePhaseFrom() with these options
enum class PhaseSource : uint8_t {
    A,      ///< Use phase from source A exclusively
    B,      ///< Use phase from source B exclusively
    Blend   ///< Interpolate via complex vector lerp (real/imag interpolation)
};

// =============================================================================
// SpectralMorphFilter Class API
// =============================================================================

/// @brief Spectral Morph Filter - Layer 2 Processor
/// @note Morphs between two audio signals by interpolating magnitude spectra
///       while preserving phase from a selectable source.
///
/// Features:
/// - Dual-input spectral morphing (FR-002)
/// - Single-input snapshot mode (FR-003)
/// - Phase source selection: A, B, or Blend (FR-005)
/// - Spectral shift via bin rotation (FR-007)
/// - Spectral tilt with 1 kHz pivot (FR-008)
/// - COLA-compliant overlap-add synthesis (FR-012)
///
/// @note Latency equals FFT size samples (FR-020)
class SpectralMorphFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// FR-001: Supported FFT sizes
    static constexpr std::size_t kMinFFTSize = 256;
    static constexpr std::size_t kMaxFFTSize = 4096;
    static constexpr std::size_t kDefaultFFTSize = 2048;

    /// FR-004: Morph amount range
    static constexpr float kMinMorphAmount = 0.0f;
    static constexpr float kMaxMorphAmount = 1.0f;

    /// FR-007: Spectral shift range (semitones)
    static constexpr float kMinSpectralShift = -24.0f;
    static constexpr float kMaxSpectralShift = +24.0f;

    /// FR-008: Spectral tilt range (dB/octave) and pivot
    static constexpr float kMinSpectralTilt = -12.0f;
    static constexpr float kMaxSpectralTilt = +12.0f;
    static constexpr float kTiltPivotHz = 1000.0f;

    /// FR-006: Snapshot averaging
    static constexpr std::size_t kDefaultSnapshotFrames = 4;

    // =========================================================================
    // Lifecycle (FR-013, FR-014)
    // =========================================================================

    /// @brief Prepare for processing
    /// @param sampleRate Sample rate in Hz
    /// @param fftSize FFT size (power of 2, 256-4096)
    /// @pre fftSize is power of 2 within [kMinFFTSize, kMaxFFTSize]
    /// @note NOT real-time safe (allocates memory)
    /// @note FR-014
    void prepare(double sampleRate, std::size_t fftSize = kDefaultFFTSize) noexcept;

    /// @brief Reset all internal state buffers
    /// @note Real-time safe
    /// @note FR-013
    void reset() noexcept;

    // =========================================================================
    // Processing (FR-002, FR-003, FR-016, FR-017)
    // =========================================================================

    /// @brief Process stereo block with dual inputs (cross-synthesis)
    /// @param inputA First input source buffer (nullptr treated as zeros)
    /// @param inputB Second input source buffer (nullptr treated as zeros)
    /// @param output Output buffer (must be at least numSamples)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept
    /// @note FR-002, FR-016
    void processBlock(const float* inputA, const float* inputB,
                      float* output, std::size_t numSamples) noexcept;

    /// @brief Process single sample (snapshot morphing mode)
    /// @param input Input sample
    /// @return Processed output sample
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept
    /// @note If no snapshot captured, returns input unchanged
    /// @note FR-003, FR-017
    [[nodiscard]] float process(float input) noexcept;

    // =========================================================================
    // Snapshot (FR-006)
    // =========================================================================

    /// @brief Capture spectral snapshot from current input
    /// @note Averages last N frames for smoother spectral fingerprint
    /// @note Replaces any existing snapshot
    /// @note FR-006
    void captureSnapshot() noexcept;

    /// @brief Set number of frames to average for snapshot
    /// @param frames Number of frames (typically 2-8)
    /// @note FR-006: Default 4 frames
    void setSnapshotFrameCount(std::size_t frames) noexcept;

    // =========================================================================
    // Parameters (FR-004, FR-005, FR-007, FR-008, FR-018)
    // =========================================================================

    /// @brief Set morph amount between sources
    /// @param amount 0.0 = source A only, 1.0 = source B only
    /// @note Smoothed internally to prevent clicks (FR-018)
    /// @note FR-004
    void setMorphAmount(float amount) noexcept;

    /// @brief Set phase source for output
    /// @param source Phase source selection (A, B, or Blend)
    /// @note Blend uses complex vector interpolation
    /// @note FR-005
    void setPhaseSource(PhaseSource source) noexcept;

    /// @brief Set spectral pitch shift
    /// @param semitones Shift amount (-24 to +24 semitones)
    /// @note Uses nearest-neighbor bin rounding
    /// @note Bins beyond Nyquist are zeroed
    /// @note FR-007
    void setSpectralShift(float semitones) noexcept;

    /// @brief Set spectral tilt (brightness control)
    /// @param dBPerOctave Tilt amount (-12 to +12 dB/octave)
    /// @note Pivot point at 1 kHz
    /// @note Smoothed internally to prevent clicks (FR-018)
    /// @note FR-008
    void setSpectralTilt(float dBPerOctave) noexcept;

    // =========================================================================
    // Query (FR-020)
    // =========================================================================

    /// @brief Get processing latency in samples
    /// @return Latency equal to FFT size
    /// @note FR-020
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;

    /// @brief Get current FFT size
    [[nodiscard]] std::size_t getFftSize() const noexcept;

    /// @brief Get current morph amount
    [[nodiscard]] float getMorphAmount() const noexcept;

    /// @brief Get current phase source
    [[nodiscard]] PhaseSource getPhaseSource() const noexcept;

    /// @brief Get current spectral shift
    [[nodiscard]] float getSpectralShift() const noexcept;

    /// @brief Get current spectral tilt
    [[nodiscard]] float getSpectralTilt() const noexcept;

    /// @brief Check if snapshot has been captured
    [[nodiscard]] bool hasSnapshot() const noexcept;

    /// @brief Check if processor is prepared
    [[nodiscard]] bool isPrepared() const noexcept;
};

} // namespace DSP
} // namespace Krate
