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
// and the control-plane Pitch Envelope, which runs BEFORE the body on a fixed
// 32-sample voice-time grid (T1.11, kControlGridSamples) and feeds the current
// pitch Hz to the body's fundamental-frequency update
// path (ModalResonatorBank::updateModes or WaveguideString::setFrequency).
// Chain order is per research.md §8 (Buchla west-coast flow).
// ==============================================================================

#include "body_bank.h"
#include "bodies/bell_mapper.h"
#include "bodies/membrane_mapper.h"
#include "bodies/natural_fundamental.h"
#include "bodies/noise_body_mapper.h"
#include "bodies/plate_mapper.h"
#include "bodies/shell_mapper.h"
#include "bodies/shell_modes.h"
#include "bodies/string_mapper.h"
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
#include <numbers>

namespace Membrum {

/// Strike-normalisation output gains of one note: the primary body bank and
/// the secondary (shell) bank. Measured by DrumVoice's probe; VoicePool keeps
/// one per pad (StrikeNormTable) and supplies it at note-on.
struct StrikeNormGains
{
    float body  = 1.0f;
    float shell = 1.0f;
};

class DrumVoice
{
public:
    DrumVoice() = default;

    /// Phase 8A.5 / T1.10: release decay rate (s^-1) the modal bank adds to
    /// every mode's damping law on noteOff, so the body finishes through its
    /// own law (STK Modal::damp() idiom). 132.5 s^-1 = -ln(0.997) x 44100,
    /// the former per-sample 0.997 radius scale at 44.1 kHz: an extra T60 of
    /// ln(1000) / 132.5 = 52 ms. It is a rate in seconds, so the release
    /// takes the same time at every sample rate (MB-012), and the bank keeps
    /// it through every retune (MB-269).
    static constexpr float kNoteOffReleaseRate = 132.5f;

    /// Stronger release rate (s^-1) for VoicePool's fast-release (choke /
    /// steal) path, where the voice must fade within ~5 ms to keep the
    /// residual click below -30 dBFS. 5637 s^-1 = -ln(0.88) x 44100 (the
    /// former 0.88 per-sample scale at 44.1 kHz): an extra T60 of 1.2 ms at
    /// every sample rate, so the bank is essentially silent by the time the
    /// fast-release ramp hits its 1e-6 floor.
    static constexpr float kFastReleaseRate = 5637.0f;

    /// Audit N-1: per-voice body STRIKE-peak budget. The measured-strike
    /// normalisation (see measuredStrikeOutputGain) trims the body so that a
    /// canonical hard strike peaks at this level. Set to the field-standard
    /// per-element peak of -6 dBFS: drums have a 15-20 dB crest factor, so the
    /// transient PEAK must sit near the top for the RMS to land at a musical
    /// ~-18 to -24 dBFS; a -12 dBFS peak budget left the body ~15 dB too quiet.
    /// The per-voice hardClip rail stays disengaged (0.5 << 1.0) and the
    /// main-bus -1 dBTP true-peak limiter owns the N-voice coincident sum.
    /// Final loudness make-up is master gain + per-pad Level (Phase-4 tuning),
    /// NOT this budget. (Was 0.25 / -12 dBFS, an impulse-peak budget mis-applied
    /// as a strike budget.)
    static constexpr float kBodyHeadroom = 0.5f;  // -6 dBFS strike-peak budget

    /// Audit N-1: tension-modulation energy scale. `tensionPitchMod = 1 +
    /// (knob * this) * modalEnergy`, with modalEnergy from the bank's
    /// gain-invariant getModalEnergy(). Calibrated so a full-knob hard hit
    /// glides ~1-2 semitones at the modal-energy peak (see
    /// test_per_mode_damping "Tom tension modulation"). Replaces the old
    /// 0.15 * velocity^2 coefficient that was applied to output energy.
    /// (Modal energy for a hard tom hit peaks ~2.4e3, so this scale yields
    /// ~1.5 semitones; the glide is clamped below for runaway energy.)
    static constexpr float kTensionEnergyScale = 3.3e-5f;

    /// Audit N-1: hard ceiling on the tension pitch-glide factor (~3 semitones)
    /// so a pathologically hot strike cannot drive the fundamental into a
    /// non-physical sweep. Real membranes saturate; the linear energy law only
    /// holds for moderate amplitudes (Avanzini-Marogna-Bank).
    static constexpr float kTensionPitchModMax = 1.19f;  // ~+3 semitones

    /// T1.11 (MB-008, MB-026, MB-151, MB-159, MB-237): the pitch envelope,
    /// tension glide, Material Morph and the wire-coupling energy follower run
    /// on a fixed grid of voice time: a control step at every sample where
    /// voiceSampleCount_ % kControlGridSamples == 0, whatever the host block
    /// size. Each step that retunes the modal bank snaps its coefficients, so
    /// the bank's per-call smoothing never turns the sweep into a lag of
    /// 2 ms x host block. 32 samples is 0.73 ms at 44.1 kHz, the decimation
    /// Marogna, Avanzini & Bank use for tension modulation (DAFx-10).
    static constexpr int kControlGridSamples = 32;

    /// T1.13 (MB-052): the voice's auto-release. Once the voice output (after
    /// level, before pan: what VoicePool used to measure) has stayed below
    /// kAutoReleaseThreshold (-60 dBFS) for kAutoReleaseHoldMs, the voice
    /// calls noteOff() at the next point of the kControlGridSamples grid, but
    /// never before kAutoReleaseGraceMs of voice time. The pool used to test
    /// the peak of each host block, so the release depended on the buffer
    /// size and a 1-sample host released every hit during its attack. The
    /// hold is the low end of the 20-50 ms range (user ruling 2026-11-24):
    /// 50 ms delayed the release of long tails, such as Orchestral pad 23,
    /// past their -80 dBFS window in test_kit_switch_infinite_ring.cpp.
    static constexpr float kAutoReleaseThreshold = 1.0e-3f;
    static constexpr float kAutoReleaseGraceMs   = 10.0f;
    static constexpr float kAutoReleaseHoldMs    = 20.0f;

    /// T1.11: the Material Morph's dirty-gate step (material units, range
    /// [0, 1]). A control step re-runs the body mapper only once the morph
    /// has moved this far since its last push, or has reached its end value:
    /// 1/256 of the range, inaudible as a step, and it keeps a morph's
    /// mapper cost below the pre-T1.11 one-push-per-host-block rate.
    static constexpr float kMorphDirtyStep = 1.0f / 256.0f;

    /// Apply the fast-release damping directly (used by VoicePool fast-release
    /// path). Like noteOff()'s body release, with a much faster rate.
    void fastReleaseDamp() noexcept
    {
        bodyBank_.getSharedBank().raiseReleaseDecayRate(kFastReleaseRate);
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
        // llround: at 48 kHz the product is an exact integer that the double
        // may land an ulp below.
        safetyNetSamples_ =
            static_cast<std::int64_t>(std::llround(kSafetyNetSeconds * sampleRate));
        autoReleaseGraceSamples_ = static_cast<int>(
            std::lround(static_cast<double>(kAutoReleaseGraceMs) * 0.001 * sampleRate));
        autoReleaseHoldSamples_ = static_cast<int>(
            std::lround(static_cast<double>(kAutoReleaseHoldMs) * 0.001 * sampleRate));

        exciterBank_.prepare(sampleRate, voiceId);
        bodyBank_.prepare(sampleRate, voiceId);
        // Phase 8D: secondary bank lives parallel to the primary body bank.
        // Prepared once here; configured per-note in noteOn() from the
        // secondarySize / secondaryMaterial parameters.
        secondaryBank_.prepare(sampleRate);
        // Phase 8E: 20 ms one-pole energy-follower coefficient.
        //   tau ~ 20 ms -> alpha = exp(-1 / (0.02 * sr)) per sample, raised to
        //   the control grid (T1.11): the follower steps once per grid point.
        const float energyAlpha = std::exp(-1.0f /
            (0.02f * static_cast<float>(sampleRate)));
        energyAlphaGrid_ =
            std::pow(energyAlpha, static_cast<float>(kControlGridSamples));
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

        // MB-032: a gain measured at the previous rate is wrong at this one.
        invalidateStrikeNormCache();
    }

    /// Forget the standalone strike-norm cache, so the next noteOn re-measures.
    void invalidateStrikeNormCache() noexcept { strikeNormKey_ = StrikeNormKey{}; }

    /// One-shot: the next noteOn uses these strike-norm gains instead of
    /// measuring (VoicePool supplies its per-pad table entry). Cleared by noteOn.
    void setSuppliedStrikeNormGains(float body, float shell) noexcept
    {
        suppliedBodyGain_  = body;
        suppliedShellGain_ = shell;
        gainsSupplied_     = true;
    }

    /// The strike-norm gains the last noteOn applied (body, shell). Shell is
    /// 1.0 when the note did not engage the secondary bank.
    [[nodiscard]] StrikeNormGains lastStrikeNormGains() const noexcept
    {
        return {.body = usedBodyGain_, .shell = usedShellGain_};
    }

    // ------------------------------------------------------------------
    // Note control
    // ------------------------------------------------------------------

