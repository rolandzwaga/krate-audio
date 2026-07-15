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

    /// Override the filter mode. Default after prepare() is Bandpass; the
    /// parallel layer in DrumVoice switches to Lowpass for cymbal/hat
    /// presets where a BP+white-noise output reads as a pitched tone at
    /// the cutoff -- Lowpass has no spectral peak, so the output is
    /// broadband cymbal hash without coloration.
    void setFilterMode(Krate::DSP::SVFMode mode) noexcept
    {
        filter_.setMode(mode);
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
        standaloneGain_ = computeStandaloneGain(cutoffHz);
        bloomActive_    = false;  // configure() disables bloom (bit-identical)

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
        standaloneGain_ = computeStandaloneGain(cutoffHz);
        bloomActive_    = false;  // configureRaw() disables bloom (bit-identical)
        envelope_.setAttack(attackMs);
        envelope_.setDecay(decayMs);
        envelope_.setSustain(0.0f);
        envelope_.setRelease(decayMs * 0.5f);
    }

    /// Optional cutoff-envelope "bloom" (CRASH-REDESIGN-PLAN.md Phase 3).
    /// The lowpass cutoff starts dark at `startHz`, rises toward `peakHz` with
    /// time-constant `riseMs`, then falls toward `endHz` with `fallMs`. This
    /// emulates the cymbal wave-turbulence energy cascade at filter level: the
    /// early wash is DARK, brightness BUILDS over the first tens of ms (delayed
    /// HF onset), then the tail darkens again. Call AFTER configure()/
    /// configureRaw() (which disable bloom). trigger() re-arms it from startHz.
    void configureBloom(float startHz, float peakHz, float endHz,
                        float riseMs, float fallMs) noexcept
    {
        bloomActive_  = true;
        bloomStartHz_ = startHz;
        bloomPeakHz_  = peakHz;
        bloomEndHz_   = endHz;
        // One-pole step coefficients evaluated once per kBloomUpdateInterval
        // samples. tau in samples = ms * sr / 1000; coef = 1 - exp(-hop/tau).
        bloomRiseCoef_ = bloomCoef(riseMs);
        bloomFallCoef_ = bloomCoef(fallMs);
        bloomFcCur_   = startHz;
        bloomPhase_   = 0;       // 0 = rising, 1 = falling
        bloomCounter_ = 0;
        filter_.setCutoff(startHz);
    }

    /// Denormalize a [0,1] cutoff value to Hz using the layer's own log map.
    /// Exposed so consumers (DrumVoice) can compute bloom bounds relative to
    /// the same cutoff the layer will use.
    [[nodiscard]] static float cutoffHzFromNorm(float norm) noexcept
    {
        return denormCutoff(norm);
    }

    /// Direct access to the underlying noise oscillator for consumers that
    /// need to override color at a finer granularity than configure() allows.
    [[nodiscard]] Krate::DSP::NoiseOscillator& oscillator() noexcept { return noise_; }

    /// Gate the envelope. Called once at noteOn after configure().
    ///
    /// Also re-seeds the internal PRNG and resets the bandpass filter so a
    /// reused voice (after steal/choke) produces a bit-identical output to a
    /// pristine voice at the same slot (FR-124 bit-identity).
    void trigger(float /*velocity*/) noexcept
    {
        noise_.reset();
        filter_.reset();
        envelope_.reset();
        envelope_.gate(true);
        if (bloomActive_) {
            bloomFcCur_   = bloomStartHz_;
            bloomPhase_   = 0;
            bloomCounter_ = 0;
            filter_.setCutoff(bloomStartHz_);
        }
    }

    /// Per-sample output. Returns the layer's natural amplitude (bandpass-
    /// filtered white noise shaped by ADSR). Consumers that mix the layer
    /// directly into the output bus (e.g. DrumVoice's parallel noise path)
    /// should multiply by `kStandaloneOutputGain` to calibrate the layer
    /// against the modal body's peak. NoiseBody's hybrid body retains its
    /// original balance by calling processSample() unscaled.
    [[nodiscard]] float processSample() noexcept
    {
        if (bloomActive_) stepBloom();
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
                if (bloomActive_) stepBloom();
                (void)noise_.process();
                (void)filter_.process(0.0f);
                (void)envelope_.process();
                out[i] = 0.0f;
            }
            return;
        }
        for (int i = 0; i < numSamples; ++i) {
            if (bloomActive_) stepBloom();
            const float n   = noise_.process();
            const float f   = filter_.process(n);
            const float env = envelope_.process();
            out[i] = mix_ * env * f;
        }
    }

    [[nodiscard]] float mix() const noexcept { return mix_; }
    [[nodiscard]] bool  isActive() const noexcept { return mix_ > 0.0f && envelope_.isActive(); }

    /// Cutoff-compensated standalone gain for the direct-to-output path in
    /// DrumVoice. kStandaloneOutputGain was calibrated at a single cutoff
    /// (norm 0.5, ~849 Hz); a 2nd-order lowpass passes white-noise energy
    /// roughly proportional to bandwidth, so a preset running a higher cutoff
    /// (e.g. the snare's 0.72 -> ~3.2 kHz) ships several dB hotter than the
    /// calibration assumed. This returns the gain that holds the layer at its
    /// calibrated ~-18 dBFS peak across cutoffs. Computed once per configure()
    /// (control-rate), so the audio hot loop just reads the cached value.
    [[nodiscard]] float standaloneGain() const noexcept { return standaloneGain_; }

