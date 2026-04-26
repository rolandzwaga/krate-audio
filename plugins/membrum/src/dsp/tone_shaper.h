#pragma once

// ==============================================================================
// ToneShaper -- Phase 2 post-body chain (data-model.md §6)
// ==============================================================================
// Implements the per-voice post-body chain:
//
//   body_output
//     -> Waveshaper (Drive)
//     -> Wavefolder
//     -> DCBlocker
//     -> SVF Filter (modulated by ADSR filter envelope)
//     -> output (passed to amp ADSR and Level in DrumVoice)
//
// NOTE: Per research.md §8 (Buchla west-coast flow), the chain order is
// Drive -> Wavefolder -> DCBlocker -> SVF Filter. This intentionally deviates
// from the literal wording of FR-040; the filter smooths aliasing residues
// from the wavefolder harmonics (west-coast flow). The DC Blocker removes
// asymmetric saturation offset before the filter to prevent the filter
// envelope from being biased.
//
// The Pitch Envelope is a CONTROL-PLANE modulator (not an audio-rate stage).
// It runs in parallel and its output (Hz) is fed to the body's fundamental
// frequency update (ModalResonatorBank::updateModes / WaveguideString::setFrequency).
//
// Phase 1 regression (FR-045): when all stages are at their bypass values
// (drive=0, fold=0, filterCutoff>=20000, filterEnvAmount=0, pitchEnvTimeMs=0),
// processSample(x) must return x within -120 dBFS RMS.
// ==============================================================================

#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/wavefolder.h>
#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/processors/multi_stage_envelope.h>

#include <algorithm>
#include <cmath>

namespace Membrum {

// Minimal enum so plugin_ids can reference filter-type values symbolically.
enum class ToneShaperFilterType : int
{
    Lowpass  = 0,
    Highpass = 1,
    Bandpass = 2,
};

enum class ToneShaperCurve : int
{
    Exponential = 0,
    Linear      = 1,
};

class ToneShaper
{
public:
    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;

        // Filter
        filter_.prepare(sampleRate);
        filter_.setMode(Krate::DSP::SVFMode::Lowpass);
        filter_.setCutoff(filterCutoffHz_);
        filter_.setResonance(Krate::DSP::SVF::kButterworthQ);
        filter_.enableSmoothing(true);

        // Filter envelope
        filterEnv_.prepare(static_cast<float>(sampleRate));
        filterEnv_.setAttack(filterEnvAttackMs_);
        filterEnv_.setDecay(filterEnvDecayMs_);
        filterEnv_.setSustain(filterEnvSustain_);
        filterEnv_.setRelease(filterEnvReleaseMs_);
        filterEnv_.setVelocityScaling(false);

        // DC blocker (5 Hz cutoff: gentle, removes DC without affecting audio)
        dcBlocker_.prepare(sampleRate, 5.0f);

        // Drive waveshaper. Phase 9 perf: switched from Tanh to ReciprocalSqrt
        // (x / sqrt(x^2 + 1)). Same odd-only harmonic character as tanh but
        // ~3x cheaper (rsqrtps vs the Padé tanh approximation). Drive amount 0
        // bypasses the stage entirely (see the if(driveAmount_>0) gate in
        // processSample), so this character change only matters when the user
        // actually engages drive.
        drive_.setType(Krate::DSP::WaveshapeType::ReciprocalSqrt);
        drive_.setDrive(1.0f);
        drive_.setAsymmetry(0.0f);

        // Wavefolder (sine fold). Fold amount 0 means passthrough (handled in
        // bypass check).
        wavefolder_.setType(Krate::DSP::WavefoldType::Sine);
        wavefolder_.setFoldAmount(0.0f);

