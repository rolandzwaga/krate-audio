// ==============================================================================
// API Contract: UnisonEngine (Layer 3 System)
// ==============================================================================
// This file documents the public API contract for the UnisonEngine class.
// It is NOT compilable code -- it is a reference for implementation.
//
// Location: dsp/include/krate/dsp/systems/unison_engine.h
// Namespace: Krate::DSP
// Layer: 3 (systems/)
// Dependencies: Layer 0 (pitch_utils, math_constants, crossfade_utils,
//               db_utils, random), Layer 1 (polyblep_oscillator)
// ==============================================================================

#pragma once

// Layer 0 dependencies
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>

// Layer 1 dependencies
#include <krate/dsp/primitives/polyblep_oscillator.h>

// Standard library
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// StereoOutput (FR-001)
// =============================================================================

/// @brief Lightweight stereo sample pair.
///
/// Simple aggregate type for returning stereo audio from process().
/// No user-declared constructors -- supports brace initialization.
struct StereoOutput {
    float left = 0.0f;   ///< Left channel sample
    float right = 0.0f;  ///< Right channel sample
};

// =============================================================================
// UnisonEngine (FR-002 through FR-031)
// =============================================================================

/// @brief Multi-voice detuned oscillator with stereo spread (Layer 3 system).
///
/// Composes up to 16 PolyBlepOscillator instances into a supersaw/unison
/// engine with non-linear detune curve (JP-8000 inspired), constant-power
/// stereo panning, equal-power center/outer blend, and gain compensation.
///
/// @par Thread Safety
/// Single-threaded ownership model. All methods must be called from the
/// same thread (typically the audio thread). No internal synchronization.
///
/// @par Real-Time Safety
/// process() and processBlock() are fully real-time safe: no allocation,
/// no exceptions, no blocking, no I/O.
///
/// @par Memory
/// All 16 oscillators are pre-allocated as a fixed-size array. No heap
/// allocation occurs at any point. Total instance size < 2048 bytes.
class UnisonEngine {
public:
    // =========================================================================
    // Constants (FR-003)
    // =========================================================================

    static constexpr size_t kMaxVoices = 16;

    // =========================================================================
    // Lifecycle (FR-004, FR-005)
    // =========================================================================

    UnisonEngine() noexcept = default;

    /// @brief Initialize all oscillators and assign random phases.
    /// NOT real-time safe.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept;

    /// @brief Reset oscillator phases to initial random values.
    /// Preserves all configured parameters.
    /// Produces bit-identical output after each reset() call.
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-006 through FR-011)
    // =========================================================================

    /// @brief Set number of active unison voices. Clamped to [1, 16].
    void setNumVoices(size_t count) noexcept;

    /// @brief Set detune spread amount. Clamped to [0, 1]. NaN/Inf ignored.
    void setDetune(float amount) noexcept;

    /// @brief Set stereo panning width. Clamped to [0, 1]. NaN/Inf ignored.
    void setStereoSpread(float spread) noexcept;

    /// @brief Set waveform for all voices simultaneously.
    void setWaveform(OscWaveform waveform) noexcept;

    /// @brief Set base frequency in Hz. NaN/Inf ignored.
    void setFrequency(float hz) noexcept;

    /// @brief Set center/outer blend. Clamped to [0, 1]. NaN/Inf ignored.
    /// 0.0 = center only, 0.5 = equal, 1.0 = outer only.
    void setBlend(float blend) noexcept;

    // =========================================================================
    // Processing (FR-021, FR-022)
    // =========================================================================

    /// @brief Generate one stereo sample. Real-time safe.
    /// @return Stereo output with gain compensation and sanitization.
    [[nodiscard]] StereoOutput process() noexcept;

    /// @brief Generate numSamples into left/right buffers. Real-time safe.
    /// Result is bit-identical to calling process() in a loop.
    void processBlock(float* left, float* right, size_t numSamples) noexcept;
};

} // namespace DSP
} // namespace Krate