private:
    /// Samples between bloom cutoff updates. Control-rate (0.33 ms @ 48 kHz),
    /// far below audibility for a 30+ ms sweep, so the audio hot loop only
    /// touches the SVF cutoff once per 16 samples.
    static constexpr int kBloomUpdateInterval = 16;

    /// One-pole step coefficient for a `ms` time-constant, evaluated once per
    /// kBloomUpdateInterval-sample hop. Clamped to (0, 1].
    [[nodiscard]] float bloomCoef(float ms) const noexcept
    {
        const float tauSamples = std::max(1.0f,
            ms * 1e-3f * static_cast<float>(sampleRate_));
        const float hop = static_cast<float>(kBloomUpdateInterval);
        return std::clamp(1.0f - std::exp(-hop / tauSamples), 1e-4f, 1.0f);
    }

    /// Advance the bloom cutoff one sample. Every kBloomUpdateInterval samples
    /// it one-poles the cutoff toward the phase target (peak while rising, end
    /// while falling) and writes the SVF cutoff. Switches rise->fall once the
    /// cutoff is within 2 % of the peak.
    void stepBloom() noexcept
    {
        if (--bloomCounter_ > 0) return;
        bloomCounter_ = kBloomUpdateInterval;
        if (bloomPhase_ == 0) {
            bloomFcCur_ += (bloomPeakHz_ - bloomFcCur_) * bloomRiseCoef_;
            if (bloomFcCur_ >= bloomPeakHz_ * 0.98f)
                bloomPhase_ = 1;
        } else {
            bloomFcCur_ += (bloomEndHz_ - bloomFcCur_) * bloomFallCoef_;
        }
        filter_.setCutoff(bloomFcCur_);
    }

    /// Reference cutoff the kStandaloneOutputGain constant was measured at
    /// (NoiseLayerParams::cutoff == 0.5 -> ~849 Hz).
    [[nodiscard]] static float calibrationCutoffHz() noexcept { return denormCutoff(0.5f); }

    /// kStandaloneOutputGain * sqrt(fcRef / fc), clamped to +/-12 dB so the
    /// square-root bandwidth law can't blow up at the extreme cutoff ends.
    [[nodiscard]] static float computeStandaloneGain(float cutoffHz) noexcept
    {
        const float fc    = std::max(cutoffHz, 1.0f);
        const float ratio = std::clamp(std::sqrt(calibrationCutoffHz() / fc),
                                       0.25f, 4.0f);
        return kStandaloneOutputGain * ratio;
    }

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

    // Amplitude calibration for the standalone-layer path in DrumVoice (not
    // used by NoiseBody's hybrid-body delegation, which keeps the primitive's
    // natural amplitude). Gain-staging Step 4 (H-2): the layer is a transient
    // ACCENT and must sit ~6 dB UNDER the modal body's strike-peak budget
    // (kBodyHeadroom = -6 dBFS), i.e. a ~-18 dBFS layer peak at mix=1.0 AND at
    // the reference cutoff (norm 0.5). This constant is the reference-cutoff
    // calibration; standaloneGain() applies the per-preset cutoff correction on
    // top of it. Verified by gain_staging_balance_test.cpp.
public:
    static constexpr float kStandaloneOutputGain = 0.243f;  // raw peak ~0.518 -> ~-18 dBFS @ fcRef
private:

    [[maybe_unused]] double      sampleRate_ = 44100.0;
    Krate::DSP::NoiseOscillator  noise_;
    Krate::DSP::SVF              filter_;
    Krate::DSP::ADSREnvelope     envelope_;
    float                        mix_ = 0.0f;
    // Cutoff-compensated standalone gain, refreshed by configure()/configureRaw().
    float                        standaloneGain_ = kStandaloneOutputGain;

    // Bloom (cutoff-envelope) state. Inactive by default -> bit-identical.
    bool  bloomActive_   = false;
    int   bloomPhase_    = 0;      // 0 = rising to peak, 1 = falling to end
    int   bloomCounter_  = 0;
    float bloomFcCur_    = 0.0f;
    float bloomStartHz_  = 0.0f;
    float bloomPeakHz_   = 0.0f;
    float bloomEndHz_    = 0.0f;
    float bloomRiseCoef_ = 0.0f;
    float bloomFallCoef_ = 0.0f;
};

} // namespace Membrum
