#pragma once

// ==============================================================================
// FMImpulseExciter -- Phase 2 (data-model.md §2.5, FR-014)
// ==============================================================================
// Chowning-style bell-like FM impulse:
//   carrier (FMOperator)   at f = 1500 Hz * carrierRatio
//   modulator (FMOperator) at f = 1500 Hz * modulatorRatio (default 1.4)
//   ampEnv       gates carrier amplitude over ≤ 100 ms
//   modIndexEnv  scales modulator amplitude (= modulation index) over ≤ 30 ms
//     → modIndexEnv decays FASTER than ampEnv (FR-014)
//
// Velocity mapping (FR-014):
//   modulation index = lerp(0.5, 3.0, velocity)
//   amplitude        = velocity
//
// NOTE: FMOperator::prepare() is NOT real-time safe (wavetable generation).
//       prepare() is only called from DrumVoice::prepare() (host setup),
//       never on the audio thread.
// ==============================================================================

#include <krate/dsp/processors/fm_operator.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Membrum {

struct FMImpulseExciter
{
    void prepare(double sampleRate, std::uint32_t /*voiceId*/) noexcept
    {
        sampleRate_ = sampleRate;
        carrier_.prepare(sampleRate);
        modulator_.prepare(sampleRate);

        carrier_.setRatio(1.0f);
        modulator_.setRatio(1.0f);
        carrier_.setFeedback(0.0f);
        modulator_.setFeedback(0.0f);

        // Envelope coefficients: exponential decay y[n] = y[n-1] * coeff, with
        // coeff chosen so the envelope reaches ~1e-3 at the target time.
        //   coeff = exp(ln(1e-3) / (tau * sampleRate))
        //         = exp(-6.9078 / (tau * sampleRate))
        // FR-014: modIndex decays FASTER than amp.
        constexpr float kAmpTauSec      = 0.080f; // 80 ms (≤ 100 ms)
        constexpr float kModIndexTauSec = 0.030f; // 30 ms — faster than amp
        ampCoeff_      = std::exp(-6.9078f / (kAmpTauSec      * static_cast<float>(sampleRate)));
        modIndexCoeff_ = std::exp(-6.9078f / (kModIndexTauSec * static_cast<float>(sampleRate)));

        reset();
    }

    void reset() noexcept
    {
        carrier_.reset();
        modulator_.reset();
        ampEnv_      = 0.0f;
        modIndexEnv_ = 0.0f;
        active_      = false;
    }

    void trigger(float velocity) noexcept
    {
        velocity = std::clamp(velocity, 0.0f, 1.0f);

        // Reset operator phases for glitch-free retrigger.
        carrier_.reset();
        modulator_.reset();

        // Chowning bell: carrier:modulator = 1:1.4 (FR-014).
        // FR-016 / SC-004: velocity scales the base frequency so the spectral
        // centroid doubles between v=0.23 and v=1.0. 400 Hz (soft) → 2500 Hz
        // (hard). The 1:1.4 ratio is preserved at every velocity.
        constexpr float kCarrierRatio  = 1.0f;
        constexpr float kModulatorRatio = 1.4f;
        carrierRatio_   = kCarrierRatio;
        modulatorRatio_ = kModulatorRatio;
        const float baseFreq = 400.0f * std::pow(6.25f, velocity); // 400 → 2500

        carrier_.setFrequency(baseFreq * kCarrierRatio);
        modulator_.setFrequency(baseFreq * kModulatorRatio);

        // Modulation index (radians of phase modulation applied to carrier):
        //   velocity 0 → 0.5 rad, velocity 1 → 3.0 rad.
        modulationIndex_ = 0.5f + (3.0f - 0.5f) * velocity;

        // Amplitude from velocity.
        carrierAmplitude_ = velocity;

        // Start envelopes at full and let them decay exponentially.
        ampEnv_      = 1.0f;
        modIndexEnv_ = 1.0f;
        active_      = true;
    }

    void release() noexcept
    {
        // Impulse; release is a no-op.
    }

    [[nodiscard]] float process(float /*bodyFeedback*/) noexcept
    {
        if (!active_)
            return 0.0f;

        // Step 1: Modulator with envelope-scaled "level" (= modulation index).
        //   FMOperator::process returns its sine output scaled by setLevel().
        //   We treat that as the modulation input to the carrier in radians.
        modulator_.setLevel(modulationIndex_ * modIndexEnv_);
        const float modOut = modulator_.process(0.0f);

        // Step 2: Carrier phase-modulated by modulator output.
        carrier_.setLevel(carrierAmplitude_ * ampEnv_);
        const float carrierOut = carrier_.process(modOut);

        // Step 3: Advance envelopes.
        ampEnv_      *= ampCoeff_;
        modIndexEnv_ *= modIndexCoeff_;

        // Flag idle when amp envelope drops below audible threshold (~-60 dB).
        if (ampEnv_ < 0.001f)
        {
            active_ = false;
        }
        return carrierOut;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

private:
    Krate::DSP::FMOperator carrier_{};
    Krate::DSP::FMOperator modulator_{};
    double sampleRate_       = 44100.0;
    float  ampEnv_           = 0.0f;
    float  modIndexEnv_      = 0.0f;
    float  ampCoeff_         = 0.0f;
    float  modIndexCoeff_    = 0.0f;
    float  carrierRatio_     = 1.0f;
    float  modulatorRatio_   = 1.4f;
    float  modulationIndex_  = 0.0f;
    float  carrierAmplitude_ = 0.0f;
    bool   active_           = false;
};

} // namespace Membrum
