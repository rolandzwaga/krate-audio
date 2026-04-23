#pragma once

// ==============================================================================
// ClickLayer -- Always-on attack transient driving the body's modes.
// ==============================================================================
// Research-backed realism ingredient (Cook "Sines + Noise + Transients";
// Avanzini/Rocchesso impact model; commuted-synthesis principle of putting
// the complexity in the excitation). Fires at every noteOn regardless of
// selected ExciterType. 2-5 ms shaped filtered-white-noise burst with an
// asymmetric envelope: fast raised-cosine rise (first 20% of the burst) to
// model initial beater-contact impact, then an exponential decay (remaining
// 80%) modeling the finite contact time.
//
// Per-sample output is summed into the exciter's excitation feed so it
// actually drives the body's modal bank -- matching how real beater impacts
// inject broadband energy into drum heads.
// ==============================================================================

#include <krate/dsp/core/pattern_freeze_types.h>
#include <krate/dsp/primitives/noise_oscillator.h>
#include <krate/dsp/primitives/svf.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Membrum {

/// Normalized parameters for the click layer. All values in [0, 1].
struct ClickLayerParams
{
    float mix        = 0.0f;   // 0 = off, 1 = full-strength click
    float contactMs  = 0.3f;   // norm 0..1 -> 2..5 ms burst duration
    float brightness = 0.6f;   // norm 0..1 -> 200..12000 Hz bandpass center
};

class ClickLayer
{
public:
    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        noise_.prepare(sampleRate);
        noise_.setSeed(voiceId * 2u + 17u);
        noise_.setColor(Krate::DSP::NoiseColor::White);

        filter_.prepare(sampleRate);
        filter_.setMode(Krate::DSP::SVFMode::Bandpass);
        filter_.setCutoff(3000.0f);
        filter_.setResonance(0.8f);

        mix_ = 0.0f;
        sampleIdx_ = 0;
        burstSamples_ = 0;
    }

    void reset() noexcept
    {
        noise_.reset();
        filter_.reset();
        sampleIdx_ = 0;
        burstSamples_ = 0;
    }

    /// Apply normalized params. Safe to call between notes.
    void configure(const ClickLayerParams& p) noexcept
    {
        mix_ = std::clamp(p.mix, 0.0f, 1.0f);

        const float cutoffHz = denormBrightness(p.brightness);
        filter_.setCutoff(cutoffHz);
        filter_.setResonance(0.8f);

        contactMsCached_ = denormContactMs(p.contactMs);
    }

    /// Fire the click. Re-triggerable (resets the burst counter).
    ///
    /// Re-seeds the internal PRNG and resets the bandpass filter state so
    /// every trigger produces a bit-identical noise burst for a given voice.
    /// FR-124 bit-identity: a stolen/choked voice reused for a new note must
    /// render sample-identical output to a pristine voice at the same slot;
    /// without the reset the PRNG state from the previous note carries over
    /// and the click's noise sequence diverges, which surfaces in the
    /// steal/choke click-free tests as a ~0.47 cleaned-click residual.
    void trigger(float velocity) noexcept
    {
        noise_.reset();
        filter_.reset();

        velocity_ = std::clamp(velocity, 0.0f, 1.0f);
        // Velocity shortens the burst slightly (harder strikes have faster
        // initial contact).
        const float vScale = 1.0f - 0.3f * velocity_;
        const float burstMs = std::max(0.5f, contactMsCached_ * vScale);
        burstSamples_ = static_cast<int>(std::lround(burstMs * 1e-3 * sampleRate_));
        if (burstSamples_ < 2) burstSamples_ = 2;
        riseSamples_ = std::max(1, burstSamples_ / 5);  // 20% raised-cosine rise
        decaySamples_ = std::max(1, burstSamples_ - riseSamples_);
        // Decay constant so the envelope falls to ~ -60 dB across decaySamples_.
        decayK_ = 6.9078f / static_cast<float>(decaySamples_); // ln(1000) ≈ 6.9078
        sampleIdx_ = 0;
    }

    /// Per-sample click output. Returns 0 after the burst finishes.
    ///
    /// Returns the click's natural amplitude (bandpass'd noise × asymmetric
    /// raised-cosine envelope × velocity × mix). Callers that route the
    /// click directly into the output or excitation bus should multiply by
    /// `kStandaloneOutputGain` to calibrate against the ImpactExciter's
    /// impulse peak (~1.0).
    [[nodiscard]] float processSample() noexcept
    {
        if (mix_ <= 0.0f || sampleIdx_ >= burstSamples_) {
            return 0.0f;
        }
        const float n = noise_.process();
        const float f = filter_.process(n);

        float env;
        if (sampleIdx_ < riseSamples_) {
            // Raised-cosine rise: 0.5 * (1 - cos(pi * t / rise))
            const float t = static_cast<float>(sampleIdx_) / static_cast<float>(riseSamples_);
            env = 0.5f * (1.0f - std::cos(kPi * t));
        } else {
            // Exponential decay from peak (env=1 at rise end) over decaySamples_.
            const int k = sampleIdx_ - riseSamples_;
            env = std::exp(-decayK_ * static_cast<float>(k));
        }

        ++sampleIdx_;
        return mix_ * velocity_ * env * f;
    }

    /// Block-rate output (writes, does not add).
    void processBlock(float* out, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i) {
            out[i] = processSample();
        }
    }

    [[nodiscard]] bool isActive() const noexcept { return sampleIdx_ < burstSamples_; }
    [[nodiscard]] float mix() const noexcept { return mix_; }

private:
    static constexpr float kPi = 3.14159265358979323846f;

public:
    // Amplitude calibration for the Phase 7 standalone path in DrumVoice.
    // 2x keeps the combined (body + click) peak under 0 dBFS (SC-008) while
    // making the click's transient clearly audible. See processSample() docs.
    static constexpr float kStandaloneOutputGain = 2.0f;
private:

    static float denormContactMs(float norm) noexcept
    {
        const float clamped = std::clamp(norm, 0.0f, 1.0f);
        return 2.0f + clamped * 3.0f;  // [2, 5] ms
    }

    static float denormBrightness(float norm) noexcept
    {
        const float clamped = std::clamp(norm, 0.0f, 1.0f);
        // Log sweep 200..12000 Hz.
        const float lo = std::log(200.0f);
        const float hi = std::log(12000.0f);
        return std::exp(lo + clamped * (hi - lo));
    }

    double                       sampleRate_ = 44100.0;
    Krate::DSP::NoiseOscillator  noise_;
    Krate::DSP::SVF              filter_;
    float mix_              = 0.0f;
    float velocity_         = 1.0f;
    float contactMsCached_  = 3.0f;
    int   burstSamples_     = 0;
    int   riseSamples_      = 0;
    int   decaySamples_     = 0;
    float decayK_           = 1.0f;
    int   sampleIdx_        = 0;
};

} // namespace Membrum
