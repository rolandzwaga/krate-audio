// ==============================================================================
// Layer 3: System - Profundum Phase 9 (specs/profundum-phase9-voice-engine)
// ==============================================================================
// ProfundumVoice (per-voice chain, HarmonicBalanceMeter, IntervalInputs).
//   Serves: FR-001 - FR-016; SC-019, SC-020, SC-033, SC-034.
//   Encodes: plan S5 (S5.1 - S5.7), S9.4 - S9.7; C-4, C-5, C-6, C-10, C-11, C-18, C-20.
//   Spec:  specs/profundum-phase9-voice-engine/spec.md
//   Plan:  specs/profundum-phase9-voice-engine/plan.md
//   Tasks: specs/profundum-phase9-voice-engine/tasks.md (T007)
//
// One Profundum voice (FR-001): ProfundumCore (+ edge excitation) -> ShapeMorph -> (+ g * Anchor) -> HarmonicFold
// -> BassCharacterStage = c; AnchorOscillator = a (L == R); out += (c * g_amp) * g_d + (a * g_amp) * g_d (FR-015,
// C-10). Pitch (FR-006, S9.4): f_res -> DropEnvelope -> DropPhaseLock -> f_core = f_res * exp2(d_c / 12); the
// Anchor reads f_res in LOCK / PUNCH / GHOST and f_core in FOLLOW. Note events are classified by the caller and
// resolved here by NoteMemory (FR-010); a declick defers the transition to the RetriggerDeclick switch sample
// (FR-011). The steal fade is the same dip (C-4): its factor in g_d is identically 1.0f.
//
// Called by ProfundumEngine (T008) at grid points: noteEvent / release, then beginInterval (the per-interval
// setters, S5.4, every one change-only, C-20), then render in chunks that never cross a grid point (S5.6).
// RT (S5.7): every method except prepare is noexcept, allocation-, lock- and I/O-free; fixed-size members only; the
// only virtual calls are the observer callbacks, made only when an observer slot is set (FR-086).
//
// Surfaced additions to the plan S5.1 API (run-log T007): consumeEnvelopeRestart() (the S5.5 "engine is told to
// call motion.noteRestart()"; a restart may apply at a switch sample inside render, so the engine polls it after
// render), noteFrequency() (the stored fNote, the POLY f_res source of S6.5) and setSamplePosition() (the engine's
// sample clock for call / tap records; the voice counts its own rendered samples otherwise).
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>                       // L0: detail::isFinite
#include <krate/dsp/core/midi_utils.h>                     // L0: midiNoteToFrequency (the POLY f_res source)
#include <krate/dsp/primitives/adsr_envelope.h>            // L1: ADSREnvelope (FR-009)
#include <krate/dsp/primitives/retrigger_declick.h>        // L1: RetriggerDeclick (FR-011)
#include <krate/dsp/processors/drop_envelope.h>            // L2: DropEnvelope
#include <krate/dsp/processors/drop_phase_lock.h>          // L2: DropPhaseLock
#include <krate/dsp/processors/note_memory.h>              // L2: NoteMemory
#include <krate/dsp/processors/spectral_shape_recipe.h>    // L2: evaluateShape / evaluateMask / applyMask (C-6)
#include <krate/dsp/processors/harmonic_fold.h>            // L2: HarmonicFold
#include <krate/dsp/systems/profundum_core.h>              // L3 peer: ProfundumCore
#include <krate/dsp/systems/shape_morph.h>                 // L3 peer: ShapeMorph
#include <krate/dsp/systems/bass_character_stage.h>        // L3 peer: BassCharacterStage
#include <krate/dsp/systems/anchor_oscillator.h>           // L3 peer: AnchorOscillator
#include <krate/dsp/systems/bass_stereo_field.h>           // L3 peer: BassStereoField (const law), VoiceState
#include <krate/dsp/systems/morph_state.h>                 // L3 peer: MorphState
#include <krate/dsp/systems/profundum_engine_observer.h>   // L3 peer: seams, records, observer (plan S8)

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>

namespace Krate::DSP {

/// FR-010 (Phase 3 D-6): the three phase-policy enums agree, so they map by static_cast.
static_assert(static_cast<std::uint8_t>(NoteMemory::PhasePolicy::Reset) == 0
              && static_cast<std::uint8_t>(ProfundumCore::RetriggerPhase::Reset) == 0
              && static_cast<std::uint8_t>(AnchorOscillator::RetriggerPhase::Reset) == 0);
static_assert(static_cast<std::uint8_t>(NoteMemory::PhasePolicy::FreeRunning) == 1
              && static_cast<std::uint8_t>(ProfundumCore::RetriggerPhase::FreeRunning) == 1
              && static_cast<std::uint8_t>(AnchorOscillator::RetriggerPhase::FreeRunning) == 1);

/// One Profundum voice (FR-001). Heavy (the core's bank is about 100 KB): owned by value inside ProfundumEngine;
/// tests construct it with std::make_unique.
class ProfundumVoice {
public:
    static constexpr std::size_t kControlInterval = 32;
    /// Ruling R-B (2026-10-20): the restart / steal dip length. Phase 4's 1.5 ms out-sloped the SC-005(a) /
    /// SC-006(a) no-steal reference on clean E1 notes by 7-51 %; 3 ms halves the slope (ratios 0.53-0.76).
    static constexpr float kDeclickMs = 3.0f;
    static_assert(kControlInterval == ProfundumCore::kControlInterval);
    static_assert(kControlInterval == AnchorOscillator::kControlInterval);
    static_assert(kControlInterval == BassStereoField::kControlInterval);
    static constexpr std::size_t kMaxChunk = kControlInterval;
    static constexpr int kNumPartials = ProfundumCore::kDefaultPartials;     // 64
    static constexpr float kHalfWaveOnDb = -22.0f;                           // C-5 (inside the -24 ... -16 band)
    static constexpr float kHalfWaveOffDb = -18.0f;
    /// G14-13 (user ruling 2026-10-21, FR-007): a new FOLLOW ratio is handed to the Anchor only after it has held
    /// for this many consecutive periods of f_res (4 / f0: 194 ms at E0, 48 ms at E2).
    static constexpr double kFollowRatioDwellPeriods = 4.0;
    static constexpr float kMeterFloor = 1.0e-6f;                            // h1 below this: keep the flag
    static_assert(DropPhaseLock::kPitchUpdateInterval == ProfundumCore::kPitchUpdateInterval);

