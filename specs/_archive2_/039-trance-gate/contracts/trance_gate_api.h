// ==============================================================================
// API Contract: TranceGate (039)
// ==============================================================================
// Layer 2 Processor - Rhythmic energy shaper (pattern-driven VCA)
//
// This file defines the public API contract. Implementation details may vary
// but all public methods, signatures, and behaviors are binding.
//
// Location: dsp/include/krate/dsp/processors/trance_gate.h
// Tests: dsp/tests/unit/processors/trance_gate_test.cpp
// ==============================================================================

#pragma once

#include <krate/dsp/core/euclidean_pattern.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// GateStep (FR-001)
// =============================================================================

/// @brief A single step in the trance gate pattern.
///
/// Holds a float gain level in [0.0, 1.0], enabling nuanced patterns with
/// ghost notes, accents, and silence -- not just boolean on/off.
struct GateStep {
    float level{1.0f};  ///< Gain level: 0.0 = silence, 1.0 = full volume
};

// =============================================================================
// TranceGateParams (FR-001 through FR-012)
// =============================================================================

/// @brief Configuration parameters for the TranceGate processor.
///
/// Uses NoteValue/NoteModifier enums (Layer 0) for tempo sync, consistent
/// with SequencerCore and delay effects.
struct TranceGateParams {
    int numSteps{16};                                ///< Active steps: [2, 32]
    float rateHz{4.0f};                              ///< Free-run step rate in Hz [0.1, 100.0]
    float depth{1.0f};                               ///< Gate depth [0.0, 1.0]: 0 = bypass, 1 = full
    float attackMs{2.0f};                            ///< Attack ramp time [1.0, 20.0] ms
    float releaseMs{10.0f};                          ///< Release ramp time [1.0, 50.0] ms
    float phaseOffset{0.0f};                         ///< Pattern rotation [0.0, 1.0]
    bool tempoSync{true};                            ///< true = tempo sync, false = free-run
    NoteValue noteValue{NoteValue::Sixteenth};       ///< Step note value (tempo sync)
    NoteModifier noteModifier{NoteModifier::None};   ///< Step note modifier (tempo sync)
    bool perVoice{true};                             ///< true = reset on noteOn, false = free-run clock
};

// =============================================================================
// TranceGate Class (Layer 2 Processor)
// =============================================================================

/// @brief Rhythmic energy shaper -- pattern-driven VCA for amplitude gating.
///
/// Applies a repeating step pattern as a multiplicative gain to the input
/// signal, with per-sample exponential smoothing for click-free transitions.
/// Designed for placement post-distortion, pre-VCA in the Ruinae voice chain.
///
/// @par Key Features
/// - Float-level step patterns (0.0-1.0) for ghost notes and accents (FR-001)
/// - Asymmetric attack/release one-pole smoothing (FR-003)
/// - Depth control for subtle rhythmic motion (FR-004)
/// - Tempo-synced and free-running modes (FR-005, FR-006)
/// - Euclidean pattern generation via EuclideanPattern (L0) (FR-007)
/// - Modulation output: current gate envelope value (FR-008)
/// - Per-voice and global clock modes (FR-010)
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free (Constitution II).
///
/// @par Usage
/// @code
/// TranceGate gate;
/// gate.prepare(44100.0);
/// gate.setTempo(120.0);
///
/// TranceGateParams params;
/// params.numSteps = 16;
/// params.noteValue = NoteValue::Sixteenth;
/// gate.setParams(params);
///
/// // Set alternating pattern
/// for (int i = 0; i < 16; ++i)
///     gate.setStep(i, (i % 2 == 0) ? 1.0f : 0.0f);
///
/// // In audio callback:
/// for (size_t s = 0; s < numSamples; ++s)
///     output[s] = gate.process(input[s]);
///
/// // Read gate value for modulation routing:
/// float modValue = gate.getGateValue();
/// @endcode
class TranceGate {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxSteps = 32;        ///< Maximum pattern length
    static constexpr int kMinSteps = 2;         ///< Minimum pattern length
    static constexpr float kMinAttackMs = 1.0f;
    static constexpr float kMaxAttackMs = 20.0f;
    static constexpr float kMinReleaseMs = 1.0f;
    static constexpr float kMaxReleaseMs = 50.0f;
    static constexpr float kMinRateHz = 0.1f;
    static constexpr float kMaxRateHz = 100.0f;
    static constexpr double kMinTempoBPM = 20.0;
    static constexpr double kMaxTempoBPM = 300.0;
    static constexpr double kDefaultSampleRate = 44100.0;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor. All steps default to 1.0 (passthrough).
    TranceGate() noexcept;

    /// @brief Prepare for processing at given sample rate.
    /// @param sampleRate Sample rate in Hz
    /// @post All time-dependent coefficients recalculated
    void prepare(double sampleRate) noexcept;

    /// @brief Reset gate state based on mode.
    /// @post In per-voice mode: step position and counter reset to 0
    /// @post In global mode: no-op (clock continues)
    void reset() noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set all gate parameters at once.
    /// @param params Configuration struct
    void setParams(const TranceGateParams& params) noexcept;

    /// @brief Set tempo in BPM. Called once per processing block.
    /// @param bpm Tempo [20.0, 300.0], clamped
    void setTempo(double bpm) noexcept;

    // =========================================================================
    // Pattern Control
    // =========================================================================

    /// @brief Set a single step's level.
    /// @param index Step index [0, numSteps-1]
    /// @param level Gain level [0.0, 1.0], clamped
    void setStep(int index, float level) noexcept;

    /// @brief Set the entire pattern from an array.
    /// @param pattern Array of step levels
    /// @param numSteps Number of active steps
    void setPattern(const std::array<float, kMaxSteps>& pattern,
                    int numSteps) noexcept;

    /// @brief Generate a Euclidean pattern.
    /// @param hits Number of active steps (pulses)
    /// @param steps Total number of steps
    /// @param rotation Pattern rotation offset (default 0)
    /// @post Active steps (hits) get level 1.0, inactive get 0.0
    void setEuclidean(int hits, int steps, int rotation = 0) noexcept;

    // =========================================================================
    // Processing (FR-012, FR-013)
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Input sample
    /// @return Gated output sample: input * g_final(t)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a mono block in-place.
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples
    void processBlock(float* buffer, size_t numSamples) noexcept;

    /// @brief Process a stereo block in-place.
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples
    /// @post Identical gain applied to both channels (SC-007)
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

    // =========================================================================
    // Queries (FR-008, FR-009)
    // =========================================================================

    /// @brief Get current smoothed, depth-adjusted gate value.
    /// @return Gate value in [0.0, 1.0], suitable as modulation source
    [[nodiscard]] float getGateValue() const noexcept;

    /// @brief Get current step index.
    /// @return Step index in [0, numSteps-1]
    [[nodiscard]] int getCurrentStep() const noexcept;
};

} // namespace DSP
} // namespace Krate
