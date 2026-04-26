#pragma once

// ==============================================================================
// DrumVoice -- Phase 2 refactored single drum voice
// ==============================================================================
// Signal path:
//   ExciterBank -> BodyBank -> ToneShaper -> UnnaturalZone::NonlinearCoupling
//   -> amp envelope -> level
//
// Phase 2.A default configuration (Impulse + Membrane + bypassed tone shaper +
// bypassed unnatural zone) is bit-identical to Phase 1's inline
// ImpactExciter + ModalResonatorBank path (FR-007, FR-031, FR-095).
//
// Phase 7 adds the Tone Shaper chain (Drive -> Wavefolder -> DCBlocker -> SVF)
// and the control-plane Pitch Envelope, which runs BEFORE the body each sample
// and feeds the current pitch Hz to the body's fundamental-frequency update
// path (ModalResonatorBank::updateModes or WaveguideString::setFrequency).
// Chain order is per research.md §8 (Buchla west-coast flow).
// ==============================================================================

#include "body_bank.h"
#include "bodies/membrane_mapper.h"
#include "bodies/natural_fundamental.h"
#include "bodies/shell_modes.h"
#include "body_model_type.h"
#include "click_layer.h"
#include "exciter_bank.h"
#include "exciter_type.h"
#include "noise_layer.h"
#include "tone_shaper.h"
#include "unnatural/unnatural_zone.h"
#include "voice_common_params.h"

#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/processors/modal_resonator_bank.h>
#include <krate/dsp/processors/waveguide_string.h>
#include <krate/dsp/systems/sympathetic_resonance.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Membrum {

class DrumVoice
{
public:
    DrumVoice() = default;

    /// Phase 8A.5: mode-radius scale applied on noteOff to accelerate decay
    /// via the body's damping law (mirrors STK Modal::damp()).
    /// For a typical Phase-1 default Membrane mode (radius ~ 0.99995),
    /// scaling the radius by 0.997 brings the effective t60 to ~50 ms,
    /// which keeps the pre-Phase-8A.5 "silent by 600 ms" behavioural tests
    /// passing while still letting the body's damping law drive every
    /// other part of the voice lifetime.
    static constexpr float kNoteOffDampScale = 0.997f;

    /// Stronger scale used by VoicePool's fast-release (choke / steal)
    /// path, where the voice must fade within ~5 ms to keep the residual
    /// click below -30 dBFS. Radius = R * 0.88 gives a body T60 under
    /// 1 ms on typical modes so the bank is essentially silent by the
    /// time the fast-release ramp hits its 1e-6 floor.
    static constexpr float kFastReleaseDampScale = 0.88f;

    /// Apply the fast-release damp directly (used by VoicePool fast-release
    /// path). Equivalent to noteOff() but uses a more aggressive scale.
    void fastReleaseDamp() noexcept
    {
        bodyBank_.getSharedBank().damp(kFastReleaseDampScale);
    }

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    /// Phase 1-compatible overload (voiceId defaulted to 0).
    void prepare(double sampleRate) noexcept
    {
        prepare(sampleRate, 0u);
    }

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        voiceId_    = voiceId;

        exciterBank_.prepare(sampleRate, voiceId);
        bodyBank_.prepare(sampleRate, voiceId);
        // Phase 8D: secondary bank lives parallel to the primary body bank.
        // Prepared once here; configured per-note in noteOn() from the
        // secondarySize / secondaryMaterial parameters.
        secondaryBank_.prepare(sampleRate);
        // Phase 8E: 20 ms one-pole energy-follower coefficient.
        //   tau ~ 20 ms -> alpha = exp(-1 / (0.02 * sr)).
        energyAlpha_ = std::exp(-1.0f /
            (0.02f * static_cast<float>(sampleRate)));
        toneShaper_.prepare(sampleRate);
        unnaturalZone_.prepare(sampleRate, voiceId);
        noiseLayer_.prepare(sampleRate, voiceId);
        // Switch the parallel noise layer's filter from its default
        // Bandpass to Lowpass. White noise through a Bandpass at any Q
        // produces a spectrally-peaked output centered on cutoff that
        // reads as a pitched tone (the user-reported "monotonous beep"
        // on hat / cymbal / snare hits, reproduced by
        // test_kit_switch_infinite_ring.cpp's recipe test). A Lowpass
        // with the same cutoff gives broadband-rolloff hash with no
        // spectral peak -- what real percussion noise sounds like.
        // NoiseBody's *internal* noise layer stays Bandpass (its
        // hardcoded Q ≈ 0.94 is Butterworth-flat, no peak there).
        noiseLayer_.setFilterMode(Krate::DSP::SVFMode::Lowpass);
        clickLayer_.prepare(sampleRate, voiceId);