    /// Plan S9.6 (C-5): period-synchronous DFT of h1 and h2 over each period of the clamped f_core, with
    /// hysteresis. Double precision. The rotor step is recomputed by setFrequency (the voice calls it at each
    /// 16-sample pitch update of the core and at each core note-on).
    class HarmonicBalanceMeter {
    public:
        void prepare(double sampleRate) noexcept {
            fs_ = (detail::isFinite(sampleRate) && sampleRate > 0.0) ? sampleRate : 48000.0;
            reset();
        }
        /// theta = 0, sums 0, flag false; the frequency is cleared (the meter holds until setFrequency).
        void reset() noexcept {
            theta_ = 0.0;
            zRe_ = 1.0;
            zIm_ = 0.0;
            wRe_ = 1.0;
            wIm_ = 0.0;
            z1Re_ = z1Im_ = z2Re_ = z2Im_ = 0.0;
            inc_ = 0.0;
            count_ = 0;
            freqHz_ = 0.0f;
            flag_ = false;
        }
        /// f in Hz, already clamped by the voice to [kMinF0Hz, capHz]. A non-finite value is ignored.
        void setFrequency(float hz) noexcept {
            if (!detail::isFinite(hz) || !(hz > 0.0f))
                return;
            freqHz_ = hz;
            inc_ = static_cast<double>(hz) / fs_;
            const double a = kTwoPi * inc_;
            wRe_ = std::cos(a);
            wIm_ = -std::sin(a);
        }
        /// One input sample (the mid of the Character input). Holds (no accumulation, no reading) for f/fs >= 0.5.
        void process(float x) noexcept {
            if (!(inc_ > 0.0 && inc_ < 0.5))
                return;
            const auto xd = static_cast<double>(x);
            z1Re_ += xd * zRe_;
            z1Im_ += xd * zIm_;
            const double z2r = zRe_ * zRe_ - zIm_ * zIm_;
            const double z2i = 2.0 * zRe_ * zIm_;
            z2Re_ += xd * z2r;
            z2Im_ += xd * z2i;
            ++count_;
            theta_ += inc_;
            const double nr = zRe_ * wRe_ - zIm_ * wIm_;
            zIm_ = zRe_ * wIm_ + zIm_ * wRe_;
            zRe_ = nr;
            if (theta_ >= 1.0)
                endPeriod();
        }
        [[nodiscard]] bool halfWaveSymmetric() const noexcept { return flag_; }
        /// theta in [0, 1) as a float (a double just below 1 never rounds up to 1.0f).
        [[nodiscard]] float theta() const noexcept {
            const auto t = static_cast<float>(theta_);
            return t < 1.0f ? t : 0.99999994f;
        }
        [[nodiscard]] float frequencyHz() const noexcept { return freqHz_; }
        [[nodiscard]] bool stateFinite() const noexcept {
            return detail::isFinite(theta_) && detail::isFinite(zRe_) && detail::isFinite(zIm_)
                   && detail::isFinite(z1Re_) && detail::isFinite(z1Im_) && detail::isFinite(z2Re_)
                   && detail::isFinite(z2Im_);
        }

    private:
        static constexpr double kTwoPi = 6.283185307179586476925;

        void endPeriod() noexcept {
            const double m1 = std::sqrt(z1Re_ * z1Re_ + z1Im_ * z1Im_);
            if (m1 > static_cast<double>(kMeterFloor) * static_cast<double>(count_)) {
                // r = 20 log10(|Z2| / |Z1|), compared as an amplitude ratio (no log of 0).
                const double m2 = std::sqrt(z2Re_ * z2Re_ + z2Im_ * z2Im_);
                if (m2 <= m1 * kOnRatio)
                    flag_ = true;
                else if (m2 >= m1 * kOffRatio)
                    flag_ = false;
            }
            theta_ -= std::floor(theta_);   // never a single -= 1 (S9.6)
            z1Re_ = z1Im_ = z2Re_ = z2Im_ = 0.0;
            count_ = 0;
            const double mag = std::sqrt(zRe_ * zRe_ + zIm_ * zIm_);
            if (mag > 0.0 && detail::isFinite(mag)) {
                zRe_ /= mag;
                zIm_ /= mag;
            } else {
                zRe_ = std::cos(kTwoPi * theta_);
                zIm_ = -std::sin(kTwoPi * theta_);
            }
        }

        static constexpr double kOnRatio = 0.079432823472428150;    // 10^(-22/20)
        static constexpr double kOffRatio = 0.12589254117941673;    // 10^(-18/20)

        double fs_ = 48000.0;
        double theta_ = 0.0;
        double zRe_ = 1.0, zIm_ = 0.0;
        double wRe_ = 1.0, wIm_ = 0.0;
        double z1Re_ = 0.0, z1Im_ = 0.0, z2Re_ = 0.0, z2Im_ = 0.0;
        double inc_ = 0.0;
        std::uint32_t count_ = 0;
        float freqHz_ = 0.0f;
        bool flag_ = false;
    };

    /// Handed by the engine at each grid point (S6.5 step 7).
    struct IntervalInputs {
        const MorphState* state = nullptr;                    // after macros and Motion, before the Mass biases
        SpectralShapeRecipe::Controls coreControls{};         // recipe with depth / body := law targets
        float foldAmount = 0.0f;                              // law target
        float anchorLevel = 0.0f;                             // FR-027 Mass-biased level
        const BassStereoField* field = nullptr;               // const: computePanVector / wideOffsets
        bool forceLinearCore = false;                         // seam (FR-086)
        const float* sharedShape = nullptr;                   // T032: evaluateShape(coreControls) by the engine, kNumPartials
        const float* sharedA1Shape = nullptr;                 // T032: evaluateShape(state->recipe) by the engine, kNumPartials
    };

    // ---- control thread ----

