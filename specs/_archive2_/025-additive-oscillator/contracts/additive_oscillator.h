// ==============================================================================
// API Contract: Additive Synthesis Oscillator
// ==============================================================================
// This file defines the public interface contract for AdditiveOscillator.
// Implementation MUST match this interface exactly.
//
// Feature: 025-additive-oscillator
// Layer: 2 (processors/)
// Spec: specs/025-additive-oscillator/spec.md
// ==============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <vector>

// Forward declarations for dependencies
namespace Krate::DSP {
struct Complex;
class FFT;
} // namespace Krate::DSP

namespace Krate {
namespace DSP {

/// @brief Additive synthesis oscillator using IFFT-based resynthesis.
///
/// Generates sound by summing up to 128 sinusoidal partials, with efficient
/// IFFT overlap-add processing. Provides per-partial control and macro
/// parameters for spectral tilt and inharmonicity.
///
/// @par Layer: 2 (processors/)
/// @par Dependencies: primitives/fft.h, core/phase_utils.h, core/window_functions.h
///
/// @par Memory Model
/// All buffers allocated in prepare(). Processing is allocation-free.
///
/// @par Thread Safety
/// Single-threaded. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// prepare(): NOT real-time safe (allocates)
/// All other methods: Real-time safe (noexcept, no allocations)
class AdditiveOscillator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Maximum number of partials supported
    static constexpr size_t kMaxPartials = 128;

    /// Minimum supported FFT size
    static constexpr size_t kMinFFTSize = 512;

    /// Maximum supported FFT size
    static constexpr size_t kMaxFFTSize = 4096;

    /// Default FFT size
    static constexpr size_t kDefaultFFTSize = 2048;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    AdditiveOscillator() noexcept;
    ~AdditiveOscillator() noexcept;

    // Non-copyable, movable
    AdditiveOscillator(const AdditiveOscillator&) = delete;
    AdditiveOscillator& operator=(const AdditiveOscillator&) = delete;
    AdditiveOscillator(AdditiveOscillator&&) noexcept;
    AdditiveOscillator& operator=(AdditiveOscillator&&) noexcept;

    /// @brief Initialize for processing at given sample rate.
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @param fftSize FFT size (512, 1024, 2048, or 4096). Default: 2048.
    /// @pre fftSize is power of 2 in [512, 4096]
    /// @post isPrepared() returns true
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, size_t fftSize = kDefaultFFTSize) noexcept;

    /// @brief Reset internal state without changing configuration.
    /// @note Real-time safe
    /// @note Phase values set via setPartialPhase() take effect here
    void reset() noexcept;

    // =========================================================================
    // Fundamental Frequency
    // =========================================================================

    /// @brief Set the fundamental frequency for all partials.
    /// @param hz Frequency in Hz, clamped to [0.1, sampleRate/2)
    /// @note Real-time safe
    void setFundamental(float hz) noexcept;

    // =========================================================================
    // Per-Partial Control
    // =========================================================================

    /// @brief Set amplitude of a specific partial.
    /// @param partialNumber Partial number [1, kMaxPartials] (1 = fundamental). Out-of-range ignored.
    /// @param amplitude Amplitude in [0, 1]. Values outside range are clamped.
    /// @note Real-time safe
    void setPartialAmplitude(size_t partialNumber, float amplitude) noexcept;

    /// @brief Set frequency ratio of a specific partial relative to fundamental.
    /// @param partialNumber Partial number [1, kMaxPartials] (1 = fundamental). Out-of-range ignored.
    /// @param ratio Frequency ratio in range (0, 64.0]. Invalid values (≤0, NaN, Inf) clamped to 0.001.
    ///        Default for partial N is N (e.g., partial 1 = 1.0x, partial 2 = 2.0x).
    /// @note Real-time safe
    void setPartialFrequencyRatio(size_t partialNumber, float ratio) noexcept;

    /// @brief Set initial phase of a specific partial.
    /// @param partialNumber Partial number [1, kMaxPartials] (1 = fundamental). Out-of-range ignored.
    /// @param phase Phase in [0, 1) where 1.0 = 2*pi radians.
    /// @note Phase takes effect at next reset() call, not applied mid-playback.
    /// @note Real-time safe
    void setPartialPhase(size_t partialNumber, float phase) noexcept;

    // =========================================================================
    // Macro Controls
    // =========================================================================

    /// @brief Set number of active partials.
    /// @param count Number of partials [1, kMaxPartials]. Clamped to range.
    /// @note Real-time safe
    void setNumPartials(size_t count) noexcept;

    /// @brief Apply spectral tilt (dB/octave rolloff) to partial amplitudes.
    /// @param tiltDb Tilt in dB/octave [-24, +12]. Positive boosts highs.
    /// @note Modifies effective amplitudes; does not change stored values.
    /// @note Real-time safe
    void setSpectralTilt(float tiltDb) noexcept;

    /// @brief Set inharmonicity coefficient for partial frequency stretching.
    /// @param B Inharmonicity coefficient [0, 0.1]. 0 = harmonic, higher = bell-like.
    /// @note Applies formula: f_n = n * f1 * sqrt(1 + B * n^2) where n is 1-based
    /// @note Real-time safe
    void setInharmonicity(float B) noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Generate output samples using IFFT overlap-add synthesis.
    /// @param output Destination buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate
    /// @pre prepare() has been called, otherwise outputs zeros
    /// @note Real-time safe: noexcept, no allocations
    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get processing latency in samples.
    /// @return FFT size (latency equals one full FFT frame), or 0 if not prepared
    [[nodiscard]] size_t latency() const noexcept;

    /// @brief Check if processor is prepared.
    /// @return true if prepare() has been called successfully
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get current sample rate.
    /// @return Sample rate in Hz, or 0 if not prepared
    [[nodiscard]] double sampleRate() const noexcept;

    /// @brief Get current FFT size.
    /// @return FFT size, or 0 if not prepared
    [[nodiscard]] size_t fftSize() const noexcept;

    /// @brief Get current fundamental frequency.
    /// @return Fundamental in Hz
    [[nodiscard]] float fundamental() const noexcept;

    /// @brief Get number of active partials.
    /// @return Partial count [1, kMaxPartials]
    [[nodiscard]] size_t numPartials() const noexcept;

private:
    // Implementation details omitted from contract
    // See data-model.md for internal structure
};

} // namespace DSP
} // namespace Krate
