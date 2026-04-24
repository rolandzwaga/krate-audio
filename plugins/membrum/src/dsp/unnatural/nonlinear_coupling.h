#pragma once

// ==============================================================================
// NonlinearCoupling -- Phase 2.E.4 (data-model.md §7, unnatural_zone_contract.md)
// ==============================================================================
// Velocity-dependent envelope-driven amplitude modulation that mimics one
// audible symptom of nonlinear plate/shell coupling -- amplitude-dependent
// spectral brightening -- via bilinear AM. This is NOT a von Karman cubic
// coupling: the true nonlinearity is proportional to w^3 (displacement cubed)
// whereas this module multiplies the body output by (1 + vel*amt*dEnv). It's
// a character knob, not a physical model. See research.md Section 7.
// Topology:
//   if (amount_ == 0.0f) return bodyOutput;   // early-out bypass (FR-055)
//   env       = envFollower_.processSample(bodyOutput)
//   dEnv      = env - previousEnv_
//   modulated = bodyOutput * (1.0 + velocity_*amount_ * dEnv)
//   return energyLimiter_.process(modulated)  // TanhADAA soft-clip
//
// SC-008 guarantee: TanhADAA ensures |output| <= 1.0 for any body output,
// any velocity, any amount.
//
// Real-time safety (FR-056): pre-allocated EnvelopeFollower + TanhADAA. No
// allocation. Early-out when amount_ == 0 returns input unchanged (exact
// bit-identical bypass, contract item 8).
// ==============================================================================

#include <krate/dsp/primitives/tanh_adaa.h>
#include <krate/dsp/processors/envelope_follower.h>

namespace Membrum {

class NonlinearCoupling
{
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;

        // Envelope follower for body energy tracking. Fast attack (~5 ms),
        // slightly slower release (~50 ms) so the dEnv term responds quickly
        // to transient onsets but decays smoothly during sustain.
        envFollower_.prepare(sampleRate, 1);
        envFollower_.setMode(Krate::DSP::DetectionMode::RMS);
        envFollower_.setAttackTime(5.0f);
        envFollower_.setReleaseTime(50.0f);
        envFollower_.reset();

        // TanhADAA energy limiter. Drive = 1.0 keeps the transfer linear
        // near zero and only soft-clips as inputs approach +/-1. We do not
        // re-prepare TanhADAA (no prepare API); it is stateless beyond
        // the previous-sample memory.
        energyLimiter_.reset();
        energyLimiter_.setDrive(1.0f);

        previousEnv_ = 0.0f;
        prepared_    = true;
    }

    void reset() noexcept
    {
        envFollower_.reset();
        energyLimiter_.reset();
        previousEnv_ = 0.0f;
    }

    void setAmount(float amount) noexcept { amount_ = amount; }
    void setVelocity(float velocity) noexcept { velocity_ = velocity; }

    [[nodiscard]] float getAmount() const noexcept { return amount_; }

    /// Process a single sample. When amount_ == 0, returns bodyOutput
    /// unchanged with no envelope follower update (exact bit-identical
    /// bypass, FR-055/contract item 8).
    [[nodiscard]] float processSample(float bodyOutput) noexcept
    {
        if (amount_ == 0.0f)
            return bodyOutput;

        // Track body output RMS energy.
        const float env = envFollower_.processSample(bodyOutput);

        // Delta envelope: rising edge contributes positively, decay contributes
        // negatively. This produces the time-varying spectral centroid
        // character required by US6-4 (contract item 6).
        const float dEnv = env - previousEnv_;
        previousEnv_     = env;

        // couplingStrength is a scaled velocity * amount product. velocity_
        // defaults to 1.0 so tests that do not explicitly set it still see
        // coupling proportional to amount_. kCouplingScale is a fixed gain
        // that prevents the (1 + couplingStrength*dEnv) factor from going
        // negative (which would invert phase); TanhADAA catches any
        // overshoot regardless.
        constexpr float kCouplingScale = 8.0f;
        const float couplingStrength   = velocity_ * amount_ * kCouplingScale;

        const float modulated = bodyOutput * (1.0f + couplingStrength * dEnv);

        // Energy limiter: TanhADAA guarantees |output| <= 1.0 for any input
        // (SC-008). This is the mandatory peak-safety layer.
        return energyLimiter_.process(modulated);
    }

private:
    Krate::DSP::EnvelopeFollower envFollower_;
    Krate::DSP::TanhADAA          energyLimiter_;
    float  amount_       = 0.0f;
    float  velocity_     = 1.0f;  // default 1.0 so tests without setVelocity work
    float  previousEnv_  = 0.0f;
    double sampleRate_   = 44100.0;
    bool   prepared_     = false;
};

} // namespace Membrum