    /// Plan S5.3. NOT real-time safe: ProfundumCore::prepare calibrates its bank.
    void prepare(double sampleRate, const MorphState& initial) noexcept {
        // Step 1: components that snap at prepare get `initial` before their prepare (the Phase 8 fixture rule).
        shape_.setShape(initial.shape);
        character_.setAlgorithm(initial.characterAlgorithm);
        character_.setAmount(initial.characterAmount);
        character_.setTone(initial.characterTone);
        fold_.setAmount(initial.foldAmount);

        // Step 2.
        fs_ = sampleRate;
        const double fsCore = std::clamp(sampleRate, ProfundumCore::kMinSampleRate, ProfundumCore::kMaxSampleRate);
        capHz_ = SpectralShapeRecipe::capFrequency(fsCore);   // the core's own expression (its capHz_ is private)
        core_.prepare(sampleRate, kNumPartials);
        shape_.prepare(sampleRate, kMaxChunk);
        fold_.prepare(sampleRate, kMaxChunk);
        character_.prepare(sampleRate, kMaxChunk);
        anchor_.prepare(sampleRate);
        drop_.prepare(sampleRate);
        lock_.prepare(sampleRate);
        declick_.prepare(sampleRate, kDeclickMs);   // R-B: 3 ms (Phase 4's own default stays 1.5 ms)
        ampEnv_.prepare(static_cast<float>(sampleRate));
        ampEnv_.setRetriggerMode(RetriggerMode::Hard);   // Phase 4 D-10
        meter_.prepare(fsCore);

        // Steps 3 - 4: the post-prepare setters, then reset (which re-hands them, so reset == post-prepare).
        reset(initial);
    }

    /// Control thread; allocation-free and bounded (the contract of ProfundumCore::reset). On the audio thread a
    /// sounding voice is cleared by a Reset-policy note event, never by reset (S5.3).
    void reset(const MorphState& initial) noexcept {
        // The snap-at-reset components take `initial` first; their reset snaps the smoothers to it.
        shape_.setShape(initial.shape);
        character_.setAlgorithm(initial.characterAlgorithm);
        character_.setAmount(initial.characterAmount);
        character_.setTone(initial.characterTone);
        character_.setChannelOffsets(0.0f, 0.0f);   // post-prepare offsets: reset() keeps them and engages on them
        character_.setFundamentalHz(BassCharacterStage::kDefaultFundamentalHz);   // reset() re-keys on the kept f0
        fold_.setAmount(initial.foldAmount);
        // S5.3 step 3: setters that are no-ops before prepare.
        core_.setControls(initial.recipe);
        core_.setExcitation(initial.excitation);
        anchor_.setControls(initial.anchor);
        drop_.setAmount(initial.dropAmountSt);
        drop_.setTime(initial.dropTimeMs);
        drop_.setShape(initial.dropShape);
        drop_.setRebound(initial.dropRebound);
        ampEnv_.setAttack(initial.ampEnv.attackMs);
        ampEnv_.setDecay(initial.ampEnv.decayMs);
        ampEnv_.setSustain(initial.ampEnv.sustain);
        ampEnv_.setRelease(initial.ampEnv.releaseMs);
        ampEnv_.setAttackCurve(initial.ampEnv.attackCurve);   // the table path for every value, 0.7 included (Q6)
        ampEnv_.setDecayCurve(initial.ampEnv.decayCurve);
        ampEnv_.setReleaseCurve(initial.ampEnv.releaseCurve);
        lock_.setBypass(initial.anchor.mode == AnchorOscillator::Mode::Follow && !seams_.followNoBypass);

        core_.reset();
        shape_.reset();
        fold_.reset();
        character_.reset();
        anchor_.reset();
        drop_.reset();
        lock_.reset();
        declick_.reset();
        ampEnv_.reset();
        fieldState_.reset();
        meter_.reset();

        // Transition state.
        pending_ = NoteMemory::Actions{};
        dipPending_ = false;
        untilSwitch_ = 0;
        coreNoteOnPending_ = false;
        anchorNoteOnPending_ = false;
        anchorRearm_ = false;
        lockTriggerPending_ = false;
        lockKind_ = DropPhaseLock::TriggerKind::SnapReset;
        coreSounding_ = false;
        syncPhasePending_ = false;
        fieldResetPending_ = false;
        envelopeRestarted_ = false;
        held_ = false;

        // Pitch, interval and handed state: the first interval hands everything (C-20).
        fNote_ = 0.0f;
        coreGridCount_ = 0;
        lastFCore_ = 0.0f;
        feedFrom_ = initial.anchorFoldFeed;
        feedTo_ = initial.anchorFoldFeed;
        feedSkip_ = true;
        followRouting_ = false;
        k_ = 0;
        intervalLen_ = 0;
        pos_ = 0;
        field_ = nullptr;
        a1Valid_ = false;
        a1_ = 0.0f;
        handedValid_ = false;
        pan_.fill(0.0f);
        lastPan_.fill(0.0f);
        clearScratch();
    }

    void setNoteMemory(NoteMemory::Mode m, NoteMemory::Retrigger r) noexcept {
        memory_.setMode(m);
        memory_.setRetrigger(r);
    }

    /// FR-086: configuration-time. The seams are copied (all off when s is null).
    void setObserverSlot(ProfundumEngineObserver* o, std::uint8_t index, const ProfundumEngineSeams* s) noexcept {
        observer_ = o;
        index_ = index;
        seams_ = (s != nullptr) ? *s : ProfundumEngineSeams{};
    }

    /// The engine's sample clock for call / tap records (otherwise the voice's own rendered-sample count).
    void setSamplePosition(std::uint64_t sample) noexcept { pos_ = sample; }

    // ---- note events, at a grid point (S5.5) ----

    void noteEvent(NoteMemory::Transition t, float fNote, bool stealOriginated) noexcept {
        fNote_ = fNote;
        held_ = true;
        // 1.
        const NoteMemory::Actions a = memory_.resolve(t, ampEnv_.isActive());
        // 2. C-4: the steal seam suppresses the dip for steal-originated events, the declick seam for every other.
        const bool suppressed = stealOriginated ? seams_.disableStealFade : seams_.disableDeclick;
        if (a.declick && !suppressed) {
            declick_.arm();   // a no-op while a dip runs: the switch sample stays
            pending_ = a;
            dipPending_ = true;
            untilSwitch_ = declick_.samplesUntilSwitch();
            return;
        }
        // 3. During a dip the new transition replaces the pending one (last call wins, Phase 4 FR-043).
        if (dipPending_) {
            pending_ = a;
            return;
        }
        // 4. At sample 0 of this interval.
        applyTransition(a);
    }

    /// Amp-envelope gate off.
    void release() noexcept {
        ampEnv_.gate(false);
        held_ = false;
    }