        ampEnvelope_.prepare(static_cast<float>(sampleRate));
        // Phase 8A.5: amp envelope holds at 1.0 during the gate so the
        // body's damping law (not a fixed 200 ms AD) drives voice lifetime
        // (STK Modal / Chromaphone / Faust physmodels idiom). Attack is a
        // short click-free smoother; release stays in the pre-Phase-8A.5
        // 300 ms ballpark because pre-existing behavioural tests (ADSR
        // silent by 600 ms, peak during release > 0) require it.
        // ModalResonatorBank::damp() fires on noteOff to simultaneously
        // accelerate body decay, so damping sweeps remain audible.
        ampEnvelope_.setAttack(0.5f);     // ms: click-free ramp-up
        ampEnvelope_.setDecay(1.0f);      // ms: ~no-op since sustain = 1.0
        ampEnvelope_.setSustain(1.0f);    // hold during gate
        ampEnvelope_.setRelease(300.0f);  // ms: same as pre-Phase-8A.5
        ampEnvelope_.setVelocityScaling(true);
    }

    // ------------------------------------------------------------------
    // Note control
    // ------------------------------------------------------------------

    void noteOn(float velocity) noexcept
    {
        // FR-054: Material Morph override. When enabled, the voice uses the
        // morph's start value as the effective Material at note-on; the body
        // mapper is re-configured per block from the morph envelope via
        // refreshBodyForMaterial(). When disabled, the DrumVoice's own
        // material_ is used verbatim (Phase 1 path).
        unnaturalZone_.materialMorph.trigger();
        const float effectiveMaterial = unnaturalZone_.materialMorph.isEnabled()
            ? unnaturalZone_.materialMorph.process()
            : material_;

        // Push cached parameters into the common bundle.
        params_.material   = effectiveMaterial;
        params_.size       = size_;
        params_.decay      = decay_;
        params_.strikePos  = strikePos_;
        params_.level      = level_;
        params_.modeStretch = unnaturalZone_.getModeStretch();
        params_.decaySkew   = unnaturalZone_.getDecaySkew();
        params_.bodyDampingB1 = bodyDampingB1_;
        params_.bodyDampingB3 = bodyDampingB3_;
        params_.airLoading    = airLoading_;
        params_.modeScatter   = modeScatter_;

        // Compute and store the body's natural (Size-derived) fundamental.
        // Each body model has its own base frequency (Membrane 500 Hz, Plate
        // 800, Shell/NoiseBody 1500, String/Bell 800); the shared helper
        // keeps the formula in one place so ToneShaper's pitch-envelope
        // baseline and ModeInject's harmonic series line up with the body
        // that's actually playing. getPendingType() is correct here because
        // bodyBank_.configureForNoteOn() (called below) applies the deferred
        // swap -- so the "pending" type is the body about to be active.
        naturalFundamentalHz_ = Bodies::naturalFundamentalHz(
            bodyBank_.getPendingType(), size_);
        toneShaper_.setNaturalFundamentalHz(naturalFundamentalHz_);

        // Cache the baseline mapper result for per-sample pitch envelope updates.
        // Only used for the Membrane body in Phase 7 (other bodies just ignore
        // the per-sample fundamental updates).
        cachedMapperResult_ = Bodies::MembraneMapper::map(params_, /*pitchHz*/ 0.0f);

        // Tone shaper lifecycle (sets up filter env, pitch env gate).
        toneShaper_.noteOn(velocity);

        // Control-plane query: pitch envelope returns its initial Hz value.
        // When disabled this is naturalFundamentalHz_; when enabled it's pitchEnvStartHz_.
        const float initialPitchHz = toneShaper_.processPitchEnvelope();

        // Configure the body for this note (applies deferred body-model swap).
        // For Membrane: we pass the baseline pitchHz — the mapper currently
        // ignores this and derives f0 from size_ (unchanged Phase 1 behavior).
        // The pitch envelope sweeping is done per-sample via updateFundamental.
        bodyBank_.configureForNoteOn(params_, initialPitchHz);

        // If the pitch envelope is active AND body is Membrane, seed the
        // sharedBank with scaled frequencies matching the initial envelope Hz
        // so the note starts at the start frequency rather than the natural f0.
        if (toneShaper_.isPitchEnvActive()
            && bodyBank_.getCurrentType() == BodyModelType::Membrane)
        {
            updateMembraneFundamental(initialPitchHz);
        }

        // UnnaturalZone: set Mode Inject fundamental (per-voice f0) and
        // randomize phases by calling trigger(). NonlinearCoupling picks up
        // velocity for its cross-modal strength.
        unnaturalZone_.modeInject.setFundamentalHz(naturalFundamentalHz_);
        unnaturalZone_.modeInject.trigger();
        unnaturalZone_.nonlinearCoupling.setVelocity(velocity);
        unnaturalZone_.nonlinearCoupling.reset();

        // Trigger exciter (applies deferred exciter-type swap).
        exciterBank_.trigger(velocity);

        // Parallel noise layer (Phase 7, always-on when mix > 0).
        noiseLayer_.configure(noiseLayerParams_);
        noiseLayer_.trigger(velocity);

        // Always-on click transient (Phase 7). Fires alongside the selected
        // exciter; its output sums into the excitation path so it drives the
        // body's modal bank.
        clickLayer_.configure(clickLayerParams_);
        clickLayer_.trigger(velocity);

        // Phase 8D: configure the secondary bank for this note. Only spin it
        // up when secondaryEnabled_ >= 0.5 AND couplingStrength_ > 0 so the
        // default (disabled) path does zero extra work. We ALWAYS reset
        // the secondary bank here regardless of whether 8D is enabled,
        // because if the previous voice on this slot had 8D enabled and
        // the current voice doesn't, the disabled path won't process the
        // bank but its integrator state (residual sin/cos at the previous
        // shell's modal frequencies) is still alive in memory. Without
        // a reset that residual energy can be exposed if 8D ever toggles
        // back on, and on some configurations it can colour subsequent
        // hits via the body bank that the new voice happens to share.
        secondaryBank_.reset();
        secondaryLastOutput_ = 0.0f;
        effectiveCoupling_   = 0.0f;
        if (secondaryEnabled_ >= 0.5f && couplingStrength_ > 0.0f)
        {
            configureSecondaryBank();
            effectiveCoupling_ = stabilityClampedCoupling();
        }

        // Phase 8E: tension modulation depth scales with velocity^2 (the
        // mechanical tension variation is proportional to vibration
        // energy, which is proportional to velocity^2). Max knob 1.0 *
        // vel^2 -> 0.15 effective, capping pitch shift at ~2 semitones.
        const float vClamp = std::clamp(velocity, 0.0f, 1.0f);
        tensionAmtEffective_ =
            std::clamp(tensionModAmt_, 0.0f, 1.0f) * 0.15f * vClamp * vClamp;
        energyEnv_ = 0.0f;

        // Reset the amp envelope so its attack ramps from 0 instead of
        // from whatever sustain level the previous voice left behind.
        // ADSREnvelope's Hard retrigger calls enterAttack() which uses
        // output_ as the start point; without a reset, a slot that's
        // been re-used (voice steal / kit switch) inherits the prior
        // note's sustain output and the new note skips most of its
        // attack ramp.
        ampEnvelope_.reset();
        ampEnvelope_.setVelocity(velocity);
        ampEnvelope_.gate(true);
        velocityGain_ = std::clamp(velocity, 0.0f, 1.0f);
        silentBlockCount_ = 0;
        active_ = true;
    }

    void noteOff() noexcept
    {
        exciterBank_.release();
        toneShaper_.noteOff();
        ampEnvelope_.gate(false);
        // Phase 8A.5 (STK Modal idiom): accelerate modal decay by scaling
        // every active mode's radius. 0.993 yields ~50 ms effective release
        // @ 44.1 kHz for typical modes; the bank's flushSilentModes() then
        // retires the voice once amplitudes fall below kSilenceThreshold.
        bodyBank_.getSharedBank().damp(kNoteOffDampScale);
    }

    /// Hard wipe of every per-voice DSP state container. Used by
    /// VoicePool::resetAllVoicesForKitSwitch when the host loads a new
    /// preset, so the next noteOn against the new kit starts from a
    /// clean slate.
    ///
    /// noteOn already resets most of these per-trigger, but kit-switch
    /// is a stronger guarantee: a voice slot that's been Active without
    /// ever receiving a noteOff (typical pad-click case) still has a
    /// hot envelope and integrator state that the user can hear if the
    /// next hit lands on this slot before the body has fully decayed.
    /// This call is also safer than relying on every sub-component's
    /// reset to be exactly right: every state-bearing primitive gets
    /// touched explicitly here.
    void resetForKitSwitch() noexcept
    {
        ampEnvelope_.reset();
        bodyBank_.reset();
        secondaryBank_.reset();
        secondaryLastOutput_ = 0.0f;
        effectiveCoupling_   = 0.0f;
        exciterBank_.reset();
        noiseLayer_.reset();
        clickLayer_.reset();
        toneShaper_.reset();
        unnaturalZone_.modeInject.reset();
        unnaturalZone_.nonlinearCoupling.reset();
        // No public reset on materialMorph: it's only triggered via
        // trigger() at noteOn and (when disabled) is a no-op. Setting
        // active_ = false below ensures process() short-circuits even
        // if the morph state is non-default until the next noteOn
        // overwrites it.
        energyEnv_           = 0.0f;
        velocityGain_        = 0.0f;
        tensionAmtEffective_ = 0.0f;
        silentBlockCount_    = 0;
        active_              = false;
    }

    // ------------------------------------------------------------------
    // Processing
    // ------------------------------------------------------------------

    [[nodiscard]] float process() noexcept
    {
        if (!active_)
            return 0.0f;

        // Pitch envelope (control plane): update body fundamental BEFORE body
        // processing so the current sample reflects the new pitch. Only when
        // the pitch envelope is active — Phase 1 regression requires this path
        // to be skipped when disabled.
        if (toneShaper_.isPitchEnvActive())
        {
            const float pitchHz = toneShaper_.processPitchEnvelope();
            updateBodyFundamental(pitchHz);
        }

        // Material Morph (FR-054): per-sample refresh of the body mapper
        // material when the morph is active. Disabled path is bit-identical
        // to the Phase 1 signal (FR-055 default-off guarantee).
        if (unnaturalZone_.materialMorph.isEnabled())
        {
            const float m = unnaturalZone_.materialMorph.process();
            refreshBodyForMaterial(m);
        }

        // Exciter takes the body's last output as feedback (only FeedbackExciter
        // uses it; all other backends ignore it). The always-on click transient
        // is sampled ONCE (with the standalone amplitude calibration applied)
        // and routed two ways:
        //   (a) half-amplitude into the excitation path so it drives the body's
        //       modes (adds a ringing tail in the modal bank), and
        //   (b) full-amplitude directly into the output bus so the beater
        //       thwack itself is audible, not masked by the body's Q.
        // This matches the commuted-synthesis convention of putting
        // complexity in the excitation AND hearing the excitation itself.
        const float clickSample =
            clickLayer_.processSample() * ClickLayer::kStandaloneOutputGain;
        const float excMain = exciterBank_.process(bodyBank_.getLastOutput());
        const float exc     = excMain + clickSample * 0.5f;
        const float body    = bodyBank_.processSample(exc);

        // Parallel noise layer sums into the body output *before* the
        // UnnaturalZone chain so ModeInject / NonlinearCoupling / ToneShaper
        // treat the combined signal (research-backed SNT pattern). The click
        // layer is also mixed directly here so it's heard as a broadband
        // transient independent of the body's modal response.
        const float noiseSample =
            noiseLayer_.processSample() * NoiseLayer::kStandaloneOutputGain;
        const float combinedBody = body + noiseSample + clickSample;

        // UnnaturalZone chain per contract: body + modeInject → nonlinear
        // coupling → tone shaper → amp env × level. When all Unnatural Zone
        // defaults hold (modeInject.amount_==0 and nonlinearCoupling.amount_==0),
        // the early-out paths make this chain bit-identical to "body → shaped
        // → env×level" (FR-055).
        const float injected = combinedBody + unnaturalZone_.modeInject.process();
        const float coupled  = unnaturalZone_.nonlinearCoupling.processSample(injected);
        const float shaped   = toneShaper_.processSample(coupled);
        const float env      = ampEnvelope_.process();
        return Krate::DSP::softClip(shaped * env * level_);
    }

    // Block-level audio-thread hot path (T043 / FR-001 / FR-002 / research.md §1,
    // plus Phase 9 SIMD emergency fallback per plan.md §SIMD Emergency Fallback /
    // FR-071). Dispatches the ExciterBank and BodyBank std::variants exactly
    // ONCE per block via their withActive() / processBlock() single-visit
    // helpers.
    //
    // Two code paths:
    //   - FAST PATH: used when pitch envelope + material morph + feedback
    //     exciter are all inactive. The exciter runs per-sample into a scratch
    //     buffer, then BodyBank::processBlock() routes the modal work through
    //     ModalResonatorBank::processBlock (SIMD-accelerated via Highway),
    //     then the unnatural + tone shaper + amp env post chain runs per-sample.
    //     This is the path that brings the 144-combination CPU budget back
    //     under the 1.25% per-voice target.
    //
    //   - SLOW PATH: used when the pitch envelope is active for Membrane,
    //     or Material Morph is active (both need per-sample mapper refresh),
    //     or the active exciter is FeedbackExciter (needs strict per-sample
    //     body feedback per research.md §3). Keeps the original nested-visit
    //     per-sample inner loop for correctness.
    static constexpr int kMaxBlockSize = 2048;

    void processBlock(float* out, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        if (!active_)
        {
            for (int i = 0; i < numSamples; ++i)
                out[i] = 0.0f;
            return;
        }

        const float level = level_;
        const bool pitchEnvActive = toneShaper_.isPitchEnvActive();
        const BodyModelType currentBody = bodyBank_.getCurrentType();
        const bool pitchEnvForMembrane =
            pitchEnvActive && (currentBody == BodyModelType::Membrane);

        const bool morphActive = unnaturalZone_.materialMorph.isEnabled();
        const bool feedbackExciter =
            exciterBank_.getCurrentType() == ExciterType::Feedback;

        // Decide fast vs slow path. Only strict per-sample FeedbackExciter
        // semantics force the original nested-visit per-sample loop. Both
        // MaterialMorph and the pitch envelope run on the fast path with
        // block-rate refresh of the modal-bank coefficients — a 20 ms pitch
        // sweep @ 44.1 kHz spans ~880 samples, so a 64-sample block refresh
        // gives ~14 update points across the sweep (well below any audible
        // stair-stepping), and a 300 ms morph spans ~13 000 samples.
        const bool useSlowPath = feedbackExciter;

        if (useSlowPath)
        {
            processBlockSlow(out, numSamples, level, pitchEnvForMembrane, morphActive);
        }
        else
        {
            processBlockFast(out, numSamples, level, morphActive, pitchEnvForMembrane);
        }

        // Phase 8A.5 retirement: the amp envelope is now only a click-free
        // smoother + release curve, not a lifetime gate. The voice is
        // finished once the bank's flushSilentModes() pass (called inside
        // ModalResonatorBank::processBlock) reports zero active modes AND
        // the envelope has completed its (optional) release. This is the
        // STK / Chromaphone / Faust physmodels idiom -- the body's damping
        // law IS the decay.
        //
        // Intentionally NOT gating retirement on noiseLayer_ / clickLayer_
        // because those layers have their own short envelopes that come
        // to rest at output=0 but hold in the Sustain stage rather than
        // transitioning to Idle. Their output is zero by design once the
        // configured decay has elapsed.
        const auto& sharedBank = bodyBank_.getSharedBank();
        const bool bankSilent = sharedBank.getNumActiveModes() == 0;
        const bool envFinished = !ampEnvelope_.isActive();
        if (bankSilent && envFinished)
        {
            active_ = false;
            silentBlockCount_ = 0;
        }
        // Safety net: if the body has been silent for a while AND the
        // voice pool's auto-noteOff hasn't fired (because peak settled
        // above the silence threshold from sustained noise / click /
        // filter ringing), force-retire so we don't sit Active forever.
        // Without this the original "infinite tone" bug regresses on
        // any preset whose layered output settles above 1e-3 once the
        // body modes have died -- pad-click hosts never send noteOff,
        // so envFinished never becomes true on its own.
        if (bankSilent)
        {
            ++silentBlockCount_;
            if (silentBlockCount_ > 256)  // ≈1.4 s at 48 kHz / 256 blocks
                active_ = false;
        }
        else
        {
            silentBlockCount_ = 0;
        }
    }

