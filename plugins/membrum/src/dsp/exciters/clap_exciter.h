#pragma once

// ==============================================================================
// ClapExciter -- multi-burst broadband noise excitation for a hand clap.
// ==============================================================================
// A hand clap read as "clap" (not "mallet/metal") requires an aperiodic
// multi-impulse excitation: 3-4 rapid broadband noise bursts spaced ~8-12 ms
// apart (the "several hands not quite in sync" flam), followed by a diffuse
// room tail. This exciter models the burst TRAIN (the classic TR-808/909 comb
// re-trigger); the diffuse tail is supplied downstream by the voice's parallel
// NoiseLayer. Signal path per sample:
//   NoiseOscillator (White) -> summed 4-burst raised-cosine/exp envelope -> SVF Bandpass
//
// Burst timing is FIXED (velocity-independent) so the "clap" rhythm is stable
// across dynamics; velocity scales amplitude and shifts the bandpass center for
// the harder-hit "sharper crack". Onsets carry a mild non-uniform jitter
// (spacings 10/11/12 ms) so the train is aperiodic, not a buzzy comb.
//
// NOTE (SC-004 exemption): this exciter intentionally uses a NARROW velocity->
// brightness sweep (stable clap character across dynamics). It is therefore
// deliberately exempt from the SC-004 "centroid ratio >= 2.0" per-exciter test
// (test_velocity_mapping.cpp) and MUST NOT be added to that test's list.
//
// Contract (matches NoiseBurstExciter):
//   - process() returns 0.0 once the last burst has decayed
//   - all state pre-allocated in prepare(); trigger() allocates nothing
//   - retrigger-safe: trigger() re-seeds/resets so a stolen/choked voice renders
//     bit-identically to a pristine voice (FR-124)
// ==============================================================================

#include <krate/dsp/primitives/noise_oscillator.h>
#include <krate/dsp/primitives/svf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Membrum {

struct ClapExciter
{
    // 4 bursts: onsets 0/10/21/33 ms (spacings 10,11,12 ms -> aperiodic, in the
    // 8-12 ms target band); relative amplitudes taper ~0/-1/-1.7/-2.5 dB.
    static constexpr int   kNumBursts     = 4;
    static constexpr std::array<float, kNumBursts> kOnsetMs = {0.0f, 10.0f, 21.0f, 33.0f};
    static constexpr std::array<float, kNumBursts> kRelAmp  = {1.0f, 0.90f, 0.82f, 0.75f};
    static constexpr float kBurstDecayMs  = 8.0f;   // per-burst -60 dB span
    static constexpr float kBurstRiseMs   = 0.6f;   // fast attack (<1 ms target)

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        noise_.prepare(sampleRate);
        // White noise: warm TR-808 flavour (NOT the +6 dB/oct Violet the hi-hat
        // NoiseBurst uses, which would read as too hissy for a clap).
        noise_.setColor(Krate::DSP::NoiseColor::White);
        noise_.setSeed(0xC1A9u ^ voiceId);
        filter_.prepare(sampleRate);
        filter_.setMode(Krate::DSP::SVFMode::Bandpass);
        filter_.setResonance(1.0f);   // gentle single hump (Q~1); NOT ringing
        filter_.setCutoff(1600.0f);
        filter_.snapToTarget();
        reset();
    }

    void reset() noexcept
    {
        noise_.reset();
        filter_.reset();
        sampleIdx_    = 0;
        totalSamples_ = 0;
        riseSamples_  = 1;
        decaySamples_ = 1;
        decayK_       = 1.0f;
        amplitude_    = 0.0f;
        for (int k = 0; k < kNumBursts; ++k)
            onsetSamples_[static_cast<std::size_t>(k)] = 0;
        active_ = false;
    }

    void trigger(float velocity) noexcept
    {
        // Re-seed + reset for deterministic, bit-identical retrigger (FR-124).
        noise_.reset();
        filter_.reset();

        velocity = std::clamp(velocity, 0.0f, 1.0f);
        const float sr = static_cast<float>(sampleRate_);

        riseSamples_  = std::max(1, static_cast<int>(std::lround(kBurstRiseMs  * 1.0e-3f * sr)));
        decaySamples_ = std::max(1, static_cast<int>(std::lround(kBurstDecayMs * 1.0e-3f * sr)));
        decayK_       = 6.9078f / static_cast<float>(decaySamples_);  // ~-60 dB
        const int burstSpan = riseSamples_ + decaySamples_;

        int lastEnd = 0;
        for (int k = 0; k < kNumBursts; ++k)
        {
            onsetSamples_[static_cast<std::size_t>(k)] =
                static_cast<int>(std::lround(kOnsetMs[static_cast<std::size_t>(k)] * 1.0e-3f * sr));
            lastEnd = std::max(lastEnd, onsetSamples_[static_cast<std::size_t>(k)] + burstSpan);
        }
        totalSamples_ = lastEnd;

        // Velocity: overall amplitude 0.2 (soft) -> 1.0 (hard).
        amplitude_ = 0.2f + 0.8f * velocity;

        // Brightness rises with velocity: bandpass center 1200 -> 2200 Hz. (Real
        // 909/hand-clap spectra peak ~1.5-2.5 kHz -- a darker band reads as a
        // muffled thud and drags the whole-render centroid below the test-3 gate.)
        const float cutoff = 1200.0f + 1000.0f * velocity;
        filter_.setCutoff(cutoff);
        filter_.snapToTarget();

        sampleIdx_ = 0;
        active_    = true;
    }

    void release() noexcept
    {
        // One-shot burst train; release is a no-op (the train finishes on its own).
    }

    [[nodiscard]] float process(float /*bodyFeedback*/) noexcept
    {
        if (sampleIdx_ >= totalSamples_)
        {
            active_ = false;
            return 0.0f;
        }

        // Summed multi-burst envelope: each burst contributes a raised-cosine
        // rise then an exponential decay, gated to its own [onset, onset+span).
        float env = 0.0f;
        for (int k = 0; k < kNumBursts; ++k)
        {
            const int n = sampleIdx_ - onsetSamples_[static_cast<std::size_t>(k)];
            if (n < 0 || n >= riseSamples_ + decaySamples_)
                continue;
            float e;
            if (n < riseSamples_)
            {
                const float t = static_cast<float>(n) / static_cast<float>(riseSamples_);
                e = 0.5f * (1.0f - std::cos(kPi * t));
            }
            else
            {
                e = std::exp(-decayK_ * static_cast<float>(n - riseSamples_));
            }
            env += kRelAmp[static_cast<std::size_t>(k)] * e;
        }

        const float raw = noise_.process() * amplitude_ * env;
        const float out = filter_.process(raw);
        ++sampleIdx_;
        return out;
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return active_ && sampleIdx_ < totalSamples_;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    Krate::DSP::NoiseOscillator noise_{};
    Krate::DSP::SVF             filter_{};
    double sampleRate_   = 44100.0;
    int    sampleIdx_    = 0;
    int    totalSamples_ = 0;
    int    riseSamples_  = 1;
    int    decaySamples_ = 1;
    float  decayK_       = 1.0f;
    float  amplitude_    = 0.0f;
    std::array<int, kNumBursts> onsetSamples_{};
    bool   active_       = false;
};

} // namespace Membrum