    [[nodiscard]] bool audible() const noexcept { return ampEnv_.isActive(); }   // Phase 4 spec line 849
    [[nodiscard]] bool held() const noexcept { return held_; }
    [[nodiscard]] bool active() const noexcept {
        return ampEnv_.isActive() || dipPending_ || declick_.isActive() || coreNoteOnPending_;
    }
    /// True once after an envelope-restarting transition was applied (FR-045, D-5): the engine then calls
    /// motion.noteRestart(). Clears the flag.
    [[nodiscard]] bool consumeEnvelopeRestart() noexcept {
        const bool r = envelopeRestarted_;
        envelopeRestarted_ = false;
        return r;
    }
    [[nodiscard]] float noteFrequency() const noexcept { return fNote_; }

    // ---- per interval (S5.4) ----

    void beginInterval(const IntervalInputs& in, const float* fRes, std::size_t n) noexcept {
        if (in.state == nullptr || fRes == nullptr || n == 0)
            return;
        const MorphState& s = *in.state;
        field_ = in.field;
        k_ = 0;
        intervalLen_ = std::min(n, kMaxChunk);
        std::copy_n(fRes, intervalLen_, fResBuf_.begin());
        followRouting_ = s.anchor.mode == AnchorOscillator::Mode::Follow || seams_.lockReadsCore;
        const bool all = !handedValid_;

        // 0. A transition due at sample 0 (a switch landing on the grid point), then the pitch stage up to the
        //    switch sample or the whole interval.
        if (dipPending_ && untilSwitch_ == 0) {
            dipPending_ = false;
            applyTransition(pending_);
        }
        // 9 (moved ahead of the pitch stage, G14-3 2026-10-21). Drop setters, latched by the component at its next
        // trigger: a transition's Drop trigger is deferred to flushDropTrigger() so that a voice which was idle (its
        // setters not run since the state changed) triggers with this interval's amount / time / shape / rebound, not
        // the ones it last applied. The POLY soak's lock notes dropped by the activity's stale amount (+3.3 / +33.5 st
        // for 1 - 2.5 s, 94 deg of SC-014 drift) before this.
        {
            bool issue = false;
            if (all || !same(s.dropAmountSt, lastDropAmount_)) {
                drop_.setAmount(s.dropAmountSt);
                lastDropAmount_ = s.dropAmountSt;
                issue = true;
            }
            if (all || !same(s.dropTimeMs, lastDropTime_)) {
                drop_.setTime(s.dropTimeMs);
                lastDropTime_ = s.dropTimeMs;
                issue = true;
            }
            if (all || !same(s.dropShape, lastDropShape_)) {
                drop_.setShape(s.dropShape);
                lastDropShape_ = s.dropShape;
                issue = true;
            }
            if (all || !same(s.dropRebound, lastDropRebound_)) {
                drop_.setRebound(s.dropRebound);
                lastDropRebound_ = s.dropRebound;
                issue = true;
            }
            logCall(ProfundumCall::DropSetters, issue, s.dropAmountSt);
        }
        flushDropTrigger();
        runPitch(0, dipPending_ ? std::min(untilSwitch_, intervalLen_) : intervalLen_);

        // 1. FR-012: the field state resets under the core's Reset branch; pan vector; WIDE offsets.
        if (fieldResetPending_) {
            fieldState_.reset();
            fieldResetPending_ = false;
        }
        panStep(fCore_[0], all);
        {
            const BassStereoField::ChannelOffsets o = (field_ != nullptr)
                                                          ? field_->wideOffsets(s.characterAlgorithm)
                                                          : BassStereoField::ChannelOffsets{.amount = 0.0f, .tone = 0.0f};
            const bool issue = all || !same(o.amount, lastWide_.amount) || !same(o.tone, lastWide_.tone);
            if (issue) {
                character_.setChannelOffsets(o.amount, o.tone);
                lastWide_ = o;
            }
            logCall(ProfundumCall::VoiceWide, issue, o.amount);
        }

        // 2. Core controls + excitation (mix 0 under forceLinearCore).
        {
            const SpectralShapeRecipe::Controls& c = in.coreControls;
            const bool issue = all || !sameRecipe(c, lastCoreControls_);
            if (issue) {
                if (in.sharedShape != nullptr)   // T032: the engine's one evaluation for every voice
                    core_.setControlsWithShape(c, std::span<const float>(in.sharedShape, static_cast<std::size_t>(kNumPartials)));
                else
                    core_.setControls(c);
                lastCoreControls_ = c;
            }
            logCall(ProfundumCall::CoreControls, issue, c.depth);
        }
        {
            ProfundumCore::ExcitationControls e = s.excitation;
            if (in.forceLinearCore)
                e.mix = 0.0f;
            const bool issue = all || !same(e.mix, lastExcitation_.mix) || !same(e.drive, lastExcitation_.drive)
                               || !same(e.asymmetry, lastExcitation_.asymmetry);
            if (issue) {
                core_.setExcitation(e);   // dirties on every call (profundum_core.h setExcitation): change-only
                lastExcitation_ = e;
            }
            logCall(ProfundumCall::CoreExcitation, issue, e.mix);
        }

        // 3. Fold amount, Fold f0 (FR-006: once per interval from f_core), SHAPE.
        {
            const bool issue = all || !same(in.foldAmount, lastFoldAmount_);
            if (issue) {
                fold_.setAmount(in.foldAmount);
                lastFoldAmount_ = in.foldAmount;
            }
            logCall(ProfundumCall::FoldAmount, issue, in.foldAmount);
        }
        {
            const float f = fCore_[0];
            const bool issue = all || !same(f, lastFoldF0_);
            if (issue) {
                fold_.setFundamentalHz(f);
                lastFoldF0_ = f;
            }
            logCall(ProfundumCall::FoldF0, issue, f);
        }
        {
            const bool issue = all || !same(s.shape, lastShape_);
            if (issue) {
                shape_.setShape(s.shape);
                lastShape_ = s.shape;
            }
            logCall(ProfundumCall::Shape, issue, s.shape);
        }

        // 4. Character: the algorithm only when it differs from the last request (FR-013), Amount, Tone.
        {
            const bool issue = all || s.characterAlgorithm != lastAlgorithm_;
            if (issue) {
                character_.setAlgorithm(s.characterAlgorithm);
                lastAlgorithm_ = s.characterAlgorithm;
            }
            logCall(ProfundumCall::CharAlgorithm, issue,
                    static_cast<float>(static_cast<std::uint8_t>(s.characterAlgorithm)));
        }
        {
            const bool issue = all || !same(s.characterAmount, lastCharAmount_);
            if (issue) {
                character_.setAmount(s.characterAmount);   // retargets on every call: change-only (C-20)
                lastCharAmount_ = s.characterAmount;
            }
            logCall(ProfundumCall::CharAmount, issue, s.characterAmount);
        }
        {
            const bool issue = all || !same(s.characterTone, lastCharTone_);
            if (issue) {
                character_.setTone(s.characterTone);
                lastCharTone_ = s.characterTone;
            }
            logCall(ProfundumCall::CharTone, issue, s.characterTone);
        }

        // 5. Rectify input rule (C-5).
        {
            const bool hw = meter_.halfWaveSymmetric();
            const bool issue = all || hw != lastHalfWave_;
            if (issue) {
                character_.setInputHalfWaveSymmetric(hw);
                lastHalfWave_ = hw;
            }
            logCall(ProfundumCall::HalfWave, issue, hw ? 1.0f : 0.0f);
        }

        // 6. FOLLOW ratio (FR-007), read after steps 4 - 5. G14-13 (2026-10-21): with the Anchor fed into the
        //    Fold (anchorFoldFeed) the FR-008 meter reads a signal that contains the Anchor, and at a hot setting
        //    (Rectify, FOLLOW, Mass 100, 96 kHz E1) flag -> ratio -> Fold input -> flag flipped every period, the
        //    Anchor alternating octaves. A changed ratio is handed only after kFollowRatioDwellPeriods of f_res.
        {
            const float r = character_.perceivedPitchRatio();
            if (!same(r, followCandidate_)) {
                followCandidate_ = r;
                followCandidateSamples_ = 0;
            }
            bool issue = all;
            if (all) {
                anchor_.setFollowRatio(r);
                lastFollowRatio_ = r;
            } else if (!same(r, lastFollowRatio_)) {
                const double f = detail::isFinite(fRes[0]) && fRes[0] > 0.0f ? static_cast<double>(fRes[0]) : 1.0;
                if (static_cast<double>(followCandidateSamples_) >= kFollowRatioDwellPeriods * fs_ / f) {
                    anchor_.setFollowRatio(r);
                    lastFollowRatio_ = r;
                    issue = true;
                }
            }
            followCandidateSamples_ += static_cast<std::uint64_t>(n);
            logCall(ProfundumCall::FollowRatio, issue, r);
        }

        // 7. Anchor controls with the Mass-biased level (copy, then assign); the lock bypass (FR-006).
        {
            AnchorOscillator::Controls c = s.anchor;
            c.level = in.anchorLevel;
            const AnchorOscillator::Controls& p = lastAnchor_;
            const bool issue = all || c.waveform != p.waveform || c.mode != p.mode || !same(c.level, p.level)
                               || !same(c.punchBoostDb, p.punchBoostDb) || !same(c.punchTimeMs, p.punchTimeMs)
                               || !same(c.ghostTimeMs, p.ghostTimeMs);
            if (issue) {
                anchor_.setControls(c);
                lastAnchor_ = c;
            }
            logCall(ProfundumCall::AnchorControls, issue, c.level);
        }
        {
            const bool bypass = s.anchor.mode == AnchorOscillator::Mode::Follow && !seams_.followNoBypass;
            const bool issue = all || bypass != lastLockBypass_;
            if (issue) {
                lock_.setBypass(bypass);
                lastLockBypass_ = bypass;
            }
            logCall(ProfundumCall::LockBypass, issue, bypass ? 1.0f : 0.0f);
        }

        // 8. a1 (FR-005, C-6, S9.7): cached shape (recipe key) and mask (f key), spans of exactly kNumPartials.
        {
            const float fA = fAnchor_[0];
            const bool shapeChanged = !a1Valid_ || !sameRecipe(s.recipe, a1RecipeKey_);
            const bool maskChanged = !a1Valid_ || !same(fA, a1FreqKey_);
            constexpr auto kN = static_cast<std::size_t>(kNumPartials);
            if (shapeChanged) {
                if (in.sharedA1Shape != nullptr)   // T032: the engine's one evaluation of this same recipe
                    std::copy_n(in.sharedA1Shape, kN, a1Shape_.data());
                else
                    SpectralShapeRecipe::evaluateShape(s.recipe, std::span<float>(a1Shape_.data(), kN));
                a1RecipeKey_ = s.recipe;
            }
            if (maskChanged) {
                SpectralShapeRecipe::evaluateMask(fA, fs_, std::span<float>(a1Mask_.data(), kN));
                a1FreqKey_ = fA;
            }
            if (shapeChanged || maskChanged) {
                SpectralShapeRecipe::applyMask(std::span<const float>(a1Shape_.data(), kN),
                                               std::span<const float>(a1Mask_.data(), kN),
                                               std::span<float>(a1Out_.data(), kN));
                a1_ = a1Out_[0] * SpectralShapeRecipe::kCenterPanGain;
            }
            a1Valid_ = true;
            const bool issue = all || !same(a1_, lastA1_);
            if (issue) {
                anchor_.setCoreH1Amplitude(a1_);
                lastA1_ = a1_;
            }
            logCall(ProfundumCall::A1, issue, a1_);
        }

        // 10. Amp envelope: times and sustain (immediate receivers), then each curve only on a bitwise change.
        {
            const MorphState::AmpEnvelopeFields& e = s.ampEnv;
            MorphState::AmpEnvelopeFields& p = lastAmpEnv_;
            bool issue = false;
            if (all || !same(e.attackMs, p.attackMs)) {
                ampEnv_.setAttack(e.attackMs);
                p.attackMs = e.attackMs;
                issue = true;
            }
            if (all || !same(e.decayMs, p.decayMs)) {
                ampEnv_.setDecay(e.decayMs);
                p.decayMs = e.decayMs;
                issue = true;
            }
            if (all || !same(e.sustain, p.sustain)) {
                ampEnv_.setSustain(e.sustain);
                p.sustain = e.sustain;
                issue = true;
            }
            if (all || !same(e.releaseMs, p.releaseMs)) {
                ampEnv_.setRelease(e.releaseMs);
                p.releaseMs = e.releaseMs;
                issue = true;
            }
            logCall(ProfundumCall::AmpEnv, issue, e.attackMs);
            bool curveIssue = false;
            if (all || !same(e.attackCurve, p.attackCurve)) {
                ampEnv_.setAttackCurve(e.attackCurve);
                p.attackCurve = e.attackCurve;
                curveIssue = true;
            }
            if (all || !same(e.decayCurve, p.decayCurve)) {
                ampEnv_.setDecayCurve(e.decayCurve);
                p.decayCurve = e.decayCurve;
                curveIssue = true;
            }
            if (all || !same(e.releaseCurve, p.releaseCurve)) {
                ampEnv_.setReleaseCurve(e.releaseCurve);
                p.releaseCurve = e.releaseCurve;
                curveIssue = true;
            }
            logCall(ProfundumCall::AmpCurve, curveIssue, e.attackCurve);
        }

        // 11. Fold-feed ramp endpoints (FR-003, S9.5); the add is skipped when both ends are +0 (C-10).
        feedFrom_ = feedTo_;
        feedTo_ = s.anchorFoldFeed;
        feedSkip_ = seams_.anchorDetached
                    || (std::bit_cast<std::uint32_t>(feedFrom_) == 0u && std::bit_cast<std::uint32_t>(feedTo_) == 0u);

        handedValid_ = true;
    }

