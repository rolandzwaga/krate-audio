#pragma once

// ==============================================================================
// NoiseLayer -- Always-on parallel filtered-noise layer for every voice.
// ==============================================================================
// Research-backed realism ingredient (Serra/Smith SMS deterministic+stochastic,
// Cook "Sines + Noise + Transients", Chromaphone/Microtonic always-on noise
// partner). Sits alongside the modal body and sums into the signal chain
// post-body / pre-ModeInject inside DrumVoice. Independent of body choice.
//
// Signal path:
//   NoiseOscillator -> SVF bandpass -> ADSR envelope -> mix
// ==============================================================================

#include <krate/dsp/core/pattern_freeze_types.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/noise_oscillator.h>
#include <krate/dsp/primitives/svf.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Membrum {

/// Normalized parameters controlling the noise layer. All values in [0, 1].
struct NoiseLayerParams
{
    float mix        = 0.0f;   // 0 = fully off, 1 = full noise into mix bus
    float cutoff     = 0.5f;   // log 40..18000 Hz
    float resonance  = 0.2f;   // 0.3..5.0
    float decay      = 0.3f;   // exp 20..2000 ms
    float color      = 0.5f;   // 0=Brown, 0.33=Pink, 0.66=White, 1=Violet
};

class NoiseLayer
{
public:
    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        noise_.prepare(sampleRate);
        noise_.setSeed(voiceId + 1u);
        noise_.setColor(Krate::DSP::NoiseColor::White);

        filter_.prepare(sampleRate);
        filter_.setMode(Krate::DSP::SVFMode::Bandpass);
        filter_.setCutoff(2000.0f);
        filter_.setResonance(0.7f);

        envelope_.prepare(static_cast<float>(sampleRate));
        envelope_.setAttack(0.5f);
        envelope_.setDecay(120.0f);
        envelope_.setSustain(0.0f);
        envelope_.setRelease(60.0f);
    }

    void reset() noexcept
    {
        noise_.reset();
        filter_.reset();
        envelope_.reset();
        mix_ = 0.0f;
    }

    /// Apply normalized params to the component. Safe to call between notes.
    void configure(const NoiseLayerParams& p) noexcept
    {
        mix_ = std::clamp(p.mix, 0.0f, 1.0f);

        noise_.setColor(denormColor(p.color));

        const float cutoffHz = denormCutoff(p.cutoff);
        const float reso     = denormResonance(p.resonance);
        filter_.setCutoff(cutoffHz);
        filter_.setResonance(reso);

        const float decayMs = denormDecayMs(p.decay);
        envelope_.setAttack(0.5f);
        envelope_.setDecay(decayMs);
        envelope_.setSustain(0.0f);
        envelope_.setRelease(decayMs * 0.5f);
    }

    /// Apply already-denormalized values directly. Used by consumers that
    /// produce raw Hz / ms values (e.g., NoiseBody's mapper path). Leaves
    /// the noise color at whatever setColor() was last called with.
    void configureRaw(float cutoffHz,
                      float resonance,
                      float attackMs,
                      float decayMs,
                      float mix) noexcept
    {
        mix_ = std::clamp(mix, 0.0f, 1.0f);
        filter_.setCutoff(cutoffHz);
        filter_.setResonance(resonance);
        envelope_.setAttack(attackMs);
        envelope_.setDecay(decayMs);
        envelope_.setSustain(0.0f);
        envelope_.setRelease(decayMs * 0.5f);
    }

    /// Direct access to the underlying noise oscillator for consumers that
    /// need to override color at a finer granularity than configure() allows.
    [[nodiscard]] Krate::DSP::NoiseOscillator& oscillator() noexcept { return noise_; }

    /// Gate the envelope. Called once at noteOn after configure().
    void trigger(float /*velocity*/) noexcept
    {
        envelope_.reset();
        envelope_.gate(true);
    }

    /// Per-sample output. Returns the layer's natural amplitude (bandpass-
    /// filtered white noise shaped by ADSR). Consumers that mix the layer
    /// directly into the output bus (e.g. DrumVoice's parallel noise path)
    /// should multiply by `kStandaloneOutputGain` to calibrate the layer
    /// against the modal body's peak. NoiseBody's hybrid body retains its
    /// original balance by calling processSample() unscaled.
    [[nodiscard]] float processSample() noexcept
    {
        if (mix_ <= 0.0f) {
            (void)noise_.process();
            (void)filter_.process(0.0f);
            (void)envelope_.process();
            return 0.0f;
        }
        const float n   = noise_.process();
        const float f   = filter_.process(n);
        const float env = envelope_.process();
        return mix_ * env * f;
    }

    /// Block-rate output into `out`. Additive behavior is the caller's
    /// responsibility -- this writes (not adds). Natural amplitude; apply
    /// `kStandaloneOutputGain` at the caller side for direct-to-output paths.
    void processBlock(float* out, int numSamples) noexcept
    {
        if (mix_ <= 0.0f) {
            for (int i = 0; i < numSamples; ++i) {
                (void)noise_.process();
                (void)filter_.process(0.0f);
                (void)envelope_.process();
                out[i] = 0.0f;
            }
            return;
        }
        for (int i = 0; i < numSamples; ++i) {
            const float n   = noise_.process();
            const float f   = filter_.process(n);
            const float env = envelope_.process();
            out[i] = mix_ * env * f;
        }
    }

    [[nodiscard]] float mix() const noexcept { return mix_; }
    [[nodiscard]] bool  isActive() const noexcept { return mix_ > 0.0f && envelope_.isActive(); }

