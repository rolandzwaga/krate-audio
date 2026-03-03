// ==============================================================================
// API Contract: SynthVoice (Layer 3 System)
// ==============================================================================
// This file defines the public API contract for the SynthVoice class.
// Implementation in: dsp/include/krate/dsp/systems/synth_voice.h
// Spec: specs/037-basic-synth-voice/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, value semantics, [[nodiscard]])
// - Principle IX: Layer 3 (depends only on Layer 0-1)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

// Layer 0 dependencies
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>

// Layer 1 dependencies
#include <krate/dsp/primitives/polyblep_oscillator.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/envelope_utils.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate::DSP {

/// @brief Complete single-voice subtractive synthesis unit.
///
/// A Layer 3 system that composes:
/// - 2 PolyBlepOscillators with mix, detune, and octave offset
/// - 1 SVF filter with envelope modulation and key tracking
/// - 2 ADSREnvelopes (amplitude and filter)
/// - Velocity mapping to amplitude and filter envelope depth
///
/// Signal flow: Osc1+Osc2 -> Mix -> Filter -> AmpEnv -> Output
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// process() and processBlock() are fully real-time safe.
/// prepare() is NOT real-time safe (initializes sub-components).
/// All setters are real-time safe (FR-031).
///
/// @par Usage
/// @code
/// SynthVoice voice;
/// voice.prepare(44100.0);
/// voice.noteOn(440.0f, 0.8f);
/// for (int i = 0; i < numSamples; ++i) {
///     output[i] = voice.process();
/// }
/// voice.noteOff();
/// // Continue processing until isActive() returns false
/// @endcode
class SynthVoice {
public:
    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    SynthVoice() noexcept = default;

    /// @brief Initialize all components for the given sample rate (FR-001).
    /// NOT real-time safe.
    void prepare(double sampleRate) noexcept;

    /// @brief Clear all internal state without reallocation (FR-002).
    /// Real-time safe.
    void reset() noexcept;

    // =========================================================================
    // Note Control (FR-004, FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Start playing at the given frequency and velocity (FR-004).
    /// On retrigger: envelopes attack from current level, phases preserved (FR-007).
    void noteOn(float frequency, float velocity) noexcept;

    /// @brief Trigger release phase of both envelopes (FR-005).
    void noteOff() noexcept;

    /// @brief Check if the voice is producing audio (FR-006).
    [[nodiscard]] bool isActive() const noexcept;

    // =========================================================================
    // Oscillator Parameters (FR-008 through FR-012)
    // =========================================================================

    /// @brief Set oscillator 1 waveform (FR-009). Default: Sawtooth.
    void setOsc1Waveform(OscWaveform waveform) noexcept;

    /// @brief Set oscillator 2 waveform (FR-009). Default: Sawtooth.
    void setOsc2Waveform(OscWaveform waveform) noexcept;

    /// @brief Set oscillator mix (FR-010). 0=osc1, 1=osc2. Default: 0.5.
    void setOscMix(float mix) noexcept;

    /// @brief Set oscillator 2 detune in cents (FR-011). Range: [-100, +100]. Default: 0.
    void setOsc2Detune(float cents) noexcept;

    /// @brief Set oscillator 2 octave offset (FR-012). Range: [-2, +2]. Default: 0.
    void setOsc2Octave(int octave) noexcept;

    // =========================================================================
    // Filter Parameters (FR-013 through FR-021)
    // =========================================================================

    /// @brief Set filter mode (FR-014). Default: Lowpass.
    void setFilterType(SVFMode type) noexcept;

    /// @brief Set base filter cutoff in Hz (FR-015). Range: [20, 20000]. Default: 1000.
    void setFilterCutoff(float hz) noexcept;

    /// @brief Set filter resonance Q (FR-016). Range: [0.1, 30]. Default: 0.707.
    void setFilterResonance(float q) noexcept;

    /// @brief Set filter envelope modulation depth in semitones (FR-017).
    /// Bipolar: positive opens, negative closes. Range: [-96, +96]. Default: 0.
    void setFilterEnvAmount(float semitones) noexcept;

    /// @brief Set filter key tracking amount (FR-020). Range: [0, 1]. Default: 0.
    void setFilterKeyTrack(float amount) noexcept;

    // =========================================================================
    // Amplitude Envelope Parameters (FR-022, FR-023, FR-024)
    // =========================================================================

    void setAmpAttack(float ms) noexcept;
    void setAmpDecay(float ms) noexcept;
    void setAmpSustain(float level) noexcept;
    void setAmpRelease(float ms) noexcept;
    void setAmpAttackCurve(EnvCurve curve) noexcept;
    void setAmpDecayCurve(EnvCurve curve) noexcept;
    void setAmpReleaseCurve(EnvCurve curve) noexcept;

    // =========================================================================
    // Filter Envelope Parameters (FR-022, FR-023, FR-024)
    // =========================================================================

    void setFilterAttack(float ms) noexcept;
    void setFilterDecay(float ms) noexcept;
    void setFilterSustain(float level) noexcept;
    void setFilterRelease(float ms) noexcept;
    void setFilterAttackCurve(EnvCurve curve) noexcept;
    void setFilterDecayCurve(EnvCurve curve) noexcept;
    void setFilterReleaseCurve(EnvCurve curve) noexcept;

    // =========================================================================
    // Velocity Mapping (FR-026, FR-027)
    // =========================================================================

    /// @brief Set velocity-to-filter-envelope scaling (FR-027).
    /// Range: [0, 1]. 0=no effect, 1=full velocity control. Default: 0.
    void setVelocityToFilterEnv(float amount) noexcept;

    // =========================================================================
    // Processing (FR-028, FR-029, FR-030)
    // =========================================================================

    /// @brief Generate one output sample (FR-030).
    /// Returns 0.0 if not prepared or not active (FR-003).
    [[nodiscard]] float process() noexcept;

    /// @brief Generate a block of samples (FR-030).
    /// Bit-identical to calling process() numSamples times (SC-004).
    void processBlock(float* output, size_t numSamples) noexcept;
};

} // namespace Krate::DSP