private:
    // FAST PATH: Phase 9 SIMD emergency fallback. The body runs at block rate
    // through ModalResonatorBank::processBlock (SIMD via Highway). The exciter
    // runs per-sample into a scratch buffer using body feedback from the
    // previous sample (within the excitation phase) OR the last body output
    // from the previous block. This matches the research.md §3 spec for
    // feedback semantics for the 5 non-feedback exciters, which do not
    // actually consume bodyFeedback (see research.md §3 and the
    // ExciterBank::process documentation). For FeedbackExciter we use the
    // slow path instead to preserve strict per-sample semantics.
    void processBlockFast(float* out,
                          int numSamples,
                          float level,
                          bool morphActive,
                          bool pitchEnvForMembrane) noexcept
    {
        // Scratch buffers live as DrumVoice members so we don't pay a large
        // stack allocation per processBlock call (audio-thread friendly).
        float* excScratch   = excScratch_.data();
        float* bodyScratch  = bodyScratch_.data();
        float* noiseScratch = noiseScratch_.data();
        float* clickScratch = clickScratch_.data();

        int offset = 0;
        int remaining = numSamples;
        float lastBody = bodyBank_.getLastOutput();

        while (remaining > 0)
        {
            const int chunk = remaining < kMaxBlockSize ? remaining : kMaxBlockSize;

            // --- Pitch envelope block-rate refresh (Phase 9 fast path). ----
            // Advance the pitch envelope by one sample at the start of the
            // chunk and push the resulting pitch into the modal bank once.
            // A 20 ms sweep at 44.1 kHz produces ~14 refresh points across
            // a 64-sample audio block — well below any audible stair-stepping.
            // Phase 8E: block-rate pitch modulation from energy tracker.
            // Observed real-drum behaviour (Avanzini et al., "Efficient
            // synthesis of tension modulation ... based on energy
            // estimation", JASA 2012) is an upward pitch jump at the strike
            // that relaxes back as vibration energy decays. For a one-pole
            // follower fed by |out|^2 (peaks at attack, decays with the
            // body), `1 + amt * energyEnv` reproduces exactly that
            // up-then-relax curve. Kirby & Sandler JASA 2021 describes the
            // related phenomenon via DCT analysis but is not the source of
            // this implementation.
            const bool tensionActive = tensionAmtEffective_ > 1e-6f;
            const float tensionPitchMod =
                tensionActive ? (1.0f + tensionAmtEffective_ * energyEnv_)
                              : 1.0f;

            if (pitchEnvForMembrane)
            {
                const float pitchHz =
                    toneShaper_.processPitchEnvelope() * tensionPitchMod;
                // Consume the remaining (chunk-1) envelope samples without
                // reissuing updateModes, so the envelope stays in sync with
                // the audio sample count.
                for (int i = 1; i < chunk; ++i)
                    (void)toneShaper_.processPitchEnvelope();
                updateMembraneFundamentalOnBank(bodyBank_.getSharedBank(), pitchHz);
            }
            else if (tensionActive
                     && bodyBank_.getCurrentType() == BodyModelType::Membrane)
            {
                // No pitch env: apply tension mod as a direct frequency
                // scale around the body's natural fundamental.
                const float pitchHz = naturalFundamentalHz_ * tensionPitchMod;
                updateMembraneFundamentalOnBank(bodyBank_.getSharedBank(), pitchHz);
            }

            // --- MaterialMorph block-rate refresh (Phase 9 fast path). -----
            // Advance the morph envelope by one sample at the start of the
            // chunk and refresh the body mapper once. The remaining samples
            // in the chunk reuse this mapper state; per-sample resolution
            // below a single audio block is imperceptible for a morph that
            // spans hundreds of milliseconds.
            if (morphActive && bodyBank_.getCurrentType() == BodyModelType::Membrane)
            {
                const float m = unnaturalZone_.materialMorph.process();
                for (int i = 1; i < chunk; ++i)
                    (void)unnaturalZone_.materialMorph.process();
                refreshBodyForMaterialOnBank(bodyBank_.getSharedBank(), m);
            }
            else if (morphActive)
            {
                // Non-Membrane bodies: advance the counter so timing stays
                // correct but do NOT re-run the mapper (per-body refresh is
                // unsupported for Phase 2 non-Membrane bodies anyway).
                for (int i = 0; i < chunk; ++i)
                    (void)unnaturalZone_.materialMorph.process();
            }

            // --- Exciter phase: per-sample into scratch. ------------------
            // Single std::visit on the exciter for the whole chunk.
            exciterBank_.withActive([&](auto& exciter) noexcept {
                for (int i = 0; i < chunk; ++i)
                {
                    // Non-feedback exciters ignore bodyFeedback; passing
                    // lastBody here is harmless for them.
                    excScratch[i] = exciter.process(lastBody);
                }
            });

            // --- Always-on click transient: write to scratch, apply the
            // Phase 7 standalone gain, then route half-amplitude into
            // excitation (drives body modes) and full-amplitude directly
            // into the output (audible thwack) in the post chain below.
            clickLayer_.processBlock(clickScratch, chunk);
            {
                const float gain = ClickLayer::kStandaloneOutputGain;
                for (int i = 0; i < chunk; ++i)
                    clickScratch[i] *= gain;
            }
            for (int i = 0; i < chunk; ++i)
                excScratch[i] += clickScratch[i] * 0.5f;

            // --- Body phase: block-rate SIMD path. ------------------------
            // Single std::visit on the body; the body implementations route
            // to ModalResonatorBank::processBlock (SIMD) internally.
            bodyBank_.processBlock(bodyScratch, excScratch, chunk);
            lastBody = bodyScratch[chunk - 1];

            // --- Phase 8D: secondary (shell) bank with scalar coupling. --
            // Only when the secondary bank was configured at noteOn (a
            // shortcut that keeps the default path zero-overhead). The
            // coupling feedback is injected one sample late -- it reads
            // the previous-block's primary output -- which is fine for a
            // ~5-50 ms body decay at 64+ sample blocks (block-rate
            // coupling update, per plan.md D.1).
            if (effectiveCoupling_ > 0.0f)
            {
                float* secExc = secondaryExcScratch_.data();
                float* secOut = secondaryOutScratch_.data();
                // Secondary excitation = primary body samples * coupling.
                // This is feedforward (body → shell), not feedback.
                for (int i = 0; i < chunk; ++i)
                    secExc[i] = bodyScratch[i] * effectiveCoupling_;
                secondaryBank_.processBlock(secExc, secOut, chunk);
                // Mix shell output into the audible body output. This is
                // passive (additive only, no return path into the body's
                // modal excitation). The shell still rings audibly --
                // user keeps the snare/tom shell character -- but it
                // can't pump energy back into the body.
                //
                // The previous closed-loop topology (`excScratch += secOut
                // * coupling/2`) created a feedback loop body↔shell whose
                // peak loop gain at frequency-aligned modes was
                //     coupling² · 0.5 · Q_body · Q_shell
                // which with typical body Q ≈ 30 and shell Q ≈ 6 and the
                // hard-clamped max coupling of 0.25 reached ≈ 5 (≫ 1).
                // That violated the Nyquist criterion for the loop, and
                // any frequency where body and shell modes happened to
                // align became a self-sustained oscillator -- exactly
                // the user-reported "continuous beep with no envelope"
                // on bayan / dayan / ride / crash. Standard physical-
                // modelling literature (Bilbao, Karjalainen, et al.)
                // requires passive coupling for stability; we drop the
                // feedback path entirely. Shell still drives the body
                // forward (body → shell input), but shell energy can
                // only leave through the audible mix and the shell's own
                // damping law -- never back through the body bank.
                for (int i = 0; i < chunk; ++i)
                    bodyScratch[i] += secOut[i];     // mix into output (passive)
                secondaryLastOutput_ = secOut[chunk - 1];
                lastBody = bodyScratch[chunk - 1];
            }

            // --- Parallel noise layer (Phase 7). -------------------------
            // Writes to noiseScratch independently of the body, then applies
            // the Phase 7 standalone calibration gain so the layer is
            // audible at moderate mix values. Per-sample post chain sums it
            // into the body output before the UnnaturalZone chain so
            // ModeInject / NonlinearCoupling / ToneShaper treat the combined
            // signal.
            noiseLayer_.processBlock(noiseScratch, chunk);
            {
                const float gain = NoiseLayer::kStandaloneOutputGain;
                for (int i = 0; i < chunk; ++i)
                    noiseScratch[i] *= gain;
            }

            // --- Post chain: unnatural -> tone shaper -> env * level. -----
            // Hoist the modeInject / nonlinearCoupling amount==0 checks out
            // of the inner loop so the compiler can aggressively dead-code
            // eliminate the UN side of the chain when the UN zone is fully
            // off. This is the common default-off Phase 1 case (FR-055).
            const bool modeInjectActive =
                unnaturalZone_.modeInject.getAmount() != 0.0f;
            const bool couplingActive =
                unnaturalZone_.nonlinearCoupling.getAmount() != 0.0f;

            if (!modeInjectActive && !couplingActive)
            {
                // Default-off fast lane — no UN per-sample work at all.
                // combined = body + noise_layer + click_layer (direct).
                for (int i = 0; i < chunk; ++i)
                {
                    const float combined = bodyScratch[i] + noiseScratch[i] + clickScratch[i];
                    const float shaped   = toneShaper_.processSample(combined);
                    const float env      = ampEnvelope_.process();
                    out[offset + i]      = Krate::DSP::softClip(shaped * env * level);
                }
            }
            else
            {
                for (int i = 0; i < chunk; ++i)
                {
                    const float combined = bodyScratch[i] + noiseScratch[i] + clickScratch[i];
                    const float injected = combined + unnaturalZone_.modeInject.process();
                    const float coupled  =
                        unnaturalZone_.nonlinearCoupling.processSample(injected);
                    const float shaped   = toneShaper_.processSample(coupled);
                    const float env      = ampEnvelope_.process();
                    out[offset + i]      = Krate::DSP::softClip(shaped * env * level);
                }
            }

            // Phase 8E: per-sample one-pole energy follower. Runs only
            // when the tension modulator is active so the default path
            // stays zero-overhead. `energyAlpha_` is the per-sample
            // coefficient from prepare() (20 ms tau).
            if (tensionActive)
            {
                const float alpha = energyAlpha_;
                const float oneMinusAlpha = 1.0f - alpha;
                float env = energyEnv_;
                for (int i = 0; i < chunk; ++i) {
                    const float s = out[offset + i];
                    env = alpha * env + oneMinusAlpha * s * s;
                }
                energyEnv_ = env;
            }

            offset    += chunk;
            remaining -= chunk;
        }
    }

    // SLOW PATH: per-sample inner loop. Used when FeedbackExciter is the
    // active exciter (strict per-sample body-feedback semantics required per
    // research.md §3). Pitch envelope and MaterialMorph refreshes are
    // hoisted to block rate for consistency with the fast path — a 20 ms
    // pitch sweep spans ~14 update points across a 64-sample audio block
    // which is well below any audible stair-stepping.
    void processBlockSlow(float* out,
                          int numSamples,
                          float level,
                          bool pitchEnvForMembrane,
                          bool morphActive) noexcept
    {
        // Block-rate refresh (Phase 9): advance each modulator by one sample
        // at the start of the block and refresh the modal bank once. The
        // remaining modulator steps are consumed below (inside the inner
        // loop) without reissuing updateModes.
        if (pitchEnvForMembrane)
        {
            const float pitchHz = toneShaper_.processPitchEnvelope();
            updateMembraneFundamentalOnBank(bodyBank_.getSharedBank(), pitchHz);
        }
        if (morphActive && bodyBank_.getCurrentType() == BodyModelType::Membrane)
        {
            const float m = unnaturalZone_.materialMorph.process();
            refreshBodyForMaterialOnBank(bodyBank_.getSharedBank(), m);
        }

        // Nested single-visit: one std::visit on the exciter, one on the body.
        // Inside both visits we hold typed references so every sample call is
        // a direct non-virtual call — no per-sample variant dispatch.
        exciterBank_.withActive([this, out, numSamples, level, pitchEnvForMembrane, morphActive](auto& exciter) noexcept {
            bodyBank_.withActive(
                [this, out, numSamples, level, pitchEnvForMembrane, morphActive, &exciter](
                    auto& body, Krate::DSP::ModalResonatorBank& sharedBank) noexcept {
                    float lastBody = bodyBank_.getLastOutput();
                    for (int i = 0; i < numSamples; ++i)
                    {
                        // Consume the remaining modulator samples to keep the
                        // envelope counters in sync with the audio clock.
                        // The first sample's mapper refresh was done above;
                        // from i==1 onward we just tick the modulators.
                        if (pitchEnvForMembrane && i > 0)
                            (void)toneShaper_.processPitchEnvelope();
                        if (morphActive && i > 0)
                            (void)unnaturalZone_.materialMorph.process();

                        const float clickSample =
                            clickLayer_.processSample() * ClickLayer::kStandaloneOutputGain;
                        const float excMain = exciter.process(lastBody);
                        const float exc     = excMain + clickSample * 0.5f;
                        const float bodyOut = body.processSample(sharedBank, exc);
                        lastBody            = bodyOut;

                        // Parallel noise layer (Phase 7) sums into body
                        // output before the UnnaturalZone chain; click layer
                        // is mixed directly so it's heard as a transient.
                        const float noiseSample =
                            noiseLayer_.processSample() * NoiseLayer::kStandaloneOutputGain;
                        const float combinedBody = bodyOut + noiseSample + clickSample;

                        // UnnaturalZone chain per contract: body + modeInject
                        // → nonlinear coupling → tone shaper → env × level.
                        const float injected = combinedBody + unnaturalZone_.modeInject.process();
                        const float coupled  =
                            unnaturalZone_.nonlinearCoupling.processSample(injected);
                        const float shaped   = toneShaper_.processSample(coupled);
                        const float env      = ampEnvelope_.process();
                        out[i]               = Krate::DSP::softClip(shaped * env * level);
                    }
                    bodyBank_.setLastOutput(lastBody);
                });
        });
    }

