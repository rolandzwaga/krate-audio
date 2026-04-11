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

        // Build the static harmonic frame shape. Amplitudes are 1/k^2 (close
        // to natural harmonic falloff) then normalized so the bank's internal
        // RMS normalization produces a modest output level. Phases are
        // overwritten on every trigger().
        for (int k = 0; k < kNumPartials; ++k)
        {
            const int harmonicIndex = k + 1;
            frame_.partials[k].harmonicIndex       = harmonicIndex;
            frame_.partials[k].relativeFrequency   = static_cast<float>(harmonicIndex);
            frame_.partials[k].inharmonicDeviation = 0.0f;
            frame_.partials[k].amplitude           = 1.0f / static_cast<float>(harmonicIndex * harmonicIndex);
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
    }

    /// Process one sample. FR-052 zero-leak: returns 0 when amount_ is 0,
    /// without calling the oscillator bank.
    [[nodiscard]] float process() noexcept
    {
        if (amount_ == 0.0f)
            return 0.0f;

        return amount_ * bank_.process();
    }

private:
    Krate::DSP::HarmonicOscillatorBank bank_;
    Krate::DSP::XorShift32             rng_;
    Krate::DSP::HarmonicFrame          frame_{};

    float  amount_        = 0.0f;
    float  fundamentalHz_ = 440.0f;
    double sampleRate_    = 44100.0;
    std::uint32_t voiceId_ = 0;
};

} // namespace Membrum