    // ---- render (S5.6): adds into the sum buffers; n <= kMaxChunk, never crossing a grid point ----

    void render(const float* fRes, float* sumL, float* sumR, std::size_t n) noexcept {
        if (sumL == nullptr || sumR == nullptr || n == 0 || k_ >= intervalLen_)
            return;
        n = std::min(n, intervalLen_ - k_);
        if (fRes != nullptr)
            std::copy_n(fRes, n, fResBuf_.begin() + static_cast<std::ptrdiff_t>(k_));
        std::size_t done = 0;
        while (done < n) {
            if (dipPending_ && untilSwitch_ == 0)
                applySwitch(k_);
            std::size_t m = n - done;
            if (dipPending_)
                m = std::min(m, untilSwitch_);
            renderSub(k_, m, sumL + done, sumR + done);
            done += m;
            k_ += m;
            pos_ += m;
            if (dipPending_)
                untilSwitch_ -= m;
        }
    }

    [[nodiscard]] float currentCoreF0() const noexcept { return lastFCore_; }

    [[nodiscard]] bool stateFinite() const noexcept {
        return core_.stateFinite() && shape_.stateFinite() && fold_.stateFinite() && character_.stateFinite()
               && anchor_.stateFinite() && meter_.stateFinite();
    }

private:
    [[nodiscard]] static bool same(float a, float b) noexcept {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    }
    [[nodiscard]] static bool sameRecipe(const SpectralShapeRecipe::Controls& a,
                                         const SpectralShapeRecipe::Controls& b) noexcept {
        return same(a.depth, b.depth) && same(a.body, b.body) && same(a.bodyCurvature, b.bodyCurvature)
               && same(a.bodyEmphasis, b.bodyEmphasis) && same(a.edge, b.edge) && same(a.shift, b.shift);
    }

