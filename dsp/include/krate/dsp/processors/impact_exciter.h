// ==============================================================================
// Layer 2: DSP Processor - ImpactExciter
// ==============================================================================
// Physical-modelling impact exciter for struck-object sound design.
//
// Generates a short percussive excitation burst from a MIDI note-on event,
// consisting of an asymmetric deterministic pulse + shaped noise, filtered
// through an SVF lowpass and strike position comb filter.
//
// Spec: 128-impact-exciter
// FRs: FR-001 through FR-024, FR-033, FR-034
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (RAII, value semantics)
// - Principle IX: Layer 2 (depends on Layer 0: XorShift32, Layer 1: SVF, DelayLine)
// ==============================================================================

#pragma once

#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/xorshift32.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/svf.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

class ImpactExciter {
public:
    ImpactExciter() noexcept = default;
    ~ImpactExciter() = default;

    // Non-copyable, movable
    ImpactExciter(const ImpactExciter&) = delete;
    ImpactExciter& operator=(const ImpactExciter&) = delete;
    ImpactExciter(ImpactExciter&&) noexcept = default;
    ImpactExciter& operator=(ImpactExciter&&) noexcept = default;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Allocate internal buffers for the given sample rate.
    /// Must be called before trigger() or process().
    /// @param sampleRate Sample rate in Hz
    /// @param voiceId Unique voice identifier for RNG seeding
    void prepare(double sampleRate, uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;

        // Seed per-voice RNG
        rng_.seed(voiceId);

        // Prepare SVF in lowpass mode
        svf_.prepare(sampleRate);
        svf_.setMode(SVFMode::Lowpass);
        svf_.setCutoff(1000.0f);
        svf_.snapToTarget();

        // Prepare comb filter delay line (55ms max at given sample rate)
        combDelay_.prepare(sampleRate, 0.055f);

        // Compute energy decay coefficient: decay = exp(-1 / (tau * sampleRate))
        // tau = 5ms = 0.005s
        constexpr float kEnergyTau = 0.005f;
        energyDecay_ = std::exp(-1.0f / (kEnergyTau * static_cast<float>(sampleRate)));

        // Compute energy threshold: ~4x single-strike energy at default params
        // amplitude_max = pow(0.5, 0.6) ~ 0.6598
        // T_samples ~ 0.5ms + 14.5ms * pow(0.3, 0.4) at 44.1kHz
        // Approximate: threshold = 4 * amp^2 * T_samples / 2
        float ampDefault = std::pow(0.5f, 0.6f);
        float tDefault = 0.0005f + 0.0145f * std::pow(0.3f, 0.4f); // seconds
        float tSamples = tDefault * static_cast<float>(sampleRate);
        energyThreshold_ = 4.0f * ampDefault * ampDefault * tSamples / 2.0f;

        // Compute attack ramp samples: 0.3ms
        attackRampSamples_ = static_cast<int>(0.0003f * static_cast<float>(sampleRate));
        if (attackRampSamples_ < 1)
            attackRampSamples_ = 1;

        prepared_ = true;
    }

    /// Clear all internal state (pulse, noise, SVF, comb).
    void reset() noexcept
    {
        pulseActive_ = false;
        bounceActive_ = false;
        pulseSampleCounter_ = 0;
        pulseSamplesTotal_ = 0;
        bounceSampleCounter_ = 0;
        bounceSamplesTotal_ = 0;
        bounceDelayCounter_ = 0;
        energy_ = 0.0f;
        pinkState_ = 0.0f;
        attackRampCounter_ = 0;

        svf_.reset();
        combDelay_.reset();
    }

    // =========================================================================
    // Triggering
    // =========================================================================

