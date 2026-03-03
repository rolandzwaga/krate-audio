// ==============================================================================
// API Contract: FM/PM Synthesis Operator
// ==============================================================================
// This file defines the public API for FMOperator.
// Implementation MUST match this contract exactly.
//
// Feature: 021-fm-pm-synth-operator
// Layer: 2 (DSP Processor)
// Date: 2026-02-05
// ==============================================================================

#pragma once

namespace Krate {
namespace DSP {

/// @brief Single FM synthesis operator (oscillator + ratio + feedback + level).
///
/// The fundamental building block for FM/PM synthesis. Uses phase modulation
/// (Yamaha DX7-style) where the modulator output is added to the carrier's
/// phase, not frequency.
///
/// @par Features
/// - Sine wave oscillation at frequency * ratio
/// - Self-modulation feedback with tanh soft limiting
/// - External phase modulation input (for operator chaining)
/// - Level-controlled output with raw output access for modulator use
///
/// @par Memory Model
/// Owns an internal WavetableData (~90 KB) for the sine wavetable.
/// Each FMOperator instance is self-contained.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// process() and all setters are fully real-time safe.
/// prepare() is NOT real-time safe (generates wavetable).
///
/// @par Usage Example
/// @code
/// // Simple carrier at 440 Hz
/// FMOperator carrier;
/// carrier.prepare(44100.0);
/// carrier.setFrequency(440.0f);
/// carrier.setRatio(1.0f);
/// carrier.setLevel(1.0f);
/// float output = carrier.process();
///
/// // Two-operator FM: modulator -> carrier
/// FMOperator modulator, carrier;
/// modulator.prepare(44100.0);
/// carrier.prepare(44100.0);
/// modulator.setFrequency(440.0f);
/// modulator.setRatio(2.0f);     // 880 Hz modulator
/// modulator.setLevel(0.5f);     // Modulation index control
/// carrier.setFrequency(440.0f);
/// carrier.setRatio(1.0f);
/// carrier.setLevel(1.0f);
///
/// for (int i = 0; i < numSamples; ++i) {
///     float modOut = modulator.process();
///     float pm = modulator.lastRawOutput() * modulator.getLevel();
///     output[i] = carrier.process(pm);
/// }
/// @endcode
class FMOperator {
public:
    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003, FR-016)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes to safe silence state (FR-001):
    /// - frequency = 0 Hz
    /// - ratio = 1.0
    /// - feedback = 0.0
    /// - level = 0.0
    /// - unprepared state
    ///
    /// process() returns 0.0 until prepare() is called (FR-016).
    FMOperator() noexcept;

    /// @brief Destructor.
    ~FMOperator() = default;

    /// @brief Copy and move operations.
    FMOperator(const FMOperator&) = default;
    FMOperator& operator=(const FMOperator&) = default;
    FMOperator(FMOperator&&) noexcept = default;
    FMOperator& operator=(FMOperator&&) noexcept = default;

    /// @brief Initialize the operator for the given sample rate (FR-002).
    ///
    /// Generates the internal sine wavetable and initializes the oscillator.
    /// All internal state is reset.
    ///
    /// @param sampleRate Sample rate in Hz (must be > 0)
    ///
    /// @note NOT real-time safe (allocates internally via FFT for wavetable)
    /// @note Calling prepare() multiple times is safe; state is fully reset
    void prepare(double sampleRate) noexcept;

    /// @brief Reset phase and feedback history, preserving configuration (FR-003).
    ///
    /// After reset():
    /// - Phase starts from 0
    /// - Feedback history cleared (no feedback contribution on first sample)
    /// - frequency, ratio, feedbackAmount, level preserved
    ///
    /// Use on note-on for clean attack in polyphonic context.
    ///
    /// @note Real-time safe: noexcept, no allocations
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-004, FR-005, FR-006, FR-007, FR-014)
    // =========================================================================

