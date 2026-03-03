// ==============================================================================
// API Contract: ADSREnvelope
// ==============================================================================
// Layer 1: DSP Primitive - ADSR Envelope Generator
//
// This file defines the public API contract for the ADSR envelope generator.
// Implementation will be in: dsp/include/krate/dsp/primitives/adsr_envelope.h
//
// Reference: specs/032-adsr-envelope-generator/spec.md
// ==============================================================================

#pragma once

#include <cstdint>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants (FR-007, FR-011)
// =============================================================================

/// Threshold below which the release stage transitions to Idle.
/// -80dB equivalent. Semantically independent from OnePoleSmoother's threshold.
inline constexpr float kEnvelopeIdleThreshold = 1e-4f;

/// Minimum time for Attack, Decay, and Release stages (milliseconds).
inline constexpr float kMinEnvelopeTimeMs = 0.1f;

/// Maximum time for Attack, Decay, and Release stages (milliseconds).
inline constexpr float kMaxEnvelopeTimeMs = 10000.0f;

/// Smoothing time for sustain level changes during Sustain stage (FR-025).
inline constexpr float kSustainSmoothTimeMs = 5.0f;

/// EarLevel Engineering canonical target ratio for exponential attack curves.
inline constexpr float kDefaultTargetRatioA = 0.3f;

/// EarLevel Engineering canonical target ratio for exponential decay/release curves.
inline constexpr float kDefaultTargetRatioDR = 0.0001f;

/// Target ratio value that approximates a linear curve.
inline constexpr float kLinearTargetRatio = 100.0f;

// =============================================================================
// Enumerations (FR-001, FR-013)
// =============================================================================

/// @brief Envelope stage state machine states (FR-001).
enum class ADSRStage : uint8_t {
    Idle = 0,  ///< Not active, output = 0.0
    Attack,    ///< Rising toward peak level
    Decay,     ///< Falling toward sustain level
    Sustain,   ///< Holding at sustain level (gate on)
    Release    ///< Falling toward 0.0 (gate off)
};

/// @brief Curve shape options for time-based stages (FR-013).
enum class EnvCurve : uint8_t {
    Exponential = 0,  ///< Fast initial change, gradual approach (default)
    Linear,           ///< Constant rate of change
    Logarithmic       ///< Slow initial change, accelerating finish
};

/// @brief Retrigger behavior modes (FR-018, FR-019).
enum class RetriggerMode : uint8_t {
    Hard = 0,  ///< Restart attack from current level (default)
    Legato     ///< Continue from current stage/level
};

// =============================================================================
// ADSREnvelope Class (FR-001 through FR-030)
// =============================================================================

/// @brief ADSR envelope generator for synthesizer applications.
///
/// Produces time-varying amplitude envelopes with four stages:
/// Attack, Decay, Sustain, Release. Uses the EarLevel Engineering
/// one-pole iterative approach for efficient per-sample computation
/// (1 multiply + 1 add per sample).
///
/// @par Features
/// - Five-state FSM: Idle, Attack, Decay, Sustain, Release
/// - Three curve shapes per stage: Exponential, Linear, Logarithmic
/// - Hard retrigger and legato modes
/// - Optional velocity scaling
/// - Real-time safe parameter changes
/// - 5ms smoothing for sustain level changes
///
/// @par Thread Safety
/// - prepare()/reset(): Call from non-audio thread only
/// - Parameter setters: Call from any thread (single-writer)
/// - process()/processBlock()/gate(): Audio thread only
///
/// @par Real-Time Safety (FR-026, FR-027, FR-028)
/// - No memory allocations
/// - No locks, exceptions, or I/O
/// - All processing methods are noexcept
/// - Denormal-free by design (one-pole overshoot/undershoot targets)
///
/// @par Layer Compliance (FR-029, FR-030)
/// - Layer 1 primitive
/// - Depends only on Layer 0 (db_utils.h) and standard library
/// - Namespace: Krate::DSP
class ADSREnvelope {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    ADSREnvelope() noexcept = default;
    ~ADSREnvelope() = default;

    // Value semantics (small, copyable state)
    ADSREnvelope(const ADSREnvelope&) noexcept = default;
    ADSREnvelope& operator=(const ADSREnvelope&) noexcept = default;
    ADSREnvelope(ADSREnvelope&&) noexcept = default;
    ADSREnvelope& operator=(ADSREnvelope&&) noexcept = default;

    // =========================================================================
    // Initialization (FR-010)
    // =========================================================================

