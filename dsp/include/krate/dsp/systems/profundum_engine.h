// ==============================================================================
// Layer 3: System - Profundum Phase 9 (specs/profundum-phase9-voice-engine)
// ==============================================================================
// ProfundumEngine, ProfundumEngineConfig, ProfundumNonlinearPlacement.
//   Serves: FR-013, FR-014, FR-020 - FR-032, FR-054, FR-060 - FR-066; SC-001 - SC-036 (engine arms).
//   Encodes: plan S6 (S6.1 - S6.11), S8 (tap / record emission), S9.1 - S9.3, S9.8; C-1, C-2, C-7, C-9, C-11,
//            C-12, C-14, C-15, C-19, C-20.
//   Spec:  specs/profundum-phase9-voice-engine/spec.md
//   Plan:  specs/profundum-phase9-voice-engine/plan.md
//   Tasks: specs/profundum-phase9-voice-engine/tasks.md (T008)
//
// The Profundum engine (FR-020): up to kMaxVoices ProfundumVoice instances (MONO through MonoHandler, POLY through
// VoiceAllocator Oldest / Hard), one BassMorphEngine -> ProfundumMacroMatrix -> ProfundumMotion -> MassLaw control
// layer, and the global chain voice sum -> MassDynamics -> BassStereoField (safety stage) -> DCBlocker x2 ->
// TruePeakLimiter (FR-025). Latency 0.
//
// Event model (C-9): every event and setter is latched at the engine's current sample and applied at the first grid
// point at or after it; the API carries no sample offsets (the caller splits its host block at each event). The
// engine renders in chunks that never cross a 32-sample grid point (C-15), so no stage depends on the host block
// size, and every component is prepared with maxBlockSize = 32.
//
// Grid step (FR-026, plan S6.5), in this order, never reordered: 1 events / latched setters; 2 morph
// advanceInterval() exactly once; 3 macro offsets; 4 Motion; 5 MassLaw base / Mass / advance; 6 field setControls /
// advanceControl, then the SOLID re-sync (C-2); 7 pitch trajectories, then per voice FR-012 / FR-013
// (ProfundumVoice::beginInterval); 8 the MassDynamics setters. Every setter is change-only (C-20); the first grid
// point after prepare / reset hands everything.
//
// RT (FR-060, S6.11): every method except prepare and setObserver is noexcept, allocation-, lock- and I/O-free;
// reset() is allocation-free and bounded but is a control-thread method (it resets eight cores, whose reset is
// control-thread). No virtual call on the audio path except the observer callbacks, made only while an observer is
// attached, and (PostSum only, C-14) the engine's internal voice-tap adapter.
//
// Surfaced (run-log T008):
//  - PostSum (C-14) reads each voice's core / Anchor / feed / gain taps through an engine-internal observer adapter,
//    because ProfundumVoice (T007) has no core-and-Anchor-only render; the per-voice Shape / Fold / Character still
//    run (their output is discarded) and FOLLOW reads the voice's own perceived-pitch ratio. The sound is C-14's;
//    the CPU saving SC-004 measures is not realised until the voice gains that render path.
//  - Includes: plan S2's list plus processors/note_memory.h, systems/shape_morph.h, processors/harmonic_fold.h and
//    systems/bass_character_stage.h, processors/spectral_shape_recipe.h, systems/morph_state.h and
//    systems/profundum_core.h, used directly here (S2: "every symbol a header uses is included directly").
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>                       // L0: detail::isFinite
#include <krate/dsp/core/midi_utils.h>                     // L0: midiNoteToFrequency (POLY f_res, C-11)
#include <krate/dsp/core/pitch_utils.h>                    // L0: semitonesToRatio (bend ratio, C-11)
#include <krate/dsp/core/random.h>                         // L0: deriveStreamSeed (FR-032, C-19)
#include <krate/dsp/primitives/dc_blocker.h>               // L1: DCBlocker
#include <krate/dsp/processors/harmonic_fold.h>            // L2: HarmonicFold (PostSum stage, C-14)
#include <krate/dsp/processors/mono_handler.h>             // L2: MonoHandler, MonoMode, PortaMode, GlideMode
#include <krate/dsp/processors/note_memory.h>              // L2: NoteMemory (Mode, Retrigger, Transition)
#include <krate/dsp/processors/note_processor.h>           // L2: NoteProcessor (pitch bend, FR-024)
#include <krate/dsp/processors/spectral_shape_recipe.h>    // L2: SpectralShapeRecipe::Controls (core controls)
#include <krate/dsp/processors/true_peak_limiter.h>        // L2: TruePeakLimiter
#include <krate/dsp/systems/bass_character_stage.h>        // L3 peer: BassCharacterStage (PostSum stage, C-14)
#include <krate/dsp/systems/bass_morph_engine.h>           // L3 peer: BassMorphEngine
#include <krate/dsp/systems/bass_stereo_field.h>           // L3 peer: BassStereoField
#include <krate/dsp/systems/mass_dynamics.h>               // L3 peer: MassDynamics
#include <krate/dsp/systems/mass_law.h>                    // L3 peer: MassLaw
#include <krate/dsp/systems/morph_state.h>                 // L3 peer: MorphState
#include <krate/dsp/systems/profundum_core.h>              // L3 peer: ProfundumCore::kControlInterval
#include <krate/dsp/systems/profundum_engine_observer.h>   // L3 peer: seams, records, observer (plan S8)
#include <krate/dsp/systems/profundum_macro_matrix.h>      // L3 peer: ProfundumMacroMatrix
#include <krate/dsp/systems/profundum_motion.h>            // L3 peer: ProfundumMotion
#include <krate/dsp/systems/profundum_voice.h>             // L3 peer: ProfundumVoice
#include <krate/dsp/systems/shape_morph.h>                 // L3 peer: ShapeMorph (PostSum stage, C-14)
#include <krate/dsp/systems/voice_allocator.h>             // L3 peer: VoiceAllocator, VoiceEvent

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Krate::DSP {

/// C-14: where the per-voice nonlinear stages run. PostSum is a configuration-time measurement variant (SC-004);
/// MONO always runs PerVoice.
enum class ProfundumNonlinearPlacement : std::uint8_t { PerVoice, PostSum };

/// Plan S6.1. Configuration-time (read at prepare).
struct ProfundumEngineConfig {
    ProfundumNonlinearPlacement placement = ProfundumNonlinearPlacement::PerVoice;
    bool dynamicsOversampling = true;            // FR-082: the engine turns the MassDynamics opt-in on
    std::size_t dynamicsOversamplingFactor = 4;  // FR-082 (Q4 refinement after P-3, 2026-10-20): the compressor's
                                                 // sidechain at 4x at every sample rate (T029: the gain is applied
                                                 // at the base rate, so no rate ceiling binds; T023 note)
    ProfundumEngineSeams seams{};
};

/// The Profundum engine (FR-020 - FR-032). Heavy (eight voices, each about 100 KB): construct it with
/// std::make_unique.
class ProfundumEngine {
public:
    enum class VoiceMode : std::uint8_t { Mono = 0, Poly = 1 };

    static constexpr std::size_t kMaxVoices = 8;                 // FR-022, Q8
    static constexpr std::size_t kDefaultPolyphony = 4;
    static constexpr std::size_t kControlInterval = 32;
    static constexpr std::size_t kMaxQueuedEvents = 256;         // C-9
    static constexpr std::size_t kNumMidiNotes = 128;            // C-9 pending-release flags
    static constexpr float kDcCutoffHz = 5.0f;                   // the Phase 6 chain value (C-10 there)
    /// G14-1 (user ruling 2026-10-21): the TruePeakLimiter's opt-in look-ahead, in samples at every rate
    /// (1.33 ms at 48 kHz). The engine's output lags its input by this amount; latencySamples() reports it.
    static constexpr std::size_t kLimiterLookaheadSamples = 64;
    static constexpr float kMinCeilingDb = -12.0f;
    static constexpr float kMaxCeilingDb = 0.0f;
    static constexpr float kDefaultCeilingDb = TruePeakLimiter::kDefaultCeilingDb;   // -1 dBTP
    static constexpr float kDefaultTempoBpm = BassStereoField::kDefaultTempoBpm;     // 120 (FR-028)
    static constexpr float kDefaultBendRangeSt = 2.0f;           // NoteProcessor's own default
    static constexpr float kMaxBendRangeSt = 24.0f;              // NoteProcessor::setPitchBendRange's clamp
    static constexpr std::uint32_t kDefaultSeed = 0xB455u;       // C-19
    static constexpr std::size_t kMotionDriftSalt = 1;
    static constexpr std::size_t kFieldDriftSalt = 2;
    static constexpr std::size_t kVoiceSaltBase = 16;
    static constexpr double kMinSampleRate = 22050.0;            // FR-064
    static constexpr double kMaxSampleRate = 192000.0;
    /// ProfundumCallRecord::voice of the engine's own records (the voices use 0 ... kMaxVoices - 1).
    static constexpr std::uint8_t kEngineRecordVoice = 0xFF;

    static_assert(kMaxVoices <= VoiceAllocator::kMaxVoices);
    static_assert(kMaxVoices < kEngineRecordVoice);
    static_assert(kControlInterval == ProfundumVoice::kControlInterval);
    static_assert(kControlInterval == ProfundumCore::kControlInterval);
    static_assert(kControlInterval == MassLaw::kControlInterval);
    static_assert(kControlInterval == MassDynamics::kControlInterval);
    static_assert(kControlInterval == BassMorphEngine::kControlInterval);
    static_assert(kControlInterval == BassStereoField::kControlInterval);
    static_assert(kControlInterval == ProfundumMotion::kControlInterval);
    static_assert(MassDynamics::latencySamples() == 0 && BassStereoField::latencySamples() == 0);   // FR-025

    ProfundumEngine() = default;
    ProfundumEngine(const ProfundumEngine&) = delete;
    ProfundumEngine& operator=(const ProfundumEngine&) = delete;
    ProfundumEngine(ProfundumEngine&&) = delete;
    ProfundumEngine& operator=(ProfundumEngine&&) = delete;
    ~ProfundumEngine() = default;

    // =========================================================================
    // Lifecycle (control thread; plan S6.4)
    // =========================================================================

    /// FR-020, FR-064. Not real-time safe (core bank calibration, limiter buffers). May be called again: every
    /// setter's pending value is kept (FR-064 edge case "sample-rate change").
    void prepare(double sampleRate, const ProfundumEngineConfig& cfg = {}) noexcept {
        // 1.
        cfg_ = cfg;
        seams_ = cfg.seams;
        double fs = detail::isFinite(sampleRate) ? sampleRate : kMinSampleRate;
        fs = std::clamp(fs, kMinSampleRate, kMaxSampleRate);
        // 2.
        morph_.prepare(fs);   // it resets
        resolveInitialState();
        // 3.
        postSumActive_ = false;
        postTap_.forward_ = observer_;
        postTap_.anchorDetached_ = seams_.anchorDetached;
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].setObserverSlot(observer_, static_cast<std::uint8_t>(v), &seams_);
            voices_[v].prepare(fs, state_);
        }
        // 4.
        mono_.prepare(fs);
        bend_.prepare(fs);
        // 5.
        motion_.setSeed(deriveStreamSeed(seed_, kMotionDriftSalt));
        motion_.prepare(fs);
        field_.setDriftSeed(deriveStreamSeed(seed_, kFieldDriftSalt));
        field_.setControls(state_.field);
        field_.prepare(fs, kControlInterval);
        // 6.
        law_.setBase(lawBaseOf(state_));
        law_.setMass(state_.mass);
        law_.prepare(fs);   // it resets
        // 7.
        dynamics_.setOversampling(cfg.dynamicsOversampling);
        dynamics_.setOversamplingFactor(cfg.dynamicsOversamplingFactor);
        handDynamicsDirect(law_.currentTargets());
        dynamics_.prepare(fs, kControlInterval);
        // 8. (C-15)
        dcL_.prepare(fs, kDcCutoffHz);
        dcR_.prepare(fs, kDcCutoffHz);
        limiter_.setCeilingDb(pending_.ceilingDb);
        limiter_.setLookaheadSamples(kLimiterLookaheadSamples);   // G14-1 (configuration-time, before prepare)
        limiter_.prepare(fs, kControlInterval);
        limiter_.setHalfbandDetector(true);   // T029 hot spot #3 (evidence/probes/p1-hotspots.md)
        // C-14 PostSum stages (snap setters before prepare, the Phase 8 fixture rule).
        snapPostStages();
        postShape_.prepare(fs, kControlInterval);
        postFold_.prepare(fs, kControlInterval);
        postChar_.prepare(fs, kControlInterval);
        prepared_ = true;
        // 9.
        reset();
    }

    /// Control thread; allocation-free and bounded (S6.4). Returns to the post-prepare state; the pending setter
    /// values, macros, endpoints and morph target are kept. A changed seed takes effect here (C-19).
    void reset() noexcept {
        morph_.reset();
        resetDependents();
        queueCount_ = 0;
        pendingRelease_.fill(false);
        anyPendingRelease_ = false;
        dropped_ = 0;
    }

    /// FR-086: configuration-time. The voices' slots are re-pointed (the PostSum adapter forwards to @p o).
    void setObserver(ProfundumEngineObserver* o) noexcept {
        observer_ = o;
        postTap_.forward_ = o;
        refreshVoiceSlots();
    }

    // =========================================================================
    // Events (audio thread; latched, applied at the next grid point, C-9)
    // =========================================================================

    /// C-12: velocity is forwarded to MonoHandler / VoiceAllocator and otherwise unused. Velocity 0 is a note-off
    /// (both handlers' rule). A full queue drops the note-on and counts it.
    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept {
        if (note >= kNumMidiNotes)
            return;
        if (velocity == 0) {
            noteOff(note);
            return;
        }
        if (queueCount_ >= kMaxQueuedEvents) {
            ++dropped_;
            return;
        }
        queue_[queueCount_++] = QueuedNote{.note = note, .velocity = velocity, .on = true};
    }

    /// Never dropped: queued in arrival order while the queue has room, otherwise recorded in the 128-entry
    /// pending-release flags, which the next grid point drains after the queue.
    void noteOff(std::uint8_t note) noexcept {
        if (note >= kNumMidiNotes)
            return;
        if (queueCount_ < kMaxQueuedEvents) {
            queue_[queueCount_++] = QueuedNote{.note = note, .velocity = 0, .on = false};
            return;
        }
        pendingRelease_[note] = true;
        anyPendingRelease_ = true;
    }

    /// [-1, 1]; non-finite keeps the previous value.
    void setPitchBend(float bipolar) noexcept {
        if (!detail::isFinite(bipolar))
            return;
        pending_.bend = std::clamp(bipolar, -1.0f, 1.0f);
    }

    /// FR-028: non-finite or <= 0 keeps the previous tempo (the field and the LFO clamp their own ranges).
    void setTempoBpm(float bpm) noexcept {
        if (!detail::isFinite(bpm) || !(bpm > 0.0f))
            return;
        pending_.tempoBpm = bpm;
    }

    // =========================================================================
    // Morph surface (FR-030). BassMorphEngine latches each of these to its next advanceInterval(), i.e. the next
    // grid point (FR-026 step 2), so they are forwarded directly.
    // =========================================================================

    bool setEndpoint(BassMorphEngine::Endpoint e, const MorphState& s) noexcept { return morph_.setEndpoint(e, s); }
    void setMorphPosition(float p) noexcept { morph_.setPosition(p); }
    void setMorphRampMs(float ms) noexcept { morph_.setPositionRampMs(ms); }
    void setAnchorMorphMode(BassMorphEngine::AnchorMorphMode m) noexcept { morph_.setAnchorMorphMode(m); }
    [[nodiscard]] std::size_t serializeMorphRuntime(std::byte* dst, std::size_t cap) const noexcept {
        return morph_.serializeRuntime(dst, cap);
    }
    /// FR-074 (Phase 11, 2026-10-22): a blob restored before any block has rendered since prepare / reset lands
    /// settled. The dependent state (voices, field, Mass law, dynamics, post stages) is re-snapped from the restored
    /// resolution as reset() does; the restored morph (target, p, p_a, selections) and queued events are kept.
    /// Restored mid-stream, the engine transitions as for any setter change.
    [[nodiscard]] bool restoreMorphRuntime(const std::byte* src, std::size_t size) noexcept {
        const bool ok = morph_.restoreRuntime(src, size);
        if (ok && prepared_ && !renderedSinceReset_)
            resetDependents();
        return ok;
    }

    // =========================================================================
    // Non-morphed controls (FR-031). Pending values; applied at the next grid point (FR-026 step 1). An
    // undeclared enum is ignored, a non-finite float keeps the previous value, ranges clamp (FR-061).
    // =========================================================================

    void setVoiceMode(VoiceMode m) noexcept {
        if (m == VoiceMode::Mono || m == VoiceMode::Poly)
            pending_.mode = m;
    }
    void setPolyphony(std::size_t n) noexcept { pending_.polyphony = std::clamp<std::size_t>(n, 1, kMaxVoices); }
    void setMonoPriority(MonoMode m) noexcept {
        if (static_cast<std::uint8_t>(m) <= static_cast<std::uint8_t>(MonoMode::HighNote))
            pending_.priority = m;
    }
    void setLegato(bool on) noexcept { pending_.legato = on; }
    void setPortamentoTimeMs(float ms) noexcept {
        if (!detail::isFinite(ms))
            return;
        pending_.portamentoMs = std::clamp(ms, MonoHandler::kMinPortamentoTimeMs, MonoHandler::kMaxPortamentoTimeMs);
    }
    void setPortamentoMode(PortaMode m) noexcept {
        if (static_cast<std::uint8_t>(m) <= static_cast<std::uint8_t>(PortaMode::LegatoOnly))
            pending_.portamentoMode = m;
    }
    void setGlideMode(GlideMode m) noexcept {
        if (static_cast<std::uint8_t>(m) <= static_cast<std::uint8_t>(GlideMode::Quantized))
            pending_.glideMode = m;
    }
    void setNoteMemoryMode(NoteMemory::Mode m) noexcept {
        if (static_cast<std::uint8_t>(m) <= static_cast<std::uint8_t>(NoteMemory::Mode::OscillatorContinuous))
            pending_.memoryMode = m;
    }
    void setNoteRetrigger(NoteMemory::Retrigger r) noexcept {
        if (static_cast<std::uint8_t>(r) <= static_cast<std::uint8_t>(NoteMemory::Retrigger::Continue))
            pending_.retrigger = r;
    }
    /// [0, 24]; guarded here (NoteProcessor::setPitchBendRange has no finite guard).
    void setPitchBendRange(float semitones) noexcept {
        if (!detail::isFinite(semitones))
            return;
        pending_.bendRangeSt = std::clamp(semitones, 0.0f, kMaxBendRangeSt);
    }
    /// [kMinCeilingDb, kMaxCeilingDb]; guarded here (TruePeakLimiter::setCeilingDb has no finite guard).
    void setOutputCeilingDb(float db) noexcept {
        if (!detail::isFinite(db))
            return;
        pending_.ceilingDb = std::clamp(db, kMinCeilingDb, kMaxCeilingDb);
    }
    /// Read at each grid point (FR-026 step 3); the matrix sanitises (non-finite keeps, clamp [0, 1]).
    void setMacro(ProfundumMacro m, float v) noexcept { macros_.setMacro(m, v); }
    void setMacros(const ProfundumMacroValues& v) noexcept { macros_.setMacros(v); }
    /// C-19: stored; takes effect at the next prepare / reset.
    void setSeed(std::uint32_t seed) noexcept { seed_ = seed; }

    // =========================================================================
    // FR-066 read-back: the pending value of every setter
    // =========================================================================

    [[nodiscard]] float pitchBend() const noexcept { return pending_.bend; }
    [[nodiscard]] float tempoBpm() const noexcept { return pending_.tempoBpm; }
    [[nodiscard]] const MorphState& getEndpoint(BassMorphEngine::Endpoint e) const noexcept {
        return morph_.endpoint(e);
    }
    [[nodiscard]] float morphPosition() const noexcept { return morph_.targetPosition(); }
    [[nodiscard]] float morphRampMs() const noexcept { return morph_.positionRampMs(); }
    [[nodiscard]] BassMorphEngine::AnchorMorphMode anchorMorphMode() const noexcept { return morph_.anchorMorphMode(); }
    [[nodiscard]] VoiceMode voiceMode() const noexcept { return pending_.mode; }
    [[nodiscard]] std::size_t polyphony() const noexcept { return pending_.polyphony; }
    [[nodiscard]] MonoMode monoPriority() const noexcept { return pending_.priority; }
    [[nodiscard]] bool legato() const noexcept { return pending_.legato; }
    [[nodiscard]] float portamentoTimeMs() const noexcept { return pending_.portamentoMs; }
    [[nodiscard]] PortaMode portamentoMode() const noexcept { return pending_.portamentoMode; }
    [[nodiscard]] GlideMode glideMode() const noexcept { return pending_.glideMode; }
    [[nodiscard]] NoteMemory::Mode noteMemoryMode() const noexcept { return pending_.memoryMode; }
    [[nodiscard]] NoteMemory::Retrigger noteRetrigger() const noexcept { return pending_.retrigger; }
    [[nodiscard]] float pitchBendRange() const noexcept { return pending_.bendRangeSt; }
    [[nodiscard]] float outputCeilingDb() const noexcept { return pending_.ceilingDb; }
    [[nodiscard]] ProfundumMacroValues macros() const noexcept { return macros_.macros(); }
    [[nodiscard]] float macro(ProfundumMacro m) const noexcept {
        const ProfundumMacroValues v = macros_.macros();
        switch (m) {
            case ProfundumMacro::Weight: return v.weight;
            case ProfundumMacro::DarkBright: return v.darkBright;
            case ProfundumMacro::TightLoose: return v.tightLoose;
            case ProfundumMacro::CleanViolent: return v.cleanViolent;
            case ProfundumMacro::Count: break;
        }
        return ProfundumMacroMatrix::kNeutral;
    }
    [[nodiscard]] std::uint32_t seed() const noexcept { return seed_; }
    /// C-19: the per-voice seeds derived at the last prepare / reset (reserved; consumed by nothing today).
    [[nodiscard]] std::uint32_t voiceSeed(std::size_t v) const noexcept {
        return v < kMaxVoices ? voiceSeeds_[v] : 0u;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// Overwrites @p left / @p right (FR-020). Any block size; split internally on the 32-sample grid (C-15).
    /// Before prepare the buffers are written with 0.
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept {
        if (left == nullptr || right == nullptr || numSamples == 0)
            return;
        if (!prepared_) {
            std::fill_n(left, numSamples, 0.0f);
            std::fill_n(right, numSamples, 0.0f);
            return;
        }
        renderedSinceReset_ = true;
        std::size_t i = 0;
        while (i < numSamples) {
            if (gridPhase_ == 0)
                gridStep();
            const std::size_t m = std::min(numSamples - i, kControlInterval - gridPhase_);
            renderChunk(left + i, right + i, m);
            i += m;
            pos_ += m;
            gridPhase_ = (gridPhase_ + m) % kControlInterval;
        }
    }

    /// FR-025 as amended by G14-1 (user ruling 2026-10-21): the output lags the input by the limiter's
    /// look-ahead (kLimiterLookaheadSamples once prepared; 0 before prepare), the only latency in the chain.
    [[nodiscard]] std::size_t latencySamples() const noexcept { return limiter_.lookaheadSamples(); }
    /// C-9 diagnostic: note-ons dropped by a full queue since the last prepare / reset.
    [[nodiscard]] std::uint64_t droppedEvents() const noexcept { return dropped_; }
    /// SC-013 soak poll (plan R-E; T019 DEFECT, resolved in T022): every voice's stateFinite() and MassDynamics'.
    [[nodiscard]] bool stateFinite() const noexcept {
        for (const ProfundumVoice& v : voices_)
            if (!v.stateFinite())
                return false;
        return dynamics_.stateFinite();
    }
    /// SC-013 soak poll (plan R-E): MassDynamics' halfband pair (mass_dynamics.h oversamplerStateFinite()).
    [[nodiscard]] bool oversamplerStateFinite() const noexcept { return dynamics_.oversamplerStateFinite(); }

private:
    // ---- plan S6.3 state types ----

    struct Settings {   // the FR-031 setters outside MorphState (pending and applied copies)
        VoiceMode mode = VoiceMode::Mono;
        std::size_t polyphony = kDefaultPolyphony;
        MonoMode priority = MonoMode::LastNote;
        bool legato = false;
        float portamentoMs = 0.0f;
        PortaMode portamentoMode = PortaMode::Always;
        GlideMode glideMode = GlideMode::ConstantTime;
        NoteMemory::Mode memoryMode = NoteMemory::Mode::AlwaysRetrigger;
        NoteMemory::Retrigger retrigger = NoteMemory::Retrigger::Reset;
        float bend = 0.0f;
        float bendRangeSt = kDefaultBendRangeSt;
        float tempoBpm = kDefaultTempoBpm;
        float ceilingDb = kDefaultCeilingDb;
    };

    struct QueuedNote {
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        bool on = false;
    };

    struct DynHanded {   // C-20: the last value handed to each MassDynamics setter
        float durationMs = 0.0f;
        float compression = 0.0f;
        float trimDb = 0.0f;
        float attackDb = 0.0f;
        float sustainDb = 0.0f;
        float keyHz = 0.0f;
    };

    struct PostHanded {   // C-20 for the PostSum stages
        float shape = 0.0f;
        float foldAmount = 0.0f;
        float foldF0 = 0.0f;
        BassCharacterStage::Algorithm algorithm = BassCharacterStage::Algorithm::Tube;
        float charAmount = 0.0f;
        float charTone = 0.0f;
        BassStereoField::ChannelOffsets wide{};
    };

    /// C-14: reads each voice's core / Anchor / feed / gain taps into the PostSum buses and forwards every callback
    /// to the attached observer (if any), so the observer sees exactly what it sees under PerVoice.
    class PostSumTap final : public ProfundumEngineObserver {
    public:
        static constexpr float kNoF0 = 1.0e30f;   // "no voice this sample" (finite: no infinity under fast-math)

        void begin(std::uint64_t chunkStart, std::size_t n) noexcept {
            chunkStart_ = chunkStart;
            n_ = n;
            std::fill_n(coreL_.data(), n, -0.0f);
            std::fill_n(coreR_.data(), n, -0.0f);
            std::fill_n(feedL_.data(), n, -0.0f);
            std::fill_n(feedR_.data(), n, -0.0f);
            std::fill_n(anchorL_.data(), n, -0.0f);
            std::fill_n(anchorR_.data(), n, -0.0f);
            std::fill_n(f0_.data(), n, kNoF0);
        }
        void onGrid(const ProfundumGridRecord& g) noexcept override {
            if (forward_ != nullptr)
                forward_->onGrid(g);
        }
        void onCall(const ProfundumCallRecord& c) noexcept override {
            if (forward_ != nullptr)
                forward_->onCall(c);
        }
        void onVoiceChunk(const ProfundumVoiceTaps& t) noexcept override {
            if (t.sample >= chunkStart_) {
                const auto off = static_cast<std::size_t>(t.sample - chunkStart_);
                const std::size_t m = (off < n_) ? std::min(t.n, n_ - off) : 0;
                for (std::size_t i = 0; i < m; ++i) {
                    const float ga = t.gAmp[i];
                    const float gd = t.gD[i];
                    const std::size_t k = off + i;
                    coreL_[k] += (t.coreL[i] * ga) * gd;
                    coreR_[k] += (t.coreR[i] * ga) * gd;
                    if (!anchorDetached_) {
                        anchorL_[k] += (t.anchorL[i] * ga) * gd;
                        anchorR_[k] += (t.anchorR[i] * ga) * gd;
                        feedL_[k] += ((t.feed[i] * t.anchorL[i]) * ga) * gd;
                        feedR_[k] += ((t.feed[i] * t.anchorR[i]) * ga) * gd;
                    }
                    const float f = t.fCore[i];
                    if (detail::isFinite(f) && f > 0.0f && f < f0_[k])
                        f0_[k] = f;
                }
            }
            if (forward_ != nullptr)
                forward_->onVoiceChunk(t);
        }
        void onEngineChunk(const ProfundumEngineTaps& t) noexcept override {
            if (forward_ != nullptr)
                forward_->onEngineChunk(t);
        }

        ProfundumEngineObserver* forward_ = nullptr;
        bool anchorDetached_ = false;
        std::uint64_t chunkStart_ = 0;
        std::size_t n_ = 0;
        std::array<float, kControlInterval> coreL_{}, coreR_{}, feedL_{}, feedR_{}, anchorL_{}, anchorR_{}, f0_{};
    };

    // ---- helpers ----

    [[nodiscard]] static bool same(float a, float b) noexcept {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    }
    /// Non-zero in either sign (a +-0 offset is skipped, S6.5 step 4, C-7).
    [[nodiscard]] static bool nonZero(float x) noexcept {
        return (std::bit_cast<std::uint32_t>(x) & 0x7FFFFFFFu) != 0u;
    }

    [[nodiscard]] static bool sameField(const BassStereoField::Controls& a,
                                        const BassStereoField::Controls& b) noexcept {
        return a.mode == b.mode && same(a.spread, b.spread) && same(a.gravity, b.gravity)
               && same(a.crossoverHz, b.crossoverHz) && same(a.curve, b.curve) && same(a.motionDepth, b.motionDepth)
               && same(a.motionRate, b.motionRate) && a.motionSource == b.motionSource && a.cycleSync == b.cycleSync
               && a.cycleNote == b.cycleNote && a.cycleModifier == b.cycleModifier;
    }
    [[nodiscard]] static bool sameBase(const MassLaw::Targets& a, const MassLaw::Targets& b) noexcept {
        return same(a.depth, b.depth) && same(a.body, b.body) && same(a.transientDurationMs, b.transientDurationMs)
               && same(a.foldAmount, b.foldAmount) && same(a.compressionAmount, b.compressionAmount)
               && same(a.anchorLevel, b.anchorLevel) && same(a.outputTrimDb, b.outputTrimDb);
    }

    /// FR-027: depth / body from the recipe after macro and Motion offsets, foldAmount and anchorLevel from the
    /// state; the Mass-driven bases stay at their Targets{} defaults. Copy-then-assign (no brace-init narrowing).
    [[nodiscard]] static MassLaw::Targets lawBaseOf(const MorphState& s) noexcept {
        MassLaw::Targets b{};
        b.depth = s.recipe.depth;
        b.body = s.recipe.body;
        b.foldAmount = s.foldAmount;
        b.anchorLevel = s.anchor.level;
        return b;
    }

    /// S6.5: the core receives the recipe with depth / body := the law targets (the Phase 6 helper's mapping,
    /// mass_dynamics_test_helpers.h:275-281). Copy-then-assign.
    [[nodiscard]] static SpectralShapeRecipe::Controls coreControlsFor(const SpectralShapeRecipe::Controls& recipe,
                                                                      const MassLaw::Targets& t) noexcept {
        SpectralShapeRecipe::Controls c = recipe;
        c.depth = t.depth;
        c.body = t.body;
        return c;
    }

    /// The exact float a MIDI note's frequency is (POLY f_res source); -1 when none matches (the Phase 4 harness's
    /// noteFromFrequency, profundum_note_behaviour_test_helpers.h:415-421).
    [[nodiscard]] static int noteFromFrequency(float hz) noexcept {
        for (int n = 0; n < static_cast<int>(kNumMidiNotes); ++n) {
            if (midiNoteToFrequency(n) == hz)
                return n;
        }
        return -1;
    }

    void logCall(ProfundumCall c, bool issued, float value) noexcept {
        if (observer_ == nullptr)
            return;
        observer_->onCall(ProfundumCallRecord{
            .sample = pos_, .voice = kEngineRecordVoice, .call = c, .issued = issued, .value = value});
    }

    /// reset() without the morph snap and the event queue (FR-074, Phase 11): every state that follows the resolved
    /// state is re-derived from the morph's current resolution, the timeline restarts, and no block has rendered.
    void resetDependents() noexcept {
        resolveInitialState();
        appliedSeed_ = seed_;
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voiceSeeds_[v] = deriveStreamSeed(appliedSeed_, kVoiceSaltBase + v);   // reserved (C-19)
            voices_[v].reset(state_);
        }
        mono_.reset();
        allocator_.reset();
        configureAllocator(pending_.polyphony);
        bend_.reset();
        motion_.setSeed(deriveStreamSeed(appliedSeed_, kMotionDriftSalt));
        motion_.reset();
        field_.setDriftSeed(deriveStreamSeed(appliedSeed_, kFieldDriftSalt));
        field_.setControls(state_.field);
        field_.reset();
        law_.setBase(lawBaseOf(state_));
        law_.setMass(state_.mass);
        law_.reset();
        handDynamicsDirect(law_.currentTargets());
        dynamics_.reset();
        dcL_.reset();
        dcR_.reset();
        limiter_.reset();
        snapPostStages();
        postShape_.reset();
        postFold_.reset();
        postChar_.reset();

        applySettings(true);

        pos_ = 0;
        gridPhase_ = 0;
        dcSynced_ = false;
        monoActiveNote_ = -1;
        followsMono_.fill(false);
        rendering_.fill(false);
        for (std::array<float, kControlInterval>& a : fResV_)
            a.fill(0.0f);
        handedValid_ = false;
        postHandedValid_ = false;
        postF0_ = BassStereoField::kDefaultF0Hz;
        setPostSumActive(false);
        renderedSinceReset_ = false;
    }

    /// The resolved state at the current morph position with the macro offsets (S6.4 step 2).
    void resolveInitialState() noexcept {
        state_ = morph_.resolved().state;
        if (!seams_.macroBypass)
            macros_.apply(state_);
    }

    void configureAllocator(std::size_t polyphony) noexcept {
        allocator_.setAllocationMode(AllocationMode::Oldest);
        allocator_.setStealMode(StealMode::Hard);
        static_cast<void>(allocator_.setVoiceCount(polyphony));   // nothing to release here (vorago_engine.h:372-378)
    }

    /// Prepare / reset: the r0 targets to MassDynamics (S6.4 step 7), unconditionally; the first grid point hands
    /// them again (handedValid_ false), change-only from then on.
    void handDynamicsDirect(const MassLaw::Targets& t) noexcept {
        const float cTL = seams_.macroBypass ? 0.0f : macros_.compressionContribution();
        dynamics_.setTransientDurationMs(t.transientDurationMs);
        dynamics_.setCompressionAmount(nonZero(cTL) ? std::clamp(t.compressionAmount + cTL, 0.0f, 1.0f)
                                                    : t.compressionAmount);
        dynamics_.setOutputTrimDb(t.outputTrimDb);
        dynamics_.setTransientAttackDb(state_.transientAttackDb);
        dynamics_.setTransientSustainDb(state_.transientSustainDb);
        dynamics_.setDetectorKeyHz(0.0f);
    }

    /// C-14 stages: the snap-at-prepare / reset setters from state_ (the voice's S5.3 step 1 rule).
    void snapPostStages() noexcept {
        postShape_.setShape(state_.shape);
        postChar_.setAlgorithm(state_.characterAlgorithm);
        postChar_.setAmount(state_.characterAmount);
        postChar_.setTone(state_.characterTone);
        postChar_.setChannelOffsets(0.0f, 0.0f);   // post-prepare offsets: reset() keeps them and engages on them
        postChar_.setFundamentalHz(BassCharacterStage::kDefaultFundamentalHz);   // reset() re-keys on the kept f0
        postFold_.setAmount(state_.foldAmount);
    }

    [[nodiscard]] ProfundumEngineObserver* voiceSlotObserver() noexcept {
        return postSumActive_ ? static_cast<ProfundumEngineObserver*>(&postTap_) : observer_;
    }
    void refreshVoiceSlots() noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v)
            voices_[v].setObserverSlot(voiceSlotObserver(), static_cast<std::uint8_t>(v), &seams_);
    }
    void setPostSumActive(bool on) noexcept {
        postSumActive_ = on;
        refreshVoiceSlots();
    }

    // ---- FR-026 step 1: settings and notes ----

    /// The latched FR-031 setters, in plan S6.5 step 1's order: mode switch (FR-023) first, then polyphony, the MONO
    /// handler settings, Note Memory to every voice, bend target, bend range, tempo, ceiling. @p all hands every
    /// value (prepare / reset); the mode switch itself runs only on a change at a grid point.
    /// @return true when anything was applied.
    bool applySettings(bool all) noexcept {
        const Settings p = pending_;
        Settings& a = applied_;
        bool any = all;
        if (!all && p.mode != a.mode) {
            switchMode();
            any = true;
        }
        if (all || p.polyphony != a.polyphony) {
            const std::span<const VoiceEvent> ev = allocator_.setVoiceCount(p.polyphony);
            for (const VoiceEvent& e : ev) {
                if (e.type == VoiceEvent::Type::NoteOff && e.voiceIndex < kMaxVoices)
                    voices_[e.voiceIndex].release();   // a voice above n keeps rendering until its release ends
            }
            any = true;
        }
        if (all || p.priority != a.priority) {
            mono_.setMode(p.priority);
            any = true;
        }
        if (all || p.legato != a.legato) {
            mono_.setLegato(p.legato);
            any = true;
        }
        if (all || !same(p.portamentoMs, a.portamentoMs)) {
            mono_.setPortamentoTime(p.portamentoMs);
            any = true;
        }
        if (all || p.portamentoMode != a.portamentoMode) {
            mono_.setPortamentoMode(p.portamentoMode);
            any = true;
        }
        if (all || p.glideMode != a.glideMode) {
            mono_.setGlideMode(p.glideMode);
            any = true;
        }
        if (all || p.memoryMode != a.memoryMode || p.retrigger != a.retrigger) {
            for (ProfundumVoice& v : voices_)
                v.setNoteMemory(p.memoryMode, p.retrigger);
            any = true;
        }
        if (all || !same(p.bend, a.bend)) {
            bend_.setPitchBend(p.bend);
            any = true;
        }
        if (all || !same(p.bendRangeSt, a.bendRangeSt)) {
            bend_.setPitchBendRange(p.bendRangeSt);
            any = true;
        }
        if (all || !same(p.tempoBpm, a.tempoBpm)) {
            field_.setTempoBpm(p.tempoBpm);
            motion_.setTempoBpm(p.tempoBpm);
            any = true;
        }
        if (all || !same(p.ceilingDb, a.ceilingDb)) {
            limiter_.setCeilingDb(p.ceilingDb);
            any = true;
        }
        a = p;
        return any;
    }

    /// FR-023 / S6.9: every voice enters its release (no note hangs, none is cut); both note handlers restart.
    void switchMode() noexcept {
        for (ProfundumVoice& v : voices_)
            v.release();
        mono_.reset();
        allocator_.reset();
        configureAllocator(pending_.polyphony);
        monoActiveNote_ = -1;
    }

    /// S6.7 (MONO, the Phase 4 harness's handleEvent, profundum_note_behaviour_test_helpers.h:463-539) and S6.8
    /// (POLY).
    void applyNoteOn(std::uint8_t note, std::uint8_t velocity) noexcept {
        if (applied_.mode == VoiceMode::Mono) {
            ProfundumVoice& v0 = voices_[0];
            const bool wasHeld = mono_.hasActiveNote();
            const bool audible = v0.audible();
            const MonoNoteEvent mev = mono_.noteOn(note, velocity);
            if (!mev.isNoteOn)
                return;
            monoActiveNote_ = noteFromFrequency(mev.frequency);
            const NoteMemory::Transition t = !audible ? NoteMemory::Transition::FromSilence
                                                      : (wasHeld ? NoteMemory::Transition::Overlap
                                                                 : NoteMemory::Transition::Tail);
            followsMono_[0] = true;
            v0.noteEvent(t, 0.0f, false);   // fNote unused in MONO: f_res is the portamento trajectory
            return;
        }
        std::array<bool, kMaxVoices> stolen{};
        const std::span<const VoiceEvent> events = allocator_.noteOn(note, velocity);
        for (const VoiceEvent& e : events) {
            const std::size_t v = e.voiceIndex;
            if (v >= kMaxVoices)
                continue;
            switch (e.type) {
                case VoiceEvent::Type::Steal:
                    stolen[v] = true;
                    break;
                case VoiceEvent::Type::NoteOn: {
                    ProfundumVoice& voice = voices_[v];
                    const NoteMemory::Transition t =
                        stolen[v] ? NoteMemory::Transition::VoiceReuse
                                  : (voice.audible() ? NoteMemory::Transition::Tail
                                                     : NoteMemory::Transition::FromSilence);
                    followsMono_[v] = false;
                    // C-11: midiNoteToFrequency(note), not VoiceEvent::frequency (the allocator's own tuning).
                    voice.noteEvent(t, midiNoteToFrequency(static_cast<int>(e.note)), stolen[v]);
                    stolen[v] = false;
                    break;
                }
                case VoiceEvent::Type::NoteOff:
                    voices_[v].release();
                    break;
            }
        }
    }

    void applyNoteOff(std::uint8_t note) noexcept {
        if (applied_.mode == VoiceMode::Mono) {
            const MonoNoteEvent mev = mono_.noteOff(note);
            if (monoActiveNote_ < 0)
                return;   // nothing held: a stray release
            if (!mev.isNoteOn) {   // the last held key released
                voices_[0].release();
                monoActiveNote_ = -1;
                return;
            }
            if (static_cast<int>(note) != monoActiveNote_)
                return;   // a non-active held key changes nothing
            monoActiveNote_ = noteFromFrequency(mev.frequency);
            followsMono_[0] = true;
            voices_[0].noteEvent(NoteMemory::Transition::ReturnToHeld, 0.0f, false);
            return;
        }
        const std::span<const VoiceEvent> events = allocator_.noteOff(note);
        for (const VoiceEvent& e : events) {
            if (e.type == VoiceEvent::Type::NoteOff && e.voiceIndex < kMaxVoices)
                voices_[e.voiceIndex].release();
        }
    }

    /// @return the number of note events applied.
    std::size_t applyQueuedNotes() noexcept {
        const std::size_t n = queueCount_;
        for (std::size_t i = 0; i < n; ++i) {
            const QueuedNote q = queue_[i];
            if (q.on)
                applyNoteOn(q.note, q.velocity);
            else
                applyNoteOff(q.note);
        }
        queueCount_ = 0;
        std::size_t flagged = 0;
        if (anyPendingRelease_) {
            anyPendingRelease_ = false;
            for (std::size_t note = 0; note < kNumMidiNotes; ++note) {
                if (pendingRelease_[note]) {
                    pendingRelease_[note] = false;
                    applyNoteOff(static_cast<std::uint8_t>(note));
                    ++flagged;
                }
            }
        }
        return n + flagged;
    }

    // ---- FR-026: the grid step ----

    void gridStep() noexcept {
        const bool all = !handedValid_;
        const bool attached = observer_ != nullptr;

        // 1. Events. POLY housekeeping first, then the latched setters, then the queued notes in arrival order.
        if (applied_.mode == VoiceMode::Poly) {
            for (std::size_t v = 0; v < kMaxVoices; ++v) {
                if (allocator_.getVoiceState(v) == ::Krate::DSP::VoiceState::Releasing && !voices_[v].active())
                    allocator_.voiceFinished(v);
            }
        }
        const bool settingsApplied = applySettings(false);
        const std::size_t notes = applyQueuedNotes();
        logCall(ProfundumCall::Events, settingsApplied || notes > 0, static_cast<float>(notes));

        // 2. Exactly once (Phase 8 handoff 9.1).
        const BassMorphEngine::Resolved& r = morph_.advanceInterval();
        logCall(ProfundumCall::MorphAdvance, true, r.position);

        // 3. Macro offsets on the resolved state (FR-053); the compression row is handed post-law (C-7).
        state_ = r.state;
        if (!seams_.macroBypass)
            macros_.apply(state_);
        const float cTL = seams_.macroBypass ? 0.0f : macros_.compressionContribution();
        logCall(ProfundumCall::Macros, !seams_.macroBypass, cTL);
        if (attached)
            afterMacros_ = state_;

        // 4. Motion (FR-040 - FR-046; S9.1, S9.2). An envelope-restarting note event restarts ENV / CYCLE (FR-045).
        motion_.setConfig(state_.motion);
        bool restart = false;
        for (ProfundumVoice& v : voices_) {
            if (v.consumeEnvelopeRestart())
                restart = true;
        }
        if (restart)
            motion_.noteRestart();
        const ProfundumMotion::Offsets& o = motion_.advanceInterval();
        if (nonZero(o.depth))
            state_.recipe.depth = std::clamp(state_.recipe.depth + o.depth, 0.0f, 1.0f);
        if (nonZero(o.body))
            state_.recipe.body = std::clamp(state_.recipe.body + o.body, 0.0f, 1.0f);
        if (nonZero(o.edge))
            state_.recipe.edge = std::clamp(state_.recipe.edge + o.edge, 0.0f, 1.0f);
        if (nonZero(o.shift))
            state_.recipe.shift = std::clamp(state_.recipe.shift + o.shift, -1.0f, 1.0f);
        const float massIn = nonZero(o.mass) ? state_.mass + o.mass : state_.mass;
        logCall(ProfundumCall::Motion, true, motion_.outputValue());

        // 5. MassLaw (FR-027): base, Mass, one slew step; the Anchor level from m_a (Q5).
        {
            const MassLaw::Targets base = lawBaseOf(state_);
            const bool issue = all || !sameBase(base, lastBase_);
            if (issue) {
                law_.setBase(base);
                lastBase_ = base;
            }
            logCall(ProfundumCall::LawSetBase, issue, base.depth);
        }
        {
            const bool issue = all || !same(massIn, lastMassIn_);
            if (issue) {
                law_.setMass(massIn);
                lastMassIn_ = massIn;
            }
            logCall(ProfundumCall::LawSetMass, issue, massIn);
        }
        const float smoothed = law_.advanceInterval();
        logCall(ProfundumCall::LawAdvance, true, smoothed);
        const MassLaw::Targets t = law_.currentTargets();
        // Resolved carries no mode (bass_morph_engine.h:55-60): the mode is read from the getter.
        const bool lock = morph_.anchorMorphMode() == BassMorphEngine::AnchorMorphMode::Lock;
        const float mA = (!lock && nonZero(o.mass)) ? r.anchorMass + o.mass : r.anchorMass;
        const float anchorLevel = MassLaw::evaluateRow(
            MassLaw::kRows[static_cast<std::size_t>(MassLaw::Target::AnchorLevel)], mA, law_.base().anchorLevel);

        // 6. Field controls (change-only, latched by the field to advanceControl), advance; SOLID re-sync (C-2).
        {
            const bool issue = all || !sameField(state_.field, lastField_);
            if (issue) {
                field_.setControls(state_.field);
                lastField_ = state_.field;
            }
            logCall(ProfundumCall::FieldSetControls, issue, state_.field.spread);
        }
        field_.advanceControl();
        logCall(ProfundumCall::FieldAdvance, true, field_.modeFade());
        if (field_.sideIdle()) {   // S6.10 (FR-029, C-1: the limiter needs no copy)
            if (!dcSynced_) {
                dcR_ = dcL_;
                dcSynced_ = true;
                logCall(ProfundumCall::DcResync, true, 0.0f);
            }
        } else {
            dcSynced_ = false;
        }

        // 7. Pitch trajectories, always (the bend smoother and the glide advance once per sample with no voice
        //    sounding, C-11); then per active voice, in index order, FR-012 / FR-013.
        const float range = applied_.bendRangeSt;
        for (std::size_t k = 0; k < kControlInterval; ++k) {
            const float b = bend_.processPitchBend();
            ratio_[k] = semitonesToRatio(b * range);
            monoFRes_[k] = mono_.processPortamento() * ratio_[k];
        }
        const bool wantPost = cfg_.placement == ProfundumNonlinearPlacement::PostSum
                              && applied_.mode == VoiceMode::Poly;   // MONO always PerVoice (C-14)
        if (wantPost != postSumActive_)
            setPostSumActive(wantPost);
        float keyHz = 0.0f;   // lowest sounding f0 (0 = no voice -> 80 Hz, mass_dynamics.h:194-196)
        const SpectralShapeRecipe::Controls coreControls = coreControlsFor(state_.recipe, t);
        // T032: one shape evaluation per changed interval for all voices (their controls are this one
        // value); each voice's core adopts it (ProfundumCore::setControlsWithShape). The clamps are the
        // core's own sanitizeField clamps; a non-finite field leaves the cores to evaluate themselves.
        const bool sharedFinite = detail::isFinite(coreControls.depth) && detail::isFinite(coreControls.body)
                                  && detail::isFinite(coreControls.bodyCurvature)
                                  && detail::isFinite(coreControls.bodyEmphasis) && detail::isFinite(coreControls.edge)
                                  && detail::isFinite(coreControls.shift);
        if (sharedFinite) {
            const SpectralShapeRecipe::Controls clamped{
                .depth = std::clamp(coreControls.depth, 0.0f, 1.0f),
                .body = std::clamp(coreControls.body, 0.0f, 1.0f),
                .bodyCurvature = std::clamp(coreControls.bodyCurvature, 0.0f, 1.0f),
                .bodyEmphasis = std::clamp(coreControls.bodyEmphasis, -1.0f, 0.0f),
                .edge = std::clamp(coreControls.edge, 0.0f, 1.0f),
                .shift = std::clamp(coreControls.shift, -1.0f, 1.0f),
            };
            const bool changed = !sharedShapeValid_
                                 || std::bit_cast<std::uint32_t>(clamped.depth) != std::bit_cast<std::uint32_t>(sharedControls_.depth)
                                 || std::bit_cast<std::uint32_t>(clamped.body) != std::bit_cast<std::uint32_t>(sharedControls_.body)
                                 || std::bit_cast<std::uint32_t>(clamped.bodyCurvature) != std::bit_cast<std::uint32_t>(sharedControls_.bodyCurvature)
                                 || std::bit_cast<std::uint32_t>(clamped.bodyEmphasis) != std::bit_cast<std::uint32_t>(sharedControls_.bodyEmphasis)
                                 || std::bit_cast<std::uint32_t>(clamped.edge) != std::bit_cast<std::uint32_t>(sharedControls_.edge)
                                 || std::bit_cast<std::uint32_t>(clamped.shift) != std::bit_cast<std::uint32_t>(sharedControls_.shift);
            if (changed) {
                SpectralShapeRecipe::evaluateShape(
                    clamped, std::span<float>(sharedShape_.data(), static_cast<std::size_t>(ProfundumVoice::kNumPartials)));
                sharedControls_ = clamped;
                sharedShapeValid_ = true;
            }
        }
        // T032: the a1 recipe (FR-005: state_.recipe, before the Mass biases) evaluated once for all voices.
        {
            const SpectralShapeRecipe::Controls& rc = state_.recipe;   // rc: the Resolved r above stays visible (C4456, Phase 10 D-3)
            const bool changed = !sharedA1Valid_
                                 || std::bit_cast<std::uint32_t>(rc.depth) != std::bit_cast<std::uint32_t>(sharedA1Controls_.depth)
                                 || std::bit_cast<std::uint32_t>(rc.body) != std::bit_cast<std::uint32_t>(sharedA1Controls_.body)
                                 || std::bit_cast<std::uint32_t>(rc.bodyCurvature) != std::bit_cast<std::uint32_t>(sharedA1Controls_.bodyCurvature)
                                 || std::bit_cast<std::uint32_t>(rc.bodyEmphasis) != std::bit_cast<std::uint32_t>(sharedA1Controls_.bodyEmphasis)
                                 || std::bit_cast<std::uint32_t>(rc.edge) != std::bit_cast<std::uint32_t>(sharedA1Controls_.edge)
                                 || std::bit_cast<std::uint32_t>(rc.shift) != std::bit_cast<std::uint32_t>(sharedA1Controls_.shift);
            if (changed) {
                SpectralShapeRecipe::evaluateShape(
                    rc, std::span<float>(sharedA1Shape_.data(), static_cast<std::size_t>(ProfundumVoice::kNumPartials)));
                sharedA1Controls_ = rc;
                sharedA1Valid_ = true;
            }
        }
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            ProfundumVoice& voice = voices_[v];
            rendering_[v] = voice.active();
            if (!rendering_[v])
                continue;   // S6.6: an inactive voice is not rendered
            const float* fRes = monoFRes_.data();
            if (!followsMono_[v]) {
                const float fNote = voice.noteFrequency();
                for (std::size_t k = 0; k < kControlInterval; ++k)
                    fResV_[v][k] = fNote * ratio_[k];
                fRes = fResV_[v].data();
            }
            voice.setSamplePosition(pos_);
            const ProfundumVoice::IntervalInputs in{.state = &state_,
                                                    .coreControls = coreControls,
                                                    .foldAmount = t.foldAmount,
                                                    .anchorLevel = anchorLevel,
                                                    .field = &field_,
                                                    .forceLinearCore = seams_.forceLinearCore,
                                                    .sharedShape = sharedFinite ? sharedShape_.data() : nullptr,
                                                    .sharedA1Shape = sharedA1Shape_.data()};
            voice.beginInterval(in, fRes, kControlInterval);
            if (voice.audible()) {
                const float last = voice.currentCoreF0();
                const float f = (detail::isFinite(last) && last > 0.0f) ? last : fRes[0];
                if (detail::isFinite(f) && f > 0.0f && (keyHz == 0.0f || f < keyHz))
                    keyHz = f;
            }
        }
        if (postSumActive_)
            postSumBeginInterval(t, keyHz);

        // 8. MassDynamics setters from the Mass targets (FR-027), change-only.
        const float compression = nonZero(cTL) ? std::clamp(t.compressionAmount + cTL, 0.0f, 1.0f)
                                               : t.compressionAmount;
        handDyn(ProfundumCall::DynDuration, all, t.transientDurationMs, lastDyn_.durationMs,
                [this](float x) noexcept { dynamics_.setTransientDurationMs(x); });
        handDyn(ProfundumCall::DynCompression, all, compression, lastDyn_.compression,
                [this](float x) noexcept { dynamics_.setCompressionAmount(x); });
        handDyn(ProfundumCall::DynTrim, all, t.outputTrimDb, lastDyn_.trimDb,
                [this](float x) noexcept { dynamics_.setOutputTrimDb(x); });
        handDyn(ProfundumCall::DynAttack, all, state_.transientAttackDb, lastDyn_.attackDb,
                [this](float x) noexcept { dynamics_.setTransientAttackDb(x); });
        handDyn(ProfundumCall::DynSustain, all, state_.transientSustainDb, lastDyn_.sustainDb,
                [this](float x) noexcept { dynamics_.setTransientSustainDb(x); });
        handDyn(ProfundumCall::DynKey, all, keyHz, lastDyn_.keyHz,
                [this](float x) noexcept { dynamics_.setDetectorKeyHz(x); });
        handedValid_ = true;

        if (attached) {
            observer_->onGrid(ProfundumGridRecord{.sample = pos_,
                                                  .resolved = &r,
                                                  .afterMacros = &afterMacros_,
                                                  .afterMotion = &state_,
                                                  .motionSource = motion_.sourceValue(),
                                                  .motionOutput = motion_.outputValue(),
                                                  .massIn = massIn,
                                                  .smoothedMass = law_.smoothedMass(),
                                                  .anchorMassUsed = mA,
                                                  .anchorLevel = anchorLevel,
                                                  .compressionHanded = compression,
                                                  .compressorGrDb = dynamics_.compressor().currentGainReductionDb(),
                                                  .targets = t});
        }
    }

    template <typename Setter>
    void handDyn(ProfundumCall c, bool all, float value, float& last, const Setter& set) noexcept {
        const bool issue = all || !same(value, last);
        if (issue) {
            set(value);
            last = value;
        }
        logCall(c, issue, value);
    }

    /// C-14: the global Shape / Fold / Character setters per interval, change-only; f0 = the lowest sounding f.
    void postSumBeginInterval(const MassLaw::Targets& t, float keyHz) noexcept {
        const bool all = !postHandedValid_;
        PostHanded& p = postHanded_;
        if (all || !same(state_.shape, p.shape)) {
            postShape_.setShape(state_.shape);
            p.shape = state_.shape;
        }
        if (all || !same(t.foldAmount, p.foldAmount)) {
            postFold_.setAmount(t.foldAmount);
            p.foldAmount = t.foldAmount;
        }
        const float f0 = keyHz > 0.0f ? keyHz : postF0_;
        if (all || !same(f0, p.foldF0)) {
            postFold_.setFundamentalHz(f0);
            p.foldF0 = f0;
        }
        if (all || state_.characterAlgorithm != p.algorithm) {
            postChar_.setAlgorithm(state_.characterAlgorithm);
            p.algorithm = state_.characterAlgorithm;
        }
        if (all || !same(state_.characterAmount, p.charAmount)) {
            postChar_.setAmount(state_.characterAmount);
            p.charAmount = state_.characterAmount;
        }
        if (all || !same(state_.characterTone, p.charTone)) {
            postChar_.setTone(state_.characterTone);
            p.charTone = state_.characterTone;
        }
        const BassStereoField::ChannelOffsets w = field_.wideOffsets(state_.characterAlgorithm);
        if (all || !same(w.amount, p.wide.amount) || !same(w.tone, p.wide.tone)) {
            postChar_.setChannelOffsets(w.amount, w.tone);
            p.wide = w;
        }
        postHandedValid_ = true;
    }

    // ---- render (S6.5 renderChunk) ----

    void renderChunk(float* left, float* right, std::size_t m) noexcept {
        const bool attached = observer_ != nullptr;
        // 1. -0 is the IEEE additive identity: with one voice the sum holds its contribution bit for bit (SC-019(b)).
        std::fill_n(sumL_.data(), m, -0.0f);
        std::fill_n(sumR_.data(), m, -0.0f);

        // 2 - 3. Each active voice renders its slice of the interval (f_res stored at beginInterval).
        if (postSumActive_) {
            postTap_.begin(pos_, m);
            for (std::size_t v = 0; v < kMaxVoices; ++v) {
                if (!rendering_[v])
                    continue;
                std::fill_n(discardL_.data(), m, 0.0f);
                std::fill_n(discardR_.data(), m, 0.0f);
                voices_[v].setSamplePosition(pos_);
                voices_[v].render(nullptr, discardL_.data(), discardR_.data(), m);
            }
            renderPostSum(m);
        } else {
            for (std::size_t v = 0; v < kMaxVoices; ++v) {
                if (!rendering_[v])
                    continue;
                voices_[v].setSamplePosition(pos_);
                voices_[v].render(nullptr, sumL_.data(), sumR_.data(), m);
            }
        }
        if (attached) {
            std::copy_n(sumL_.data(), m, tapSumL_.data());
            std::copy_n(sumR_.data(), m, tapSumR_.data());
        }

        // 4. Global chain (FR-025).
        dynamics_.processBlock(sumL_.data(), sumR_.data(), m);
        if (attached) {
            std::copy_n(sumL_.data(), m, tapDynL_.data());
            std::copy_n(sumR_.data(), m, tapDynR_.data());
        }
        field_.processBlock(sumL_.data(), sumR_.data(), m);
        if (attached) {
            std::copy_n(sumL_.data(), m, tapFieldL_.data());
            std::copy_n(sumR_.data(), m, tapFieldR_.data());
        }
        dcL_.processBlock(sumL_.data(), m);
        dcR_.processBlock(sumR_.data(), m);
        if (attached) {
            std::copy_n(sumL_.data(), m, tapDcL_.data());
            std::copy_n(sumR_.data(), m, tapDcR_.data());
        }
        limiter_.processBlock(sumL_.data(), sumR_.data(), static_cast<int>(m));
        std::copy_n(sumL_.data(), m, left);
        std::copy_n(sumR_.data(), m, right);

        if (attached) {
            const auto tapOf = [m](const std::array<float, kControlInterval>& a) noexcept {
                return std::span<const float>(a.data(), m);
            };
            observer_->onEngineChunk(ProfundumEngineTaps{.sample = pos_,
                                                         .n = m,
                                                         .sumL = tapOf(tapSumL_),
                                                         .sumR = tapOf(tapSumR_),
                                                         .dynL = tapOf(tapDynL_),
                                                         .dynR = tapOf(tapDynR_),
                                                         .fieldL = tapOf(tapFieldL_),
                                                         .fieldR = tapOf(tapFieldR_),
                                                         .dcL = tapOf(tapDcL_),
                                                         .dcR = tapOf(tapDcR_),
                                                         .outL = tapOf(sumL_),
                                                         .outR = tapOf(sumR_)});
        }
    }

    /// C-14: core bus -> ShapeMorph -> (+ the fold-feed bus) -> HarmonicFold -> BassCharacterStage, at f0 = the
    /// lowest sounding f_core per sample; the Anchor bus is added after it.
    void renderPostSum(std::size_t m) noexcept {
        PostSumTap& b = postTap_;
        for (std::size_t i = 0; i < m; ++i) {
            if (b.f0_[i] >= PostSumTap::kNoF0)
                b.f0_[i] = postF0_;
            else
                postF0_ = b.f0_[i];
        }
        postShape_.processBlock(b.coreL_.data(), b.coreR_.data(), m, b.f0_.data());
        for (std::size_t i = 0; i < m; ++i) {
            b.coreL_[i] += b.feedL_[i];
            b.coreR_[i] += b.feedR_[i];
        }
        postFold_.processBlock(b.coreL_.data(), b.coreR_.data(), m);
        postChar_.processBlock(b.coreL_.data(), b.coreR_.data(), m, b.f0_.data());
        for (std::size_t i = 0; i < m; ++i) {
            sumL_[i] = b.coreL_[i] + b.anchorL_[i];
            sumR_[i] = b.coreR_[i] + b.anchorR_[i];
        }
    }

    // ---- components (plan S6.3) ----
    std::array<ProfundumVoice, kMaxVoices> voices_{};
    MonoHandler mono_;
    VoiceAllocator allocator_;
    NoteProcessor bend_;
    BassMorphEngine morph_;
    ProfundumMacroMatrix macros_;
    ProfundumMotion motion_;
    MassLaw law_;
    MassDynamics dynamics_;
    BassStereoField field_;
    DCBlocker dcL_;
    DCBlocker dcR_;
    TruePeakLimiter limiter_;
    // C-14 PostSum stages.
    ShapeMorph postShape_;
    HarmonicFold postFold_;
    BassCharacterStage postChar_;
    PostSumTap postTap_;

    // ---- configuration ----
    ProfundumEngineConfig cfg_{};
    ProfundumEngineSeams seams_{};
    ProfundumEngineObserver* observer_ = nullptr;
    bool prepared_ = false;
    bool renderedSinceReset_ = false;   // FR-074: restoreMorphRuntime re-snaps until the first rendered block

    // ---- working state ----
    MorphState state_{};         // the working copy per interval (after macros and Motion)
    MorphState afterMacros_{};   // kept only while an observer is attached (S8)
    Settings pending_{};
    Settings applied_{};
    std::uint32_t seed_ = kDefaultSeed;
    std::uint32_t appliedSeed_ = kDefaultSeed;
    std::array<std::uint32_t, kMaxVoices> voiceSeeds_{};

    // ---- events (C-9) ----
    std::array<QueuedNote, kMaxQueuedEvents> queue_{};
    std::size_t queueCount_ = 0;
    std::array<bool, kNumMidiNotes> pendingRelease_{};
    bool anyPendingRelease_ = false;
    std::uint64_t dropped_ = 0;
    int monoActiveNote_ = -1;
    std::array<bool, kMaxVoices> followsMono_{};   // the voice's f_res is the MONO portamento trajectory
    std::array<bool, kMaxVoices> rendering_{};     // active at this interval's grid point (S6.6)

    // ---- clock ----
    std::uint64_t pos_ = 0;
    std::size_t gridPhase_ = 0;
    bool dcSynced_ = false;

    // ---- handed values (C-20) ----
    bool handedValid_ = false;
    MassLaw::Targets lastBase_{};
    float lastMassIn_ = 0.0f;
    BassStereoField::Controls lastField_{};
    DynHanded lastDyn_{};
    bool postSumActive_ = false;
    bool postHandedValid_ = false;
    PostHanded postHanded_{};
    float postF0_ = BassStereoField::kDefaultF0Hz;

    // ---- per-interval scratch ----
    std::array<float, kControlInterval> ratio_{};
    std::array<float, kControlInterval> monoFRes_{};
    std::array<std::array<float, kControlInterval>, kMaxVoices> fResV_{};
    std::array<float, static_cast<std::size_t>(ProfundumVoice::kNumPartials)> sharedShape_{};   // T032
    SpectralShapeRecipe::Controls sharedControls_{};                                            // T032: the controls sharedShape_ holds
    bool sharedShapeValid_ = false;                                                              // T032
    std::array<float, static_cast<std::size_t>(ProfundumVoice::kNumPartials)> sharedA1Shape_{}; // T032: FR-005 recipe shape
    SpectralShapeRecipe::Controls sharedA1Controls_{};                                          // T032
    bool sharedA1Valid_ = false;                                                                // T032
    std::array<float, kControlInterval> sumL_{}, sumR_{};
    std::array<float, kControlInterval> discardL_{}, discardR_{};
    std::array<float, kControlInterval> tapSumL_{}, tapSumR_{}, tapDynL_{}, tapDynR_{};
    std::array<float, kControlInterval> tapFieldL_{}, tapFieldR_{}, tapDcL_{}, tapDcR_{};
};

}  // namespace Krate::DSP