public:

    /// Phase 8A.5: voice lifetime now tracks the modal bank, not the amp
    /// envelope. The bank's flushSilentModes() path (run inside
    /// processBlock) retires modes below kSilenceThreshold; this accessor
    /// reports the aggregate voice-active flag maintained by processBlock.
    [[nodiscard]] bool isActive() const noexcept { return active_; }

    // ------------------------------------------------------------------
    // Phase 1 parameter setters (unchanged API; FR-007)
    // ------------------------------------------------------------------

    void setMaterial(float v) noexcept
    {
        material_ = v;
        if (active_)
            updateModalParameters();
    }

    void setSize(float v) noexcept
    {
        size_ = v;
        if (active_)
            updateModalParameters();
    }

    void setDecay(float v) noexcept
    {
        decay_ = v;
        if (active_)
            updateModalParameters();
    }

    void setStrikePosition(float v) noexcept
    {
        strikePos_ = v;
        if (active_)
            updateModalParameters();
    }

    void setLevel(float v) noexcept { level_ = v; }

    // ------------------------------------------------------------------
    // Phase 2 setters
    // ------------------------------------------------------------------

    void setExciterType(ExciterType type) noexcept
    {
        exciterBank_.setExciterType(type);
    }

    void setBodyModel(BodyModelType type) noexcept
    {
        bodyBank_.setBodyModel(type);
    }

    /// Test-only accessor to the body bank (for verifying internal state
    /// from unit tests). Not part of the public API.
    BodyBank& getBodyBankForTest() noexcept { return bodyBank_; }

    // ------------------------------------------------------------------
    // Phase 7 setters (parallel noise layer + always-on click transient)
    // ------------------------------------------------------------------
    void setNoiseLayerMix(float v) noexcept       { noiseLayerParams_.mix       = v; }
    void setNoiseLayerCutoff(float v) noexcept    { noiseLayerParams_.cutoff    = v; }
    void setNoiseLayerResonance(float v) noexcept { noiseLayerParams_.resonance = v; }
    void setNoiseLayerDecay(float v) noexcept     { noiseLayerParams_.decay     = v; }
    void setNoiseLayerColor(float v) noexcept     { noiseLayerParams_.color     = v; }

    void setClickLayerMix(float v) noexcept        { clickLayerParams_.mix        = v; }
    void setClickLayerContactMs(float v) noexcept  { clickLayerParams_.contactMs  = v; }
    void setClickLayerBrightness(float v) noexcept { clickLayerParams_.brightness = v; }

    // ------------------------------------------------------------------
    // Phase 8A setters (per-mode damping law)
    // ------------------------------------------------------------------
    /// Set normalised [0,1] b1 override, or -1.0f to keep the legacy
    /// decay-derived value (bit-identical to Phase 1).
    void setBodyDampingB1(float v) noexcept
    {
        bodyDampingB1_ = v;
        if (active_)
            updateModalParameters();
    }

    void setBodyDampingB3(float v) noexcept
    {
        bodyDampingB3_ = v;
        if (active_)
            updateModalParameters();
    }

    // ------------------------------------------------------------------
    // Phase 8C setters (air-loading + per-mode scatter)
    // ------------------------------------------------------------------
    void setAirLoading(float v) noexcept
    {
        airLoading_ = v;
        if (active_)
            updateModalParameters();
    }

    void setModeScatter(float v) noexcept
    {
        modeScatter_ = v;
        if (active_)
            updateModalParameters();
    }

    // ------------------------------------------------------------------
    // Phase 8D setters (head <-> shell coupling)
    // ------------------------------------------------------------------
    void setCouplingStrength(float v)  noexcept { couplingStrength_  = v; }
    void setSecondaryEnabled(float v)  noexcept { secondaryEnabled_  = v; }
    void setSecondarySize(float v)     noexcept { secondarySize_     = v; }
    void setSecondaryMaterial(float v) noexcept { secondaryMaterial_ = v; }

    // ------------------------------------------------------------------
    // Phase 8E setter (nonlinear tension modulation / pitch glide)
    // ------------------------------------------------------------------
    void setTensionModAmt(float v) noexcept { tensionModAmt_ = v; }

    /// Phase 7 bug-fix: plumb PadConfig::noiseBurstDuration (normalized) into
    /// the NoiseBurstExciter so the host-side parameter finally takes effect.
    void setNoiseBurstContactMs(float v) noexcept
    {
        exciterBank_.setNoiseBurstContactMs(v);
    }

    [[nodiscard]] ToneShaper& toneShaper() noexcept { return toneShaper_; }
    [[nodiscard]] UnnaturalZone& unnaturalZone() noexcept { return unnaturalZone_; }

    /// Body-type-aware natural fundamental frequency (Hz), computed at the
    /// last noteOn(). Exposed so tests can verify that ToneShaper / ModeInject
    /// see the correct f0 for the active body model (not just the membrane
    /// value). Returns 0 before the first noteOn().
    [[nodiscard]] float getNaturalFundamentalHz() const noexcept
    {
        return naturalFundamentalHz_;
    }

    // Exposed for testing and state helpers
    [[nodiscard]] const ExciterBank& exciterBank() const noexcept { return exciterBank_; }
    [[nodiscard]] const BodyBank& bodyBank() const noexcept { return bodyBank_; }

    /// Extract the first N partial frequencies from the body's modal bank.
    /// Returns a SympatheticPartialInfo with up to kSympatheticPartialCount freqs.
    /// Used by VoicePool coupling hooks to register resonators on noteOn.
    [[nodiscard]] Krate::DSP::SympatheticPartialInfo getPartialInfo() const noexcept
    {
        Krate::DSP::SympatheticPartialInfo info{};
        const auto& bank = bodyBank_.getSharedBank();
        for (int k = 0; k < Krate::DSP::kSympatheticPartialCount; ++k)
        {
            info.frequencies[static_cast<size_t>(k)] = bank.getModeFrequency(k);
        }
        return info;
    }