        // Pitch envelope: 2-stage exponential fall from 1.0 to 0.0.
        pitchEnv_.prepare(static_cast<float>(sampleRate));
        pitchEnv_.setNumStages(4);
        // Stage 0: instant rise to 1.0 (time = 0)
        pitchEnv_.setStage(0, 1.0f, 0.0f, Krate::DSP::EnvCurve::Linear);
        // Stage 1: fall to 0.0 over pitchEnvTimeMs_ with current curve
        pitchEnv_.setStage(1, 0.0f, std::max(1.0f, pitchEnvTimeMs_),
                           (pitchEnvCurve_ == ToneShaperCurve::Exponential)
                               ? Krate::DSP::EnvCurve::Logarithmic
                               : Krate::DSP::EnvCurve::Linear);
        // Stages 2..3: held at 0.0
        pitchEnv_.setStage(2, 0.0f, 0.0f, Krate::DSP::EnvCurve::Linear);
        pitchEnv_.setStage(3, 0.0f, 0.0f, Krate::DSP::EnvCurve::Linear);
        pitchEnv_.setSustainPoint(3);

        prepared_ = true;
    }

    void reset() noexcept
    {
        filter_.reset();
        filterEnv_.reset();
        dcBlocker_.reset();
        pitchEnv_.reset();
        pitchEnvEnabled_ = false;
        cutoffUpdateCounter_ = kCutoffUpdateInterval; // force update on next sample
    }

    // ------------------------------------------------------------------
    // Filter setters
    // ------------------------------------------------------------------

    void setFilterType(ToneShaperFilterType type) noexcept
    {
        filterType_ = type;
        switch (type)
        {
        case ToneShaperFilterType::Lowpass:
            filter_.setMode(Krate::DSP::SVFMode::Lowpass);
            break;
        case ToneShaperFilterType::Highpass:
            filter_.setMode(Krate::DSP::SVFMode::Highpass);
            break;
        case ToneShaperFilterType::Bandpass:
            filter_.setMode(Krate::DSP::SVFMode::Bandpass);
            break;
        }
    }

    void setFilterCutoff(float hz) noexcept
    {
        filterCutoffHz_ = hz;
        // Base cutoff stored; env modulation applied per-sample in processSample().
        filter_.setCutoff(filterCutoffHz_);
    }

    void setFilterResonance(float q) noexcept
    {
        filterResonance_ = q;
        // Map [0, 1] to a useful Q range [Butterworth, 10.0].
        const float qMapped = Krate::DSP::SVF::kButterworthQ
                              + std::clamp(q, 0.0f, 1.0f) * (10.0f - Krate::DSP::SVF::kButterworthQ);
        filter_.setResonance(qMapped);
    }

    void setFilterEnvAmount(float amount) noexcept
    {
        filterEnvAmount_ = std::clamp(amount, -1.0f, 1.0f);
    }

    void setFilterEnvAttackMs(float ms) noexcept
    {
        filterEnvAttackMs_ = ms;
        filterEnv_.setAttack(ms);
    }

    void setFilterEnvDecayMs(float ms) noexcept
    {
        filterEnvDecayMs_ = ms;
        filterEnv_.setDecay(ms);
    }

    void setFilterEnvSustain(float level) noexcept
    {
        filterEnvSustain_ = level;
        filterEnv_.setSustain(level);
    }

    void setFilterEnvReleaseMs(float ms) noexcept
    {
        filterEnvReleaseMs_ = ms;
        filterEnv_.setRelease(ms);
    }

    // ------------------------------------------------------------------
    // Drive / fold setters
    // ------------------------------------------------------------------

    void setDriveAmount(float amount) noexcept
    {
        driveAmount_ = std::clamp(amount, 0.0f, 1.0f);
        // Map [0, 1] to internal drive scaling [1.0, 10.0].
        drive_.setDrive(1.0f + driveAmount_ * 9.0f);
    }

    void setFoldAmount(float amount) noexcept
    {
        foldAmount_ = std::clamp(amount, 0.0f, 1.0f);
        // Map [0, 1] to wavefolder fold intensity [0, pi] (Serge-typical).
        wavefolder_.setFoldAmount(foldAmount_ * 3.14159265f);
    }