    void noteOn(float velocity) noexcept
    {
        // FR-054: Material Morph override. When enabled, the voice uses the
        // morph's start value as the effective Material at note-on; the body
        // mapper is re-configured on the control grid from the morph envelope
        // via controlStep(). When disabled, the DrumVoice's own
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
        // Phase 5: cap NoiseBody modes on the FeedbackExciter slow path (which
        // pays per-sample coefficient smoothing) so the dense 64-mode crash
        // cloud doesn't blow the CPU budget on that one costly combination.
        params_.maxModes =
            (exciterBank_.getPendingType() == ExciterType::Feedback)
            ? Bodies::NoiseBodyMapper::kFeedbackModeCap : 0;

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

        // Cache the baseline mapper result for control-grid pitch-envelope and
        // Material-Morph updates. Audit H-3/M-1: this must reflect the body
        // that's ACTUALLY about to play (getPendingType() -- the deferred swap
        // applies inside configureForNoteOn() below), so the pitch-envelope
        // scaling and morph refresh operate on the correct modal spectrum for
        // every modal body, not just Membrane. (String ignores this; its pitch
        // and material run through the waveguide setters instead.)
        cachedMapperResult_ =
            mapModalBody(bodyBank_.getPendingType(), params_, /*pitchHz*/ 0.0f);

        // Tone shaper lifecycle (sets up filter env, pitch env gate).
        toneShaper_.noteOn(velocity);

        // Control-plane query: pitch envelope returns its initial Hz value.
        // When disabled this is naturalFundamentalHz_; when enabled it's pitchEnvStartHz_.
        const float initialPitchHz = toneShaper_.processPitchEnvelope();

        // Phase 11 (spec 145): configure the modal bank's output stage based on
        // the exciter the next note will use. FeedbackExciter closes a
        // body -> exciter loop and is the only documented passivity-loss path,
        // so it keeps the +/-0.707 soft-clip safety net. Every other exciter
        // gets the field-standard transparent path -- 1/N summation
        // normalisation only -- so per-mode damping modulation (Material,
        // decay, body-damping b1/b3) propagates audibly. See research
        // citations: Faust physmodels.lib :> /(nModes); STK Modal (no clip);
        // Bilbao NSS Chapter 5; Aramaki & Kronland-Martinet on material-as-
        // damping; AAS Chromaphone Material/Tone split.
        // Configure the body for this note (applies deferred body-model swap).
        // For Membrane: we pass the baseline pitchHz — the mapper currently
        // ignores this and derives f0 from size_ (unchanged Phase 1 behavior).
        // The pitch envelope sweep runs on the control grid (controlStep).
        bodyBank_.configureForNoteOn(params_, initialPitchHz);

        // Audit H-3: if the pitch envelope is active, seed the body with the
        // initial envelope Hz so the note starts at the START frequency rather
        // than the body's natural f0 -- for EVERY body type, not just Membrane.
        // retuneFundamental() scales the modal bank (Membrane/Plate/Shell/Bell/
        // NoiseBody) or retunes the String waveguide as appropriate. T1.11
        // (MB-237, MB-026): snap the bank onto the seeded targets, or the start
        // pitch only becomes a target the bank never reaches. setModes just
        // cleared the states, so the snap (inside pushModes) is click-free,
        // and the strike-norm probe below measures the bank at the pitch the
        // note starts at.
        voiceSampleCount_  = 0;
        quietRunSamples_   = 0;      // T1.13: re-arm the auto-release
        releaseLatched_    = false;
        lastPushedPitchHz_ = initialPitchHz;
        lastPushedMorph_   = effectiveMaterial;
        currentPitchRatio_ = 1.0f;
        const bool seedPitch = toneShaper_.isPitchEnvActive();
        if (seedPitch)
            retuneFundamental(initialPitchHz);

        // D4 (06-orchestralKit-fix-plan.md): mallet contact time scales with
        // the struck body -- crotale-sized bodies get ~1 ms contacts, timpani
        // keep the long soft thump. Cached in the bank, so it survives the
        // deferred exciter swap inside trigger(). Set BEFORE the strike-norm
        // probe below, so the probe strikes with this pad's own contact (it
        // used to run with the previous note's hint: MB-003).
        exciterBank_.setMalletBodySizeHint(size_);

        {
            // Phase 11 (spec 145): drop the modal bank's output soft-clip
            // entirely for non-feedback voices, and pre-scale the bank
            // output by 1/sqrt(N) so the summed mode peak fits below
            // unity for typical body presets. The 1/sqrt(N) is the
            // energy-equalisation norm; the peak of N initially-in-phase
            // modes with amplitudes summing to S has expected unit-impulse
            // peak ~= S, which 1/sqrt(N) shapes back to roughly the body's
            // unsaturated-RMS level (matches the steady-state RMS sum of
            // uncorrelated mode contributions). Removing the clip is the
            // field-standard approach (Faust physmodels.lib `:> /(nModes)`,
            // STK Modal, Bilbao NSS): per-mode IIR poles are strictly
            // |R| < 1 so the bank is BIBO-safe; the per-sample tanh at
            // -3 dBFS was gain-staging masquerading as a stability
            // safeguard. Downstream `softClip(shaped * env * level)` in
            // this voice's post-chain still bounds the final voice output
            // to [-1, 1] for the audio bus.
            //
            // FeedbackExciter loops body -> exciter and IS a documented
            // passivity-loss path (Karjalainen et al.), so it keeps the
            // legacy -3 dBFS safety clip. No gain scaling for that path
            // either -- the loop's own gain budget is calibrated against
            // the clipped output.
            const bool feedbackExciter =
                exciterBank_.getPendingType() == ExciterType::Feedback;
            auto& bank = bodyBank_.getSharedBank();
            if (feedbackExciter)
            {
                bank.setOutputGain(1.0f);
                bank.setOutputSoftClipThreshold(0.707f);
                usedBodyGain_ = 1.0f;
            }
            else if (bodyBank_.getCurrentType() == BodyModelType::String)
            {
                // String runs a WaveguideString, not the shared modal bank, so
                // the bank's output gain never reaches the audio path. Keep the
                // cheap impulse-bound norm (harmless; no modes are configured,
                // getInputGainSum() returns 1.0).
                usedBodyGain_ = kBodyHeadroom / bank.getInputGainSum();
                bank.setOutputGain(usedBodyGain_);
                bank.setOutputSoftClipThreshold(0.0f);
            }
            else
            {
                // Audit N-1: bound the body to its budget against the REALISED
                // resonant-strike peak, not the t=0 in-phase impulse bound
                // (getInputGainSum). A bank driven by a multi-sample strike
                // rings UP -- its real peak exceeds Sum|a_k| by ~3-18x, and the
                // over-run is NOT a per-body constant (it is dominated by Size:
                // larger body -> lower f0 -> weaker b3*f^2 damping -> more
                // ring-up). So a precomputed scalar, a pure constant-peak-gain
                // (2/(1-R^2)) norm, and a runtime peak follower are all wrong
                // (see AUDIT-signal-path-2026-06-07.md N-1 design decision +
                // J.O. Smith / Steiglitz constant-peak-gain resonator). Instead
                // we measure the configured bank's peak response to a canonical
                // strike once per pad config and normalise kBodyHeadroom to it.
                // In the plugin, VoicePool's StrikeNormTable measures it on a
                // probe voice (in prepare, or on a worker after an edit) and
                // supplies the per-pad gain here, so this note-on does not
                // render. A DrumVoice used on its own measures here, cached on
                // every input that changes the strike (MB-003 / MB-021).
                const float g = gainsSupplied_
                    ? suppliedBodyGain_
                    : measuredStrikeOutputGain(bank, params_,
                                               bodyBank_.getCurrentType(),
                                               exciterBank_.getPendingType(),
                                               seedPitch ? initialPitchHz : 0.0f);
                bank.setOutputGain(g);
                bank.setOutputSoftClipThreshold(0.0f);
                usedBodyGain_ = g;
            }
            // NOTE: the secondary (shell) bank's output gain is set AFTER
            // configureSecondaryBank() below, once its modes (and hence
            // getInputGainSum()) are configured for THIS note -- setting it
            // here would use the previous note's mode profile.
        }

        // UnnaturalZone: set Mode Inject fundamental (per-voice f0) and
        // randomize phases by calling trigger(). NonlinearCoupling picks up
        // velocity for its cross-modal strength.
        unnaturalZone_.modeInject.setFundamentalHz(naturalFundamentalHz_);
        // D2 (06-orchestralKit-fix-plan.md): tie the inject decay envelope to
        // the pad's decay so the synthetic series dies WITH the drum instead
        // of ringing as an undamped plateau. Perceptual curve, not physical:
        // decay 0 -> ~0.3 s, decay 1 -> ~5 s.
        unnaturalZone_.modeInject.setDecaySeconds(0.3f + 4.7f * decay_ * decay_);
        unnaturalZone_.modeInject.trigger();
        unnaturalZone_.nonlinearCoupling.setVelocity(velocity);
        unnaturalZone_.nonlinearCoupling.reset();

        // Trigger exciter (applies deferred exciter-type swap).
        exciterBank_.trigger(velocity);

        // Parallel noise layer (Phase 7, always-on when mix > 0).
        noiseLayer_.configure(noiseLayerParams_);
        // Crash "bloom" (CRASH-REDESIGN-PLAN.md Phase 3): when NonlinearCoupling
        // is engaged (cymbals), sweep the parallel noise cutoff DARK -> BRIGHT
        // -> darker so the wash's HF energy builds over the first tens of ms
        // (the wave-turbulence cascade) instead of being brightest at the hit.
        // Gated on coupling > 0, so hats/toms (coupling == 0) are bit-identical.
        // Bloom is a CYMBAL-WASH feature: gate it on the NoiseBody body (the
        // cymbal/hat family) AND coupling > 0. This keeps it off the snare,
        // which is a Membrane body that also uses NonlinearCoupling (for its
        // attack bloom) but whose bright wire buzz must NOT be filter-swept.
        const float nlcAmt = unnaturalZone_.nonlinearCoupling.getAmount();
        if (nlcAmt > 0.0f &&
            bodyBank_.getCurrentType() == BodyModelType::NoiseBody) {
            const float baseHz  = NoiseLayer::cutoffHzFromNorm(noiseLayerParams_.cutoff);
            const float startHz = baseHz * 0.35f;                 // dark at contact
            // Peak brightness scales with velocity: a soft tap blooms darker and
            // less (bell-like), a hard hit blooms bright and broadband
            // (Stowell tap=bell / hard=broadband).
            const float peakHz  = std::clamp(
                baseHz * (1.0f + 1.2f * nlcAmt) * (0.45f + 0.55f * velocity),
                1500.0f, 16000.0f);
            const float endHz   = baseHz * 0.55f;                 // darkening tail
            const float riseMs  = 140.0f - 90.0f * velocity;      // hard hit blooms faster
            const float fallMs  = 500.0f;
            noiseLayer_.configureBloom(startHz, peakHz, endHz, riseMs, fallMs);
        }
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
        usedShellGain_       = 1.0f;
        if (secondaryEnabled_ >= 0.5f && couplingStrength_ > 0.0f)
        {
            configureSecondaryBank();
            // Now that the shell modes are set for this note, apply the same
            // measured-strike normalisation as the body (audit N-1): the shell
            // is a modal bank too, so its resonant-strike peak likewise exceeds
            // the Sum|a_k| impulse bound. In the plugin the gain comes from
            // VoicePool's per-pad StrikeNormTable (measured off the audio
            // path); a DrumVoice used on its own measures it here, uncached.
            const float sg = gainsSupplied_
                ? suppliedShellGain_
                : measuredStrikeOutputGain(secondaryBank_);
            secondaryBank_.setOutputGain(sg);
            secondaryBank_.setOutputSoftClipThreshold(0.0f);
            usedShellGain_ = sg;
            effectiveCoupling_ = effectiveCouplingStrength();
        }

        // Phase 8E / audit N-1: tension modulation depth. The quasistatic
        // tension variation is LINEAR in the system's vibrational energy
        // (Avanzini-Marogna-Bank 2012; Tolonen-Valimaki-Karjalainen 2000), and
        // that energy already carries the velocity dependence (a harder strike
        // injects more modal energy ~ velocity^2). So the effective depth is
        // just knob * a fixed scale -- the explicit velocity^2 here was
        // double-counting (it multiplied an output-energy term that itself
        // scaled with velocity^2, giving a non-physical velocity^4 law). The
        // glide is now driven by the bank's gain-invariant modal energy (see
        // the energy follower below), not the radiated output, so it no longer
        // tracks the output gain staging. kTensionEnergyScale is calibrated so
        // a hard hit (knob 1.0) reaches ~1-2 semitones at the modal-energy peak.
        tensionAmtEffective_ =
            std::clamp(tensionModAmt_, 0.0f, 1.0f) * kTensionEnergyScale;
        energyEnv_ = 0.0f;
        // Wire coupling: reset the running energy peak and gain modulation so a
        // reused voice starts tracking from scratch.
        wireEnergyPeak_ = 0.0f;
        wireGainMod_    = 1.0f;

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
        silentSamples_ = 0;
        active_ = true;
        gainsSupplied_ = false;  // one-shot: the next note measures or is supplied anew
    }

