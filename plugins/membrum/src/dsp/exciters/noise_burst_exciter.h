#pragma once

// ==============================================================================
// NoiseBurstExciter -- Phase 2 (data-model.md §2.3, FR-012) + Phase 7 fix.
// ==============================================================================
// A short broadband noise burst filtered by an SVF:
//   NoiseOscillator (Layer 1, per-sample) → raised-cosine envelope → SVF
//
// Phase 7 fixes:
//   (1) `setContactMs(ms)` now actually plumbs `PadConfig::noiseBurstDuration`
//       through to the exciter (previously stored and never read).
//   (2) Envelope switched from linear decay to asymmetric raised-cosine rise
//       + exponential decay, matching published impact models and the
//       `ClickLayer` sibling component.
//
// Velocity mapping (FR-012):
//   burst duration  = contactMs * (1.2 - 0.4 * velocity)
//                     (harder strikes are slightly shorter)
//   SVF cutoff      = 800 * 12^velocity  (kept for FR-016/SC-004)
//   burst amplitude = 0.2 + 0.8 * velocity
//
// NOTE: Uses Krate::DSP::NoiseOscillator (Layer 1, per-sample) NOT
//       NoiseGenerator (Layer 2, block-oriented) — see research.md §2.
//
// Contract:
//   - process() returns 0.0 when burst is done
//   - All state pre-allocated in prepare()
//   - Retrigger safe (no allocation, resets envelope)
// ==============================================================================

#include <krate/dsp/core/pattern_freeze_types.h>
#include <krate/dsp/primitives/noise_oscillator.h>
#include <krate/dsp/primitives/svf.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Membrum {

struct NoiseBurstExciter
{
    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        noise_.prepare(sampleRate);
        // Violet noise (+6 dB/oct) has the highest spectral centroid of all
        // colors — chosen to guarantee NoiseBurst dominates the upper spectrum
        // compared to ImpactExciter transients (US1-3).
        noise_.setColor(Krate::DSP::NoiseColor::Violet);
        noise_.setSeed(0xB357u ^ voiceId);
        filter_.prepare(sampleRate);
        // Bandpass so velocity maps to a narrow resonance that shifts the
        // spectral-centroid strongly with velocity (FR-016/SC-004) while
        // keeping the overall brightness higher than a lowpass impulse burst
        // (US1-3).
        filter_.setMode(Krate::DSP::SVFMode::Bandpass);
        filter_.setResonance(2.0f);
        filter_.setCutoff(2000.0f);
        filter_.snapToTarget();
        reset();
    }

    void reset() noexcept
    {
        noise_.reset();
        filter_.reset();
        sampleIdx_   = 0;
        burstTotal_  = 0;
        riseSamples_ = 0;
        decayK_      = 1.0f;
        amplitude_   = 0.0f;
        active_      = false;
    }

    /// Phase 7: plumb PadConfig::noiseBurstDuration (normalized 0..1) into
    /// the burst length. Values are denormalized to [2, 15] ms per the UI
    /// labelling. Called by the Processor before each noteOn; velocity then
    /// modulates the cached contact time downwards for harder strikes.
    void setContactMs(float normalized) noexcept
    {
        const float clamped = std::clamp(normalized, 0.0f, 1.0f);
        contactMsCached_ = 2.0f + clamped * 13.0f;  // [2, 15] ms
    }

    void trigger(float velocity) noexcept
    {
        velocity = std::clamp(velocity, 0.0f, 1.0f);

        // Phase 7: honor the configured contact time and shorten slightly for
        // harder strikes (factor 1.2 -> 0.8 across velocity).
        const float durationMs = std::max(0.5f, contactMsCached_ * (1.2f - 0.4f * velocity));
        const float samplesF   = durationMs * 1e-3f * static_cast<float>(sampleRate_);
        burstTotal_  = std::max(2, static_cast<int>(std::lround(samplesF)));
        riseSamples_ = std::max(1, burstTotal_ / 5);  // 20% raised-cosine rise
        const int decaySamples = std::max(1, burstTotal_ - riseSamples_);
        // ~ -60 dB over the decay span.
        decayK_ = 6.9078f / static_cast<float>(decaySamples);
        sampleIdx_ = 0;

        // Bandpass center: velocity 0.23 → ~1200 Hz, velocity 1.0 → ~8000 Hz.
        // Wide range ensures the centroid ratio clears the SC-004 threshold
        // (≥ 2.0) and the overall centroid beats an Impulse transient.
        const float cutoff = 800.0f * std::pow(12.0f, velocity); // 800 → 9600
        filter_.setCutoff(cutoff);
        filter_.snapToTarget();

        // Amplitude: 0.2 (soft) → 1.0 (hard)
        amplitude_ = 0.2f + 0.8f * velocity;

        active_ = true;
    }

    void release() noexcept
    {
        // Burst has no sustain phase; release is a no-op.
    }

    [[nodiscard]] float process(float /*bodyFeedback*/) noexcept
    {
        if (sampleIdx_ >= burstTotal_)
        {
            active_ = false;
            return 0.0f;
        }

        float env;
        if (sampleIdx_ < riseSamples_)
        {
            const float t = static_cast<float>(sampleIdx_) / static_cast<float>(riseSamples_);
            env = 0.5f * (1.0f - std::cos(kPi * t));
        }
        else
        {
            const int k = sampleIdx_ - riseSamples_;
            env = std::exp(-decayK_ * static_cast<float>(k));
        }
        const float raw = noise_.process() * amplitude_ * env;
        const float out = filter_.process(raw);

        ++sampleIdx_;
        return out;
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return active_ && sampleIdx_ < burstTotal_;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    Krate::DSP::NoiseOscillator noise_{};
    Krate::DSP::SVF             filter_{};
    double sampleRate_       = 44100.0;
    int    sampleIdx_        = 0;
    int    burstTotal_       = 0;
    int    riseSamples_      = 0;
    float  decayK_           = 1.0f;
    float  amplitude_        = 0.0f;
    float  contactMsCached_  = 8.5f;  // centre of [2, 15] ms range
    bool   active_           = false;
};

} // namespace Membrum
