// ==============================================================================
// Layer 2: DSP Processors
// bow_exciter.h - Bow Model Exciter with STK Power-Law Friction
// ==============================================================================
// Spec 130 - Bow Model Exciter
//
// Models stick-slip friction between a bow and a resonating body.
// Takes continuous feedback velocity from a resonator and outputs
// excitation force. Contains internal state for bow velocity,
// friction jitter (LFO + noise), bow hair LPF, and energy control.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (constexpr, value semantics)
// - Principle IX: Layer 2 (depends only on Layers 0-1)
// - Principle X: DC blocking via external resonator
//
// Per-sample flow (R5 from research.md):
//  1. Compute bow acceleration from ADSR envelope
//  2. Integrate to velocity, clamp by speed ceiling
//  3. Compute deltaV = bowVelocity - feedbackVelocity
//  4. Apply rosin jitter (LFO + noise offset)
//  5. Evaluate bow table: reflCoeff = clamp(1/(x*x*x*x), 0.01, 0.98)
//     where x = |deltaV * slope + offset| + 0.75
//  6. Force = deltaV * reflCoeff
//  7. Scale by position impedance
//  8. Apply bow hair LPF
//  9. Apply energy-aware gain control
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/one_pole.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

/// @brief Bow exciter implementing STK power-law friction model.
///
/// @par Usage Example
/// @code
/// BowExciter bow;
/// bow.prepare(44100.0);
/// bow.setPressure(0.3f);
/// bow.setSpeed(0.5f);
/// bow.setPosition(0.13f);
/// bow.trigger(0.8f); // MIDI velocity
///
/// // Per-sample in audio loop:
/// float fbVel = resonator->getFeedbackVelocity();
/// bow.setEnvelopeValue(adsr.process());
/// float excitation = bow.process(fbVel);
/// float output = resonator->process(excitation);
/// @endcode
class BowExciter {
public:
    // ===== Constants =====
    static constexpr float kDefaultPressure = 0.3f;
    static constexpr float kDefaultSpeed = 0.5f;
    static constexpr float kDefaultPosition = 0.13f;
    static constexpr float kHairLpfCutoff = 8000.0f;   // Hz (FR-009)
    static constexpr float kRosinLfoRate = 0.7f;        // Hz (FR-008)
    static constexpr float kRosinLfoDepth = 0.003f;     // (FR-008)
    static constexpr float kRosinNoiseDepth = 0.001f;   // (FR-008)
    static constexpr float kRosinNoiseCutoff = 200.0f;  // Hz highpass (FR-008)

    // Maximum acceleration constant (tuned for musical response)
    static constexpr float kMaxAcceleration = 50.0f;

    // ===== Lifecycle =====
    BowExciter() noexcept = default;
    ~BowExciter() noexcept = default;

    // Non-copyable, movable
    BowExciter(const BowExciter&) = delete;
    BowExciter& operator=(const BowExciter&) = delete;
    BowExciter(BowExciter&&) noexcept = default;
    BowExciter& operator=(BowExciter&&) noexcept = default;

    // ===== Setup =====

    /// @brief Initialize for given sample rate.
    /// @param sampleRate Host sample rate (e.g. 44100.0, 48000.0, 96000.0)
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        invSampleRate_ = 1.0f / static_cast<float>(sampleRate);

        // Hair LPF at 8 kHz (FR-009)
        hairLpf_.prepare(sampleRate);
        hairLpf_.setCutoff(kHairLpfCutoff);

        // Rosin LFO at 0.7 Hz (FR-008)
        rosinLfo_.prepare(sampleRate);
        rosinLfo_.setFrequency(kRosinLfoRate);
        rosinLfo_.setWaveform(Waveform::Sine);

        // Noise highpass coefficient: simple one-pole HP at ~200 Hz
        // HP coeff = exp(-2 * pi * fc / sr)
        noiseHpCoeff_ = std::exp(-2.0f * 3.14159265f * kRosinNoiseCutoff
                                 / static_cast<float>(sampleRate));

        // Energy EMA alpha: ~10ms time constant
        float tauSamples = static_cast<float>(sampleRate) * 0.01f;
        energyAlpha_ = 1.0f / (1.0f + tauSamples);