    void logCall(ProfundumCall c, bool issued, float value) noexcept {
        if (observer_ == nullptr)
            return;
        observer_->onCall(ProfundumCallRecord{.sample = pos_, .voice = index_, .call = c, .issued = issued,
                                              .value = value});
    }

    [[nodiscard]] std::size_t gridPhase() const noexcept {   // the Phase 4 harness form
        constexpr std::size_t U = ProfundumCore::kPitchUpdateInterval;
        return (U - static_cast<std::size_t>(coreGridCount_) % U) % U;
    }

    /// S9.4 for interval positions [from, to): a pending lock trigger first, then per sample d, d_c, f_core, f_a.
    void runPitch(std::size_t from, std::size_t to) noexcept {
        if (from >= to)
            return;
        if (lockTriggerPending_) {
            lock_.trigger(drop_.plannedCurve(), fResBuf_[from], gridPhase(), ProfundumCore::kMinF0Hz, capHz_,
                          lockKind_);
            lockTriggerPending_ = false;
        }
        for (std::size_t i = from; i < to; ++i) {
            d_[i] = drop_.process();
            dc_[i] = lock_.process(d_[i]);
            fCore_[i] = fResBuf_[i] * std::exp2(dc_[i] / 12.0f);   // C-11
            fAnchor_[i] = followRouting_ ? fCore_[i] : fResBuf_[i];
        }
    }

    /// FR-012 step 2, change-only (C-20).
    void panStep(float fCore0, bool all) noexcept {
        if (field_ != nullptr)
            field_->computePanVector(fieldState_, fCore0, pan_, kNumPartials);
        else
            pan_.fill(0.0f);
        bool changed = all;
        for (std::size_t k = 0; !changed && k < pan_.size(); ++k)
            changed = !same(pan_[k], lastPan_[k]);
        if (changed) {
            core_.setPartialPanOffsets(pan_);
            lastPan_ = pan_;
        }
        logCall(ProfundumCall::VoicePan, changed, pan_[1]);
    }

    /// S5.5 "applying a transition" (the Phase 4 harness's applyTransition).
    void applyTransition(const NoteMemory::Actions& a) noexcept {
        core_.setRetriggerPhase(static_cast<ProfundumCore::RetriggerPhase>(static_cast<std::uint8_t>(a.phase)));
        anchor_.setRetriggerPhase(static_cast<AnchorOscillator::RetriggerPhase>(static_cast<std::uint8_t>(a.phase)));
        const bool resetBranch = a.phase == NoteMemory::PhasePolicy::Reset || !coreSounding_;
        if (resetBranch) {
            coreGridCount_ = 0;          // the core's grid restarts at the note
            fieldResetPending_ = true;   // FR-012 step 1
            syncPhasePending_ = true;    // FR-008
        }
        if (a.dropTrigger) {
            // G14-3: deferred to flushDropTrigger(), after this interval's Drop setters (beginInterval) or at once
            // at the declick switch (applySwitch), where the setters of this interval are already in place.
            dropTriggerPending_ = true;
            dropSnapPending_ = a.dropSnap;
            dropResetBranch_ = resetBranch;
        }
        coreNoteOnPending_ = true;
        anchorNoteOnPending_ = true;
        anchorRearm_ = a.anchorRearm;
        if (a.envelopeRestart) {
            ampEnv_.gate(true);   // Hard: from the current level on the table path
            envelopeRestarted_ = true;
        }
        coreSounding_ = true;
    }

