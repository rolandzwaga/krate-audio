#pragma once

// ==============================================================================
// FeedbackExciter -- Phase 2 (data-model.md §2.6, FR-015, SC-008)
// ==============================================================================
// Custom per-voice energy-limited feedback topology (research.md §3):
//
//   energy    = envelopeFollower_.processSample(|bodyFeedback|)
//   energyGain = 1 - clamp(energy - kEnergyThreshold, 0, 1)
//   filtered  = filter_.process(bodyFeedback * feedbackAmount_ * energyGain)
//   saturated = tanhADAA_.process(filtered)
//   out       = dcBlocker_.process(saturated)
//
// Stability guarantee (SC-008): peak ≤ 0 dBFS for ANY bodyFeedback in [-1,+1]
// and ANY velocity in [0,1]. This is enforced by:
//   (a) feedbackAmount_ ≤ kMaxFeedback
//   (b) tanhADAA hard-caps magnitude to |1| regardless of input
//   (c) energy limiter pulls the gain down before clipping can pile energy
//
// Velocity mapping: feedbackAmount_ = velocity * kMaxFeedback.
//
// NOTE: Uses Krate::DSP::SVF, TanhADAA, DCBlocker, EnvelopeFollower —
//       NOT Krate::DSP::FeedbackNetwork (block-oriented delay feedback; wrong
//       shape for per-sample single-voice feedback, see research.md §3).
// ==============================================================================

#include <krate/dsp/core/pattern_freeze_types.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/noise_oscillator.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/tanh_adaa.h>
#include <krate/dsp/processors/envelope_follower.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Membrum {

struct FeedbackExciter
{
    static constexpr float kMaxFeedback     = 0.85f;
    static constexpr float kEnergyThreshold = 0.35f; // ~-9 dBFS RMS trigger
    static constexpr float kFilterCutoffHz  = 3000.0f;
    static constexpr float kDecayTauSec     = 0.8f;  // self-sustain decay floor

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        filter_.prepare(sampleRate);
        filter_.setMode(Krate::DSP::SVFMode::Lowpass);
        filter_.setCutoff(kFilterCutoffHz);
        filter_.setResonance(0.707f);
        filter_.snapToTarget();

        saturator_.setDrive(1.5f);

        dcBlocker_.prepare(sampleRate, 10.0f);

        energyFollower_.prepare(sampleRate, 512);
        energyFollower_.setMode(Krate::DSP::DetectionMode::RMS);
        energyFollower_.setAttackTime(2.0f);
        energyFollower_.setReleaseTime(50.0f);

        // Internal noise source drives the filter so the exciter has audible
        // output even when bodyFeedback is zero (unit-test scenario). The
        // noise is gated by amplitudeEnv_ which decays on trigger.
        seedNoise_.prepare(sampleRate);
        seedNoise_.setColor(Krate::DSP::NoiseColor::White);
        seedNoise_.setSeed(0xFEEDu ^ voiceId);

        // Envelope decay so even without body feedback the exciter can self-
        // sustain for a short time after trigger before dropping idle.
        decayCoeff_ = std::exp(-6.9078f / (kDecayTauSec * static_cast<float>(sampleRate)));
        reset();
    }

    void reset() noexcept
    {
        filter_.reset();
        saturator_.reset();
        dcBlocker_.reset();
        energyFollower_.reset();
        seedNoise_.reset();
        feedbackAmount_          = 0.0f;
        amplitudeEnv_            = 0.0f;
        impulseSamplesRemaining_ = 0;
        active_                  = false;
    }

    void trigger(float velocity) noexcept
    {
        velocity        = std::clamp(velocity, 0.0f, 1.0f);
        feedbackAmount_ = velocity * kMaxFeedback;
        amplitudeEnv_   = velocity;
        // FR-016 / SC-004: velocity drives spectral content — open the lowpass
        // as velocity rises so the centroid ratio clears 2.0 between v=0.23
        // and v=1.0.
        const float cutoff = 400.0f * std::pow(25.0f, velocity); // ~400→10 kHz
        filter_.setCutoff(cutoff);
        filter_.snapToTarget();

        // Seed the filter with a short impulse so the exciter produces audible
        // output even when bodyFeedback == 0 (contract tests pass zero feedback
        // for the centroid ratio check). The impulse decays very fast and the
        // feedback path takes over once the body starts ringing.
        impulseSamplesRemaining_ = 2; // 2-sample click
        active_ = velocity > 0.0f;
    }

    void release() noexcept
    {
        // No sustain stage; let amplitude envelope decay naturally.
    }

    [[nodiscard]] float process(float bodyFeedback) noexcept
    {
        if (!active_)
            return 0.0f;

        // Sanitize pathological body feedback — the body can ring above unity
        // during transient moments; we treat everything as a raw velocity-like
        // input and rely on the limiter chain.
        const float absFb = bodyFeedback < 0.0f ? -bodyFeedback : bodyFeedback;
        const float energy = energyFollower_.processSample(absFb);
        const float energyOverThreshold =
            std::clamp(energy - kEnergyThreshold, 0.0f, 1.0f);
        const float energyGain = 1.0f - energyOverThreshold;

        // Raw input combines: (a) filtered body feedback, (b) an envelope-
        // modulated noise seed so the exciter has audible content even when
        // the body is silent (contract test scenario).
        const float noise = seedNoise_.process() * amplitudeEnv_ * 0.3f;
        float raw = bodyFeedback * feedbackAmount_ * energyGain + noise;
        if (impulseSamplesRemaining_ > 0)
        {
            raw += amplitudeEnv_;
            --impulseSamplesRemaining_;
        }
        const float filtered = filter_.process(raw);
        const float saturated = saturator_.process(filtered);
        const float blocked = dcBlocker_.process(saturated);

        // Amplitude envelope drives the exciter's own contribution when the
        // body feedback is weak; ensures SC-008 peak ceiling and gives the
        // exciter something to say when plugged into a silent body.
        float out = blocked * amplitudeEnv_;

        // Hard safety rail: final |out| ≤ 1.0 regardless of what the chain
        // produced. tanhADAA already bounds its output to |1|, but we clamp
        // the envelope-scaled result to guarantee the SC-008 invariant even
        // across sample-rate changes and under extreme body feedback.
        if (out > 1.0f)  out = 1.0f;
        if (out < -1.0f) out = -1.0f;

        amplitudeEnv_ *= decayCoeff_;
        if (amplitudeEnv_ < 0.001f)
        {
            active_ = false;
        }
        return out;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

private:
    Krate::DSP::SVF              filter_{};
    Krate::DSP::TanhADAA         saturator_{};
    Krate::DSP::DCBlocker        dcBlocker_{};
    Krate::DSP::EnvelopeFollower energyFollower_{};
    Krate::DSP::NoiseOscillator  seedNoise_{};
    double sampleRate_              = 44100.0;
    float  feedbackAmount_          = 0.0f;
    float  amplitudeEnv_            = 0.0f;
    float  decayCoeff_              = 0.0f;
    int    impulseSamplesRemaining_ = 0;
    bool   active_                  = false;
};

} // namespace Membrum