    // ------------------------------------------------------------------
    // Pitch envelope setters
    // ------------------------------------------------------------------

    void setPitchEnvStartHz(float hz) noexcept
    {
        pitchEnvStartHz_ = std::clamp(hz, 20.0f, 2000.0f);
    }

    void setPitchEnvEndHz(float hz) noexcept
    {
        pitchEnvEndHz_ = std::clamp(hz, 20.0f, 2000.0f);
    }

    void setPitchEnvTimeMs(float ms) noexcept
    {
        pitchEnvTimeMs_ = std::max(0.0f, ms);
        if (pitchEnvTimeMs_ > 0.0f)
        {
            // Clamp to minimum of 1 ms to prevent divide-by-zero / infinite slope.
            const float clampedMs = std::max(1.0f, pitchEnvTimeMs_);
            const auto curve = (pitchEnvCurve_ == ToneShaperCurve::Exponential)
                                   ? Krate::DSP::EnvCurve::Logarithmic
                                   : Krate::DSP::EnvCurve::Linear;
            pitchEnv_.setStage(1, 0.0f, clampedMs, curve);
        }
        // Note: when ms==0, pitchEnvTimeMs_=0 disables the env entirely and
        // processPitchEnvelope() short-circuits — the pitchEnv_ stage may hold
        // stale timing internally, but it is never consulted, so this is
        // intentional and safe.
    }

    void setPitchEnvCurve(ToneShaperCurve curve) noexcept
    {
        pitchEnvCurve_ = curve;
        if (pitchEnvTimeMs_ > 0.0f)
        {
            const float clampedMs = std::max(1.0f, pitchEnvTimeMs_);
            const auto envCurve = (curve == ToneShaperCurve::Exponential)
                                      ? Krate::DSP::EnvCurve::Logarithmic
                                      : Krate::DSP::EnvCurve::Linear;
            pitchEnv_.setStage(1, 0.0f, clampedMs, envCurve);
        }
    }

    // ------------------------------------------------------------------
    // Natural fundamental (stored by DrumVoice from the body's size-derived f0)
    // ------------------------------------------------------------------

    void setNaturalFundamentalHz(float hz) noexcept
    {
        naturalFundamentalHz_ = hz;
    }

    [[nodiscard]] float getNaturalFundamentalHz() const noexcept
    {
        return naturalFundamentalHz_;
    }

    [[nodiscard]] bool isPitchEnvActive() const noexcept
    {
        return pitchEnvTimeMs_ > 0.0f;
    }

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    void noteOn(float velocity) noexcept
    {
        // Reset the SVF integrator state on every retrigger. Without
        // this, a voice slot that's been re-used (voice-steal or
        // polyphony rotation) inherits the *previous* note's filter
        // state. With high-Q settings on cymbals/hats the residual
        // resonant energy decays at the previous note's cutoff and
        // bleeds into the new hit's first samples -- the perceived
        // "monotonous beep that's there sometimes" the user reported
        // intermittently after kit switches and rapid hits. (The SVF
        // doc recommends snapToTarget() for retriggers under the
        // assumption that stale energy decays naturally, but with
        // Q close to 1 it doesn't decay fast enough.)
        // Wipe per-note state from any previous voice that was on this
        // slot. Three things to clear:
        //   (1) SVF integrator tail (ic1eq/ic2eq) -- without this the
        //       previous note's resonant energy decays at the previous
        //       cutoff and bleeds into the new hit as a residual tone.
        //   (2) Filter envelope output -- ADSR's Hard retrigger calls
        //       enterAttack() but ramps from the current output_, not
        //       from 0. If the previous voice left the env in Sustain at
        //       a non-zero level (e.g. Jazz Brushes snare uses
        //       filterEnvSustain = 0.30), the new attack starts partway
        //       up and the filter modulation is wrong.
        //   (3) Pitch envelope output -- same issue. A previous voice
        //       that swept to the END pitch (e.g. an 808 boom that ended
        //       at 50 Hz) would leave pitchEnv at 0; gate(true) ramps
        //       from there, so the new note's pitch glide starts at the
        //       wrong frequency.
        filter_.resetIntegrators();
        filterEnv_.reset();
        pitchEnv_.reset();
        cutoffUpdateCounter_ = kCutoffUpdateInterval; // force update on first sample
        filterEnv_.setVelocity(velocity);
        filterEnv_.gate(true);
        if (pitchEnvTimeMs_ > 0.0f)
        {
            pitchEnv_.gate(true);
            pitchEnvEnabled_ = true;
        }
        else
        {
            pitchEnvEnabled_ = false;
        }
    }