    void noteOff() noexcept
    {
        // T1.13: a voice is released once, by MIDI or by its own auto-release
        // (autoReleaseAtGridPoint), whichever comes first; noteOn re-arms it.
        if (releaseLatched_)
            return;
        releaseLatched_ = true;
        exciterBank_.release();
        toneShaper_.noteOff();
        ampEnvelope_.gate(false);
        // Phase 8A.5 (STK Modal idiom): accelerate modal decay by adding
        // kNoteOffReleaseRate (52 ms extra T60 at every sample rate) to every
        // mode's damping law; the bank's flushSilentModes() then retires the
        // voice once amplitudes fall below kSilenceThreshold. The bank call is
        // idempotent too, so a second release never compounds it (MB-034).
        bodyBank_.getSharedBank().raiseReleaseDecayRate(kNoteOffReleaseRate);
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
        tensionAmtEffective_ = 0.0f;
        wireEnergyPeak_      = 0.0f;
        wireGainMod_         = 1.0f;
        silentSamples_       = 0;
        active_              = false;
    }

    /// resetForKitSwitch() plus a full re-arm of every exciter alternative
    /// (re-seeds the Impulse / Mallet RNG that reset() keeps). Used by the
    /// strike-norm probe voice so a pad config measures one deterministic
    /// gain, independent of what the probe measured before. Allocation-free.
    void resetForStrikeProbe() noexcept
    {
        resetForKitSwitch();
        exciterBank_.restartAll();
    }

    // ------------------------------------------------------------------
    // Processing
    // ------------------------------------------------------------------

    [[nodiscard]] float process() noexcept
    {
        if (!active_)
            return 0.0f;

        // T1.13: the auto-release acts at grid points of voice time.
        autoReleaseAtGridPoint();

        // Pitch envelope, tension, Material Morph and the wire follower
        // (control plane, T1.11): the same 32-sample voice-time grid as the
        // block renderers, BEFORE the body so a grid sample already sounds at
        // the new pitch; every other sample only advances the modulators.
        // Skipped entirely when nothing modulates, so the default path stays
        // bit-identical to Phase 1 (FR-055).
        if (isControlGridActive())
        {
            if (voiceSampleCount_ % kControlGridSamples == 0)
                controlStep(bodyBank_.getSharedBank());
            else
                tickModulators(1);
        }
        ++voiceSampleCount_;

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
        // wireGainMod_ carries the wire-coupling modulation, updated by the
        // energy follower on the control grid (controlStep), as on the block
        // path. The secondary shell bank stays block-path-only.
        const float noiseSample =
            noiseLayer_.processSample() * noiseLayer_.standaloneGain()
            * noiseLayerGain_ * wireGainMod_;
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
        // Gain-staging Step 2 (H-1/H-4): voice output is LINEAR. `level_` is a
        // true attenuation before a transparent safety rail (hardClip engages
        // only on a pathological/runaway voice, never on a musical hit at the
        // Step-3/4 budget). The real ceiling is owned by the main-bus true-peak
        // limiter, not a per-voice softClip gain stage.
        const float y = Krate::DSP::hardClip(shaped * env * level_);
        trackQuietRun(&y, 1);
        return y;
    }

    // Block-level audio-thread hot path (T043 / FR-001 / FR-002 / research.md §1,
    // plus Phase 9 SIMD emergency fallback per plan.md §SIMD Emergency Fallback /
    // FR-071). Dispatches the ExciterBank and BodyBank alternatives exactly
    // ONCE per block via their withActive() / processBlock() single-dispatch
    // helpers.
    //
    // Two code paths:
    //   - FAST PATH: every exciter except FeedbackExciter. The exciter runs
    //     per-sample into a scratch buffer, then BodyBank::processBlock()
    //     routes the modal work through ModalResonatorBank::processBlock
    //     (SIMD-accelerated via Highway), then the unnatural + tone shaper +
    //     amp env post chain runs per-sample. This is the path that brings the
    //     144-combination CPU budget back under the 1.25% per-voice target.
    //
    //   - SLOW PATH: the active exciter is FeedbackExciter (needs strict
    //     per-sample body feedback per research.md §3). Keeps the original
    //     nested-visit per-sample inner loop for correctness.
    //
    // Both paths run the pitch envelope, tension, Material Morph and the wire
    // follower on the kControlGridSamples voice-time grid (T1.11): while any
    // of them is active, each chunk is cut at the next grid point and a grid
    // point retunes and snaps the bank (controlStep), so the render does not
    // depend on the host block size.
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
        const bool feedbackExciter =
            exciterBank_.getCurrentType() == ExciterType::Feedback;

        // Decide fast vs slow path. Only strict per-sample FeedbackExciter
        // semantics force the original nested-visit per-sample loop. The
        // pitch envelope, tension and Material Morph run on both paths on the
        // 32-sample voice-time grid with a snap (controlStep): a 20 ms sweep
        // @ 44.1 kHz gets ~28 retunes whatever the host block size.
        const bool useSlowPath = feedbackExciter;

        if (useSlowPath)
        {
            processBlockSlow(out, numSamples, level);
        }
        else
        {
            processBlockFast(out, numSamples, level);
        }