private:
    /// Recompute body mapping without clearing filter state (Phase 1 behavior).
    ///
    /// Only Membrane supports mid-note parameter updates in Phase 2; other bodies
    /// recompute via setModes on noteOn only — live-update path is deferred to a
    /// future phase.
    // Phase 8D: configure the secondary bank's modes from the per-pad
    // secondarySize + secondaryMaterial. Uses a small shell-style mode
    // set (24 partials, free-free beam ratios, shell damping) at a
    // fraction of the primary body's natural fundamental.
    void configureSecondaryBank() noexcept
    {
        constexpr int kSecondaryModes = 24;
        // Secondary fundamental: slide between the head f0 (secondarySize=0)
        // and a quarter of it (secondarySize=1). Default 0.5 maps to ~0.6x
        // head f0 -- Chromaphone's "shell f0 ~0.6 f_head" rule of thumb.
        const float sizeRatio =
            1.0f - std::clamp(secondarySize_, 0.0f, 1.0f) * 0.75f;
        const float f0 = std::max(naturalFundamentalHz_, 20.0f) * sizeRatio;

        // Shell ratios (free-free Euler-Bernoulli beam) -- reuse the first
        // 24 values from shell_modes.h.
        float freqs[kSecondaryModes];
        float amps[kSecondaryModes];
        const int shellCount =
            std::min(kSecondaryModes, Bodies::kShellModeCount);
        for (int k = 0; k < shellCount; ++k)
        {
            freqs[k] = f0 * Bodies::kShellRatios[k];
            amps[k]  = std::abs(Bodies::computeShellAmplitude(k, 0.3f));
            if (amps[k] < 0.03f) amps[k] = 0.03f;
        }
        for (int k = shellCount; k < kSecondaryModes; ++k) {
            freqs[k] = 0.0f;
            amps[k]  = 0.0f;
        }

        // Shell-like damping. The earlier values (b1 = 1.5..5.5 s^-1,
        // t60 of 1.25..4.6 s) made the shell ring much longer than the
        // body it's coupled to (typical body t60 ≈ 0.3 s under the
        // default damping law). Because processBlock feeds the shell's
        // output back into the body's excitation bus, a long shell tail
        // keeps re-energising body modes that have already decayed below
        // the silence threshold -- the voice never retires and the user
        // hears a sustained sine at the shell's fundamental.
        //
        // Range b1 in [25, 40] s^-1 (t60 ≈ 0.17..0.28 s) so the shell
        // always decays faster than the bodies it's coupled to. The
        // shell still contributes audible ring during the attack but
        // won't outlast its primary. Reproduced and verified by the
        // stress test in test_kit_switch_infinite_ring.cpp.
        // Shell damping tuned to keep the body↔shell feedback loop stable.
        //
        // Damping law follows Chaigne (1993) / standard modal-synthesis
        // form: R_k = b1 + b3·f_k², where b1 sets global decay and b3
        // gives high-frequency rolloff.
        //
        // Stability requirement: at any pair of frequency-aligned body and
        // shell modes the closed-loop gain
        //     coupling² · 0.5 · Q_body · Q_shell
        // must stay below unity (Q = ω / (2·b1)). Because effectiveCoupling
        // is hard-clamped to 0.25 (max) the worst-case factor coupling²·0.5
        // is 0.03125, so we need Q_body · Q_shell < 32. Real instrument
        // research shows snare-drum shell modes at t60 ≈ 0.5..1.5 s
        // (b1 ≈ 4.6..14 s⁻¹), but those values combined with the body Q
        // produce loop gains in the hundreds -- the source of the
        // user-reported "infinite ring" stress reproduction.
        //
        // We scale the shell's b1 to be a multiple of the primary body's
        // b1 floor so shell t60 is always ≤ ~half the body's t60. With
        // typical body b1 ≈ 20 s⁻¹ and the multiplier below, shell b1
        // sits in [40, 80] s⁻¹ -- the shell still contributes audible
        // ring on the attack but cannot outlast the body it's coupled
        // to, so the feedback loop dies with the body.
        const float mat = std::clamp(secondaryMaterial_, 0.0f, 1.0f);
        // Body's effective b1 from current per-pad damping override.
        const float bodyN = std::clamp((bodyDampingB1_ >= 0.0f)
                                       ? bodyDampingB1_ : 0.4f, 0.0f, 1.0f);
        const float bodyB1 = 0.2f + bodyN * 49.8f;
        // Shell b1 must sit at least 2.5× above the body so its t60 is at
        // most ~40 % of the body's; soft floor of 25 s⁻¹ keeps the loop
        // stable even when the body is set to a low-damping (long-tail)
        // configuration.
        const float b1 = std::max(25.0f, 2.5f * bodyB1) +
                         (1.0f - mat) * 15.0f;
        const float b3 = mat * 1.0e-5f;
        const Krate::DSP::ModalResonatorBank::DampingLaw law{b1, b3};
        secondaryBank_.setModes(freqs, amps, shellCount,
                                law, /*stretch*/ 0.0f, /*scatter*/ 0.0f);
    }

    // Plan D.5 stability: keep (coupling^2 * primaryDecay * secondaryDecay)
    // below 1 so the feedback loop eigenvalue stays inside the unit circle.
    // Primary/secondary decays are bounded by ~0.99995 per-sample, so a
    // hard 0.25 ceiling is comfortably below the instability threshold.
    float stabilityClampedCoupling() const noexcept
    {
        return std::clamp(couplingStrength_, 0.0f, 1.0f) * 0.25f;
    }

    void updateModalParameters() noexcept
    {
        if (bodyBank_.getCurrentType() != BodyModelType::Membrane)
            return;

        VoiceCommonParams p{};
        p.material   = material_;
        p.size       = size_;
        p.decay      = decay_;
        p.strikePos  = strikePos_;
        p.level      = level_;
        p.modeStretch = unnaturalZone_.getModeStretch();
        p.decaySkew   = unnaturalZone_.getDecaySkew();
        p.bodyDampingB1 = bodyDampingB1_;
        p.bodyDampingB3 = bodyDampingB3_;
        p.airLoading    = airLoading_;
        p.modeScatter   = modeScatter_;

        const auto r = Bodies::MembraneMapper::map(p, /*pitchHz*/ 0.0f);
        cachedMapperResult_ = r;
        bodyBank_.getSharedBank().updateModes(
            r.frequencies, r.amplitudes, r.numPartials,
            r.damping, r.stretch, r.scatter);
    }

    /// Per-sample pitch envelope update dispatcher.
    ///
    /// Phase 2 scope: per-sample pitch envelope is only applied to Membrane
    /// because SC-009 tests only Membrane. String/Plate/Bell/Shell/NoiseBody
    /// receive the initial envelope value at noteOn via configureForNoteOn but
    /// do not track the sweep per-sample. Extending per-sample glide to
    /// WaveguideString/other bodies is a future phase deliverable.
    void updateBodyFundamental(float pitchHz) noexcept
    {
        const BodyModelType t = bodyBank_.getCurrentType();
        if (t == BodyModelType::Membrane)
        {
            updateMembraneFundamental(pitchHz);
        }
        // Other body types: no-op in Phase 7. String body would require
        // hooking into the StringBody's waveguide from here; see task T095
        // note. Phase 7 SC-009 test only uses Membrane.
    }

    /// Recompute modal bank frequencies scaled by pitchHz / naturalFundamentalHz.
    /// Uses updateModes() which preserves filter state (vs setModes() which resets).
    void updateMembraneFundamental(float pitchHz) noexcept
    {
        updateMembraneFundamentalOnBank(bodyBank_.getSharedBank(), pitchHz);
    }

    /// Same as updateMembraneFundamental() but with the sharedBank reference
    /// already resolved (for use inside the processBlock inner loop, where the
    /// bodyBank_ visitor already holds a sharedBank reference).
    void updateMembraneFundamentalOnBank(Krate::DSP::ModalResonatorBank& sharedBank,
                                         float pitchHz) noexcept
    {
        if (naturalFundamentalHz_ <= 0.0f || cachedMapperResult_.numPartials <= 0)
            return;

        const float ratio = pitchHz / naturalFundamentalHz_;
        float scaled[Bodies::MapperResult::kMaxModes];
        const int n = cachedMapperResult_.numPartials;
        for (int k = 0; k < n; ++k)
            scaled[k] = cachedMapperResult_.frequencies[k] * ratio;

        sharedBank.updateModes(
            scaled,
            cachedMapperResult_.amplitudes,
            n,
            cachedMapperResult_.damping,
            cachedMapperResult_.stretch,
            cachedMapperResult_.scatter);
    }

    /// FR-054: Material Morph refresh. Re-run the Membrane mapper with the
    /// morph's current material value and push the new coefficients into the
    /// shared bank via updateModes() (which preserves filter state). Only
    /// Membrane in Phase 2 — other bodies ignore per-sample morph updates.
    void refreshBodyForMaterial(float material) noexcept
    {
        refreshBodyForMaterialOnBank(bodyBank_.getSharedBank(), material);
    }

    void refreshBodyForMaterialOnBank(Krate::DSP::ModalResonatorBank& sharedBank,
                                      float material) noexcept
    {
        if (bodyBank_.getCurrentType() != BodyModelType::Membrane)
            return;

        VoiceCommonParams p{};
        p.material    = material;
        p.size        = size_;
        p.decay       = decay_;
        p.strikePos   = strikePos_;
        p.level       = level_;
        p.modeStretch = unnaturalZone_.getModeStretch();
        p.bodyDampingB1 = bodyDampingB1_;
        p.bodyDampingB3 = bodyDampingB3_;
        p.airLoading    = airLoading_;
        p.modeScatter   = modeScatter_;
        p.decaySkew   = unnaturalZone_.getDecaySkew();

        cachedMapperResult_ = Bodies::MembraneMapper::map(p, /*pitchHz*/ 0.0f);
        sharedBank.updateModes(
            cachedMapperResult_.frequencies,
            cachedMapperResult_.amplitudes,
            cachedMapperResult_.numPartials,
            cachedMapperResult_.damping,
            cachedMapperResult_.stretch,
            cachedMapperResult_.scatter);
    }

    // Sub-components
    ExciterBank                exciterBank_;
    BodyBank                   bodyBank_;
    ToneShaper                 toneShaper_;
    UnnaturalZone              unnaturalZone_;
    NoiseLayer                 noiseLayer_;
    ClickLayer                 clickLayer_;
    Krate::DSP::ADSREnvelope   ampEnvelope_;

    // Scratch buffers for the fast block-rate path. Kept as members so each
    // processBlock call avoids the 16 KB stack footprint of 2048-sample
    // float arrays. Audio-thread safe (no allocation, pre-sized).
    std::array<float, kMaxBlockSize> excScratch_{};
    std::array<float, kMaxBlockSize> bodyScratch_{};
    std::array<float, kMaxBlockSize> noiseScratch_{};
    std::array<float, kMaxBlockSize> clickScratch_{};
    // Phase 8D: secondary bank (head <-> shell coupling). Separate scratch
    // holds the secondary excitation and output for the fast-path's
    // block-rate coupling loop.
    std::array<float, kMaxBlockSize> secondaryExcScratch_{};
    std::array<float, kMaxBlockSize> secondaryOutScratch_{};
    Krate::DSP::ModalResonatorBank   secondaryBank_{};
    float secondaryLastOutput_ = 0.0f;

    // Phase 7 per-voice layer params (applied at noteOn).
    NoiseLayerParams noiseLayerParams_{};
    ClickLayerParams clickLayerParams_{};

    // Reusable parameter bundle (populated on every noteOn to avoid alloc).
    VoiceCommonParams params_{};

    // Cached baseline mapper result (used for per-sample pitch env scaling).
    Bodies::MapperResult cachedMapperResult_{};

    // Natural (Size-derived) body fundamental in Hz, computed at noteOn.
    float naturalFundamentalHz_ = 0.0f;

    // Cached Phase 1 parameters (normalized 0-1)
    float material_  = 0.5f;
    float size_      = 0.5f;
    float decay_     = 0.3f;
    float strikePos_ = 0.3f;
    float level_     = 0.8f;

    // Phase 8A.5: velocity scaling applied at the output (previously handled
    // by the amp envelope's peakLevel_). Captured at each noteOn.
    float velocityGain_ = 1.0f;

    // Phase 8A: per-mode damping law overrides (-1.0f = use legacy derivation).
    float bodyDampingB1_ = -1.0f;
    float bodyDampingB3_ = -1.0f;

    // Phase 8C: air-loading + per-mode scatter.
    float airLoading_  = 0.0f;
    float modeScatter_ = 0.0f;

    // Phase 8D: head <-> shell coupling.
    float couplingStrength_   = 0.0f;
    float secondaryEnabled_   = 0.0f;  // 0 = off, >= 0.5 = on
    float secondarySize_      = 0.5f;
    float secondaryMaterial_  = 0.4f;
    // Cached at noteOn: effective coupling after stability clamp.
    float effectiveCoupling_  = 0.0f;

    // Phase 8E: nonlinear tension modulation (energy-dependent pitch
    // glide, Avanzini et al. JASA 2012 "Efficient synthesis of tension
    // modulation in strings and membranes based on energy estimation").
    float tensionModAmt_        = 0.0f;  // per-pad depth (norm)
    float tensionAmtEffective_  = 0.0f;  // depth * velocity^2
    float energyEnv_            = 0.0f;  // one-pole energy follower
    float energyAlpha_          = 0.0f;  // block-rate smoothing coeff

    // Voice identity
    std::uint32_t voiceId_    = 0;
    double        sampleRate_ = 0.0;

    // State
    bool active_ = false;
    /// Per-block counter incremented while bankSilent is true; cleared
    /// otherwise. Used by the retirement safety-net so a voice whose
    /// output peak settles above the voice-pool silence threshold (and
    /// therefore never gets auto-noteOff'd) still retires after a few
    /// blocks of body-modal silence.
    int silentBlockCount_ = 0;
};

} // namespace Membrum