    /// G14-3: the Drop trigger a transition deferred, with the lock trigger it implies (runPitch runs the lock's solve).
    void flushDropTrigger() noexcept {
        if (!dropTriggerPending_)
            return;
        dropTriggerPending_ = false;
        drop_.trigger(dropSnapPending_);
        lockTriggerPending_ = true;
        lockKind_ = !dropSnapPending_ ? DropPhaseLock::TriggerKind::Bridged
                                      : (dropResetBranch_ ? DropPhaseLock::TriggerKind::SnapReset
                                                          : DropPhaseLock::TriggerKind::SnapFreeRunning);
    }

    /// S5.6 step 1: the deferred transition at the switch sample (interval position k).
    void applySwitch(std::size_t k) noexcept {
        dipPending_ = false;
        // Main-loop defect fix (2026-10-20, P-2 locator): the dip has brought g_d to 0 at this sample, but the
        // chain's own memory (ShapeMorph filters, HarmonicFold, the Character stage's oversampler and DC blockers)
        // still holds the old note, so when g_d returns to 1 the old content reappeared at full level (a 0.28
        // step on the sum at E1). The chain is cleared here, at the one sample where its output is muted, so the
        // new note enters a silent chain exactly as the first note did.
        shape_.reset();
        fold_.reset();
        character_.reset();
        applyTransition(pending_);
        flushDropTrigger();   // G14-3: this interval's Drop setters were applied at its head
        runPitch(k, intervalLen_);
        if (fieldResetPending_) {
            fieldResetPending_ = false;
            fieldState_.reset();
            panStep(fCore_[k], false);
        }
    }

    [[nodiscard]] float meterFrequency(float fCore) const noexcept {
        if (!detail::isFinite(fCore))
            return meter_.frequencyHz() > 0.0f ? meter_.frequencyHz() : ProfundumCore::kMinF0Hz;
        return std::clamp(fCore, ProfundumCore::kMinF0Hz, capHz_);
    }

    /// S5.6 steps 2 - 11 for interval positions [k0, k0 + m), adding into outL / outR [0, m).
    void renderSub(std::size_t k0, std::size_t m, float* outL, float* outR) noexcept {
        // 2. Pending note-ons at offset 0 of the sub-chunk.
        const bool noteOnNow = coreNoteOnPending_;
        if (syncPhasePending_) {
            character_.syncPhase(0.0f);   // FR-008: the core's h1 phase is 0 by construction here
            syncPhasePending_ = false;
            logCall(ProfundumCall::SyncPhase, true, 0.0f);
        }
        if (coreNoteOnPending_) {
            core_.noteOn(fCore_[k0]);
            coreNoteOnPending_ = false;
            logCall(ProfundumCall::NoteOnCore, true, fCore_[k0]);
        }
        if (anchorNoteOnPending_) {
            anchor_.noteOn(fAnchor_[k0], anchorRearm_);
            anchorNoteOnPending_ = false;
            logCall(ProfundumCall::NoteOnAnchor, true, fAnchor_[k0]);
        }

        // 3. Core -> core tap.
        float* const cL = coreL_.data() + k0;
        float* const cR = coreR_.data() + k0;
        core_.processBlock(cL, cR, m, fCore_.data() + k0);
        const std::uint32_t gridAtStart = coreGridCount_;
        coreGridCount_ += static_cast<std::uint32_t>(m);

        // 4. Main path: ShapeMorph in place.
        float* const xL = mainL_.data() + k0;
        float* const xR = mainR_.data() + k0;
        std::copy_n(cL, m, xL);
        std::copy_n(cR, m, xR);
        shape_.processBlock(xL, xR, m, fCore_.data() + k0);

        // 5. Anchor (rendered once; used for the feed and the direct path) -> Anchor tap.
        float* const aL = anchorL_.data() + k0;
        float* const aR = anchorR_.data() + k0;
        if (seams_.anchorPhaseTap) {
            for (std::size_t i = 0; i < m; ++i) {
                anchor_.processBlock(aL + i, aR + i, 1, fAnchor_.data() + k0 + i);
                anchorPhase_[k0 + i] = static_cast<float>(anchor_.phase());
            }
        } else {
            anchor_.processBlock(aL, aR, m, fAnchor_.data() + k0);
            std::fill_n(anchorPhase_.data() + k0, m, static_cast<float>(anchor_.phase()));
        }

        // 5b. The Rectify meter (C-5, S9.6) on the Shape output mid, BEFORE the Anchor feed (G14-13, 2026-10-21):
        //     the Fold preserves half-wave symmetry (odd sine fold, soft saturation, a gain), the Anchor at ratio 2
        //     is the even harmonic that broke it, and a meter that saw the Anchor fed back into the ratio.
        constexpr std::uint32_t U = static_cast<std::uint32_t>(ProfundumCore::kPitchUpdateInterval);
        for (std::size_t i = 0; i < m; ++i) {
            if ((gridAtStart + static_cast<std::uint32_t>(i)) % U == 0u || (i == 0 && noteOnNow))
                meter_.setFrequency(meterFrequency(fCore_[k0 + i]));
            meter_.process(0.5f * (xL[i] + xR[i]));
        }

        // 6. Fold feed (FR-003, C-10, S9.5): skipped, not multiplied by 0.
        if (feedSkip_) {
            std::fill_n(feed_.data() + k0, m, 0.0f);
        } else {
            for (std::size_t i = 0; i < m; ++i) {
                const std::size_t k = k0 + i;
                const float g = feedFrom_ + (feedTo_ - feedFrom_) * static_cast<float>(k + 1) / 32.0f;
                feed_[k] = g;
                xL[i] += g * aL[i];
                xR[i] += g * aR[i];
            }
        }

        // 7. HarmonicFold (the Rectify meter moved to 5b, G14-13).
        fold_.processBlock(xL, xR, m);

        // 8. BassCharacterStage -> Character tap c.
        character_.processBlock(xL, xR, m, fCore_.data() + k0);

        // 9 - 10. Gains and the FR-015 output (C-10: literally (c * g_amp) * g_d + (a * g_amp) * g_d).
        const bool detached = seams_.anchorDetached;
        for (std::size_t i = 0; i < m; ++i) {
            const std::size_t k = k0 + i;
            const float ga = ampEnv_.process();
            const float gd = declick_.isActive() ? declick_.process() : 1.0f;   // steal fade == 1.0f (C-4)
            gAmp_[k] = ga;
            gD_[k] = gd;
            if (detached) {
                outL[i] += (xL[i] * ga) * gd;
                outR[i] += (xR[i] * ga) * gd;
            } else {
                outL[i] += (xL[i] * ga) * gd + (aL[i] * ga) * gd;
                outR[i] += (xR[i] * ga) * gd + (aR[i] * ga) * gd;
            }
        }

        // 11.
        lastFCore_ = fCore_[k0 + m - 1];

        if (observer_ != nullptr) {
            const auto tap = [m, k0](const std::array<float, kMaxChunk>& a) noexcept {
                return std::span<const float>(a.data() + k0, m);
            };
            observer_->onVoiceChunk(ProfundumVoiceTaps{.voice = index_,
                                                       .sample = pos_,
                                                       .n = m,
                                                       .coreL = tap(coreL_),
                                                       .coreR = tap(coreR_),
                                                       .charL = tap(mainL_),
                                                       .charR = tap(mainR_),
                                                       .anchorL = tap(anchorL_),
                                                       .anchorR = tap(anchorR_),
                                                       .gAmp = tap(gAmp_),
                                                       .gD = tap(gD_),
                                                       .fRes = tap(fResBuf_),
                                                       .fCore = tap(fCore_),
                                                       .fAnchor = tap(fAnchor_),
                                                       .anchorPhase = tap(anchorPhase_),
                                                       .feed = tap(feed_),
                                                       .followRatio = lastFollowRatio_,
                                                       .a1 = a1_,
                                                       .anchorLevel = lastAnchor_.level,
                                                       .feedFrom = feedFrom_,
                                                       .feedTo = feedTo_,
                                                       .meterTheta = meter_.theta(),
                                                       .meterFreqHz = meter_.frequencyHz(),
                                                       .halfWave = lastHalfWave_});
        }
    }

