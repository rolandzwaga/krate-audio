// ==============================================================================
// API Contract: Phase Distortion Oscillator
// ==============================================================================
// This file defines the public API for PhaseDistortionOscillator.
// Implementation: dsp/include/krate/dsp/processors/phase_distortion_oscillator.h
//
// Spec: specs/024-phase-distortion-oscillator/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// PDWaveform Enumeration (FR-002)
// =============================================================================

/// @brief Waveform types for Phase Distortion synthesis.
///
/// Non-resonant waveforms (0-4) use piecewise-linear phase transfer functions.
/// Resonant waveforms (5-7) use windowed sync technique for filter-like timbres.
enum class PDWaveform : uint8_t {
    Saw = 0,              ///< Sawtooth via two-segment phase transfer
    Square = 1,           ///< Square wave via four-segment phase transfer
    Pulse = 2,            ///< Variable-width pulse via asymmetric duty cycle
    DoubleSine = 3,       ///< Octave-doubled tone via phase doubling
    HalfSine = 4,         ///< Half-wave rectified tone via phase reflection
    ResonantSaw = 5,      ///< Resonant peak with falling sawtooth window
    ResonantTriangle = 6, ///< Resonant peak with triangle window
    ResonantTrapezoid = 7 ///< Resonant peak with trapezoid window
};

/// @brief Number of waveform types in PDWaveform enum.
inline constexpr size_t kNumPDWaveforms = 8;

// =============================================================================
// PhaseDistortionOscillator Class (FR-001)
// =============================================================================

