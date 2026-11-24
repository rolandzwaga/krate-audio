#pragma once

// ==============================================================================
// FeedbackExciter -- Phase 2 (data-model.md §2.6, FR-015, SC-008), T1.16
// ==============================================================================
// Custom per-voice energy-regulated feedback topology (research.md §3). Two
// parts with their own envelopes:
//
//   strike    = (noise * kStrikeNoiseLevel + 2-sample impulse
//                + kStrikePulseSec Hann pulse) * strikeEnv
//               strikeEnv = velocity, -60 dB after kStrikeDecaySec (fixed,
//               never gated), so the open-loop strike the voice's strike
//               normalisation probes is the strike the note plays. The
//               coherent pulse is the mallet-like part that drives the body's
//               low modes (and with them the tension glide).
//   loop      = hp(bodyFeedback) * feedbackAmount_ * energyGain * loopEnv
//               hp        = kLoopHighpassHz one-pole highpass
//               energy    = RMS follower of |hp|
//               energyGain = clamp(2 - energy / (kEnergyThreshold * v), 0, 1)
//               loopEnv   = 1 for Amount x kMaxLoopHoldSec, then -60 dB after
//                           kMinLoopDecaySec (Amount 0) .. kMaxLoopDecaySec (1)
//   out       = dcBlocker(tanhADAA(lowpass(strike + loop)))
//
// The loop holds the highpassed body near kEnergyThreshold x velocity RMS (the
// body runs strike-normalised to kBodyHeadroom, T1.16 / MB-017) and Feedback
// Amount sets how long the loop envelope keeps it fed: the audible sustain.
// Velocity sets the strike level, the regulated loop level and, through the
// lowpass cutoff, the brightness (FR-016 / SC-004).
//
// Stability guarantee (SC-008): peak ≤ 0 dBFS for ANY bodyFeedback in [-1,+1]
// and ANY velocity in [0,1]. This is enforced by:
//   (a) feedbackAmount_ ≤ kMaxFeedback
//   (b) tanhADAA hard-caps magnitude to |1| regardless of input
//   (c) a final ±1 clamp after the DC blocker
// The loop envelope decays to zero, so the sustain is always finite.
//
// NOTE: Uses Krate::DSP::SVF, OnePoleHP, TanhADAA, DCBlocker, EnvelopeFollower
//       — NOT Krate::DSP::FeedbackNetwork (block-oriented delay feedback; wrong
//       shape for per-sample single-voice feedback, see research.md §3).
// ==============================================================================

#include "dsp/float_guard.h"

#include <krate/dsp/core/pattern_freeze_types.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/noise_oscillator.h>
#include <krate/dsp/primitives/one_pole.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/tanh_adaa.h>
#include <krate/dsp/processors/envelope_follower.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace Membrum {

struct FeedbackExciter
{
    static constexpr float kMaxFeedback     = 0.85f;
    static constexpr float kFilterCutoffHz  = 3000.0f;
    // T1.16 (MB-017) loop and strike law, chosen against the strike-normalised
    // body (test_feedback_exciter_headroom.cpp): a velocity-1 Feedback hit
    // stays under the body budget, Amount 0 -> 1 lengthens the -40 dB fall
    // time and adds more than 3 dB of sustain on Membrane and Plate, the
    // longest sustain retires no later than the same pad struck by Mallet, and
    // the strike moves the body's low modes enough for the tension glide
    // ("Phase 8E: tension glide also fires on the slow (Feedback) path").
    static constexpr float kEnergyThreshold  = 0.04f;  // highpassed body RMS the loop holds at velocity 1
    static constexpr float kLoopHighpassHz   = 700.0f; // the loop leaves the body's lowest modes alone
    static constexpr float kStrikeDecaySec   = 0.15f;  // strike -60 dB time, fixed
    static constexpr float kStrikeNoiseLevel = 0.3f;   // strike noise, x velocity
    static constexpr float kStrikePulseLevel = 1.0f;   // coherent strike pulse peak, x velocity
    static constexpr float kStrikePulseSec   = 0.001f; // coherent strike pulse length (Hann)
    static constexpr float kMaxLoopHoldSec   = 0.8f;   // loop hold at Feedback Amount 1 (0 at Amount 0)
    static constexpr float kMinLoopDecaySec  = 0.15f;  // loop -60 dB time after the hold, Amount 0
    static constexpr float kMaxLoopDecaySec  = 0.6f;   // loop -60 dB time after the hold, Amount 1

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        filter_.prepare(sampleRate);
        filter_.setMode(Krate::DSP::SVFMode::Lowpass);
        filter_.setCutoff(kFilterCutoffHz);
        filter_.setResonance(0.707f);
        filter_.snapToTarget();

