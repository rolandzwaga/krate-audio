#pragma once

// ==============================================================================
// NoiseBurstExciter -- Phase 2 (data-model.md §2.3, FR-012)
// ==============================================================================
// A short broadband noise burst filtered by an SVF:
//   NoiseOscillator (Layer 1, per-sample) → linear-decay envelope → SVF
//
// Velocity mapping (FR-012):
//   burst duration  = lerp(15 ms, 2 ms,   velocity)
//   SVF cutoff      = lerp(200,   5000 Hz, velocity)
//   burst amplitude = 0.2 + 0.8 * velocity
//
// NOTE: Uses Krate::DSP::NoiseOscillator (Layer 1, per-sample) NOT
//       NoiseGenerator (Layer 2, block-oriented) — see research.md §2.
//
// Contract:
//   - process() returns 0.0 when burstSamplesRemaining_ <= 0
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
        burstSamplesRemaining_ = 0.0f;
        burstSamplesTotal_     = 0.0f;
        amplitude_             = 0.0f;
        active_                = false;
    }

    void trigger(float velocity) noexcept
    {
        velocity = std::clamp(velocity, 0.0f, 1.0f);

        // Duration: 15 ms (soft) → 6 ms (hard) — long enough to fill the
        // 20 ms spectral-analysis window used in character tests (US1-3).
        const float durationMs = 15.0f + (6.0f - 15.0f) * velocity;
        const float samples    = durationMs * 0.001f * static_cast<float>(sampleRate_);
        burstSamplesTotal_     = std::max(1.0f, samples);
        burstSamplesRemaining_ = burstSamplesTotal_;

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
        if (burstSamplesRemaining_ <= 0.0f)
        {
            active_ = false;
            return 0.0f;
        }

        // Linear decay envelope: 1.0 at start → 0.0 at end.
        const float env = burstSamplesRemaining_ / burstSamplesTotal_;
        const float raw = noise_.process() * amplitude_ * env;
        const float out = filter_.process(raw);

        burstSamplesRemaining_ -= 1.0f;
        return out;
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return active_ && burstSamplesRemaining_ > 0.0f;
    }

private:
    Krate::DSP::NoiseOscillator noise_{};
    Krate::DSP::SVF             filter_{};
    double sampleRate_            = 44100.0;
    float  burstSamplesRemaining_ = 0.0f;
    float  burstSamplesTotal_     = 0.0f;
    float  amplitude_             = 0.0f;
    bool   active_                = false;
};

} // namespace Membrum
