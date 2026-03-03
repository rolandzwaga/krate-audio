// ==============================================================================
// Layer 2: DSP Processor - Transient Detector Modulation Source
// ==============================================================================
// Generates attack-decay envelopes triggered by rapid amplitude rises.
// Uses envelope derivative analysis for detection.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends only on Layer 0-1)
//
// Reference: specs/008-modulation-system/spec.md (FR-048 to FR-054)
// ==============================================================================

#pragma once

#include <krate/dsp/core/modulation_source.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

/// @brief Transient detector modulation source.
///
/// Detects transients using envelope derivative analysis and generates
/// attack-decay envelopes. Supports retrigger from current envelope level.
///
/// @par Algorithm
/// 1. Compute running amplitude envelope (fast one-pole follower)
/// 2. Compute envelope derivative (current - previous)
/// 3. Detect when BOTH amplitude > ampThreshold AND delta > rateThreshold
/// 4. On detection: start linear attack ramp from current level to 1.0
/// 5. After peak: exponential decay to 0.0
///
/// @par Output Range: [0, +1]
class TransientDetector : public ModulationSource {
public:
    static constexpr float kMinSensitivity = 0.0f;
    static constexpr float kMaxSensitivity = 1.0f;
    static constexpr float kDefaultSensitivity = 0.5f;
    static constexpr float kMinAttackMs = 0.5f;
    static constexpr float kMaxAttackMs = 10.0f;
    static constexpr float kDefaultAttackMs = 2.0f;
    static constexpr float kMinDecayMs = 20.0f;
    static constexpr float kMaxDecayMs = 200.0f;
    static constexpr float kDefaultDecayMs = 50.0f;

    TransientDetector() noexcept = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    /// @brief Process one audio sample for transient detection.
    /// @param sample Input audio sample (absolute value taken internally)
    void process(float sample) noexcept;

    // ModulationSource interface
    [[nodiscard]] float getCurrentValue() const noexcept override;
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override;

    // Parameter setters
    void setSensitivity(float sensitivity) noexcept;
    void setAttackTime(float ms) noexcept;
    void setDecayTime(float ms) noexcept;

private:
    enum class State : uint8_t {
        Idle = 0,
        Attack,
        Decay
    };

    void updateCoefficients() noexcept;
    void triggerAttack() noexcept;

    // Detection state
    float inputEnvelope_ = 0.0f;       // Fast amplitude follower
    float prevEnvelope_ = 0.0f;        // Previous envelope for delta
    float inputFollowerCoeff_ = 0.0f;  // Fast attack coefficient (~1ms)

    // Output envelope
    float envelope_ = 0.0f;            // Current output [0, 1]
    State state_ = State::Idle;

    // Attack ramp
    float attackIncrement_ = 0.0f;     // Per-sample linear increment
    float attackTarget_ = 1.0f;        // Always 1.0

    // Decay
    float decayCoeff_ = 0.0f;          // Exponential decay coefficient

    // Thresholds (derived from sensitivity)
    float ampThreshold_ = 0.25f;       // 0.5 * (1 - sensitivity)
    float rateThreshold_ = 0.05f;      // 0.1 * (1 - sensitivity)

    // Parameters
    float sensitivity_ = kDefaultSensitivity;
    float attackMs_ = kDefaultAttackMs;
    float decayMs_ = kDefaultDecayMs;
    double sampleRate_ = 44100.0;
};

}  // namespace DSP
}  // namespace Krate
