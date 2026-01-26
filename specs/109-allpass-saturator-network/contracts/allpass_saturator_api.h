// ==============================================================================
// API Contract: AllpassSaturator
// ==============================================================================
// This file defines the PUBLIC API for the AllpassSaturator processor.
// Implementation details are omitted - this is the interface contract only.
//
// Feature: 109-allpass-saturator-network
// Layer: 2 (DSP Processors)
// Dependencies:
//   - Layer 1: Biquad, DelayLine, Waveshaper, DCBlocker, OnePoleSmoother, OnePoleLP
//   - Layer 0: math_constants.h, db_utils.h, sigmoid.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle IX: Layer 2 (composes Layer 0/1 only)
// - Principle X: DSP Constraints (saturation, DC blocking, feedback limiting)
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/waveshaper.h>  // For WaveshapeType

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// NetworkTopology Enumeration
// =============================================================================

/// @brief Available network topology configurations.
///
/// Each topology creates different resonant characteristics:
/// - SingleAllpass: Pitched resonance at specified frequency
/// - AllpassChain: Inharmonic, bell-like tones from cascaded stages
/// - KarplusStrong: Plucked string synthesis
/// - FeedbackMatrix: Dense, evolving textures with cross-channel interaction
enum class NetworkTopology : uint8_t {
    SingleAllpass = 0,   ///< Single allpass + saturator feedback loop
    AllpassChain = 1,    ///< 4 cascaded allpasses at prime frequency ratios
    KarplusStrong = 2,   ///< Delay + lowpass + saturator (string synthesis)
    FeedbackMatrix = 3   ///< 4x4 Householder matrix of cross-fed saturators
};

// =============================================================================
// AllpassSaturator Class
// =============================================================================

/// @brief Resonant distortion processor using allpass filters with saturation.
///
/// Creates pitched, self-oscillating resonances that can be excited by input.
/// Supports four topologies for different timbral characteristics.
///
/// @par Signal Flow (varies by topology)
/// @code
/// SingleAllpass:
///   input -> [+] -> [allpass] -> [saturator] -> [soft clip] -> output
///             ^                                      |
///             |_______ feedback * gain _____________|
///
/// KarplusStrong:
///   input -> [delay] -> [saturator] -> [1-pole LP] -> [soft clip] -> output
///              ^                                          |
///              |__________ feedback _____________________|
/// @endcode
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
/// - Principle IX: Layer 2 (depends only on Layer 0/1)
/// - Principle X: DSP Constraints (saturation, DC blocking, feedback < 100%)
///
/// @par Usage Example
/// @code
/// AllpassSaturator processor;
/// processor.prepare(44100.0, 512);
/// processor.setTopology(NetworkTopology::SingleAllpass);
/// processor.setFrequency(440.0f);
/// processor.setFeedback(0.9f);
/// processor.setDrive(2.0f);
///
/// // In audio callback:
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = processor.process(input[i]);
/// }
/// @endcode
///
/// @see specs/109-allpass-saturator-network/spec.md
class AllpassSaturator {
public:
    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor.
    AllpassSaturator() noexcept;

    /// @brief Destructor.
    ~AllpassSaturator() = default;

    // Non-copyable (contains delay buffers)
    AllpassSaturator(const AllpassSaturator&) = delete;
    AllpassSaturator& operator=(const AllpassSaturator&) = delete;

    // Movable
    AllpassSaturator(AllpassSaturator&&) noexcept = default;
    AllpassSaturator& operator=(AllpassSaturator&&) noexcept = default;

    /// @brief Prepare the processor for processing.
    ///
    /// Allocates internal buffers and initializes components.
    /// Must be called before process().
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @param maxBlockSize Maximum samples per process() call (unused, for API consistency)
    ///
    /// @post prepared_ = true, ready for process() calls
    /// @note FR-001, FR-003: Supports 44100Hz to 192000Hz
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Clear all internal state without reallocation.
    ///
    /// Use when starting new audio to prevent artifacts from previous processing.
    /// Does not change parameter values.
    ///
    /// @note FR-002: Resets all delay buffers, filter states, and feedback values
    void reset() noexcept;

    // =========================================================================
    // Topology Selection (FR-004 to FR-009)
    // =========================================================================

    /// @brief Set the network topology configuration.
    ///
    /// Changes take effect immediately. State is reset to prevent artifacts.
    ///
    /// @param topology Network configuration to use
    ///
    /// @note FR-004: Immediate topology change
    /// @note FR-009: Resets state on topology change
    void setTopology(NetworkTopology topology) noexcept;