        loopHighpass_.prepare(sampleRate);
        loopHighpass_.setCutoff(kLoopHighpassHz);

        saturator_.setDrive(1.5f);

        dcBlocker_.prepare(sampleRate, 10.0f);

        energyFollower_.prepare(sampleRate, 512);
        energyFollower_.setMode(Krate::DSP::DetectionMode::RMS);
        energyFollower_.setAttackTime(2.0f);
        energyFollower_.setReleaseTime(50.0f);

        // Internal noise source: the strike, audible even when bodyFeedback
        // is zero (unit-test scenario and the open-loop strike probe).
        seedNoise_.prepare(sampleRate);
        seedNoise_.setColor(Krate::DSP::NoiseColor::White);
        seedNoise_.setSeed(0xFEEDu ^ voiceId);

        strikeCoeff_ = decayCoeffFor(kStrikeDecaySec);
        pulseLen_    = std::max(1, static_cast<int>(kStrikePulseSec * static_cast<float>(sampleRate)));
        reset();
    }

    void reset() noexcept
    {
        filter_.reset();
        loopHighpass_.reset();
        saturator_.reset();
        dcBlocker_.reset();
        energyFollower_.reset();
        seedNoise_.reset();
        feedbackAmount_          = 0.0f;
        strikeEnv_               = 0.0f;
        loopEnv_                 = 0.0f;
        loopHoldRemaining_       = 0;
        impulseSamplesRemaining_ = 0;
        pulsePos_                = pulseLen_;
        active_                  = false;
    }

    /// Set the Feedback Amount from a normalized [0,1] host value
    /// (plugin_ids.h kExciterFeedbackAmountId). It sets the sustain: the loop
    /// envelope holds for Amount x kMaxLoopHoldSec, then falls -60 dB in
    /// kMinLoopDecaySec (0) .. kMaxLoopDecaySec (1); it also raises the loop
    /// gain above the velocity-derived baseline (0 = velocity only,
    /// 1 = kMaxFeedback). Applied on the next trigger(). SC-008 is preserved
    /// because the effective drive stays in [0, 1] -> feedbackAmount_ ≤
    /// kMaxFeedback.
    void setFeedbackAmount(float normalized) noexcept
    {
        feedbackDrive_ = std::clamp(normalized, 0.0f, 1.0f);
    }

    void trigger(float velocity) noexcept
    {
        velocity        = std::clamp(velocity, 0.0f, 1.0f);
        const float drive = velocity + feedbackDrive_ * (1.0f - velocity);
        feedbackAmount_  = drive * kMaxFeedback;
        strikeEnv_       = velocity;
        loopEnv_         = 1.0f;
        loopHoldRemaining_ = static_cast<int>(
            feedbackDrive_ * kMaxLoopHoldSec * static_cast<float>(sampleRate_));
        loopCoeff_       = decayCoeffFor(
            kMinLoopDecaySec + feedbackDrive_ * (kMaxLoopDecaySec - kMinLoopDecaySec));
        energyThreshold_ = kEnergyThreshold * velocity;
        // FR-016 / SC-004: velocity drives spectral content — open the lowpass
        // as velocity rises so the centroid ratio clears 2.0 between v=0.23
        // and v=1.0.
        const float cutoff = 400.0f * std::pow(25.0f, velocity); // ~400→10 kHz
        filter_.setCutoff(cutoff);
        filter_.snapToTarget();

        // A 2-sample click and the coherent pulse at the start of the strike.
        impulseSamplesRemaining_ = 2;
        pulsePos_ = 0;
        active_ = velocity > 0.0f;
    }

    void release() noexcept
    {
        // No sustain stage; both envelopes decay on their own.
    }

    [[nodiscard]] float process(float bodyFeedback) noexcept
    {
        if (!active_)
            return 0.0f;

        // Sanitize non-finite body feedback BEFORE it reaches the energy
        // follower. A closed feedback loop can emit NaN/Inf under sustained
        // drive; the RMS EnvelopeFollower does not validate its input, so a
        // single bad sample would poison its smoothing state permanently (NaN
        // survives flushDenormal, and an Inf becomes Inf*0 = NaN downstream)
        // and silently disable the energy regulator for the life of the voice.
        // isNonFinite() lives in a -fno-fast-math TU because an inline check
        // would be folded away under the SDK's global -ffast-math
        // (-ffinite-math-only) -- see dsp/float_guard.h.
        if (Membrum::DSP::isNonFinite(bodyFeedback))
            bodyFeedback = 0.0f;

        // Loop: the highpassed body, regulated near energyThreshold_ (the gain
        // falls from 1 at the threshold to 0 at twice it), faded by the loop
        // envelope Feedback Amount sets.
        const float hp = loopHighpass_.process(bodyFeedback);
        const float energy = energyFollower_.processSample(hp < 0.0f ? -hp : hp);
        const float energyGain = std::clamp(
            2.0f - energy / std::max(energyThreshold_, 1.0e-6f), 0.0f, 1.0f);
        float raw = hp * feedbackAmount_ * energyGain * loopEnv_;

        // Strike: noise, a 2-sample click and a short coherent pulse under the
        // fixed strike envelope, never gated by the loop, so the note's strike
        // is the probed strike.
        raw += seedNoise_.process() * strikeEnv_ * kStrikeNoiseLevel;
        if (impulseSamplesRemaining_ > 0)
        {
            raw += strikeEnv_;
            --impulseSamplesRemaining_;
        }
        if (pulsePos_ < pulseLen_)
        {
            const float ph = static_cast<float>(pulsePos_) / static_cast<float>(pulseLen_);
            raw += kStrikePulseLevel * strikeEnv_ * 0.5f
                 * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * ph));
            ++pulsePos_;
        }
        const float filtered = filter_.process(raw);
        const float saturated = saturator_.process(filtered);
        float out = dcBlocker_.process(saturated);

        // Hard safety rail: final |out| ≤ 1.0 regardless of what the chain
        // produced (SC-008), even across sample-rate changes and under
        // extreme body feedback.
        out = std::min(out, 1.0f);
        out = std::max(out, -1.0f);

        strikeEnv_ *= strikeCoeff_;
        if (loopHoldRemaining_ > 0)
            --loopHoldRemaining_;
        else
            loopEnv_ *= loopCoeff_;
        if (strikeEnv_ < 0.001f && loopEnv_ < 0.001f)
        {
            active_ = false;
        }
        return out;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