    void noteOff() noexcept
    {
        filterEnv_.gate(false);
        pitchEnv_.gate(false);
    }

    // ------------------------------------------------------------------
    // Audio-rate and control-plane processing
    // ------------------------------------------------------------------

    /// Control-plane: returns the current body fundamental in Hz.
    /// When pitch env is disabled (pitchEnvTimeMs_ == 0), returns the
    /// Size-derived natural fundamental stored via setNaturalFundamentalHz().
    /// When enabled, interpolates from pitchEnvStartHz_ to pitchEnvEndHz_
    /// via the MultiStageEnvelope output (1.0 -> 0.0 envelope).
    [[nodiscard]] float processPitchEnvelope() noexcept
    {
        if (pitchEnvTimeMs_ == 0.0f)
        {
            return naturalFundamentalHz_;
        }
        // Envelope output: 1.0 at trigger, decays to 0.0 over time (Stage 0->1).
        // Interpolate: end + (start - end) * env
        const float env = pitchEnv_.process();
        return pitchEnvEndHz_ + (pitchEnvStartHz_ - pitchEnvEndHz_) * env;
    }

    /// Audio-rate process (body_output -> filtered/driven/folded output).
    /// Chain order per research.md §8 (Buchla west-coast flow):
    ///   Drive -> Wavefolder -> DCBlocker -> SVF Filter
    [[nodiscard]] float processSample(float bodyOutput) noexcept
    {
        float x = bodyOutput;

        // ----- Drive (Waveshaper) -----
        // Bypass when amount == 0 (FR-045).
        if (driveAmount_ > 0.0f)
        {
            // Blend shaped vs dry so amount=0 is exactly dry.
            const float shaped = drive_.process(x);
            x = x + driveAmount_ * (shaped - x);
        }

        // ----- Wavefolder -----
        if (foldAmount_ > 0.0f)
        {
            const float folded = wavefolder_.process(x);
            x = x + foldAmount_ * (folded - x);
        }

        // ----- DC Blocker -----
        // Only active when a nonlinear stage was engaged; otherwise
        // preserves bypass identity.
        if (driveAmount_ > 0.0f || foldAmount_ > 0.0f)
        {
            x = dcBlocker_.process(x);
        }

        // ----- SVF Filter -----
        // Bypass when cutoff is at its max (>= 20 kHz) AND filter env amount is 0.
        const bool filterBypass = (filterCutoffHz_ >= 20000.0f)
                                  && (filterEnvAmount_ == 0.0f);
        if (!filterBypass)
        {
            // Phase 9 perf: control-rate cutoff modulation. The naive form
            // recomputed `cutoffHz * 8^(envAmt * envVal)` and called
            // SVF::setCutoff (which itself triggers std::tan inside
            // SVF::computeG and a per-sample divide inside the SVF
            // smoother's advanceSmoother) on every audio sample. With the
            // filter env active, that turned the per-voice slow path into
            // ~30+ cycles of transcendentals + divides per sample. The
            // filter envelope itself is slow (>=1 ms attack typical), so
            // updating the cutoff target every kCutoffUpdateInterval
            // samples (~167 us at 48 kHz) is well below audible artefacts.
            // The SVF's own per-sample one-pole smoother interpolates
            // between targets so the audio path stays click-free.
            //
            // The envelope itself advances every sample so its internal
            // counters stay synchronised with the audio clock; the
            // expensive modCutoff/setCutoff path is gated.
            const float envValue = filterEnv_.process();
            ++cutoffUpdateCounter_;
            if (cutoffUpdateCounter_ >= kCutoffUpdateInterval)
            {
                cutoffUpdateCounter_ = 0;
                // 8^x == exp2(3x); exp2 is ~5x cheaper than pow.
                const float modCutoff =
                    filterCutoffHz_ * std::exp2(3.0f * filterEnvAmount_ * envValue);
                filter_.setCutoff(modCutoff);
            }
            x = filter_.process(x);
        }

        return x;
    }

