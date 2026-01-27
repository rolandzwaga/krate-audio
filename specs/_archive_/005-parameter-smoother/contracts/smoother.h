// ============================================================================
// API Contract: Parameter Smoother
// Feature: 005-parameter-smoother
// Layer: 1 - DSP Primitive
// ============================================================================
// This file defines the PUBLIC API contract for parameter smoothing.
// Implementation must conform to these signatures and behaviors.
// ============================================================================

#pragma once

#include <cstddef>  // size_t

namespace Iterum::DSP {

// ============================================================================
// Constants
// ============================================================================

constexpr float kDefaultSmoothingTimeMs = 5.0f;
constexpr float kCompletionThreshold = 0.0001f;
constexpr float kMinSmoothingTimeMs = 0.1f;
constexpr float kMaxSmoothingTimeMs = 1000.0f;
constexpr float kDenormalThreshold = 1e-15f;

// ============================================================================
// Utility Functions
// ============================================================================

/// Calculate one-pole smoothing coefficient from time constant and sample rate
/// @param smoothTimeMs Time to reach 99% of target (in milliseconds)
/// @param sampleRate Sample rate in Hz
/// @return Coefficient for one-pole filter (0.0 to 1.0)
[[nodiscard]] constexpr float calculateOnePolCoefficient(
    float smoothTimeMs,
    float sampleRate
) noexcept;

/// Calculate linear ramp increment per sample
/// @param delta Total change (target - current)
/// @param rampTimeMs Duration of ramp in milliseconds
/// @param sampleRate Sample rate in Hz
/// @return Per-sample increment value
[[nodiscard]] constexpr float calculateLinearIncrement(
    float delta,
    float rampTimeMs,
    float sampleRate
) noexcept;

/// Convert rate from units/ms to units/sample
/// @param unitsPerMs Rate in units per millisecond
/// @param sampleRate Sample rate in Hz
/// @return Rate in units per sample
[[nodiscard]] constexpr float calculateSlewRate(
    float unitsPerMs,
    float sampleRate
) noexcept;

// ============================================================================
// OnePoleSmoother
// ============================================================================
/// Exponential smoothing for audio parameters.
/// Use for: gain, filter cutoff, mix levels, most UI parameters.
/// Characteristic: Fast initial response, asymptotic approach to target.

class OnePoleSmoother {
public:
    /// Default constructor - initializes to 0 with default smoothing time
    OnePoleSmoother() noexcept;

    /// Construct with initial value
    /// @param initialValue Starting value
    explicit OnePoleSmoother(float initialValue) noexcept;

    /// Configure smoothing time and sample rate
    /// @param smoothTimeMs Time to reach 99% of target
    /// @param sampleRate Sample rate in Hz
    void configure(float smoothTimeMs, float sampleRate) noexcept;

    /// Set the target value to approach
    /// @param target New target value
    void setTarget(float target) noexcept;

    /// Get the current target value
    [[nodiscard]] float getTarget() const noexcept;

    /// Get the current smoothed value without advancing
    [[nodiscard]] float getCurrentValue() const noexcept;

    /// Process one sample and return smoothed value
    /// @return Current smoothed value after one step
    [[nodiscard]] float process() noexcept;

    /// Process a block of samples, writing smoothed values
    /// @param output Buffer to write smoothed values
    /// @param numSamples Number of samples to process
    void processBlock(float* output, size_t numSamples) noexcept;

    /// Check if smoothing is complete (within threshold of target)
    [[nodiscard]] bool isComplete() const noexcept;

    /// Immediately set current value to target (no smoothing)
    void snapToTarget() noexcept;

    /// Immediately set both current and target to a new value
    /// @param value New value for both current and target
    void snapTo(float value) noexcept;

    /// Reset to initial state (value 0, target 0)
    void reset() noexcept;