    /// Trigger a new impact excitation burst.
    /// Called on note-on. Does NOT reset resonator state.
    void trigger(float velocity, float hardness, float mass,
                 float brightness, float position, float f0) noexcept
    {
        if (!prepared_)
            return;

        // FR-018: Effective hardness with velocity cross-modulation
        float effectiveHardness = std::clamp(hardness + velocity * 0.1f, 0.0f, 1.0f);

        // FR-004: Peakiness gamma = 1.0 + 3.0 * effectiveHardness
        gamma_ = 1.0f + 3.0f * effectiveHardness;

        // FR-003: Asymmetry skew = 0.3 * effectiveHardness
        skew_ = 0.3f * effectiveHardness;

        // FR-006: Amplitude from velocity (nonlinear power curve)
        amplitude_ = std::pow(velocity, 0.6f);

        // FR-005: Pulse duration from mass (Hertzian scaling)
        constexpr float kTMin = 0.0005f; // 0.5ms
        constexpr float kTMax = 0.015f;  // 15ms
        float T = kTMin + (kTMax - kTMin) * std::pow(mass, 0.4f);

        // FR-020: Velocity duration shortening
        // Clamp (1-v) to avoid pow(0, 0.2) = 0 at full velocity
        float velDurationFactor = std::pow(std::max(1.0f - velocity, 0.01f), 0.2f);
        T *= velDurationFactor;

        // FR-014: Micro-variation on gamma and T
        gamma_ *= (1.0f + rng_.nextFloatSigned() * 0.02f);
        T *= (1.0f + rng_.nextFloatSigned() * 0.05f);

        // Convert T to samples
        pulseSamplesTotal_ = std::max(1, static_cast<int>(T * static_cast<float>(sampleRate_)));
        pulseSampleCounter_ = 0;
        pulseActive_ = true;

        // FR-033: Attack ramp resets from zero on every trigger
        attackRampCounter_ = 0;

        // Store effective hardness for noise computation
        effectiveHardness_ = effectiveHardness;

        // FR-011: Noise level from hardness
        noiseLevel_ = 0.25f + (0.08f - 0.25f) * effectiveHardness; // lerp(0.25, 0.08, h)

        // Reset pinking state for clean trigger
        pinkState_ = 0.0f;

        // -- SVF cutoff --
        // FR-015: Exponential mapping from hardness: 500 Hz at 0.0, 12000 Hz at 1.0
        // Using: cutoff = 500 * (12000/500)^hardness = 500 * 24^hardness
        float baseCutoff = 500.0f * std::pow(24.0f, effectiveHardness);

        // FR-016: Brightness trim offset (+-12 semitones = +-1 octave)
        float effectiveCutoff = baseCutoff * std::exp2(brightness);

        // FR-019: Velocity exponential modulation
        effectiveCutoff *= std::exp2(velocity * 1.5f);

        // Clamp to valid range
        float nyquist = static_cast<float>(sampleRate_) * 0.495f;
        effectiveCutoff = std::clamp(effectiveCutoff, 20.0f, nyquist);

        svf_.setCutoff(effectiveCutoff);
        svf_.snapToTarget();

        // -- Strike position comb filter --
        // FR-022: combDelay = floor(position * sampleRate / f0)
        if (f0 > 0.0f && position > 0.0f) {
            float periodSamples = static_cast<float>(sampleRate_) / f0;
            combDelaySamples_ = static_cast<int>(std::floor(position * periodSamples));
            // Clamp to delay line maximum
            int maxDelay = static_cast<int>(0.055f * static_cast<float>(sampleRate_)) - 1;
            combDelaySamples_ = std::clamp(combDelaySamples_, 0, maxDelay);
        } else {
            combDelaySamples_ = 0;
        }
        combWet_ = 0.7f; // FR-023: 70% wet blend

        // -- Micro-bounce (FR-007, FR-008) --
        if (effectiveHardness > 0.6f) {
            // FR-007: Bounce delay 0.5-2ms (shorter for harder)
            float bounceDelayMs = 2.0f - 1.5f * (effectiveHardness - 0.6f) / 0.4f;
            // FR-008: Randomize delay
            bounceDelayMs *= (1.0f + rng_.nextFloatSigned() * 0.15f);
            bounceDelay_ = std::max(1, static_cast<int>(bounceDelayMs * 0.001f * static_cast<float>(sampleRate_)));

            // FR-007: Bounce amplitude 10-20% of primary (less for harder)
            float bounceAmpFraction = 0.2f - 0.1f * (effectiveHardness - 0.6f) / 0.4f;
            // FR-008: Randomize amplitude
            bounceAmpFraction *= (1.0f + rng_.nextFloatSigned() * 0.10f);
            bounceAmplitude_ = amplitude_ * bounceAmpFraction;

            // Bounce duration same as primary (but shorter perceptually)
            bounceSamplesTotal_ = pulseSamplesTotal_ / 2;
            if (bounceSamplesTotal_ < 1)
                bounceSamplesTotal_ = 1;
            bounceGamma_ = gamma_;

            bounceDelayCounter_ = bounceDelay_;
            bounceSampleCounter_ = 0;
            bounceActive_ = true;
        } else {
            bounceActive_ = false;
        }
    }

    // =========================================================================
    // Processing (FR-001)
    // =========================================================================