    // ------------------------------------------------------------------
    // Bypass query
    // ------------------------------------------------------------------

    [[nodiscard]] bool isBypassed() const noexcept
    {
        return driveAmount_ == 0.0f
               && foldAmount_ == 0.0f
               && filterCutoffHz_ >= 20000.0f
               && filterEnvAmount_ == 0.0f
               && pitchEnvTimeMs_ == 0.0f;
    }

    // Getters used by tests
    [[nodiscard]] float getFilterCutoff() const noexcept { return filterCutoffHz_; }
    [[nodiscard]] float getDriveAmount() const noexcept { return driveAmount_; }
    [[nodiscard]] float getFoldAmount() const noexcept { return foldAmount_; }
    [[nodiscard]] float getPitchEnvStartHz() const noexcept { return pitchEnvStartHz_; }
    [[nodiscard]] float getPitchEnvEndHz() const noexcept { return pitchEnvEndHz_; }
    [[nodiscard]] float getPitchEnvTimeMs() const noexcept { return pitchEnvTimeMs_; }

private:
    double sampleRate_ = 44100.0;

    // Filter state
    ToneShaperFilterType filterType_     = ToneShaperFilterType::Lowpass;
    float filterCutoffHz_                = 20000.0f;
    float filterResonance_               = 0.0f;
    float filterEnvAmount_               = 0.0f;
    float filterEnvAttackMs_             = 1.0f;
    float filterEnvDecayMs_              = 100.0f;
    float filterEnvSustain_              = 0.0f;
    float filterEnvReleaseMs_            = 100.0f;

    // Drive / fold state
    float driveAmount_ = 0.0f;
    float foldAmount_  = 0.0f;

    // Pitch envelope state
    float pitchEnvStartHz_        = 160.0f;
    float pitchEnvEndHz_          = 50.0f;
    float pitchEnvTimeMs_         = 0.0f;     // 0 = disabled
    ToneShaperCurve pitchEnvCurve_ = ToneShaperCurve::Exponential;
    bool  pitchEnvEnabled_        = false;

    // Stored body natural fundamental (set by DrumVoice from size-derived f0).
    float naturalFundamentalHz_ = 0.0f;

    // Phase 9 perf: control-rate cutoff modulation counter. Incremented every
    // call to processSample(); when it hits kCutoffUpdateInterval the heavy
    // exp2 + setCutoff path runs and the counter resets. The SVF's own
    // per-sample smoother interpolates between coarse targets so the audio
    // stays click-free.
    static constexpr int kCutoffUpdateInterval = 8;  // ~167 us at 48 kHz
    int cutoffUpdateCounter_ = kCutoffUpdateInterval; // first sample updates immediately

    // Member DSP objects
    Krate::DSP::SVF                 filter_{};
    Krate::DSP::ADSREnvelope        filterEnv_{};
    Krate::DSP::Waveshaper          drive_{};
    Krate::DSP::Wavefolder          wavefolder_{};
    Krate::DSP::DCBlocker           dcBlocker_{};
    Krate::DSP::MultiStageEnvelope  pitchEnv_{};

    bool prepared_ = false;
};

} // namespace Membrum