    /// Update sample rate (recalculates coefficient)
    /// @param sampleRate New sample rate in Hz
    void setSampleRate(float sampleRate) noexcept;
};

// ============================================================================
// LinearRamp
// ============================================================================
/// Constant-rate parameter changes for predictable transitions.
/// Use for: delay time (creates tape-like pitch effect), crossfades.
/// Characteristic: Predictable duration, constant rate of change.

class LinearRamp {
public:
    /// Default constructor - initializes to 0 with default ramp time
    LinearRamp() noexcept;

    /// Construct with initial value
    /// @param initialValue Starting value
    explicit LinearRamp(float initialValue) noexcept;

    /// Configure ramp time and sample rate
    /// @param rampTimeMs Time to complete full 0-1 ramp
    /// @param sampleRate Sample rate in Hz
    void configure(float rampTimeMs, float sampleRate) noexcept;

    /// Set the target value to ramp toward
    /// @param target New target value
    void setTarget(float target) noexcept;

    /// Get the current target value
    [[nodiscard]] float getTarget() const noexcept;

    /// Get the current ramped value without advancing
    [[nodiscard]] float getCurrentValue() const noexcept;

    /// Process one sample and return ramped value
    /// @return Current ramped value after one step
    [[nodiscard]] float process() noexcept;

    /// Process a block of samples, writing ramped values
    /// @param output Buffer to write ramped values
    /// @param numSamples Number of samples to process
    void processBlock(float* output, size_t numSamples) noexcept;

    /// Check if ramp is complete (at target)
    [[nodiscard]] bool isComplete() const noexcept;

    /// Immediately set current value to target (no ramping)
    void snapToTarget() noexcept;

    /// Immediately set both current and target to a new value
    /// @param value New value for both current and target
    void snapTo(float value) noexcept;

    /// Reset to initial state (value 0, target 0)
    void reset() noexcept;

    /// Update sample rate (affects ramp rate)
    /// @param sampleRate New sample rate in Hz
    void setSampleRate(float sampleRate) noexcept;
};

// ============================================================================
// SlewLimiter
// ============================================================================
/// Rate-limited parameter changes with separate rise/fall rates.
/// Use for: feedback amount, preventing sudden parameter jumps.
/// Characteristic: Maximum rate of change, different up/down speeds possible.

class SlewLimiter {
public:
    /// Default constructor - initializes to 0 with default rates
    SlewLimiter() noexcept;

    /// Construct with initial value
    /// @param initialValue Starting value
    explicit SlewLimiter(float initialValue) noexcept;

    /// Configure slew rates and sample rate
    /// @param riseRatePerMs Maximum rise rate in units per millisecond
    /// @param fallRatePerMs Maximum fall rate in units per millisecond
    /// @param sampleRate Sample rate in Hz
    void configure(float riseRatePerMs, float fallRatePerMs, float sampleRate) noexcept;

    /// Configure with symmetric rise/fall rate
    /// @param ratePerMs Maximum rate in units per millisecond (both directions)
    /// @param sampleRate Sample rate in Hz
    void configure(float ratePerMs, float sampleRate) noexcept;

    /// Set the target value to approach (rate-limited)
    /// @param target New target value
    void setTarget(float target) noexcept;

    /// Get the current target value
    [[nodiscard]] float getTarget() const noexcept;

    /// Get the current limited value without advancing
    [[nodiscard]] float getCurrentValue() const noexcept;

    /// Process one sample and return rate-limited value
    /// @return Current limited value after one step
    [[nodiscard]] float process() noexcept;

    /// Process a block of samples, writing rate-limited values
    /// @param output Buffer to write limited values
    /// @param numSamples Number of samples to process
    void processBlock(float* output, size_t numSamples) noexcept;

    /// Check if limiting is complete (at target)
    [[nodiscard]] bool isComplete() const noexcept;

    /// Immediately set current value to target (no limiting)
    void snapToTarget() noexcept;

    /// Immediately set both current and target to a new value
    /// @param value New value for both current and target
    void snapTo(float value) noexcept;

    /// Reset to initial state (value 0, target 0)
    void reset() noexcept;

    /// Update sample rate (affects rates)
    /// @param sampleRate New sample rate in Hz
    void setSampleRate(float sampleRate) noexcept;
};

}  // namespace Iterum::DSP