    /// @brief Get the current topology.
    /// @return Current network topology
    [[nodiscard]] NetworkTopology getTopology() const noexcept;

    // =========================================================================
    // Frequency Control (FR-010 to FR-012)
    // =========================================================================

    /// @brief Set the resonant frequency.
    ///
    /// For most topologies, this sets the pitch of the resonance.
    /// For AllpassChain, sets the base frequency (stages at f, 1.5f, 2.33f, 3.67f).
    ///
    /// @param hz Frequency in Hz (clamped to [20, sampleRate * 0.45])
    ///
    /// @note FR-010, FR-011: Clamped to valid range
    /// @note FR-012: 10ms smoothing for click-free changes
    void setFrequency(float hz) noexcept;

    /// @brief Get the current frequency setting.
    /// @return Target frequency in Hz
    [[nodiscard]] float getFrequency() const noexcept;

    // =========================================================================
    // Feedback Control (FR-013 to FR-016)
    // =========================================================================

    /// @brief Set the feedback amount.
    ///
    /// Controls resonance intensity and sustain:
    /// - 0.0 = no feedback (single pass through)
    /// - 0.5 = moderate resonance
    /// - 0.9+ = self-oscillation with input excitation
    ///
    /// @param feedback Feedback amount (clamped to [0.0, 0.999])
    ///
    /// @note FR-013: Valid range 0.0 to 1.0
    /// @note FR-014: Values > 0.9 enable self-oscillation
    /// @note FR-015: Soft clipping at +/-2.0 prevents unbounded growth
    /// @note FR-016: 10ms smoothing for click-free changes
    void setFeedback(float feedback) noexcept;

    /// @brief Get the current feedback setting.
    /// @return Target feedback amount
    [[nodiscard]] float getFeedback() const noexcept;

    // =========================================================================
    // Saturation Control (FR-017 to FR-020)
    // =========================================================================

    /// @brief Set the saturation transfer function.
    ///
    /// @param type Waveshape algorithm to use (from Waveshaper primitive)
    ///
    /// @note FR-017, FR-018: Supports all WaveshapeType values
    void setSaturationCurve(WaveshapeType type) noexcept;

    /// @brief Get the current saturation curve.
    /// @return Current waveshape type
    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept;

    /// @brief Set the saturation drive amount.
    ///
    /// Controls saturation intensity:
    /// - 0.1 = subtle warmth
    /// - 1.0 = moderate saturation
    /// - 10.0 = aggressive distortion
    ///
    /// @param drive Drive amount (clamped to [0.1, 10.0])
    ///
    /// @note FR-019: Valid range 0.1 to 10.0
    /// @note FR-020: 10ms smoothing for click-free changes
    void setDrive(float drive) noexcept;

    /// @brief Get the current drive setting.
    /// @return Target drive amount
    [[nodiscard]] float getDrive() const noexcept;

    // =========================================================================
    // Karplus-Strong Specific (FR-021 to FR-023)
    // =========================================================================

    /// @brief Set the decay time for KarplusStrong topology.
    ///
    /// Controls how long the string resonates after excitation.
    /// Only affects KarplusStrong topology; ignored for others.
    ///
    /// @param seconds Decay time in seconds (RT60)
    ///
    /// @note FR-021, FR-022: Only affects KarplusStrong
    /// @note FR-023: Converted to lowpass cutoff for string-like decay
    void setDecay(float seconds) noexcept;

    /// @brief Get the current decay setting.
    /// @return Target decay time in seconds
    [[nodiscard]] float getDecay() const noexcept;

    // =========================================================================
    // Processing (FR-024 to FR-030)
    // =========================================================================

    /// @brief Process a single sample.
    ///
    /// @param input Input sample
    /// @return Processed output sample
    ///
    /// @note FR-024: In-place block processing available
    /// @note FR-025: Real-time safe (no allocations)
    /// @note FR-026: Handles NaN/Inf by resetting and returning 0
    /// @note FR-027: Flushes denormals
    /// @note FR-028: DC blocking after saturation
    /// @note FR-029, FR-030: Bounded output via soft clipping
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    ///
    /// @param buffer Audio buffer (modified in place)
    /// @param numSamples Number of samples to process
    ///
    /// @note Equivalent to calling process() for each sample
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Check if processor has been prepared.
    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get the current sample rate.
    /// @return Sample rate in Hz, or 0 if not prepared
    [[nodiscard]] double getSampleRate() const noexcept;
};

} // namespace DSP
} // namespace Krate
