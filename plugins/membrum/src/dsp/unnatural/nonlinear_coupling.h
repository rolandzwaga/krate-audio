#pragma once

// ==============================================================================
// NonlinearCoupling -- Phase 2.E.4 (data-model.md §7, unnatural_zone_contract.md)
// ==============================================================================
// Velocity-dependent, amplitude-driven nonlinear waveshaping that mimics one
// audible symptom of nonlinear plate/shell coupling -- amplitude-dependent
// spectral brightening (harder strike -> more high-frequency energy). It is a
// character knob, not a full physical model: the von Karman cubic coupling and
// the energy cascade from low to high modes (Ducceschi/Bilbao gong & cymbal
// models; Poirot 2024 coupled-filter matrix) require per-sample coupled
// nonlinear ODEs, too costly and stability-fragile for a flavour control. The
// accepted cheap, controllable approximation -- Buchla-style envelope-
// controlled waveshaping (drive a bounded shaper harder as amplitude rises ->
// more harmonics) -- is used here instead.
//
// Topology (M-3 / M-4, AUDIT-signal-path):
//   if (amount_ == 0.0f) return bodyOutput;        // exact bypass (FR-055)
//   env    = envFollower_.processSample(bodyOutput) // SUSTAINED RMS level
//   drive  = 1 + kDriveScale*velocity_*amount_*env  // amplitude-dependent
//   shaped = Sigmoid::recipSqrt(bodyOutput * drive) // odd-harmonic generation
//   return bodyOutput + amount_ * (shaped - bodyOutput)  // AM-only, continuous
//
// Why env LEVEL, not the previous dEnv (envelope delta): the old design's
// modulation was proportional to dEnv, which -> 0 in sustain, so the effect
// was a brief attack-edge wiggle that vanished while the body rang (M-4), and
// the whole signal was always run through recipSqrt whenever amount != 0,
// acting as a fixed ~-18% compressor independent of amount and discontinuous
// from the amount=0 bypass (M-3). Driving the shaper from the sustained level
// makes louder = brighter for the full ring, and adding only the (shaped -
// body) excess scaled by amount keeps the stage continuous: amount -> 0 and
// quiet passages both approach exact passthrough.
//
// Real-time safety (FR-056): one envelope-follower step + one recipSqrt; no
// allocation. Early-out when amount_ == 0 returns input unchanged (exact
// bit-identical bypass, contract item 8). Output peaks are owned downstream
// by the per-voice hardClip safety rail + the main-bus true-peak limiter
// (gain-staging design), not by this stage.
// ==============================================================================

#include <krate/dsp/core/sigmoid.h>
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

        // M-4: the follower now supplies the SUSTAINED body level that drives
        // the waveshaper (no delta state needed).
        prepared_ = true;
    }

    void reset() noexcept
    {
        envFollower_.reset();
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

        // Sustained body RMS level: drives the shaper harder when the body is
        // louder, so the brightening tracks amplitude through the whole ring
        // instead of only the attack edge (M-4).
        const float env = envFollower_.processSample(bodyOutput);

        // Amplitude-dependent waveshaper drive. velocity_ defaults to 1.0 so
        // tests that do not set it still see coupling proportional to amount_.
        // kDriveScale sets how hard a unit-velocity, full-amount, loud hit is
        // driven (drive ~ 1 + kDriveScale*env at the loudest). Kept moderate:
        // recipSqrt is a soft clipper, so an over-hot drive squares a tonal
        // body (lowers crest, sustains) -- reintroducing the very compressor
        // character M-3 removed. This value brightens audibly while leaving a
        // tonal pad's envelope/crest recognisably drum-like.
        constexpr float kDriveScale = 6.0f;
        const float drive  = 1.0f + kDriveScale * velocity_ * amount_ * env;

        // recipSqrt(x) = x / sqrt(x^2 + 1): a bounded, odd-symmetric soft
        // clipper. Driven harder it generates progressively more odd harmonics
        // -> brighter. Add only the (shaped - body) excess scaled by amount so
        // the stage is continuous in amount and approaches exact passthrough
        // for quiet passages / small amount (M-3).
        const float shaped = Krate::DSP::Sigmoid::recipSqrt(bodyOutput * drive);
        return bodyOutput + amount_ * (shaped - bodyOutput);
    }

private:
    Krate::DSP::EnvelopeFollower envFollower_;
    float  amount_       = 0.0f;
    float  velocity_     = 1.0f;  // default 1.0 so tests without setVelocity work
    double sampleRate_   = 44100.0;
    bool   prepared_     = false;
};

} // namespace Membrum