    /// @brief Configure the envelope for the target sample rate.
    /// Recalculates all coefficients. Preserves current output level.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    void prepare(float sampleRate) noexcept;

    /// @brief Reset to idle state with output at 0.0.
    void reset() noexcept;

    // =========================================================================
    // Gate Control (FR-002, FR-018, FR-019, FR-020)
    // =========================================================================

    /// @brief Set the gate state (note on/off).
    ///
    /// gate(true):
    /// - Hard retrigger mode: enters Attack from current output level
    /// - Legato mode: no action if already active; if in Release,
    ///   returns to Sustain (or Decay if above sustain level)
    ///
    /// gate(false):
    /// - Enters Release from current output level (from any active stage)
    /// - No action if already in Idle or Release
    ///
    /// @param on true = note on, false = note off
    void gate(bool on) noexcept;

    // =========================================================================
    // Parameter Setters (FR-011, FR-012, FR-023, FR-024, FR-025)
    // =========================================================================

    /// @brief Set attack time in milliseconds.
    /// Recalculates attack coefficient if currently in Attack stage.
    /// @param ms Attack time [0.1, 10000] ms
    void setAttack(float ms) noexcept;

    /// @brief Set decay time in milliseconds.
    /// Recalculates decay coefficient if currently in Decay stage.
    /// @param ms Decay time [0.1, 10000] ms
    void setDecay(float ms) noexcept;

    /// @brief Set sustain level.
    /// During Sustain stage, output smoothly transitions to new level over 5ms.
    /// @param level Sustain level [0.0, 1.0]
    void setSustain(float level) noexcept;

    /// @brief Set release time in milliseconds.
    /// Recalculates release coefficient if currently in Release stage.
    /// @param ms Release time [0.1, 10000] ms
    void setRelease(float ms) noexcept;

    // =========================================================================
    // Curve Shape Setters (FR-013, FR-014, FR-015, FR-016, FR-017)
    // =========================================================================

    /// @brief Set attack curve shape.
    /// @param curve Exponential (default), Linear, or Logarithmic
    void setAttackCurve(EnvCurve curve) noexcept;

    /// @brief Set decay curve shape.
    /// @param curve Exponential (default), Linear, or Logarithmic
    void setDecayCurve(EnvCurve curve) noexcept;

    /// @brief Set release curve shape.
    /// @param curve Exponential (default), Linear, or Logarithmic
    void setReleaseCurve(EnvCurve curve) noexcept;

    // =========================================================================
    // Retrigger Mode (FR-018, FR-019)
    // =========================================================================

    /// @brief Set retrigger behavior mode.
    /// @param mode Hard (default) or Legato
    void setRetriggerMode(RetriggerMode mode) noexcept;

    // =========================================================================
    // Velocity Scaling (FR-021, FR-022)
    // =========================================================================

    /// @brief Enable or disable velocity scaling.
    /// When disabled (default), peak level is always 1.0.
    /// @param enabled true to enable velocity scaling
    void setVelocityScaling(bool enabled) noexcept;

    /// @brief Set the velocity value for scaling.
    /// Only affects peak level when velocity scaling is enabled.
    /// @param velocity Velocity value [0.0, 1.0]
    void setVelocity(float velocity) noexcept;

    // =========================================================================
    // Processing (FR-008, FR-026, FR-027)
    // =========================================================================

    /// @brief Process one sample and return the envelope output.
    /// @return Current envelope value [0.0, peakLevel]
    [[nodiscard]] float process() noexcept;

    /// @brief Process a block of samples.
    /// Produces identical output to calling process() N times sequentially.
    /// @param output Buffer to write envelope values (must have numSamples capacity)
    /// @param numSamples Number of samples to process
    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // State Queries (FR-001, FR-009)
    // =========================================================================

    /// @brief Get the current envelope stage.
    /// @return Current stage (Idle, Attack, Decay, Sustain, Release)
    [[nodiscard]] ADSRStage getStage() const noexcept;

    /// @brief Check if the envelope is active (any stage except Idle).
    /// @return true if not in Idle stage
    [[nodiscard]] bool isActive() const noexcept;

    /// @brief Check if the envelope is in the Release stage.
    /// @return true if in Release stage
    [[nodiscard]] bool isReleasing() const noexcept;

    /// @brief Get the current output value without advancing state.
    /// @return Current envelope output [0.0, peakLevel]
    [[nodiscard]] float getOutput() const noexcept;
};

} // namespace DSP
} // namespace Krate