private:
    static float denormCutoff(float norm) noexcept
    {
        const float clamped = std::clamp(norm, 0.0f, 1.0f);
        constexpr float kMinHz = 40.0f;
        constexpr float kMaxHz = 18000.0f;
        const float logMin = std::log(kMinHz);
        const float logMax = std::log(kMaxHz);
        return std::exp(logMin + clamped * (logMax - logMin));
    }

    static float denormResonance(float norm) noexcept
    {
        return 0.3f + std::clamp(norm, 0.0f, 1.0f) * (5.0f - 0.3f);
    }

    static float denormDecayMs(float norm) noexcept
    {
        const float clamped = std::clamp(norm, 0.0f, 1.0f);
        constexpr float kMinMs = 20.0f;
        constexpr float kMaxMs = 2000.0f;
        const float logMin = std::log(kMinMs);
        const float logMax = std::log(kMaxMs);
        return std::exp(logMin + clamped * (logMax - logMin));
    }

    static Krate::DSP::NoiseColor denormColor(float norm) noexcept
    {
        const float c = std::clamp(norm, 0.0f, 1.0f);
        if (c < 0.25f) return Krate::DSP::NoiseColor::Brown;
        if (c < 0.55f) return Krate::DSP::NoiseColor::Pink;
        if (c < 0.80f) return Krate::DSP::NoiseColor::White;
        return Krate::DSP::NoiseColor::Violet;
    }

    // Amplitude calibration for the Phase 7 standalone-layer path in
    // DrumVoice (not used by NoiseBody's hybrid-body delegation, which keeps
    // the primitive's natural amplitude to preserve the pre-refactor mix
    // balance). The bandpass SVF + ADSR chain attenuates white noise to
    // ~0.1 RMS for typical Q values; a 3x boost lets mix=1.0 produce a
    // layer peak comparable to the modal body without exceeding the 0 dBFS
    // per-voice peak contract (SC-008).
public:
    static constexpr float kStandaloneOutputGain = 3.0f;
private:

    [[maybe_unused]] double      sampleRate_ = 44100.0;
    Krate::DSP::NoiseOscillator  noise_;
    Krate::DSP::SVF              filter_;
    Krate::DSP::ADSREnvelope     envelope_;
    float                        mix_ = 0.0f;
};

} // namespace Membrum
