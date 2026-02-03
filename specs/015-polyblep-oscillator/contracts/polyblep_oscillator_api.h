// ==============================================================================
// API Contract: PolyBLEP Oscillator
// ==============================================================================
// This file defines the public API contract for PolyBlepOscillator.
// It is a DESIGN DOCUMENT, not compilable code.
//
// Location: dsp/include/krate/dsp/primitives/polyblep_oscillator.h
// Layer: 1 (primitives/)
// Namespace: Krate::DSP
// Dependencies: core/polyblep.h, core/phase_utils.h, core/math_constants.h, core/db_utils.h
// ==============================================================================

#pragma once

#include <krate/dsp/core/polyblep.h>
#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// OscWaveform Enumeration (FR-001, FR-002)
// =============================================================================

/// @brief Waveform types for the PolyBLEP oscillator.
///
/// File-scope enum in Krate::DSP namespace, shared by downstream components
/// (sync oscillator, sub-oscillator, unison engine).
///
/// Values are sequential starting from 0, usable as array indices.
enum class OscWaveform : uint8_t {
    Sine = 0,       ///< Pure sine wave (no PolyBLEP correction needed)
    Sawtooth = 1,   ///< Band-limited sawtooth with PolyBLEP at wrap
    Square = 2,     ///< Band-limited square with PolyBLEP at both edges
    Pulse = 3,      ///< Band-limited pulse with variable width, PolyBLEP at both edges
    Triangle = 4    ///< Band-limited triangle via leaky-integrated PolyBLEP square
};

// =============================================================================
// PolyBlepOscillator Class (FR-003)
// =============================================================================

/// @brief Band-limited audio-rate oscillator using PolyBLEP anti-aliasing.
///
/// Generates sine, sawtooth, square, pulse, and triangle waveforms at audio
/// rates with polynomial band-limited step (PolyBLEP) correction to reduce
/// aliasing at waveform discontinuities.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread
/// (typically the audio thread). No internal synchronization.
///
/// @par Real-Time Safety
/// process() and processBlock() are fully real-time safe: no allocation,
/// no exceptions, no blocking, no I/O.
///
/// @par Usage
/// @code
/// PolyBlepOscillator osc;
/// osc.prepare(44100.0);
/// osc.setFrequency(440.0f);
/// osc.setWaveform(OscWaveform::Sawtooth);
/// for (int i = 0; i < numSamples; ++i) {
///     output[i] = osc.process();
/// }
/// @endcode
class PolyBlepOscillator {
public:
    // =========================================================================
    // Lifecycle (FR-004, FR-005)
    // =========================================================================

    PolyBlepOscillator() noexcept = default;
    ~PolyBlepOscillator() = default;

    // Copyable and movable (value semantics, no heap allocation)
    PolyBlepOscillator(const PolyBlepOscillator&) noexcept = default;
    PolyBlepOscillator& operator=(const PolyBlepOscillator&) noexcept = default;
    PolyBlepOscillator(PolyBlepOscillator&&) noexcept = default;
    PolyBlepOscillator& operator=(PolyBlepOscillator&&) noexcept = default;

    /// @brief Initialize the oscillator for the given sample rate.
    /// Resets all internal state. NOT real-time safe.
    /// @param sampleRate Sample rate in Hz (e.g., 44100.0, 48000.0, 96000.0)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset phase and internal state without changing configuration.
    /// Resets: phase to 0, integrator to 0, phaseWrapped to false, FM/PM to 0.
    /// Preserves: frequency, waveform, pulse width, sample rate.
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Set the oscillator frequency in Hz.
    /// Silently clamped to [0, sampleRate/2) to satisfy PolyBLEP precondition.
    /// @param hz Frequency in Hz
    void setFrequency(float hz) noexcept;

    /// @brief Select the active waveform.
    /// When switching away from or to Triangle, the leaky integrator state is cleared.
    /// Phase is maintained for continuity.
    /// @param waveform The waveform to generate
    void setWaveform(OscWaveform waveform) noexcept;

    /// @brief Set the pulse width for the Pulse waveform.
    /// Silently clamped to [0.01, 0.99]. Has no effect on other waveforms.
    /// @param width Duty cycle (0.5 = square wave)
    void setPulseWidth(float width) noexcept;

    // =========================================================================
    // Processing (FR-009, FR-010, FR-011 through FR-016)
    // =========================================================================

    /// @brief Generate and return one sample of anti-aliased output.
    /// Real-time safe: no allocation, no exceptions, no blocking, no I/O.
    /// @return Audio sample, nominally in [-1, 1] with small PolyBLEP overshoot
    [[nodiscard]] float process() noexcept;

    /// @brief Generate numSamples into the provided buffer.
    /// Result is identical to calling process() that many times (SC-008).
    /// @param output Pointer to output buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate (0 = no-op)
    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Phase Access (FR-017, FR-018, FR-019)
    // =========================================================================

    /// @brief Get the current phase position.
    /// @return Phase in [0, 1), representing the oscillator's position in the cycle
    [[nodiscard]] double phase() const noexcept;

    /// @brief Check if the most recent process() call produced a phase wrap.
    /// @return true if the phase crossed from near-1.0 to near-0.0
    [[nodiscard]] bool phaseWrapped() const noexcept;

    /// @brief Force the phase to a specific position.
    /// Value is wrapped to [0, 1) if outside range.
    /// When used for hard sync, the Triangle integrator state is preserved.
    /// @param newPhase Phase position (default: 0.0)
    void resetPhase(double newPhase = 0.0) noexcept;

    // =========================================================================
    // Modulation Inputs (FR-020, FR-021)
    // =========================================================================

    /// @brief Add a phase modulation offset for the current sample.
    /// Converted from radians to normalized [0, 1) internally.
    /// Does NOT accumulate between samples -- set before each process() call.
    /// @param radians Phase offset in radians
    void setPhaseModulation(float radians) noexcept;

    /// @brief Add a frequency modulation offset for the current sample.
    /// Effective frequency is clamped to [0, sampleRate/2).
    /// Does NOT accumulate between samples -- set before each process() call.
    /// @param hz Frequency offset in Hz (can be negative)
    void setFrequencyModulation(float hz) noexcept;

private:
    // Internal state (cache-friendly layout, hot-path data first)
    PhaseAccumulator phaseAcc_;     // Phase state (16 bytes: phase + increment)
    float dt_ = 0.0f;              // Cached phase increment as float
    float sampleRate_ = 0.0f;      // Sample rate in Hz
    float frequency_ = 440.0f;     // Base frequency in Hz
    float pulseWidth_ = 0.5f;      // Pulse width [0.01, 0.99]
    float integrator_ = 0.0f;      // Leaky integrator state (Triangle)
    float fmOffset_ = 0.0f;        // FM offset in Hz (per-sample, reset)
    float pmOffset_ = 0.0f;        // PM offset in radians (per-sample, reset)
    OscWaveform waveform_ = OscWaveform::Sine;
    bool phaseWrapped_ = false;     // Last process() produced a wrap
};

} // namespace DSP
} // namespace Krate