    /// Generate one sample of excitation signal.
    [[nodiscard]] float process() noexcept
    {
        if (!pulseActive_ && !bounceActive_)
            return 0.0f;

        float output = 0.0f;

        // -- Primary pulse --
        float pulseEnvelope = 0.0f;
        if (pulseActive_) {
            float t = static_cast<float>(pulseSampleCounter_) / static_cast<float>(pulseSamplesTotal_);

            // FR-003: Skewed raised half-sine
            float skewedX = std::pow(t, 1.0f - skew_);
            float pulseSample = amplitude_ * std::pow(std::sin(kPi * skewedX), gamma_);
            pulseEnvelope = std::pow(std::sin(kPi * skewedX), gamma_);

            // FR-009, FR-010, FR-011: Noise component follows pulse envelope
            float white = rng_.nextFloatSigned();
            // One-pole pinking: pink = white - 0.9 * prev
            float pink = white - 0.9f * pinkState_;
            pinkState_ = pink;

            float noiseComponent = pink * pulseEnvelope * noiseLevel_;

            // FR-002: Combined pulse + noise
            output = pulseSample + noiseComponent;

            // Advance counter
            ++pulseSampleCounter_;
            if (pulseSampleCounter_ >= pulseSamplesTotal_)
                pulseActive_ = false;
        }

        // -- Micro-bounce --
        if (bounceActive_) {
            if (bounceDelayCounter_ > 0) {
                --bounceDelayCounter_;
            } else {
                float bt = static_cast<float>(bounceSampleCounter_) / static_cast<float>(bounceSamplesTotal_);
                float bounceSkewedX = std::pow(bt, 1.0f - skew_);
                float bounceSample = bounceAmplitude_ * std::pow(std::sin(kPi * bounceSkewedX), bounceGamma_);
                output += bounceSample;

                ++bounceSampleCounter_;
                if (bounceSampleCounter_ >= bounceSamplesTotal_)
                    bounceActive_ = false;
            }
        }

        // FR-033: Attack ramp (applied every trigger)
        if (attackRampCounter_ < attackRampSamples_) {
            float rampGain = static_cast<float>(attackRampCounter_) / static_cast<float>(attackRampSamples_);
            output *= rampGain;
            ++attackRampCounter_;
        }

        // FR-013, FR-015: SVF lowpass filter
        output = svf_.process(output);

        // FR-022, FR-023: Strike position comb filter
        if (combDelaySamples_ > 0) {
            combDelay_.write(output);
            float delayed = combDelay_.read(combDelaySamples_);
            float combOut = output - delayed; // H(z) = 1 - z^(-D)
            // FR-023: 70/30 blend
            output = output + (combOut - output) * combWet_; // lerp(dry, combOut, wet)
        }

        // FR-034: Energy capping
        energy_ = energy_ * energyDecay_ + output * output;
        if (energy_ > energyThreshold_ && energyThreshold_ > 0.0f) {
            float gain = energyThreshold_ / energy_;
            output *= gain;
        }

        return output;
    }

    /// Generate a block of excitation samples.
    /// Convenience wrapper that loops over process().
    void processBlock(float* output, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    // =========================================================================
    // Queries
    // =========================================================================

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] bool isActive() const noexcept { return pulseActive_ || bounceActive_; }

private:
    // -- Configuration --
    double sampleRate_ = 0.0;
    bool prepared_ = false;

    // -- Pulse state --
    int pulseSamplesTotal_ = 0;
    int pulseSampleCounter_ = 0;
    bool pulseActive_ = false;

    // -- Pulse shape parameters --
    float gamma_ = 1.0f;
    float skew_ = 0.0f;
    float amplitude_ = 0.0f;
    float effectiveHardness_ = 0.5f;

    // -- Micro-bounce state --
    bool bounceActive_ = false;
    int bounceSamplesTotal_ = 0;
    int bounceSampleCounter_ = 0;
    int bounceDelay_ = 0;
    int bounceDelayCounter_ = 0;
    float bounceAmplitude_ = 0.0f;
    float bounceGamma_ = 1.0f;

    // -- Noise state --
    XorShift32 rng_;
    float pinkState_ = 0.0f;
    float noiseLevel_ = 0.15f;

    // -- SVF filter --
    SVF svf_;

    // -- Strike position comb filter --
    DelayLine combDelay_;
    int combDelaySamples_ = 0;
    float combWet_ = 0.7f;

    // -- Energy capping (FR-034) --
    float energy_ = 0.0f;
    float energyDecay_ = 0.0f;
    float energyThreshold_ = 0.0f;

    // -- Attack ramp (FR-033) --
    int attackRampSamples_ = 1;
    int attackRampCounter_ = 0;
};

} // namespace DSP
} // namespace Krate
