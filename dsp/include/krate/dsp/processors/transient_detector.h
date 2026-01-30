// ==============================================================================
// Layer 2: DSP Processor - Transient Detector Modulation Source
// ==============================================================================
// Generates attack-decay envelopes triggered by rapid amplitude rises.
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
#include <numbers>
#include <utility>

namespace Krate {
namespace DSP {

/// @brief Transient detector modulation source.
///
/// Detects transients using envelope derivative analysis and generates
/// attack-decay envelopes. Supports retrigger from current envelope level.
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

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        updateCoefficients();
        reset();
    }

    void reset() noexcept {
        inputEnvelope_ = 0.0f;
        prevEnvelope_ = 0.0f;
        envelope_ = 0.0f;
        state_ = State::Idle;
    }

    /// @brief Process one audio sample for transient detection.
    /// @param sample Input audio sample (absolute value taken internally)
    void process(float sample) noexcept {
        // Fast amplitude follower (~1ms attack)
        float absInput = std::abs(sample);
        if (absInput > inputEnvelope_) {
            inputEnvelope_ = absInput + inputFollowerCoeff_ * (inputEnvelope_ - absInput);
        } else {
            inputEnvelope_ = absInput + 0.9999f * (inputEnvelope_ - absInput);
        }

        // Compute delta (envelope derivative)
        float delta = inputEnvelope_ - prevEnvelope_;
        prevEnvelope_ = inputEnvelope_;

        // Check for transient
        bool detected = (inputEnvelope_ > ampThreshold_) && (delta > rateThreshold_);

        // State machine
        switch (state_) {
            case State::Idle:
                if (detected) {
                    triggerAttack();
                }
                break;

            case State::Attack:
                if (detected && envelope_ < 0.95f) {
                    // Retrigger from current level (FR-053)
                    triggerAttack();
                }
                // Linear ramp toward 1.0
                envelope_ += attackIncrement_;
                if (envelope_ >= 1.0f) {
                    envelope_ = 1.0f;
                    state_ = State::Decay;
                }
                break;

            case State::Decay:
                if (detected) {
                    // Retrigger from current level
                    triggerAttack();
                } else {
                    // Exponential decay
                    envelope_ *= decayCoeff_;
                    if (envelope_ < 0.001f) {
                        envelope_ = 0.0f;
                        state_ = State::Idle;
                    }
                }
                break;
        }
    }

    // ModulationSource interface
    [[nodiscard]] float getCurrentValue() const noexcept override {
        return envelope_;
    }

    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {0.0f, 1.0f};
    }

    // Parameter setters
    void setSensitivity(float sensitivity) noexcept {
        sensitivity_ = std::clamp(sensitivity, kMinSensitivity, kMaxSensitivity);
        updateThresholds();
    }

    void setAttackTime(float ms) noexcept {
        attackMs_ = std::clamp(ms, kMinAttackMs, kMaxAttackMs);
        updateCoefficients();
    }

    void setDecayTime(float ms) noexcept {
        decayMs_ = std::clamp(ms, kMinDecayMs, kMaxDecayMs);
        updateCoefficients();
    }

    // Parameter getters
    [[nodiscard]] float getSensitivity() const noexcept { return sensitivity_; }
    [[nodiscard]] float getAttackTime() const noexcept { return attackMs_; }
    [[nodiscard]] float getDecayTime() const noexcept { return decayMs_; }

private:
    enum class State : uint8_t {
        Idle = 0,
        Attack,
        Decay
    };

    void updateCoefficients() noexcept {
        if (sampleRate_ <= 0.0) return;

        // Attack: linear ramp from current to 1.0
        float attackSamples = attackMs_ * 0.001f * static_cast<float>(sampleRate_);
        attackIncrement_ = (attackSamples > 0.0f) ? (1.0f / attackSamples) : 1.0f;

        // Decay: exponential fall
        float decaySamples = decayMs_ * 0.001f * static_cast<float>(sampleRate_);
        // Time constant: reach ~0.001 in decaySamples
        // exp(-6.9 / decaySamples) per sample
        decayCoeff_ = std::exp(-6.9f / decaySamples);

        // Fast input follower (~1ms attack)
        float followerSamples = 0.001f * static_cast<float>(sampleRate_);
        inputFollowerCoeff_ = std::exp(-2.0f * static_cast<float>(std::numbers::pi) / followerSamples);

        updateThresholds();
    }

    void updateThresholds() noexcept {
        // FR-050: thresholds from sensitivity
        ampThreshold_ = 0.5f * (1.0f - sensitivity_);
        rateThreshold_ = 0.1f * (1.0f - sensitivity_);
    }

    void triggerAttack() noexcept {
        state_ = State::Attack;
        // Recalculate increment from current level to 1.0
        float remaining = 1.0f - envelope_;
        float attackSamples = attackMs_ * 0.001f * static_cast<float>(sampleRate_);
        attackIncrement_ = (attackSamples > 0.0f && remaining > 0.0f)
                           ? (remaining / attackSamples)
                           : remaining;
    }

    // Detection state
    float inputEnvelope_ = 0.0f;
    float prevEnvelope_ = 0.0f;
    float inputFollowerCoeff_ = 0.0f;

    // Output envelope
    float envelope_ = 0.0f;
    State state_ = State::Idle;

    // Attack ramp
    float attackIncrement_ = 0.0f;

    // Decay
    float decayCoeff_ = 0.0f;

    // Thresholds
    float ampThreshold_ = 0.25f;
    float rateThreshold_ = 0.05f;

    // Parameters
    float sensitivity_ = kDefaultSensitivity;
    float attackMs_ = kDefaultAttackMs;
    float decayMs_ = kDefaultDecayMs;
    double sampleRate_ = 44100.0;
};

}  // namespace DSP
}  // namespace Krate