private:
    // Per-sample multiplier that takes an envelope to -60 dB in `seconds`.
    [[nodiscard]] float decayCoeffFor(float seconds) const noexcept
    {
        return std::exp(-6.9078f / (seconds * static_cast<float>(sampleRate_)));
    }

    Krate::DSP::SVF              filter_{};
    Krate::DSP::OnePoleHP        loopHighpass_{};
    Krate::DSP::TanhADAA         saturator_{};
    Krate::DSP::DCBlocker        dcBlocker_{};
    Krate::DSP::EnvelopeFollower energyFollower_{};
    Krate::DSP::NoiseOscillator  seedNoise_{};
    double sampleRate_              = 44100.0;
    float  feedbackAmount_          = 0.0f;
    // User Feedback Amount [0,1]. Persists across reset()/retrigger (it is a
    // host parameter, not voice state); ExciterBank replays it after a
    // variant swap.
    float  feedbackDrive_           = 0.0f;
    float  strikeEnv_               = 0.0f;
    float  strikeCoeff_             = 0.0f;
    float  loopEnv_                 = 0.0f;
    float  loopCoeff_               = 0.0f;
    float  energyThreshold_         = 0.0f;
    int    loopHoldRemaining_       = 0;
    int    impulseSamplesRemaining_ = 0;
    int    pulseLen_                = 1;
    int    pulsePos_                = 1;
    bool   active_                  = false;
};

} // namespace Membrum