        // Phase 8A.5 retirement: the amp envelope is now only a click-free
        // smoother + release curve, not a lifetime gate. The voice is
        // finished once the body is silent AND the envelope has completed its
        // (optional) release. This is the STK / Chromaphone / Faust physmodels
        // idiom -- the body's damping law IS the decay. "Body silent" is
        // body-aware (MB-106, MB-160): a modal body is silent once the bank's
        // flushSilentModes() pass (run once per block by both renderers:
        // inside ModalResonatorBank::processBlock on the fast path, after the
        // per-sample loop in processBlockSlow) reports zero active modes; the
        // String body, which never runs the bank, once its waveguide output
        // has decayed below -100 dBFS RMS.
        //
        // Intentionally NOT gating retirement on noiseLayer_ / clickLayer_
        // because those layers have their own short envelopes that come
        // to rest at output=0 but hold in the Sustain stage rather than
        // transitioning to Idle. Their output is zero by design once the
        // configured decay has elapsed.
        const bool bodySilent = bodyBank_.isBodySilent();
        const bool envFinished = !ampEnvelope_.isActive();
        if (bodySilent && envFinished)
        {
            active_ = false;
            silentSamples_ = 0;
        }
        // Safety net: if the body has been silent for a while AND the
        // voice's auto-release (T1.13, autoReleaseAtGridPoint) hasn't fired
        // (because the output settled above kAutoReleaseThreshold from
        // sustained noise / click / filter ringing), force-retire so we
        // don't sit Active forever.
        // Without this the original "infinite tone" bug regresses on
        // any preset whose layered output settles above 1e-3 once the
        // body modes have died -- pad-click hosts never send noteOff,
        // so envFinished never becomes true on its own. The net is counted in
        // samples, so it is kSafetyNetSeconds (1.365 s) of body silence at
        // every host block size and sample rate. ">=" retires at the end of
        // the block in which the net is reached: with ">" every block size
        // that divides the net would overshoot it by one whole block, an
        // overshoot that differs per host block size.
        if (bodySilent)
        {
            silentSamples_ += numSamples;
            if (silentSamples_ >= safetyNetSamples_)
                active_ = false;
        }
        else
        {
            silentSamples_ = 0;
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
                          float level) noexcept
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
        // T1.11: while a modulation runs, cut every chunk at the next point
        // of the voice-time control grid; a chunk that starts on one runs
        // controlStep first. Without modulation the chunking is unchanged.
        const bool gridActive = isControlGridActive();

        while (remaining > 0)
        {
            // T1.13: the auto-release acts at the chunk start, and the chunk
            // ends at the next grid point where it could act.
            autoReleaseAtGridPoint();
            int chunk = remaining < kMaxBlockSize ? remaining : kMaxBlockSize;
            chunk = autoReleaseChunk(chunk);
            if (gridActive)
                chunk = beginControlChunk(bodyBank_.getSharedBank(), chunk);

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
            // feedforward coupling is applied one sample late on this path --
            // it reads the previous-block's primary output -- which is fine
            // for a ~5-50 ms body decay at 64+ sample blocks (block-rate
            // coupling update, per plan.md D.1). The slow path
            // (processBlockSlow) applies the same stage per-sample.
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
            // Mix 0 (MB-048): the layer is silent, so skip it and the gain
            // pass and just zero the scratch once.
            if (noiseLayer_.mix() <= 0.0f)
            {
                std::fill(noiseScratch, noiseScratch + chunk, 0.0f);
            }
            else
            {
                noiseLayer_.processBlock(noiseScratch, chunk);
                // Wire coupling: wireGainMod_ (from the modal energy at the
                // last control-grid point) scales the buzz so it tracks the head.
                // 1.0 when coupling is off => bit-exact legacy path.
                const float gain =
                    noiseLayer_.standaloneGain() * noiseLayerGain_ * wireGainMod_;
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
                    out[offset + i]      = Krate::DSP::hardClip(shaped * env * level);
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
                    out[offset + i]      = Krate::DSP::hardClip(shaped * env * level);
                }
            }

            trackQuietRun(out + offset, chunk);
            voiceSampleCount_ += chunk;
            offset    += chunk;
            remaining -= chunk;
        }
    }

    // SLOW PATH: per-sample inner loop. Used when FeedbackExciter is the
    // active exciter (strict per-sample body-feedback semantics required per
    // research.md §3). The pitch envelope, tension, Material Morph and the
    // wire follower run on the same 32-sample voice-time control grid as the
    // fast path (T1.11): while one is active the block is cut into sub-chunks
    // at the grid points, each with its own block-rate smoothing pass.
    void processBlockSlow(float* out,
                          int numSamples,
                          float level) noexcept
    {
        const bool gridActive = isControlGridActive();

        // Phase 8D secondary (shell) bank gate, mirrored from the fast path.
        // Computed once per block; when engaged the per-sample loop drives a
        // feedforward body -> shell coupling and mixes the shell output back
        // into the audible signal (passive: no return path into the body's
        // modal excitation, so it cannot self-oscillate at any coupling value).
        const bool secondaryActive = effectiveCoupling_ > 0.0f;

        // Nested single-visit: one std::visit on the exciter, one on the body.
        // Inside both visits we hold typed references so every sample call is
        // a direct non-virtual call — no per-sample variant dispatch.
        exciterBank_.withActive([this, out, numSamples, level, gridActive, secondaryActive](auto& exciter) noexcept {
            bodyBank_.withActive(
                [this, out, numSamples, level, gridActive, secondaryActive, &exciter](
                    auto& body, Krate::DSP::ModalResonatorBank& sharedBank) noexcept {
                    float lastBody  = bodyBank_.getLastOutput();
                    float lastShell = secondaryLastOutput_;
                    int offset = 0;
                    while (offset < numSamples)
                    {
                        // T1.11: one sub-chunk per control-grid interval while a
                        // modulation runs; the whole block otherwise.
                        // T1.13: the auto-release acts at the sub-chunk start,
                        // and the sub-chunk ends at the next grid point where
                        // it could act.
                        autoReleaseAtGridPoint();
                        int len = autoReleaseChunk(numSamples - offset);
                        if (gridActive)
                            len = beginControlChunk(sharedBank, len);

                        // Phase 9 perf: hoist the modal bank's coefficient
                        // smoothing out of the per-sample inner loop. Without this
                        // the slow path's per-sample processSample() smooths
                        // 3 floats * numModes per sample, which after the Phase 8B
                        // mode-count bump (membrane 16->48, plate 16->48,
                        // shell 12->32) blew the FeedbackExciter combo's per-voice
                        // CPU budget. processSampleNoSmooth() below pairs with
                        // this single block-rate prepare and additionally routes
                        // through the SIMD modal kernel when bow taps are off.
                        sharedBank.prepareBlockSmoothing();

                        // Audit M-6: the secondary (shell) bank must run on the
                        // slow path too -- it was previously dropped, so
                        // FeedbackExciter + coupling pads silently lost all shell
                        // character. Use the same block-rate smoothing hoist as the
                        // body bank so the 24-mode shell doesn't reintroduce per-
                        // sample smoothing cost on the budget-sensitive slow path.
                        if (secondaryActive)
                            secondaryBank_.prepareBlockSmoothing();

                        for (int i = offset; i < offset + len; ++i)
                        {
                            const float clickSample =
                                clickLayer_.processSample() * ClickLayer::kStandaloneOutputGain;
                            const float excMain = exciter.process(lastBody);
                            const float exc     = excMain + clickSample * 0.5f;
                            const float bodyOut = body.processSampleNoSmooth(sharedBank, exc);
                            lastBody            = bodyOut;

                            // Phase 8D shell stage (slow path). Feedforward
                            // body -> shell, then a passive additive mix into the
                            // body signal -- identical topology to the fast path
                            // (processBlockFast), just per-sample instead of
                            // block-rate. No return path into the body excitation.
                            float bodyWithShell = bodyOut;
                            if (secondaryActive)
                            {
                                lastShell = secondaryBank_.processSampleNoSmooth(
                                    bodyOut * effectiveCoupling_);
                                bodyWithShell += lastShell;
                            }

                            // Parallel noise layer (Phase 7) sums into body
                            // output before the UnnaturalZone chain; click layer
                            // is mixed directly so it's heard as a transient.
                            const float noiseSample =
                                noiseLayer_.processSample() * noiseLayer_.standaloneGain()
                                * noiseLayerGain_ * wireGainMod_;
                            const float combinedBody = bodyWithShell + noiseSample + clickSample;

                            // UnnaturalZone chain per contract: body + modeInject
                            // → nonlinear coupling → tone shaper → env × level.
                            const float injected = combinedBody + unnaturalZone_.modeInject.process();
                            const float coupled  =
                                unnaturalZone_.nonlinearCoupling.processSample(injected);
                            const float shaped   = toneShaper_.processSample(coupled);
                            const float env      = ampEnvelope_.process();
                            out[i]               = Krate::DSP::hardClip(shaped * env * level);
                        }
                        trackQuietRun(out + offset, len);
                        voiceSampleCount_ += len;
                        offset += len;
                    }
                    // T1.7 (MB-027): processSampleNoSmooth never culls silent
                    // modes, so flush once per block as the banks'
                    // processBlock does on the fast path. Without it the
                    // active-mode count never reaches 0 and the voice never
                    // retires. A String body leaves the shared bank unused;
                    // the flush is then a no-op.
                    sharedBank.flushSilentModes();
                    if (secondaryActive)
                        secondaryBank_.flushSilentModes();
                    bodyBank_.setLastOutput(lastBody);
                    if (secondaryActive)
                        secondaryLastOutput_ = lastShell;
                });
        });
    }