        prepared_ = true;
    }

    /// @brief Reset all state to initial values.
    void reset() noexcept
    {
        pressure_ = kDefaultPressure;
        speed_ = kDefaultSpeed;
        position_ = kDefaultPosition;
        bowVelocity_ = 0.0f;
        maxVelocity_ = 0.0f;
        envelopeValue_ = 0.0f;
        resonatorEnergy_ = 0.0f;
        targetEnergy_ = 0.0f;
        currentEnergy_ = 0.0f;
        noiseState_ = 12345;
        noiseHpState_ = 0.0f;
        active_ = false;

        hairLpf_.reset();
        rosinLfo_.reset();
    }

    // ===== Note Events =====

    /// @brief Trigger bow excitation from MIDI note-on.
    /// @param velocity MIDI velocity 0.0-1.0, sets maxVelocity and targetEnergy
    void trigger(float velocity) noexcept
    {
        maxVelocity_ = velocity;
        targetEnergy_ = velocity * speed_;
        bowVelocity_ = 0.0f;
        currentEnergy_ = 0.0f;
        rosinLfo_.retrigger();
        active_ = true;
    }

    /// @brief Release bow excitation (ADSR will ramp velocity down).
    void release() noexcept
    {
        // The ADSR envelope will drive velocity to zero over its release phase.
        // We don't instantly deactivate - the envelope controls the ramp-down.
    }

    // ===== Per-Sample Processing =====

    /// @brief Compute one sample of excitation force.
    /// @param feedbackVelocity Current string/resonator velocity at bow point
    /// @return Excitation force to feed into resonator
    [[nodiscard]] float process(float feedbackVelocity) noexcept
    {
        if (!active_) return 0.0f;

        // Step 1: Compute bow acceleration from ADSR envelope (FR-004)
        float acceleration = envelopeValue_ * kMaxAcceleration;

        // Step 2: Integrate to velocity, clamp by speed ceiling (FR-005)
        bowVelocity_ += acceleration * invSampleRate_;
        float velocityCeiling = maxVelocity_ * speed_;
        bowVelocity_ = std::clamp(bowVelocity_, 0.0f, velocityCeiling);

        // Step 3: Compute deltaV (FR-006)
        float deltaV = bowVelocity_ - feedbackVelocity;

        // Step 4: Rosin jitter - LFO + highpassed noise (FR-008)
        float lfoJitter = rosinLfo_.process() * kRosinLfoDepth;

        // LCG noise generator
        noiseState_ = noiseState_ * 1664525u + 1013904223u;
        float rawNoise = static_cast<float>(
            static_cast<int32_t>(noiseState_)) / 2147483648.0f;

        // Simple one-pole highpass: y = x - lpf(x)
        noiseHpState_ = noiseHpCoeff_ * noiseHpState_
                        + (1.0f - noiseHpCoeff_) * rawNoise;
        float hpNoise = rawNoise - noiseHpState_;
        float noiseJitter = hpNoise * kRosinNoiseDepth;

        float jitter = lfoJitter + noiseJitter;

        // Step 5: Evaluate bow table (FR-002, FR-003)
        // slope = clamp(5.0 - 4.0 * pressure, 1.0, 10.0)
        float slope = std::clamp(5.0f - 4.0f * pressure_, 1.0f, 10.0f);
        float offset = 0.0f;  // Base offset, jitter added below
        float x = std::fabs(deltaV * slope + (offset + jitter)) + 0.75f;
        float x2 = x * x;
        float x4 = x2 * x2;
        float reflectionCoeff = std::clamp(1.0f / x4, 0.01f, 0.98f);

        // Step 6: Excitation force = deltaV * reflectionCoeff (FR-006)
        float force = deltaV * reflectionCoeff;

        // Step 7: Position impedance scaling (FR-007)
        float beta = position_;
        float betaProduct = beta * (1.0f - beta) * 4.0f;
        float positionImpedance = 1.0f / std::max(betaProduct, 0.1f);
        force *= positionImpedance;

        // Step 8: Bow hair LPF at 8 kHz (FR-009)
        force = hairLpf_.process(force);

        // Step 9: Energy-aware gain control (FR-010)
        if (targetEnergy_ > 0.0f) {
            // Update EMA energy tracker
            currentEnergy_ += energyAlpha_ * (resonatorEnergy_ - currentEnergy_);

            float energyRatio = currentEnergy_ / targetEnergy_;
            if (energyRatio > 1.0f) {
                float energyGain = 1.0f / (1.0f + (energyRatio - 1.0f) * 2.0f);
                force *= energyGain;
            }
        }

        return force;
    }

    // ===== Parameter Setters =====

    /// @brief Set bow pressure (0.0-1.0). Maps to friction slope.
    void setPressure(float pressure) noexcept
    {
        pressure_ = std::clamp(pressure, 0.0f, 1.0f);
    }

    /// @brief Set bow speed (0.0-1.0). Scales velocity ceiling.
    void setSpeed(float speed) noexcept
    {
        speed_ = std::clamp(speed, 0.0f, 1.0f);
    }

    /// @brief Set bow position (0.0-1.0). 0=bridge, 1=fingerboard.
    void setPosition(float position) noexcept
    {
        position_ = std::clamp(position, 0.0f, 1.0f);
    }

    /// @brief Set current ADSR envelope value for acceleration computation.
    void setEnvelopeValue(float envelopeValue) noexcept
    {
        envelopeValue_ = envelopeValue;
    }

    /// @brief Set resonator control energy for energy-aware gain control.
    void setResonatorEnergy(float energy) noexcept
    {
        resonatorEnergy_ = energy;
    }

    // ===== Queries =====

    [[nodiscard]] bool isActive() const noexcept { return active_; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    // Parameters
    float pressure_{kDefaultPressure};
    float speed_{kDefaultSpeed};
    float position_{kDefaultPosition};

    // Bow state
    float bowVelocity_{0.0f};
    float maxVelocity_{0.0f};
    float envelopeValue_{0.0f};
    float resonatorEnergy_{0.0f};

    // Energy control (FR-010)
    float targetEnergy_{0.0f};
    float currentEnergy_{0.0f};
    float energyAlpha_{0.0f};

    // Rosin jitter (FR-008)
    LFO rosinLfo_;
    uint32_t noiseState_{12345};
    float noiseHpState_{0.0f};
    float noiseHpCoeff_{0.0f};

    // Bow hair LPF (FR-009)
    OnePoleLP hairLpf_;

    // State
    double sampleRate_{0.0};
    float invSampleRate_{0.0f};
    bool prepared_{false};
    bool active_{false};
};

} // namespace DSP
} // namespace Krate
