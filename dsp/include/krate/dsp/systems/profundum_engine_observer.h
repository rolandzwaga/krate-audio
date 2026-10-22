// ==============================================================================
// Layer 3: System - Profundum Phase 9 (specs/profundum-phase9-voice-engine)
// ==============================================================================
// ProfundumEngineSeams, call / grid / tap records and ProfundumEngineObserver.
//   Serves (T004): FR-086 (test taps, seams and the observer).
//   Encodes: plan S8 verbatim; ProfundumEngineSeams moved here from plan S6.1 (as S8 says), so
//            profundum_voice.h and profundum_engine.h both see complete types without an include cycle (plan S2).
//   Spec:  specs/profundum-phase9-voice-engine/spec.md
//   Plan:  specs/profundum-phase9-voice-engine/plan.md
//   Tasks: specs/profundum-phase9-voice-engine/tasks.md
//
// Exposure is configuration-time only (ProfundumEngine::setObserver, ProfundumEngineConfig::seams); no
// compile-time macro (a header-only class compiled twice in one binary would be an ODR violation). The engine
// and the voice never allocate, copy or lock for a tap; spans point into their scratch and are valid only for
// the duration of the callback. The observer copies on the test side. Every callback is noexcept.
// This header includes neither the voice nor the engine.
// ==============================================================================

#pragma once

#include <krate/dsp/systems/morph_state.h>         // L3 peer: MorphState
#include <krate/dsp/systems/bass_morph_engine.h>   // L3 peer: BassMorphEngine::Resolved
#include <krate/dsp/systems/mass_law.h>            // L3 peer: MassLaw::Targets

#include <cstddef>
#include <cstdint>
#include <span>

namespace Krate::DSP {

/// FR-086: configuration-time test seams, all off by default (plan S6.1, declared here per plan S8).
struct ProfundumEngineSeams {
    bool macroBypass = false;
    bool anchorDetached = false;
    bool forceLinearCore = false;
    bool disableStealFade = false;
    bool disableDeclick = false;
    bool followNoBypass = false;
    bool lockReadsCore = false;
    bool anchorPhaseTap = false;                 // per-sample Anchor render for phase() (S5.6 step 5)
};

/// The order is normative: SC-032 compares call sequences.
enum class ProfundumCall : std::uint8_t {
    Events, MorphAdvance, Macros, Motion, LawSetBase, LawSetMass, LawAdvance, FieldSetControls, FieldAdvance, DcResync,
    VoicePan, VoiceWide, CoreControls, CoreExcitation, FoldAmount, FoldF0, Shape, CharAlgorithm, CharAmount, CharTone,
    HalfWave, FollowRatio, AnchorControls, LockBypass, A1, DropSetters, AmpEnv, AmpCurve, SyncPhase, NoteOnCore,
    NoteOnAnchor, DynDuration, DynCompression, DynTrim, DynAttack, DynSustain, DynKey };
// AmpEnv = the time / sustain step of S5.4 step 10; AmpCurve = its curve setters (issued only on a bitwise change).
struct ProfundumCallRecord { std::uint64_t sample; std::uint8_t voice; ProfundumCall call; bool issued; float value; };

struct ProfundumGridRecord {                     // per interval
    std::uint64_t sample;
    const BassMorphEngine::Resolved* resolved;
    const MorphState* afterMacros;               // a copy kept by the engine only while an observer is attached
    const MorphState* afterMotion;
    float motionSource, motionOutput, massIn, smoothedMass, anchorMassUsed, anchorLevel, compressionHanded;
    float compressorGrDb;                        // dynamics_.compressor().currentGainReductionDb() (mass_dynamics.h:571, 272)
    MassLaw::Targets targets;
};
struct ProfundumVoiceTaps {                      // per voice, per rendered chunk; spans into the voice's scratch
    std::uint8_t voice; std::uint64_t sample; std::size_t n;
    std::span<const float> coreL, coreR, charL, charR, anchorL, anchorR, gAmp, gD, fRes, fCore, fAnchor, anchorPhase,
                           feed;                 // the per-sample fold-feed g actually multiplied (S5.6 step 6; +0 when skipped)
    float followRatio, a1, anchorLevel, feedFrom, feedTo, meterTheta, meterFreqHz; bool halfWave;
};
struct ProfundumEngineTaps {                     // per chunk
    std::uint64_t sample; std::size_t n;
    std::span<const float> sumL, sumR, dynL, dynR, fieldL, fieldR, dcL, dcR, outL, outR;
};
class ProfundumEngineObserver {
public:
    virtual ~ProfundumEngineObserver() = default;
    virtual void onGrid(const ProfundumGridRecord&) noexcept {}
    virtual void onCall(const ProfundumCallRecord&) noexcept {}
    virtual void onVoiceChunk(const ProfundumVoiceTaps&) noexcept {}
    virtual void onEngineChunk(const ProfundumEngineTaps&) noexcept {}
};

} // namespace Krate::DSP
