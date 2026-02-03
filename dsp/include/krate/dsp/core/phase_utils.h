// ==============================================================================
// Layer 0: Core Utility - Phase Accumulator Utilities
// ==============================================================================
// Centralized phase accumulator and utility functions for oscillator
// infrastructure. Provides standardized phase management to replace
// duplicated logic in lfo.h, audio_rate_filter_fm.h, and frequency_shifter.h.
//
// Design decisions:
// - PhaseAccumulator is a value type (struct with public members) for
//   lightweight composition into any oscillator class.
// - Phase and increment use double precision to prevent accumulated rounding
//   error over long playback durations (matching existing codebase patterns).
// - Phase wrapping uses subtraction (not std::fmod) for performance and
//   compatibility with existing oscillator implementations.
// - wrapPhase(double) wraps to [0, 1) for oscillator use. This is distinct
//   from spectral_utils.h::wrapPhase(float) which wraps to [-pi, pi].
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations)
// - Principle III: Modern C++ (constexpr, [[nodiscard]], C++20)
// - Principle IX: Layer 0 (depends only on stdlib)
// - Principle XII: Test-First Development
//
// Reference: specs/013-polyblep-math/spec.md
// ==============================================================================

#pragma once

namespace Krate {
namespace DSP {

// =============================================================================
// Phase Utility Functions
// =============================================================================

/// @brief Calculate normalized phase increment from frequency and sample rate.
///
/// @param frequency Oscillator frequency in Hz
/// @param sampleRate Sample rate in Hz
/// @return Normalized phase increment (frequency / sampleRate).
///         Returns 0.0 if sampleRate is 0 (division-by-zero guard).
///
/// @example
/// @code
/// double inc = calculatePhaseIncrement(440.0f, 44100.0f);  // ~0.009977
/// @endcode
[[nodiscard]] constexpr double calculatePhaseIncrement(
    float frequency,
    float sampleRate
) noexcept {
    if (sampleRate == 0.0f) {
        return 0.0;
    }
    return static_cast<double>(frequency) / static_cast<double>(sampleRate);
}

/// @brief Wrap phase to [0, 1) range using subtraction.
///
/// Handles both positive overflow (phase >= 1.0) and negative values
/// (phase < 0.0) by iterative addition/subtraction of 1.0.
///
/// @note This function wraps to [0, 1) for oscillator phase accumulation.
///       For spectral phase wrapping to [-pi, pi], use spectral_utils.h.
///
/// @param phase Phase value to wrap (any finite double value)
/// @return Phase wrapped to [0, 1)
///
/// @example
/// @code
/// double a = wrapPhase(1.3);   // returns 0.3
/// double b = wrapPhase(-0.2);  // returns 0.8
/// double c = wrapPhase(0.5);   // returns 0.5 (no change)
/// @endcode
[[nodiscard]] constexpr double wrapPhase(double phase) noexcept {
    while (phase >= 1.0) {
        phase -= 1.0;
    }
    while (phase < 0.0) {
        phase += 1.0;
    }
    return phase;
}

/// @brief Detect whether a phase wrap occurred between two phase values.
///
/// Returns true when current phase is less than previous phase, indicating
/// that a wrap from near-1.0 back to near-0.0 occurred. Assumes monotonically
/// increasing phase (positive increment only).
///
/// @param currentPhase Current phase value [0, 1)
/// @param previousPhase Previous phase value [0, 1)
/// @return true if a phase wrap occurred (current < previous)
[[nodiscard]] constexpr bool detectPhaseWrap(
    double currentPhase,
    double previousPhase
) noexcept {
    return currentPhase < previousPhase;
}

/// @brief Calculate the fractional sample position where a phase wrap occurred.
///
/// After a wrap, the current phase represents how far past the wrap point
/// the phase has advanced. This function returns the fractional position
/// within the current sample interval [0, 1) where the wrap happened.
///
/// This is critical for sub-sample-accurate PolyBLEP correction placement.
///
/// @param phase Current phase after wrapping [0, 1)
/// @param increment Phase increment per sample (must be > 0)
/// @return Fractional sample position [0, 1) where the wrap occurred.
///         Returns 0.0 if increment is 0 (no advancement).
///
/// @example
/// @code
/// // Phase was 0.98, increment is 0.05, so after advance:
/// // unwrapped = 1.03, wrapped = 0.03
/// // offset = 0.03 / 0.05 = 0.6 (wrap happened 60% through the sample)
/// double offset = subsamplePhaseWrapOffset(0.03, 0.05);  // returns 0.6
/// @endcode
[[nodiscard]] constexpr double subsamplePhaseWrapOffset(
    double phase,
    double increment
) noexcept {
    if (increment > 0.0) {
        return phase / increment;
    }
    return 0.0;
}

// =============================================================================
// PhaseAccumulator Struct (FR-019, FR-020, FR-021)
// =============================================================================

/// @brief Lightweight phase accumulator for oscillator phase management.
///
/// Value type with public members designed for composition into oscillator
/// classes. Uses double precision for phase and increment to prevent
/// accumulated rounding error over long playback durations.
///
/// @note This is a value type (POD-like struct), not an encapsulated class.
///       Direct member access is intentional for performance and simplicity.
///
/// @example
/// @code
/// PhaseAccumulator acc;
/// acc.setFrequency(440.0f, 44100.0f);
/// for (int i = 0; i < numSamples; ++i) {
///     float saw = 2.0f * static_cast<float>(acc.phase) - 1.0f;
///     bool wrapped = acc.advance();
///     if (wrapped) { /* apply BLEP correction */ }
///     output[i] = saw;
/// }
/// @endcode
struct PhaseAccumulator {
    double phase = 0.0;       ///< Current phase position [0, 1)
    double increment = 0.0;   ///< Phase advance per sample

    /// @brief Advance phase by one sample and wrap if necessary.
    /// @return true if the phase wrapped around (crossed 1.0), false otherwise.
    [[nodiscard]] bool advance() noexcept {
        phase += increment;
        if (phase >= 1.0) {
            phase -= 1.0;
            return true;
        }
        return false;
    }

    /// @brief Reset phase to 0.0. Preserves increment.
    void reset() noexcept {
        phase = 0.0;
    }

    /// @brief Set the phase increment from frequency and sample rate.
    /// @param frequency Oscillator frequency in Hz
    /// @param sampleRate Sample rate in Hz
    void setFrequency(float frequency, float sampleRate) noexcept {
        increment = calculatePhaseIncrement(frequency, sampleRate);
    }
};

} // namespace DSP
} // namespace Krate
