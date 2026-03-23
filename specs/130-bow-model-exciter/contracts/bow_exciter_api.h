// API Contract: BowExciter
// Location: dsp/include/krate/dsp/processors/bow_exciter.h
// Layer: 2 (processors)
// Namespace: Krate::DSP
//
// This file documents the public API contract for the BowExciter class.
// It is NOT compiled -- it serves as a design reference for implementation.

#pragma once

#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/one_pole.h>

namespace Krate::DSP {

/// @brief Bow exciter implementing STK power-law friction model.
///
/// Models stick-slip friction between a bow and a resonating body.
/// Takes continuous feedback velocity from a resonator and outputs
/// excitation force. Contains internal state for bow velocity,
/// friction jitter (LFO + noise), bow hair LPF, and energy control.
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
/// - Principle III: Modern C++ (constexpr, value semantics)
/// - Principle IX: Layer 2 (depends only on Layers 0-1)
/// - Principle X: DC blocking via external resonator
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
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all state to initial values.
    void reset() noexcept;

    // ===== Note Events =====

    /// @brief Trigger bow excitation from MIDI note-on.
    /// @param velocity MIDI velocity 0.0-1.0, sets maxVelocity and targetEnergy
    void trigger(float velocity) noexcept;

    /// @brief Release bow excitation (ADSR will ramp velocity down).
    void release() noexcept;

    // ===== Per-Sample Processing =====

    /// @brief Compute one sample of excitation force.
    /// @param feedbackVelocity Current string/resonator velocity at bow point
    /// @return Excitation force to feed into resonator
    ///
    /// Per-sample flow (FR-002 through FR-010):
    /// 1. Compute bow acceleration from ADSR envelope
    /// 2. Integrate to velocity, clamp by speed ceiling
    /// 3. Compute deltaV = bowVelocity - feedbackVelocity
    /// 4. Apply rosin jitter (LFO + noise offset)
    /// 5. Evaluate bow table: reflCoeff = clamp(1/(x*x*x*x), 0.01, 0.98)
    ///    where x = |deltaV * slope + offset| + 0.75
    /// 6. Force = deltaV * reflCoeff
    /// 7. Scale by position impedance
    /// 8. Apply bow hair LPF
    /// 9. Apply energy-aware gain control
    [[nodiscard]] float process(float feedbackVelocity) noexcept;

    // ===== Parameter Setters =====

    /// @brief Set bow pressure (0.0-1.0). Maps to friction slope.
    /// @param pressure 0.0=no pressure, 1.0=maximum pressure
    void setPressure(float pressure) noexcept;

    /// @brief Set bow speed (0.0-1.0). Scales velocity ceiling.
    /// @param speed 0.0=stationary, 1.0=maximum speed
    void setSpeed(float speed) noexcept;

    /// @brief Set bow position (0.0-1.0). 0=bridge, 1=fingerboard.
    /// @param position Controls harmonic emphasis via node placement
    void setPosition(float position) noexcept;

    /// @brief Set current ADSR envelope value for acceleration computation.
    /// @param envelopeValue ADSR output (0.0-1.0), drives bow acceleration
    void setEnvelopeValue(float envelopeValue) noexcept;

    /// @brief Set resonator control energy for energy-aware gain control.
    /// @param energy Current resonator energy (from getControlEnergy())
    void setResonatorEnergy(float energy) noexcept;

    // ===== Queries =====

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

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
    float currentEnergy_{0.0f};  // EMA energy tracker: running estimate of resonator output energy
    float energyAlpha_{0.0f};    // EMA coefficient (computed from sampleRate in prepare())

    // Rosin jitter (FR-008)
    LFO rosinLfo_;
    uint32_t noiseState_{12345};
    float noiseHpState_{0.0f};
    float noiseHpCoeff_{0.0f};

    // Bow hair LPF (FR-009)
    OnePoleLP hairLpf_;

    // State
    double sampleRate_{0.0};
    bool prepared_{false};
    bool active_{false};
};

} // namespace Krate::DSP
