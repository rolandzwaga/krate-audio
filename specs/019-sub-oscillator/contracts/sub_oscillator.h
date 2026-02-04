// ==============================================================================
// CONTRACT: Layer 2 DSP Processor - Sub-Oscillator
// ==============================================================================
// This is the API contract for implementation. It defines the public interface
// that tests will be written against. The implementation must match this exactly.
//
// Location: dsp/include/krate/dsp/processors/sub_oscillator.h
// Layer: 2 (Processor) - depends on Layer 0 + Layer 1 only
// Namespace: Krate::DSP
//
// Reference: specs/019-sub-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/minblep_table.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// SubOctave Enumeration (FR-001)
// =============================================================================

/// @brief Frequency division depth for the SubOscillator.
///
/// File-scope enum in Krate::DSP namespace, shared by downstream components.
enum class SubOctave : uint8_t {
    OneOctave = 0,   ///< Divide master frequency by 2 (one octave below)
    TwoOctaves = 1   ///< Divide master frequency by 4 (two octaves below)
};

// =============================================================================
// SubWaveform Enumeration (FR-002)
// =============================================================================

/// @brief Waveform type for the SubOscillator.
///
/// File-scope enum in Krate::DSP namespace, shared by downstream components.
enum class SubWaveform : uint8_t {
    Square = 0,    ///< Classic analog flip-flop output with minBLEP correction
    Sine = 1,      ///< Digital sine at sub frequency via phase accumulator
    Triangle = 2   ///< Digital triangle at sub frequency via phase accumulator
};

// =============================================================================
// SubOscillator Class (FR-003)
// =============================================================================

/// @brief Frequency-divided sub-oscillator tracking a master oscillator (Layer 2).
///
/// Implements frequency division using a flip-flop state machine, replicating
/// the classic analog sub-oscillator behavior of Moog, Sequential, and Oberheim
/// synthesizers. Supports square (flip-flop with minBLEP), sine, and triangle
/// waveforms at one-octave (divide-by-2) or two-octave (divide-by-4) depths.
///
/// @par Ownership Model
/// Constructor takes a `const MinBlepTable*` (caller owns lifetime).
/// Multiple SubOscillator instances can share one MinBlepTable (read-only
/// after prepare). Each instance maintains its own Residual buffer.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// process() and processMixed() are fully real-time safe.
///
/// @par Usage
/// @code
/// MinBlepTable table;
/// table.prepare(64, 8);
///
/// PolyBlepOscillator master;
/// master.prepare(44100.0);
/// master.setFrequency(440.0f);
/// master.setWaveform(OscWaveform::Sawtooth);
///
/// SubOscillator sub(&table);
/// sub.prepare(44100.0);
/// sub.setOctave(SubOctave::OneOctave);
/// sub.setWaveform(SubWaveform::Square);
/// sub.setMix(0.5f);
///
/// for (int i = 0; i < numSamples; ++i) {
///     float mainOut = master.process();
///     bool wrapped = master.phaseWrapped();
///     float phaseInc = 440.0f / 44100.0f; // master phase increment
///     output[i] = sub.processMixed(mainOut, wrapped, phaseInc);
/// }
/// @endcode
class SubOscillator {
public:
    // =========================================================================
    // Constructor (FR-003)
    // =========================================================================

    /// @brief Construct with a pointer to a shared MinBlepTable.
    /// @param table Pointer to prepared MinBlepTable (caller owns lifetime).
    ///        May be nullptr; prepare() will validate before use.
    explicit SubOscillator(const MinBlepTable* table = nullptr) noexcept;

    ~SubOscillator() = default;

    SubOscillator(const SubOscillator&) = default;
    SubOscillator& operator=(const SubOscillator&) = default;
    SubOscillator(SubOscillator&&) noexcept = default;
    SubOscillator& operator=(SubOscillator&&) noexcept = default;

    // =========================================================================
    // Lifecycle (FR-004, FR-005)
    // =========================================================================

    /// @brief Initialize for the given sample rate. NOT real-time safe.
    ///
    /// Initializes flip-flop states to false, phase accumulator to 0.0,
    /// and the minBLEP residual buffer. Sets prepared_ to false if the
    /// MinBlepTable pointer is nullptr, not prepared, or has length > 64.
    ///
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept;

    /// @brief Reset state without changing configuration.
    ///
    /// Resets flip-flop states to false, sub phase to 0.0, clears residual.
    /// Preserves: octave, waveform, mix, sample rate.
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Select the frequency division mode.
    /// @param octave OneOctave (master/2) or TwoOctaves (master/4)
    void setOctave(SubOctave octave) noexcept;

    /// @brief Select the sub-oscillator waveform type.
    /// @param waveform Square, Sine, or Triangle
    void setWaveform(SubWaveform waveform) noexcept;

    /// @brief Set the dry/wet balance.
    /// @param mix 0.0 = main only, 1.0 = sub only. Clamped to [0, 1].
    ///        NaN/Inf ignored (previous value retained).
    void setMix(float mix) noexcept;

    // =========================================================================
    // Processing (FR-009, FR-010)
    // =========================================================================

    /// @brief Generate one sample of sub-oscillator output.
    ///
    /// @param masterPhaseWrapped true if the master oscillator's phase
    ///        wrapped (crossed 1.0) on this sample
    /// @param masterPhaseIncrement The master's instantaneous phase
    ///        increment (frequency / sampleRate) for this sample
    /// @return Sub-oscillator output sample. Sanitized to [-2.0, 2.0].
    ///         Returns 0.0 if not prepared.
    [[nodiscard]] float process(bool masterPhaseWrapped,
                                float masterPhaseIncrement) noexcept;

    /// @brief Generate one mixed sample (main + sub with equal-power crossfade).
    ///
    /// @param mainOutput The main oscillator's output for this sample
    /// @param masterPhaseWrapped true if the master's phase wrapped
    /// @param masterPhaseIncrement The master's phase increment
    /// @return Mixed output = mainOutput * mainGain + subOutput * subGain
    [[nodiscard]] float processMixed(float mainOutput,
                                     bool masterPhaseWrapped,
                                     float masterPhaseIncrement) noexcept;

private:
    // Internal state - see data-model.md for layout details
    const MinBlepTable* table_;
    MinBlepTable::Residual residual_;
    PhaseAccumulator subPhase_;

    float sampleRate_;
    float mix_;
    float mainGain_;
    float subGain_;

    bool flipFlop1_;
    bool flipFlop2_;

    SubOctave octave_;
    SubWaveform waveform_;
    bool prepared_;
};

} // namespace DSP
} // namespace Krate