    /// @brief Set the base frequency in Hz (FR-004).
    ///
    /// The effective oscillation frequency is `frequency * ratio`.
    ///
    /// @param hz Frequency in Hz, clamped to [0, sampleRate/2)
    ///
    /// @note NaN and Infinity inputs are sanitized to 0 Hz
    /// @note Real-time safe
    void setFrequency(float hz) noexcept;

    /// @brief Set the frequency ratio (multiplier) (FR-005).
    ///
    /// The effective oscillation frequency is `frequency * ratio`.
    /// - Integer ratios (1, 2, 3) produce harmonic partials
    /// - Non-integer ratios (1.41, 3.5) produce inharmonic/metallic tones
    ///
    /// @param ratio Frequency multiplier, clamped to [0, 16.0]
    ///
    /// @note The effective frequency is also Nyquist-clamped
    /// @note Real-time safe
    void setRatio(float ratio) noexcept;

    /// @brief Set the self-modulation feedback amount (FR-006).
    ///
    /// Controls the intensity of self-modulation:
    /// - 0.0: Pure sine wave (no feedback)
    /// - 0.3-0.5: Progressively saw-like waveform
    /// - 1.0: Maximum harmonic richness, sawtooth-like
    ///
    /// The feedback signal is soft-limited using fastTanh to prevent
    /// instability: `feedbackPM = tanh(previousOutput * feedbackAmount)`.
    ///
    /// @param amount Feedback intensity, clamped to [0, 1]
    ///
    /// @note Real-time safe
    void setFeedback(float amount) noexcept;

    /// @brief Set the output level (amplitude) (FR-007).
    ///
    /// Scales the operator's output AFTER the sine computation.
    /// When used as a modulator, level controls the modulation index.
    ///
    /// @param level Output amplitude, clamped to [0, 1]
    ///
    /// @note Real-time safe
    void setLevel(float level) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get the current base frequency in Hz.
    [[nodiscard]] float getFrequency() const noexcept;

    /// @brief Get the current frequency ratio.
    [[nodiscard]] float getRatio() const noexcept;

    /// @brief Get the current feedback amount.
    [[nodiscard]] float getFeedback() const noexcept;

    /// @brief Get the current output level.
    [[nodiscard]] float getLevel() const noexcept;

    // =========================================================================
    // Processing (FR-008, FR-010, FR-012, FR-013, FR-015)
    // =========================================================================

    /// @brief Generate one output sample (FR-008, FR-015).
    ///
    /// @param phaseModInput External phase modulation in radians (FR-010).
    ///        A modulator output of +/-1.0 represents +/-1.0 radians of PM.
    ///        Default is 0.0 (no external modulation).
    ///
    /// @return Output sample, level-scaled and sanitized to [-2.0, 2.0] (FR-013)
    ///
    /// @note Returns 0.0 if prepare() has not been called (FR-016)
    /// @note Real-time safe: noexcept, no allocations (FR-015)
    ///
    /// @par Signal Flow
    /// 1. effectiveFreq = frequency * ratio (Nyquist-clamped)
    /// 2. feedbackPM = tanh(previousRawOutput * feedbackAmount)
    /// 3. totalPM = phaseModInput + feedbackPM
    /// 4. rawOutput = sin(phase + totalPM)
    /// 5. output = rawOutput * level
    /// 6. return sanitize(output)
    [[nodiscard]] float process(float phaseModInput = 0.0f) noexcept;

    // =========================================================================
    // Output Access (FR-009)
    // =========================================================================

    /// @brief Get the most recent raw (pre-level) output (FR-009).
    ///
    /// Returns the output before level scaling, for use when this operator
    /// serves as a modulator. The raw output is the sine value directly,
    /// ranging approximately [-1, 1].
    ///
    /// @return Last raw output sample (before level scaling)
    ///
    /// @note Use this when chaining operators:
    /// @code
    /// float pm = modulator.lastRawOutput() * modulatorLevel;
    /// output = carrier.process(pm);
    /// @endcode
    [[nodiscard]] float lastRawOutput() const noexcept;
};

} // namespace DSP
} // namespace Krate