/// @brief Casio CZ-style Phase Distortion oscillator at Layer 2.
///
/// Generates audio by reading a cosine wavetable at variable rates determined
/// by piecewise-linear phase transfer functions (non-resonant waveforms) or
/// windowed sync technique (resonant waveforms).
///
/// @par Features
/// - 8 waveform types with characteristic timbres
/// - DCW (distortion) parameter morphs from sine to full waveform shape
/// - Phase modulation input for FM/PM synthesis integration
/// - Automatic mipmap anti-aliasing via internal WavetableOscillator
///
/// @par Memory Model
/// Owns internal WavetableData (~90 KB) for the cosine wavetable.
/// Each PhaseDistortionOscillator instance is self-contained.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// - prepare(): NOT real-time safe (generates wavetable)
/// - reset(), setters, process(), processBlock(): Real-time safe
///
/// @par Layer Dependencies
/// - Layer 0: phase_utils.h, math_constants.h, db_utils.h, interpolation.h, wavetable_data.h
/// - Layer 1: wavetable_oscillator.h, wavetable_generator.h
class PhaseDistortionOscillator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @brief Default maximum resonance factor for resonant waveforms.
    /// At distortion=1.0, resonanceMultiplier = 1 + maxResonanceFactor = 9.0
    static constexpr float kDefaultMaxResonanceFactor = 8.0f;

    // =========================================================================
    // Lifecycle (FR-016, FR-017, FR-029)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes to safe silence state:
    /// - frequency = 440 Hz
    /// - distortion = 0.0 (pure sine)
    /// - waveform = Saw
    /// - unprepared state (process() returns 0.0)
    PhaseDistortionOscillator() noexcept;

    /// @brief Destructor.
    ~PhaseDistortionOscillator() = default;

    /// @brief Copy and move operations.
    PhaseDistortionOscillator(const PhaseDistortionOscillator&) = default;
    PhaseDistortionOscillator& operator=(const PhaseDistortionOscillator&) = default;
    PhaseDistortionOscillator(PhaseDistortionOscillator&&) noexcept = default;
    PhaseDistortionOscillator& operator=(PhaseDistortionOscillator&&) noexcept = default;

    /// @brief Initialize the oscillator for the given sample rate (FR-016).
    ///
    /// Generates the internal cosine wavetable and initializes the oscillator.
    /// All internal state is reset. Memory allocation occurs here.
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000 supported)
    ///
    /// @note NOT real-time safe (generates wavetable via FFT)
    /// @note Calling prepare() multiple times is safe; state is fully reset
    void prepare(double sampleRate) noexcept;

    /// @brief Reset phase and internal state without changing configuration (FR-017).
    ///
    /// After reset():
    /// - Phase starts from 0
    /// - Configuration preserved: frequency, distortion, waveform
    ///
    /// Use on note-on for clean attack in polyphonic context.
    ///
    /// @note Real-time safe: noexcept, no allocations
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-018, FR-019, FR-020)
    // =========================================================================

    /// @brief Set the fundamental frequency in Hz (FR-018).
    ///
    /// @param hz Frequency in Hz, clamped to [0, sampleRate/2)
    ///
    /// @note NaN and Infinity inputs are sanitized to 0 Hz
    /// @note Negative frequencies are clamped to 0 Hz
    /// @note Real-time safe
    void setFrequency(float hz) noexcept;

    /// @brief Set the waveform type (FR-019).
    ///
    /// @param waveform Waveform type from PDWaveform enum
    ///
    /// @note Change takes effect on next process() call
    /// @note Phase is preserved to minimize discontinuities
    /// @note Real-time safe
    void setWaveform(PDWaveform waveform) noexcept;

    /// @brief Set the distortion (DCW) amount (FR-020).
    ///
    /// @param amount Distortion intensity [0, 1]
    ///        - 0.0: Pure sine wave (regardless of waveform)
    ///        - 1.0: Full characteristic waveform shape
    ///
    /// @note NaN and Infinity inputs preserve previous value
    /// @note Out-of-range values are clamped to [0, 1]
    /// @note Real-time safe
    void setDistortion(float amount) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get the current frequency in Hz.
    [[nodiscard]] float getFrequency() const noexcept;

    /// @brief Get the current waveform type.
    [[nodiscard]] PDWaveform getWaveform() const noexcept;

    /// @brief Get the current distortion amount.
    [[nodiscard]] float getDistortion() const noexcept;

    // =========================================================================
    // Processing (FR-021, FR-022, FR-026, FR-027, FR-028, FR-029)
    // =========================================================================

    /// @brief Generate one output sample (FR-021).
    ///
    /// @param phaseModInput External phase modulation in radians (FR-026).
    ///        Added to linear phase BEFORE phase distortion transfer function.
    ///        Default is 0.0 (no external modulation).
    ///
    /// @return Output sample, sanitized to [-2.0, 2.0] (FR-028)
    ///
    /// @note Returns 0.0 if prepare() has not been called (FR-029)
    /// @note Real-time safe: noexcept, no allocations (FR-027)
    [[nodiscard]] float process(float phaseModInput = 0.0f) noexcept;

    /// @brief Generate multiple samples at constant parameters (FR-022).
    ///
    /// Produces output identical to calling process() numSamples times.
    ///
    /// @param output Output buffer to fill
    /// @param numSamples Number of samples to generate
    ///
    /// @note Real-time safe: noexcept, no allocations (FR-027)
    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Phase Access (FR-023, FR-024, FR-025)
    // =========================================================================

    /// @brief Get the current phase position (FR-023).
    ///
    /// @return Phase in [0, 1) range
    [[nodiscard]] double phase() const noexcept;

    /// @brief Check if the most recent process() caused a phase wrap (FR-024).
    ///
    /// @return true if phase wrapped from near-1.0 to near-0.0
    [[nodiscard]] bool phaseWrapped() const noexcept;

    /// @brief Force the phase to a specific position (FR-025).
    ///
    /// @param newPhase Phase position, wrapped to [0, 1)
    void resetPhase(double newPhase = 0.0) noexcept;

    // =========================================================================
    // Advanced Configuration
    // =========================================================================

    /// @brief Set the maximum resonance factor for resonant waveforms.
    ///
    /// Controls how high the resonant frequency goes at full distortion.
    /// resonanceMultiplier = 1 + distortion * maxResonanceFactor
    ///
    /// @param factor Maximum factor [1, 16], default 8.0
    ///
    /// @note Real-time safe
    void setMaxResonanceFactor(float factor) noexcept;

    /// @brief Get the current maximum resonance factor.
    [[nodiscard]] float getMaxResonanceFactor() const noexcept;
};

} // namespace DSP
} // namespace Krate
