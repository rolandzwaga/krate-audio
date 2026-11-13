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


namespace Krate::DSP {

/// @brief Transient detector modulation source.
///
/// Detects transients as a rise of the signal's level against its own recent
/// level, and generates attack-decay envelopes. Supports retrigger from current
/// envelope level.
///
/// A fast peak follower (sub-millisecond attack, 50 ms release) is compared with
/// a 20 ms smoothed copy of itself. A transient is the fast level rising past the
/// smoothed one by the threshold, 21 dB at sensitivity 0 to 3 dB at 1 (12 dB at
/// the default 0.5), whatever the signal's absolute level; nothing below -60 dBFS
/// counts. The detector re-arms once the rise falls under half the threshold (in
/// dB), so a held tone fires once and every new hit fires again.
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
        fastLevel_ = 0.0f;
        slowLevel_ = 0.0f;
        armed_ = true;
        envelope_ = 0.0f;
        state_ = State::Idle;
    }

    /// @brief Process one audio sample for transient detection.
    /// @param sample Input audio sample (absolute value taken internally)
    void process(float sample) noexcept {
        // Fast peak follower, and a smoothed copy of it: the signal's recent level
        const float absInput = std::abs(sample);
        const float fastCoeff = (absInput > fastLevel_) ? inputFollowerCoeff_ : fastReleaseCoeff_;
        fastLevel_ = absInput + fastCoeff * (fastLevel_ - absInput);
        slowLevel_ = fastLevel_ + slowCoeff_ * (slowLevel_ - fastLevel_);

        // A rise past the threshold against the recent level; re-arm once it falls back
        const float reference = std::max(slowLevel_, kDetectionFloor);
        bool detected = false;
        if (armed_) {
            if (fastLevel_ > kDetectionFloor && fastLevel_ >= thresholdRatio_ * reference) {
                detected = true;
                armed_ = false;
            }
        } else if (fastLevel_ < rearmRatio_ * reference) {
            armed_ = true;
        }

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

        // Fast peak follower: ~1 ms attack (time constant 1 ms / 2 pi), 50 ms release;
        // the recent level smooths it over 20 ms
        const auto sr = static_cast<float>(sampleRate_);
        float followerSamples = 0.001f * sr;
        inputFollowerCoeff_ = std::exp(-2.0f * static_cast<float>(std::numbers::pi) / followerSamples);
        fastReleaseCoeff_ = std::exp(-1.0f / (kFastReleaseMs * 0.001f * sr));
        slowCoeff_ = std::exp(-1.0f / (kSlowMs * 0.001f * sr));

        updateThresholds();
    }

    void updateThresholds() noexcept {
        // FR-050: the rise a transient needs, 21 dB at sensitivity 0 to 3 dB at 1;
        // re-arm below half of it (in dB)
        const float thresholdDb = kMaxRiseDb - (kMaxRiseDb - kMinRiseDb) * sensitivity_;
        thresholdRatio_ = std::pow(10.0f, thresholdDb / 20.0f);
        rearmRatio_ = std::pow(10.0f, thresholdDb / 40.0f);
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

    static constexpr float kMaxRiseDb = 21.0f;          // rise needed at sensitivity 0
    static constexpr float kMinRiseDb = 3.0f;           // rise needed at sensitivity 1
    static constexpr float kDetectionFloor = 0.001f;    // -60 dBFS: nothing quieter counts
    static constexpr float kFastReleaseMs = 50.0f;
    static constexpr float kSlowMs = 20.0f;

    // Detection state
    float fastLevel_ = 0.0f;
    float slowLevel_ = 0.0f;
    bool armed_ = true;
    float inputFollowerCoeff_ = 0.0f;
    float fastReleaseCoeff_ = 0.0f;
    float slowCoeff_ = 0.0f;

    // Output envelope
    float envelope_ = 0.0f;
    State state_ = State::Idle;

    // Attack ramp
    float attackIncrement_ = 0.0f;

    // Decay
    float decayCoeff_ = 0.0f;

    // Thresholds (12 dB / 6 dB at the default sensitivity)
    float thresholdRatio_ = 3.9810717f;
    float rearmRatio_ = 1.9952623f;

    // Parameters
    float sensitivity_ = kDefaultSensitivity;
    float attackMs_ = kDefaultAttackMs;
    float decayMs_ = kDefaultDecayMs;
    double sampleRate_ = 44100.0;
};

} // namespace Krate::DSP