    void clearScratch() noexcept {
        for (std::array<float, kMaxChunk>* a : {&fResBuf_, &d_, &dc_, &fCore_, &fAnchor_, &coreL_, &coreR_, &mainL_,
                                                &mainR_, &anchorL_, &anchorR_, &feed_, &gAmp_, &gD_, &anchorPhase_})
            a->fill(0.0f);
        a1Shape_.fill(0.0f);
        a1Mask_.fill(0.0f);
        a1Out_.fill(0.0f);
    }

    // ---- components (FR-001) ----
    ProfundumCore core_;
    ShapeMorph shape_;
    HarmonicFold fold_;
    BassCharacterStage character_;
    AnchorOscillator anchor_;
    DropEnvelope drop_;
    DropPhaseLock lock_;
    NoteMemory memory_;
    RetriggerDeclick declick_;
    ADSREnvelope ampEnv_;
    BassStereoField::VoiceState fieldState_{};
    HarmonicBalanceMeter meter_{};

    double fs_ = 48000.0;
    float capHz_ = 0.0f;

    // ---- per-interval scratch (indexed by interval position; spans of it are the FR-086 taps) ----
    std::array<float, kMaxChunk> fResBuf_{}, d_{}, dc_{}, fCore_{}, fAnchor_{};
    std::array<float, kMaxChunk> coreL_{}, coreR_{}, mainL_{}, mainR_{}, anchorL_{}, anchorR_{};
    std::array<float, kMaxChunk> feed_{}, gAmp_{}, gD_{}, anchorPhase_{};
    std::array<float, kMaxPartials> pan_{}, lastPan_{};

    // ---- a1 cache (C-6) ----
    std::array<float, static_cast<std::size_t>(kNumPartials)> a1Shape_{}, a1Mask_{}, a1Out_{};
    SpectralShapeRecipe::Controls a1RecipeKey_{};
    float a1FreqKey_ = 0.0f;
    float a1_ = 0.0f;
    bool a1Valid_ = false;

    // ---- last handed values (C-20) ----
    bool handedValid_ = false;
    BassStereoField::ChannelOffsets lastWide_{};
    SpectralShapeRecipe::Controls lastCoreControls_{};
    ProfundumCore::ExcitationControls lastExcitation_{};
    float lastFoldAmount_ = 0.0f;
    float lastFoldF0_ = 0.0f;
    float lastShape_ = 0.0f;
    BassCharacterStage::Algorithm lastAlgorithm_ = BassCharacterStage::Algorithm::Tube;
    float lastCharAmount_ = 0.0f;
    float lastCharTone_ = 0.0f;
    bool lastHalfWave_ = false;
    float lastFollowRatio_ = 1.0f;
    float followCandidate_ = 1.0f;               // G14-13: the measured ratio waiting out the dwell
    std::uint64_t followCandidateSamples_ = 0;   // samples the candidate has held
    AnchorOscillator::Controls lastAnchor_{};
    bool lastLockBypass_ = false;
    float lastA1_ = 0.0f;
    float lastDropAmount_ = 0.0f;
    float lastDropTime_ = 0.0f;
    float lastDropShape_ = 0.0f;
    float lastDropRebound_ = 0.0f;
    MorphState::AmpEnvelopeFields lastAmpEnv_{};

    // ---- transition (mirrors the Phase 4 harness) ----
    NoteMemory::Actions pending_{};
    bool dipPending_ = false;
    std::size_t untilSwitch_ = 0;   // samples to render before the switch sample
    bool coreNoteOnPending_ = false;
    bool anchorNoteOnPending_ = false;
    bool anchorRearm_ = false;
    bool lockTriggerPending_ = false;
    DropPhaseLock::TriggerKind lockKind_ = DropPhaseLock::TriggerKind::SnapReset;
    bool dropTriggerPending_ = false;   // G14-3: a transition's Drop trigger, flushed after the Drop setters
    bool dropSnapPending_ = true;
    bool dropResetBranch_ = true;
    bool coreSounding_ = false;
    bool syncPhasePending_ = false;
    bool fieldResetPending_ = false;
    bool envelopeRestarted_ = false;
    bool held_ = false;

    // ---- pitch and interval ----
    float fNote_ = 0.0f;
    std::uint32_t coreGridCount_ = 0;   // samples since the core's grid restart
    float lastFCore_ = 0.0f;
    float feedFrom_ = 0.0f;
    float feedTo_ = 0.0f;
    bool feedSkip_ = true;
    bool followRouting_ = false;
    std::size_t k_ = 0;                 // position inside the current interval
    std::size_t intervalLen_ = 0;
    std::uint64_t pos_ = 0;             // record clock
    const BassStereoField* field_ = nullptr;

    // ---- observer slot (FR-086) ----
    ProfundumEngineObserver* observer_ = nullptr;
    std::uint8_t index_ = 0;
    ProfundumEngineSeams seams_{};
};

}  // namespace Krate::DSP
