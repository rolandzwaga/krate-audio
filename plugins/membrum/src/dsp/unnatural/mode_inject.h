#pragma once

// ==============================================================================
// ModeInject -- Phase 2.E.3 (data-model.md §7, unnatural_zone_contract.md)
// ==============================================================================
// Wraps Krate::DSP::HarmonicOscillatorBank with per-voice phase randomization
// via Krate::DSP::XorShift32. Phase 2 uses a fixed harmonic preset: integer
// ratios 1, 2, 3, 4, 5, 6, 7, 8 at the body's fundamental. On every trigger()
// the starting phases are re-randomized (spec 135 mandate).
//
// FR-052 zero-leak bypass: when amount_ == 0.0f, process() returns 0.0f
// immediately without calling the oscillator bank. No leftover state contributes
// to the output (contract item 5).
//
// Real-time safety (FR-056): HarmonicOscillatorBank is pre-allocated in
// prepare(). trigger() writes new phase values via loadFrame(); no allocation.
// XorShift32 is per-voice, thread-local to the audio thread (spec 135 + spec
// 137 research.md §6).
// ==============================================================================

#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/xorshift32.h>
#include <krate/dsp/processors/harmonic_oscillator_bank.h>
#include <krate/dsp/processors/harmonic_types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Membrum {

class ModeInject
{
public:
    /// Number of integer-harmonic partials injected when enabled.
    static constexpr int kNumPartials = 8;

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        voiceId_    = voiceId;

        bank_.prepare(sampleRate);
        rng_.seed(voiceId);
        updateEnvCoeff();  // D2: coeff depends on the sample rate

        // Build the static harmonic frame shape. Amplitudes follow the natural
        // harmonic falloff 1/k (-6 dB/oct, the sawtooth/bowed spectrum), then
        // normalized so the bank's internal RMS normalization produces a modest
        // output level. The earlier 1/k^2 (-12 dB/oct) was the triangle/plucked
        // spectrum, which is fundamental-dominated and dull (audit §3-B); 1/k
        // gives the injected series a brighter, more distinctive timbre. Phases
        // are overwritten on every trigger().
        for (int k = 0; k < kNumPartials; ++k)
        {
            const int harmonicIndex = k + 1;
            frame_.partials[k].harmonicIndex       = harmonicIndex;
            frame_.partials[k].relativeFrequency   = static_cast<float>(harmonicIndex);
            frame_.partials[k].inharmonicDeviation = 0.0f;
            frame_.partials[k].amplitude           = 1.0f / static_cast<float>(harmonicIndex);
            frame_.partials[k].phase               = 0.0f;
            frame_.partials[k].bandwidth           = 0.0f;
            frame_.partials[k].stability           = 1.0f;
            frame_.partials[k].age                 = 0;
            frame_.partials[k].frequency           = 0.0f;
            frame_.partials[k].sourceId            = 0;
        }
        frame_.numPartials     = kNumPartials;
        frame_.f0              = fundamentalHz_;
        frame_.f0Confidence    = 1.0f;
        frame_.spectralCentroid = 0.0f;
        frame_.brightness      = 0.0f;
        frame_.noisiness       = 0.0f;
        frame_.globalAmplitude = 1.0f;
    }

    void reset() noexcept
    {
        bank_.reset();
    }

    void setAmount(float amount) noexcept { amount_ = amount; }

    /// D2 (06-orchestralKit-fix-plan.md): decay envelope T60 in seconds.
    /// Without an envelope any amount > 0 rang as an undamped flat plateau
    /// (~-20 dBFS) outlasting the drum -- the "synth bass note" class of bug.
    /// DrumVoice ties this to the pad's decay at noteOn; the default keeps
    /// the series finite even for callers that never wire it.
    void setDecaySeconds(float t60Seconds) noexcept
    {
        decayT60Sec_ = std::clamp(t60Seconds, 0.05f, 20.0f);
        updateEnvCoeff();
    }

    void setFundamentalHz(float hz) noexcept
    {
        fundamentalHz_ = hz;
        if (hz > 0.0f)
            bank_.setTargetPitch(hz);
    }

    [[nodiscard]] float getAmount() const noexcept { return amount_; }

    /// Trigger randomizes the starting phase of each injected partial
    /// independently via XorShift32 and (re)loads the harmonic frame into
    /// the bank. Called from DrumVoice::noteOn().
    void trigger() noexcept
    {
        // Randomize 8 phases in [0, 2*pi).
        for (int k = 0; k < kNumPartials; ++k)
        {
            const float r = rng_.nextFloat();                     // [0, 1)
            frame_.partials[k].phase = r * 2.0f * Krate::DSP::kPi; // [0, 2*pi)
        }

        // Clear bank state so the next loadFrame() re-initialises oscillator
        // phases from the supplied partial.phase values (HarmonicOscillatorBank
        // only sets sin/cos state from partial.phase when !frameLoaded_).
        bank_.reset();

        const float f0 = (fundamentalHz_ > 0.0f) ? fundamentalHz_ : 440.0f;
        frame_.f0      = f0;

        // skipNormalization=true because we pre-chose small per-partial
        // amplitudes; the adaptive normalizer would boost them unexpectedly.
        bank_.loadFrame(frame_, f0, /*skipNormalization=*/true);

        // D2: re-arm the decay envelope for this note.
        env_ = 1.0f;
        updateEnvCoeff();
    }

    /// Process one sample. FR-052 zero-leak: returns 0 when amount_ is 0,
    /// without calling the oscillator bank. D2: the one-pole decay envelope
    /// makes the injected series die with the drum instead of ringing as an
    /// undamped plateau.
    [[nodiscard]] float process() noexcept
    {
        if (amount_ == 0.0f)
            return 0.0f;

        const float s = amount_ * env_ * bank_.process();
        env_ *= envCoeff_;
        return s;
    }

private:
    void updateEnvCoeff() noexcept
    {
        // T60: 60 dB (factor 10^-3) over decayT60Sec_ seconds.
        const double n = std::max(1.0, sampleRate_ * static_cast<double>(decayT60Sec_));
        envCoeff_ = static_cast<float>(std::pow(10.0, -3.0 / n));
    }

    Krate::DSP::HarmonicOscillatorBank bank_;
    Krate::DSP::XorShift32             rng_;
    Krate::DSP::HarmonicFrame          frame_{};

    float  amount_        = 0.0f;
    float  fundamentalHz_ = 440.0f;
    float  decayT60Sec_   = 1.5f;   // D2 default: finite even if never wired
    float  env_           = 1.0f;
    float  envCoeff_      = 1.0f;
    double sampleRate_    = 44100.0;
    std::uint32_t voiceId_ = 0;
};

} // namespace Membrum