public:

    /// Phase 8A.5: voice lifetime now tracks the modal bank, not the amp
    /// envelope. The bank's flushSilentModes() pass (run once per block by
    /// both renderers, processBlockFast and processBlockSlow) retires modes
    /// below kSilenceThreshold; this accessor
    /// reports the aggregate voice-active flag maintained by processBlock.
    [[nodiscard]] bool isActive() const noexcept { return active_; }

    /// T1.6: VoicePool calls this on a slot's main voice right before it
    /// re-configures the voice for a new note (applyPadConfigToSlot, then
    /// noteOn). A retriggered or re-acquired voice can still read active, and
    /// then every guarded body setter re-maps the modes that noteOn re-maps
    /// anyway. noteOn sets the flag again.
    void markInactiveForNoteOn() noexcept { active_ = false; }

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
    /// Snare-body fix: per-pad multiplier on the standalone noise-layer gain
    /// (default 1.0). Lets a snare push its wire buzz to near-body level, which
    /// the mix knob + global -18 dBFS ceiling cannot reach on their own.
    void setNoiseLayerGain(float v) noexcept      { noiseLayerGain_ = v; }
    /// Wire coupling: 0 = independent buzz envelope (legacy, bit-exact), 1 =
    /// buzz amplitude fully tracks the body's modal-energy envelope (snare wires
    /// driven by head motion). Reuses the gain-invariant getModalEnergy()
    /// follower; block-rate.
    void setWireCoupling(float v) noexcept        { wireCoupling_ = std::clamp(v, 0.0f, 1.0f); }

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

    /// Audit findings 3-5: plumb the secondary exciter parameters (FM Ratio,
    /// Feedback Amount, Friction Pressure) through to their exciters so the
    /// host-side knobs finally take effect. Normalized [0,1] in every case.
    void setFMRatio(float v) noexcept { exciterBank_.setFMRatio(v); }
    void setFeedbackAmount(float v) noexcept { exciterBank_.setFeedbackAmount(v); }
    void setFrictionPressure(float v) noexcept { exciterBank_.setFrictionPressure(v); }

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
    // Probe window (MB-263): render at least `minSec`; then stop once no new
    // maximum has appeared for 10 ms, and at the latest after 100 ms. A slow
    // ring-up (Mallet / Plate and Mallet / Bell peak at 40-48 ms) keeps setting
    // new maxima, so the window grows with it instead of cutting it at 30 ms.
    static constexpr float kProbeQuietSec      = 0.010f;
    static constexpr float kProbeMaxSec        = 0.100f;
    static constexpr float kProbeMinExcitedSec = 0.060f;  // primary-body probe

    // Audit N-1: render a canonical strike through the (already mode-configured)
    // bank and return its raw peak. RT-safe: no allocation, the excitation is a
    // short raised-cosine generated per-sample, and the probe state is cleared
    // afterwards (reset() zeroes the oscillators but preserves the modes), so
    // the real note proceeds from a clean bank. The window covers the input
    // pulse plus the resonant ring-up where the strike peak lives: at least
    // 20 ms, then the adaptive stop above. The bank was just configured, so its
    // smoothed coefficients already sit on their targets: one block-rate
    // smoothing pass, then the SIMD per-sample kernel.
    [[nodiscard]] float probeStrikePeak(Krate::DSP::ModalResonatorBank& bank) const noexcept
    {
        const float sr = static_cast<float>(sampleRate_);
        const int strikeLen = std::max(1, static_cast<int>(0.002f * sr));  // 2 ms
        const int minLen    = std::max(strikeLen,
                                       static_cast<int>(0.020f * sr));     // 20 ms
        const int quietLen  = std::max(1, static_cast<int>(kProbeQuietSec * sr));
        const int maxLen    = std::max(minLen, static_cast<int>(kProbeMaxSec * sr));

        const float prevGain = bank.getOutputGain();
        const float prevClip = bank.getOutputSoftClipThreshold();
        bank.setOutputGain(1.0f);
        bank.setOutputSoftClipThreshold(0.0f);
        bank.prepareBlockSmoothing();

        float peak = 0.0f;
        int lastMaxIndex = 0;
        for (int i = 0; i < maxLen; ++i) {
            if (i >= minLen && i - lastMaxIndex >= quietLen)
                break;
            float in = 0.0f;
            if (i < strikeLen) {
                const float ph = static_cast<float>(i) / static_cast<float>(strikeLen);
                in = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * ph));
            }
            const float a = std::abs(bank.processSampleNoSmooth(in));
            if (a > peak) {
                peak = a;
                lastMaxIndex = i;
            }
        }

        bank.reset();  // clear probe oscillator state, keep configured modes
        bank.setOutputGain(prevGain);
        bank.setOutputSoftClipThreshold(prevClip);
        return peak;
    }

    // Snare-body Fix D (INVESTIGATION-snare-body): probe the bank with the REAL
    // excitation shape instead of a clean raised-cosine. The N-1 raised-cosine
    // proxy rings the low modes coherently and peaks HIGHER than the note's
    // actual excitation does for phase-random / HF-tilted exciters (NoiseBurst
    // above all), so kBodyHeadroom / probePeak came out too small and the body
    // shipped several dB below its budget -- the core "snare sounds like a
    // hi-hat" cause. Here we drive the bank with a local copy of the configured
    // exciter at a canonical hard strike (velocity 1.0) plus the click layer's
    // half-amplitude excitation contribution, exactly as process() feeds the
    // body. Local instances leave the live exciter/click untouched.
    //
    // RT-safe / allocation-free: this reuses the voice's LIVE exciter and click
    // layer (already prepared in DrumVoice::prepare) rather than constructing
    // local copies -- ImpactExciter::prepare allocates a comb-delay line, so a
    // per-probe local ExciterBank would allocate on the audio thread. Triggering
    // exciterBank_ here applies the deferred exciter-type swap that noteOn()
    // performs a few lines later anyway; the swap only re-arms an alternative
    // ExciterBank::prepare already prepared, so it allocates nothing (MB-002),
    // and both components are re-triggered for the audible note below --
    // trigger() fully re-seeds/resets their state, so the probe leaves no
    // residue on the note (bit-identity preserved). exciterType is passed only
    // to key the cache; the live bank already holds the pending type.
    [[nodiscard]] float probeStrikePeakExcited(
        Krate::DSP::ModalResonatorBank& bank, ExciterType exciterType) noexcept
    {
        (void)exciterType;
        const float sr = static_cast<float>(sampleRate_);
        // At least 60 ms: spans the NoiseBurst (up to ~15 ms) and the body's
        // resonant ring-up, including the slow beating ring-up of a large
        // Mallet / Plate (Orchestral 49 sits near its 20 ms level until 47 ms,
        // then peaks at 47.8 ms: no new maximum for over 25 ms, so a 30 ms
        // minimum with the 10 ms quiet stop still cut it, MB-263). Then the
        // adaptive stop (no new maximum for 10 ms, 100 ms at most).
        const int minLen   = std::max(1, static_cast<int>(kProbeMinExcitedSec * sr));
        const int quietLen = std::max(1, static_cast<int>(kProbeQuietSec * sr));
        const int maxLen   = std::max(minLen, static_cast<int>(kProbeMaxSec * sr));

        const float prevGain = bank.getOutputGain();
        const float prevClip = bank.getOutputSoftClipThreshold();
        bank.setOutputGain(1.0f);
        bank.setOutputSoftClipThreshold(0.0f);
        // Freshly configured bank: smoothed coefficients already equal their
        // targets, so one block-rate pass, then the SIMD per-sample kernel.
        bank.prepareBlockSmoothing();

        // Reference velocity 1.0 = canonical hard strike (the level
        // kBodyHeadroom is defined against). Applies the deferred exciter swap.
        // Reset first: a retrigger keeps exciter state the previous note left
        // (the Friction bow's energy and rosin-noise position), which moved the
        // probed peak by ~5 dB against a fresh voice (MB-021 test (b)).
        exciterBank_.reset();
        exciterBank_.trigger(1.0f);
        clickLayer_.configure(clickLayerParams_);
        clickLayer_.trigger(1.0f);

        float peak     = 0.0f;
        float lastBody = 0.0f;
        int lastMaxIndex = 0;
        for (int i = 0; i < maxLen; ++i)
        {
            if (i >= minLen && i - lastMaxIndex >= quietLen)
                break;
            const float clickSample =
                clickLayer_.processSample() * ClickLayer::kStandaloneOutputGain;
            // Non-feedback exciters ignore the feedback arg; Feedback never
            // reaches this path (it keeps the soft-clip branch instead).
            const float exc = exciterBank_.process(lastBody) + clickSample * 0.5f;
            lastBody = bank.processSampleNoSmooth(exc);
            const float a = std::abs(lastBody);
            if (a > peak) {
                peak = a;
                lastMaxIndex = i;
            }
        }

        // Leave the live components clean; noteOn() re-triggers both below.
        exciterBank_.reset();
        clickLayer_.reset();

        bank.reset();  // clear probe ring, keep configured modes
        bank.setOutputGain(prevGain);
        bank.setOutputSoftClipThreshold(prevClip);
        return peak;
    }

    // Audit N-1: output gain that normalises the bank's measured resonant-strike
    // peak to kBodyHeadroom. Guards a near-silent probe (falls back to the
    // impulse bound) so an empty/degenerate bank can't divide-by-~0.
    // Raised-cosine proxy path -- used for the secondary (shell) bank, which is
    // driven by the body output rather than an exciter.
    [[nodiscard]] float measuredStrikeOutputGain(
        Krate::DSP::ModalResonatorBank& bank) const noexcept
    {
        const float peak = probeStrikePeak(bank);
        if (peak <= 1.0e-4f)
            return kBodyHeadroom / bank.getInputGainSum();
        return kBodyHeadroom / peak;
    }

    // Snare-body Fix D: real-excitation variant for the PRIMARY body.
    [[nodiscard]] float measuredStrikeOutputGainExcited(
        Krate::DSP::ModalResonatorBank& bank, ExciterType exciter) noexcept
    {
        const float peak = probeStrikePeakExcited(bank, exciter);
        if (peak <= 1.0e-4f)
            return kBodyHeadroom / bank.getInputGainSum();
        return kBodyHeadroom / peak;
    }

    // Cached variant for the primary body: re-measures only when the mapper
    // inputs (and hence the configured mode set) OR the excitation shape change.
    // Snare-body Fix D: keyed additionally on the exciter type + the click/
    // noise-burst excitation-shaping params so a change to how the body is
    // struck re-runs the real-excitation probe.
    [[nodiscard]] float measuredStrikeOutputGain(
        Krate::DSP::ModalResonatorBank& bank,
        const VoiceCommonParams& p,
        BodyModelType body,
        ExciterType exciter,
        float startPitchHz) noexcept
    {
        const StrikeNormKey key{
            .body = body, .exciter = exciter, .material = p.material, .size = p.size, .decay = p.decay, .strikePos = p.strikePos, .modeStretch = p.modeStretch,
            .decaySkew = p.decaySkew, .airLoading = p.airLoading, .modeScatter = p.modeScatter,
            .b1 = p.bodyDampingB1, .b3 = p.bodyDampingB3,
            .clickMix = clickLayerParams_.mix, .clickContact = clickLayerParams_.contactMs,
            .clickBright = clickLayerParams_.brightness,
            .noiseBurstContact = exciterBank_.getPendingNoiseBurstContactNorm(),
            .fmRatio = exciterBank_.getPendingFMRatioNorm(),
            .frictionPressure = exciterBank_.getPendingFrictionNorm(),
            .feedbackAmount = exciterBank_.getPendingFeedbackNorm(),
            .sampleRate = static_cast<float>(sampleRate_),
            .startPitchHz = startPitchHz};
        if (!(key == strikeNormKey_)) {
            strikeNormGain_ = measuredStrikeOutputGainExcited(bank, exciter);
            strikeNormKey_  = key;
        }
        return strikeNormGain_;
    }

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
        // Strike position drives the secondary (shell) bank's mode shape too --
        // audit §3-B: the strikePos was hard-coded to 0.3, so the coupled shell
        // timbre never responded to the Strike Position knob. Use the voice's
        // current strikePos so the shell accent tracks where the body is struck.
        const float shellStrike = std::clamp(strikePos_, 0.0f, 1.0f);
        for (int k = 0; k < shellCount; ++k)
        {
            freqs[k] = f0 * Bodies::kShellRatios[k];
            amps[k]  = std::abs(Bodies::computeShellAmplitude(k, shellStrike));
            amps[k] = std::max(amps[k], 0.03f);
        }
        for (int k = shellCount; k < kSecondaryModes; ++k) {
            freqs[k] = 0.0f;
            amps[k]  = 0.0f;
        }

        // Shell-like damping, following Chaigne (1993) / standard modal-
        // synthesis form: R_k = b1 + b3·f_k², where b1 sets global decay and
        // b3 gives high-frequency rolloff.
        //
        // Topology note (audit L-7/L-8): the head<->shell coupling is now
        // FEEDFORWARD + a PASSIVE additive mix -- the shell is a parallel
        // resonator driven by the body output and summed into the audible
        // signal, with NO return path into the body's modal excitation. The
        // earlier closed-loop topology (shell output fed back into the body
        // bus) could self-oscillate at frequency-aligned modes -- the source
        // of the user-reported "continuous beep / infinite ring". That loop
        // was removed, so there is no longer a closed-loop stability bound on
        // the shell damping or on the coupling gain.
        //
        // We still keep the shell decaying faster than the body it accents:
        // not for stability, but so a long shell tail can't outlast its
        // primary and leave an audible hanging layer after the body modes
        // (which gate voice retirement) have gone silent. b1 sits roughly in
        // [25, 80] s^-1 (t60 ≈ 0.09..0.28 s). Verified by the stress test in
        // test_kit_switch_infinite_ring.cpp.
        const float mat = std::clamp(secondaryMaterial_, 0.0f, 1.0f);
        // Body's effective b1 from current per-pad damping override.
        const float bodyN = std::clamp((bodyDampingB1_ >= 0.0f)
                                       ? bodyDampingB1_ : 0.4f, 0.0f, 1.0f);
        const float bodyB1 = 0.2f + bodyN * 49.8f;
        // Shell b1 sits at least 2.5× above the body so its t60 is at most
        // ~40 % of the body's; soft floor of 25 s⁻¹ keeps the shell short
        // even when the body is set to a low-damping (long-tail) config.
        const float b1 = std::max(25.0f, 2.5f * bodyB1) +
                         (1.0f - mat) * 15.0f;
        const float b3 = mat * 1.0e-5f;
        const Krate::DSP::ModalResonatorBank::DampingLaw law{.b1 = b1, .b3 = b3};
        secondaryBank_.setModes(freqs, amps, shellCount,
                                law, /*stretch*/ 0.0f, /*scatter*/ 0.0f);
    }

    // Map the per-pad couplingStrength (0..1) to the feedforward drive gain
    // for the shell stage. Audit L-7: the old 0.25 ceiling existed to keep the
    // closed-loop eigenvalue (coupling² · primaryDecay · secondaryDecay) inside
    // the unit circle. That loop was removed -- coupling is now a feedforward
    // drive into a passive parallel resonator (see configureSecondaryBank), so
    // there is no feedback eigenvalue to bound. The strength maps directly to
    // the full range; the shell mix is bounded by the secondary bank's own
    // unit-peak output normalisation and the main-bus true-peak limiter.
    float effectiveCouplingStrength() const noexcept
    {
        return std::clamp(couplingStrength_, 0.0f, 1.0f);
    }

    /// Block-rate wire-buzz gain modulation from the modal-energy envelope.
    /// Modal energy ~ amplitude^2, so sqrt(energyNorm) tracks the head's
    /// AMPLITUDE envelope. Self-normalising: wireEnergyPeak_ is the running peak
    /// of energyEnv_ since noteOn, so the ratio is velocity- and gain-staging-
    /// independent (a hard hit and a soft hit both start at ~1 and relax to 0).
    /// Guards: pre-strike (peak ~ 0) and non-modal bodies (String keeps modal
    /// energy at 0) both return 1.0, so coupling silently bypasses.
    [[nodiscard]] float computeWireGainMod() const noexcept
    {
        if (wireCoupling_ <= 1.0e-6f)
            return 1.0f;
        if (wireEnergyPeak_ <= 1.0e-12f)
            return 1.0f;
        const float norm  = std::clamp(energyEnv_ / wireEnergyPeak_, 0.0f, 1.0f);
        const float track = std::sqrt(norm);
        return 1.0f + wireCoupling_ * (track - 1.0f);   // lerp(1, track, coupling)
    }

    /// Build the common parameter bundle from the voice's cached Phase-1/8
    /// state, overriding only `material`. Single source of truth for the live
    /// param-edit (updateModalParameters) and Material-Morph refresh paths.
    VoiceCommonParams makeBodyParams(float material) noexcept
    {
        VoiceCommonParams p{};
        p.material      = material;
        p.size          = size_;
        p.decay         = decay_;
        p.strikePos     = strikePos_;
        p.level         = level_;
        p.modeStretch   = unnaturalZone_.getModeStretch();
        p.decaySkew     = unnaturalZone_.getDecaySkew();
        p.bodyDampingB1 = bodyDampingB1_;
        p.bodyDampingB3 = bodyDampingB3_;
        p.airLoading    = airLoading_;
        p.modeScatter   = modeScatter_;
        // Keep the Feedback slow-path mode cap consistent on live re-maps
        // (material-morph / pitch-env refresh) so mode count never jumps
        // mid-note (Phase 5).
        p.maxModes =
            (exciterBank_.getCurrentType() == ExciterType::Feedback)
            ? Bodies::NoiseBodyMapper::kFeedbackModeCap : 0;
        return p;
    }

    /// Dispatch the per-body modal mapper. Returns a MapperResult for any modal
    /// body (Membrane/Plate/Shell/Bell/NoiseBody). String is a waveguide, not a
    /// modal bank, so it falls through to the Membrane result, which the String
    /// callers never consume (they route through the BodyBank waveguide setters
    /// instead).
    static Bodies::MapperResult mapModalBody(BodyModelType type,
                                             const VoiceCommonParams& p,
                                             float pitchHz) noexcept
    {
        switch (type)
        {
        case BodyModelType::Plate:     return Bodies::PlateMapper::map(p, pitchHz);
        case BodyModelType::Shell:     return Bodies::ShellMapper::map(p, pitchHz);
        case BodyModelType::Bell:      return Bodies::BellMapper::map(p, pitchHz);
        // NoiseBodyMapper returns a composite (modal + noise-layer params);
        // only the modal spectrum drives the shared bank that the pitch
        // envelope / morph scale, so take its `.modal` MapperResult.
        case BodyModelType::NoiseBody: return Bodies::NoiseBodyMapper::map(p, pitchHz).modal;
        case BodyModelType::Membrane:
        case BodyModelType::String:
        case BodyModelType::kCount:
        default:                       return Bodies::MembraneMapper::map(p, pitchHz);
        }
    }

    /// Live Phase-1/8 parameter edit on a held note (Size/Material/StrikePos/
    /// damping/air-loading/scatter). Audit H-3/M-1 theme: re-run the ACTIVE
    /// body's mapper so these edits reach every modal body, not only Membrane.
    /// String runs through its own waveguide setters and is left untouched here
    /// (its sustained live retuning is a separate concern). T1.12 (MB-161): the
    /// re-map uses the material the voice is playing (the morph's last pushed
    /// value while Material Morph is on), and pushModes keeps the current
    /// pitch-envelope / tension ratio, so a live edit does not drop the voice
    /// back to its natural pitch.
    void updateModalParameters() noexcept
    {
        const BodyModelType t = bodyBank_.getCurrentType();
        if (t == BodyModelType::String)
            return;

        const float material = unnaturalZone_.materialMorph.isEnabled()
            ? lastPushedMorph_
            : material_;
        cachedMapperResult_ = mapModalBody(t, makeBodyParams(material), 0.0f);
        pushModes(bodyBank_.getSharedBank());
    }

    /// Audit H-3: retune the active body to absolute pitchHz. Modal bodies
    /// store the ratio to the natural fundamental and push their cached modes
    /// scaled by it; the String waveguide retunes via its frequency smoother.
    /// Used by the seed at noteOn and by controlStep.
    void retuneFundamental(float pitchHz) noexcept
    {
        if (bodyBank_.getCurrentType() == BodyModelType::String)
        {
            bodyBank_.setStringFrequency(pitchHz);
            return;
        }
        setPitchRatio(pitchHz);
        pushModes(bodyBank_.getSharedBank());
    }

    /// currentPitchRatio_ = pitchHz / naturalFundamentalHz_ (the pitch
    /// envelope and the tension factor relative to the body's natural f0).
    void setPitchRatio(float pitchHz) noexcept
    {
        currentPitchRatio_ = (naturalFundamentalHz_ > 0.0f)
            ? pitchHz / naturalFundamentalHz_
            : 1.0f;
    }

    // ------------------------------------------------------------------
    // T1.11 control grid (MB-008, MB-026, MB-151, MB-159, MB-237)
    // ------------------------------------------------------------------

    /// Phase 8E tension modulation is a Membrane-only physics path.
    [[nodiscard]] bool isTensionActive() const noexcept
    {
        return tensionAmtEffective_ > 1e-6f
            && bodyBank_.getCurrentType() == BodyModelType::Membrane;
    }

    /// Wire coupling drives the same modal-energy follower as tension, but
    /// ANY modal body participates.
    [[nodiscard]] bool isWireActive() const noexcept { return wireCoupling_ > 1e-6f; }

    /// True while anything runs on the control grid. False keeps every
    /// renderer's pre-T1.11 chunking, so an unmodulated pad is bit-identical.
    [[nodiscard]] bool isControlGridActive() const noexcept
    {
        return toneShaper_.isPitchEnvActive()
            || unnaturalZone_.materialMorph.isEnabled()
            || isTensionActive()
            || isWireActive();
    }

    /// Advance the pitch envelope and the morph envelope by `n` samples
    /// without retuning: every rendered sample that is not a grid point.
    void tickModulators(int n) noexcept
    {
        if (toneShaper_.isPitchEnvActive())
            for (int i = 0; i < n; ++i)
                (void)toneShaper_.processPitchEnvelope();
        if (unnaturalZone_.materialMorph.isEnabled())
            for (int i = 0; i < n; ++i)
                (void)unnaturalZone_.materialMorph.process();
    }

    /// Start a renderer chunk of at most `maxLen` samples while the grid is
    /// active: cut it at the next grid point, run controlStep when it starts
    /// on one, and advance the modulators over its other samples. Returns
    /// the chunk length; the caller renders it and adds it to
    /// voiceSampleCount_.
    [[nodiscard]] int beginControlChunk(Krate::DSP::ModalResonatorBank& sharedBank,
                                        int maxLen) noexcept
    {
        const int phase = static_cast<int>(voiceSampleCount_ % kControlGridSamples);
        const int len = std::min(maxLen, kControlGridSamples - phase);
        int ticks = len;
        if (phase == 0)
        {
            controlStep(sharedBank);
            --ticks;
        }
        tickModulators(ticks);
        return len;
    }

    // ------------------------------------------------------------------
    // T1.13 auto-release (MB-052)
    // ------------------------------------------------------------------

    /// At a grid point of voice time, past the onset grace period, release
    /// the voice once its output has stayed quiet for the hold time. Every
    /// renderer calls this at the start of each chunk, before rendering it.
    void autoReleaseAtGridPoint() noexcept
    {
        if (!releaseLatched_
            && voiceSampleCount_ % kControlGridSamples == 0
            && voiceSampleCount_ >= autoReleaseGraceSamples_
            && quietRunSamples_ >= autoReleaseHoldSamples_)
            noteOff();
    }

    /// The longest chunk (at most `maxLen`) a renderer may run before the
    /// next grid point at which autoReleaseAtGridPoint() could fire: that is
    /// the first grid point past the grace period that is at least the rest
    /// of the hold time away. The release therefore starts at the same voice
    /// sample whatever the host block size, while a loud voice still renders
    /// in long chunks.
    [[nodiscard]] int autoReleaseChunk(int maxLen) const noexcept
    {
        if (releaseLatched_)
            return maxLen;
        const std::int64_t now = voiceSampleCount_;
        const std::int64_t earliest = std::max<std::int64_t>(
            autoReleaseGraceSamples_,
            now + std::max(1, autoReleaseHoldSamples_ - quietRunSamples_));
        const std::int64_t gridPoint =
            (earliest + kControlGridSamples - 1) / kControlGridSamples * kControlGridSamples;
        return static_cast<int>(std::min<std::int64_t>(maxLen, gridPoint - now));
    }

    /// Extend or restart the run of output samples below the threshold. The
    /// run is capped at the hold time: a longer run means nothing more.
    void trackQuietRun(const float* y, int n) noexcept
    {
        int q = quietRunSamples_;
        for (int i = 0; i < n; ++i)
            q = (std::fabs(y[i]) < kAutoReleaseThreshold) ? q + 1 : 0;
        quietRunSamples_ = std::min(q, autoReleaseHoldSamples_);
    }

    /// One control step at a grid point. Reading the pitch and morph envelopes
    /// here counts as this sample's advance.
    void controlStep(Krate::DSP::ModalResonatorBank& sharedBank) noexcept
    {
        // 1. Audit N-1: one-pole follower on the bank's MODAL energy (not the
        // radiated output^2). getModalEnergy() = sum of mode amplitude^2 is a
        // smooth envelope invariant to the body output gain, so the tension
        // glide is physically grounded (Avanzini-Marogna-Bank 2012). One step
        // per grid point with the per-sample 20 ms coefficient raised to the
        // grid length, so the follower no longer depends on the host block.
        const bool tension = isTensionActive();
        const bool wire    = isWireActive();
        if (tension || wire)
        {
            const float e = sharedBank.getModalEnergy();
            energyEnv_ = energyAlphaGrid_ * energyEnv_ + (1.0f - energyAlphaGrid_) * e;
            if (wire)
            {
                wireEnergyPeak_ = std::max(wireEnergyPeak_, energyEnv_);
                wireGainMod_    = computeWireGainMod();
            }
        }

        // 2. Pitch. Phase 8E (Avanzini et al., JASA 2012): an energy-dependent
        // upward pitch jump at the strike that relaxes as vibration energy
        // decays, `1 + amt * energyEnv` around the envelope or natural pitch.
        const float basePitchHz = toneShaper_.isPitchEnvActive()
            ? toneShaper_.processPitchEnvelope()
            : naturalFundamentalHz_;
        const float pitchHz = tension
            ? basePitchHz * std::min(1.0f + tensionAmtEffective_ * energyEnv_,
                                     kTensionPitchModMax)
            : basePitchHz;

        // 3. Material Morph.
        const bool morph = unnaturalZone_.materialMorph.isEnabled();
        const float m = morph ? unnaturalZone_.materialMorph.process() : lastPushedMorph_;

        // 4. Dirty gate: retune only when the pitch or the morph moved. The
        // morph re-runs the body mapper, the costly half of a step, so it
        // pushes once the material has moved by kMorphDirtyStep (a 300 ms
        // sweep over the whole range then pushes every ~1.2 ms) or has
        // reached the morph's end value.
        const bool pitchDirty =
            std::abs(pitchHz - lastPushedPitchHz_) > 1e-6f * pitchHz;
        const bool morphDirty = morph && m != lastPushedMorph_
            && (std::abs(m - lastPushedMorph_) >= kMorphDirtyStep
                || m == unnaturalZone_.materialMorph.getEnd());
        if (!pitchDirty && !morphDirty)
            return;

        // T1.12 (MB-028, MB-161): the morph refresh only re-maps
        // cachedMapperResult_ (unscaled); the pitch ratio is applied on top of
        // it by the one push below, so neither feature overwrites the other.
        const bool retune = pitchDirty || pitchHz != naturalFundamentalHz_;
        if (morphDirty)
        {
            refreshBodyForMaterialOnBank(m);
            lastPushedMorph_ = m;
        }
        if (bodyBank_.getCurrentType() == BodyModelType::String)
        {
            // The waveguide has its own frequency smoother (no snap), and its
            // material refresh leaves the frequency alone.
            if (retune)
            {
                bodyBank_.setStringFrequency(pitchHz);
                lastPushedPitchHz_ = pitchHz;
            }
            return;
        }
        if (retune)
        {
            setPitchRatio(pitchHz);
            lastPushedPitchHz_ = pitchHz;
        }
        pushModes(sharedBank);
    }

    /// The one push into the modal bank: cachedMapperResult_ (the unscaled
    /// modes of the current material) with its frequencies scaled by
    /// currentPitchRatio_, one state-preserving updateModes(), then a snap of
    /// the smoothed coefficients onto the new targets, so the bank's per-call
    /// smoothing step is a no-op and the render does not depend on how the
    /// host splits blocks. Body-agnostic: the ratio retunes Plate/Shell/Bell/
    /// NoiseBody exactly as it does Membrane. Used by the noteOn seed,
    /// controlStep and the live parameter edits (updateModalParameters).
    void pushModes(Krate::DSP::ModalResonatorBank& sharedBank) noexcept
    {
        float scaled[Bodies::MapperResult::kMaxModes];
        const int n = cachedMapperResult_.numPartials;
        for (int k = 0; k < n; ++k)
            scaled[k] = cachedMapperResult_.frequencies[k] * currentPitchRatio_;

        sharedBank.updateModes(
            scaled,
            cachedMapperResult_.amplitudes,
            n,
            cachedMapperResult_.damping,
            cachedMapperResult_.stretch,
            cachedMapperResult_.scatter);
        sharedBank.snapCoefficients();
    }

    /// FR-054 / Audit M-1: Material Morph refresh. Re-run the ACTIVE body's
    /// mapper with the morph's current material value. Modal bodies only
    /// re-map cachedMapperResult_ (the caller pushes it once, scaled by the
    /// current pitch ratio: pushModes); String re-applies its material-derived
    /// brightness/decay through the waveguide.
    void refreshBodyForMaterialOnBank(float material) noexcept
    {
        const BodyModelType t = bodyBank_.getCurrentType();
        const VoiceCommonParams p = makeBodyParams(material);

        if (t == BodyModelType::String)
        {
            bodyBank_.refreshStringMaterial(p);
            return;
        }

        cachedMapperResult_ = mapModalBody(t, p, /*pitchHz*/ 0.0f);
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
    // Snare-body fix: per-pad noise-layer gain multiplier (default 1.0).
    float noiseLayerGain_ = 1.0f;

    // Wire coupling: modulate the parallel noise layer ("wire buzz") by the
    // body's modal energy so the buzz tracks the head's vibration envelope
    // (Bilbao 2012: snare wires are driven by head motion). 0 = fixed ADSR buzz
    // (bit-exact legacy), 1 = full tracking. Block-rate, RT-safe. Reuses the
    // same gain-invariant getModalEnergy() follower as tension modulation
    // (energyEnv_ below); wireEnergyPeak_ self-normalises the tracking so it is
    // velocity- and gain-staging-independent.
    float wireCoupling_   = 0.0f;  // per-pad depth (norm)
    float wireGainMod_    = 1.0f;  // block-rate gain modulation into noise layer
    float wireEnergyPeak_ = 0.0f;  // running peak of energyEnv_ since noteOn

    // Reusable parameter bundle (populated on every noteOn to avoid alloc).
    VoiceCommonParams params_{};

    // The UNSCALED mapper result for the material the voice plays (set at
    // noteOn, by the morph refresh and by updateModalParameters); pushModes
    // scales it by currentPitchRatio_.
    Bodies::MapperResult cachedMapperResult_{};

    // Audit N-1: cache for the body's measured-strike output gain, used by a
    // DrumVoice that is driven on its own (unit tests, tools/membrum-fit); in
    // the plugin VoicePool supplies a per-pad gain instead. Keyed on the
    // mapper inputs that change the configured mode set, the excitation-shaping
    // params and the sample rate; a repeated hit on an unchanged config reuses
    // the gain instead of re-running the probe render.
    struct StrikeNormKey {
        BodyModelType body = BodyModelType::kCount;  // sentinel = "no cache yet"
        ExciterType   exciter = ExciterType::kCount; // Fix D: excitation shape
        float material = 0.0f, size = 0.0f, decay = 0.0f, strikePos = 0.0f;
        float modeStretch = 0.0f, decaySkew = 0.0f, airLoading = 0.0f;
        float modeScatter = 0.0f, b1 = 0.0f, b3 = 0.0f;
        // Fix D: excitation-shaping params that change the probed strike peak.
        float clickMix = 0.0f, clickContact = 0.0f, clickBright = 0.0f;
        float noiseBurstContact = 0.0f;
        // MB-021 / MB-032: the other exciter params and the sample rate.
        float fmRatio = 0.0f, frictionPressure = 0.0f, feedbackAmount = 0.0f;
        float sampleRate = 0.0f;
        // T1.11 (MB-026): the seeded pitch-envelope start the probe measures
        // the bank at (0 when the pitch envelope is off).
        float startPitchHz = 0.0f;
        [[nodiscard]] bool operator==(const StrikeNormKey&) const = default;
    };
    StrikeNormKey strikeNormKey_{};
    float         strikeNormGain_ = 1.0f;

    // One-shot strike-norm gains supplied by VoicePool for the next noteOn,
    // and the gains the last noteOn applied (lastStrikeNormGains()).
    float suppliedBodyGain_  = 0.0f;
    float suppliedShellGain_ = 0.0f;
    bool  gainsSupplied_     = false;
    float usedBodyGain_      = 1.0f;
    float usedShellGain_     = 1.0f;

    // Natural (Size-derived) body fundamental in Hz, computed at noteOn.
    float naturalFundamentalHz_ = 0.0f;

    // Cached Phase 1 parameters (normalized 0-1)
    float material_  = 0.5f;
    float size_      = 0.5f;
    float decay_     = 0.3f;
    float strikePos_ = 0.3f;
    float level_     = 0.8f;

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
    float energyAlphaGrid_      = 0.0f;  // follower coeff per control-grid step

    // T1.11 control grid: samples rendered since noteOn, and the pitch (Hz,
    // including the tension factor) and morph material last pushed into the
    // body. Grid points sit at voiceSampleCount_ % kControlGridSamples == 0.
    std::int64_t voiceSampleCount_  = 0;
    float        lastPushedPitchHz_ = 0.0f;
    float        lastPushedMorph_   = 0.0f;
    // T1.12: lastPushedPitchHz_ / naturalFundamentalHz_, the ratio pushModes
    // scales every re-mapped mode set by (noteOn seed, controlStep).
    float        currentPitchRatio_ = 1.0f;

    // T1.13 auto-release: the grace and hold times in samples (set by
    // prepare), the current run of consecutive output samples below
    // kAutoReleaseThreshold, and whether the voice was released (by MIDI or
    // by the auto-release) since its noteOn.
    int  autoReleaseGraceSamples_ = 0;
    int  autoReleaseHoldSamples_  = 0;
    int  quietRunSamples_         = 0;
    bool releaseLatched_          = false;

    // Voice identity
    std::uint32_t voiceId_    = 0;
    double        sampleRate_ = 0.0;

    // State
    bool active_ = false;
    /// Samples rendered while the body has been silent; cleared otherwise.
    /// Used by the retirement safety-net so a voice whose output peak
    /// settles above the voice-pool silence threshold (and therefore never
    /// gets auto-noteOff'd) still retires after safetyNetSamples_ of body
    /// silence.
    std::int64_t silentSamples_ = 0;
    /// The safety net's length: 65536 samples at 48 kHz (1.365 s), the old
    /// 256-block net at 48 kHz with 256-sample blocks (which retired after
    /// 257 blocks, one block later), while the net no longer depends on the
    /// block size. 65536 is a whole number of blocks for every power-of-two
    /// block size up to 65536, so with ">=" no such host overshoots it.
    static constexpr double kSafetyNetSeconds = 65536.0 / 48000.0;
    std::int64_t safetyNetSamples_ = 65536;
};

} // namespace Membrum
