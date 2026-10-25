#pragma once

// ==============================================================================
// Profundum - processor render support for the moved Phase 9 readers (Phase 13 T015; plan 4.7 second block,
// 5.2, 5.3; rulings P-1, P-6, P-11)
// ==============================================================================
// The moved readers (dsp/tests/unit/systems/profundum_audibility_readers.h) render through the P-1 shim
// auRd::render::voiceEngineRender / voiceEngineRenderWithTwin. Under
// auRd::render::ScriptRenderScope(processorRenderHook, &ctx) every one of those renders runs through a
// ::Profundum::Processor instead (renderScriptThroughProcessor), with the script's state carried by the host
// surface:
//   - effectiveScriptFor (plan 5.2): probe context (ctx.preset == nullptr) returns the script unchanged; preset
//     context overlays the decoded preset's carriers (macros, voice mode, polyphony, seed, and the seven
//     note-behaviour fields plus 1002 / 1000 as sample-0 events prepended BEFORE the script's own events, so a
//     lever event the composite placed at sample 0 wins in the stable sort).
//   - renderScriptThroughProcessor (plan 5.3 steps 1-7): refusals (Deviation 3: recorded in ctx.stop, empty
//     taps returned), the endpoint guard, the state load, one parameter-only block, the P-11 seam, the render
//     with blocks split at every carrier sample, the capture, and the engine-direct twin / fidelity arm.
//   - processorRenderHook: the auRd::render::ScriptRenderFn adapter; FAIL(ctx.stop) on a refusal (FR-023 stop).
//
// NAMESPACE RULE (plan 4.7): aliases only. param_path_audibility_helpers.h ports same-named functions into
// ProfundumPluginTest, so the moved readers are reached only as auRd::X, never through `using namespace`.
//
// Recorded choices (T015 notes):
//   - PresetArmContext gains `std::string stop` (tasks.md Deviation 3) and `ProcessorFixture* processor`: an
//     optional processor to render on (nullptr: a fresh heap fixture per render). The reuse slot exists so a test
//     can observe that a render leaves the processor's engine seams at their defaults (plan 5.3 step 4).
//   - Blocks keep the engine-direct driver's host-block grid (voiceEngineRender splits each grid block at its
//     events, profundum_voice_engine_test_helpers.h:405-419) and are additionally split at every carrier sample.
//   - The fidelity arm appends ONE ctx.fidelity entry per twin render: the worst of sumL / sumR / characterL /
//     characterR, `tap` naming the worst channel (the struct's "one per twin render").
//   - Voice Mode follows a recorded ctx.overrides entry for kVoiceModeId (normalised, >= 0.5 is Poly) when one
//     exists (plan 5.2 "unless there is a recorded override"), else the decoded value.
//
// T018 (plan 5.4, 5.8, P-2 - P-5, P-7, P-10): showcaseArmPlan / showcaseColumn (one plan per column: the
// preconditions, the declared endpoints and ONE moved entry point per cell; values copied from the AuCell, no
// arithmetic on readings), describePreset (the C-7 descriptor), presetDistance and presetDistinctnessThreshold.
// Recorded choices (T018 notes):
//   - A precondition that P does not store is set on the arm's working copy of P (both arms) and recorded as
//     "parameter=value" (joined with ';'); a primary arm with a non-empty record is measured and fails (SC-007).
//     Endpoint preconditions apply to A for single-state columns and to both endpoints for S(P) columns. Voice Mode
//     reaches the renderer as a ctx.overrides entry, the note-behaviour fields through the working copy's events.
//   - The hi enumerator a contrast names is a precondition too (AT-11 PUNCH, AT-12 GHOST, AT-13 FOLLOW, AT-14..16
//     the row's waveform, AT-17 OctaveDownSine), like AT-04..09's algorithm; a single-state claim records
//     kMorphPositionId=0 when P stores p != 0 (P-3).
//   - P-2: an AT-13 claim whose endpoint A stores neither Fracture nor Rectify at Amount >= 1 - 1e-6 yields arms
//     with variant "none", measured == false, pass == false (no algorithm override).
//   - describePreset's sweep render runs to note-off + 2^16 (every descriptor window ends there; a causal render's
//     samples do not depend on its length), the window components read the processor output (bands) and the Sum
//     tap (component 14); hostBlock applies to the sweep render, the steady / rise renders keep auScript's block.
//     seedOverride == kPresetStoredSeed (0) renders the stored seed.
//
// T019 (plan 4.7, 5.5, 5.6, 5.8 window, 5.12; R-4): PresetSweepObserver, renderPresetSweep (the C-6 host render
// of a Comp: setState, the optional FR-041 four-block prelude, setupProcessing, getTailSamples, notes on at 0 and
// off at 3 s), presetSweepReadings and the presetReadings cache. Recorded choices (T019 notes):
//   - The render runs to max(noteOff + tail, noteOff + 20 ms + 2^16) so the plan 5.6 W2 window always exists; the
//     tail RMS, true peak, finite and SOLID checks read [0, noteOff + tail) only.
//   - The mono-sum and descriptor-window readings exist on 48 kHz renders only (monoMeasured).
//   - PresetRenderReadings gains the counts and windows the [long] cases print (nonFinite, monoSumWindow, ...).
// ==============================================================================

#include "param_path_audibility_helpers.h"   // ppNorm, kPpPortTolerance (Phase 11 laws)
#include "profundum_test_fixture.h"

#include "band_level_metric.h"
#include "preset_test_support.h"
#include "profundum_preset_defs.h"   // tools/ is on the test include path (plan 9)
#include "profundum_preset_host.h"   // T019: buildPresetComponentState (the presetReadings cache)

#include "engine/profundum_engine_config.h"
#include "parameters/global_params.h"
#include "parameters/morph_params.h"
#include "parameters/note_params.h"
#include "parameters/sound_param_table.h"   // T019: kSoundRows (parameter-space slots)
#include "parameters/sound_param_row.h"
#include "plugin_ids.h"

#include "profundum_audibility_readers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <enable_ftz_daz.h>
#include <low_frequency_metrics.h>
#include <render_fingerprint.h>
#include <vst_event_list.h>

#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ProfundumPluginTest {

namespace auRd = Krate::DSP::ProfundumAudibility;
namespace auPt = Krate::DSP::ProfundumTest;

/// The state a script is overlaid on (plan 5.2). preset == nullptr: probe context (script authoritative, default
/// state).
struct PresetArmContext {
    const DecodedPresetState* preset = nullptr;
    std::span<const std::uint8_t> comp;            // the preset's Comp bytes (preset context)
    bool macrosFromScript = false;                 // true only for the AT-47..50 performance arms
    /// Preset context: the (a, b) endpoint pairs the arm passes to the entry point. Every render's script must match
    /// one pair leaf for leaf (plan 5.3 step 1a, P-10 endpoint guard). Single-state readers declare (x, x).
    std::vector<std::pair<Krate::DSP::MorphState, Krate::DSP::MorphState>> allowedEndpoints;
    std::vector<float> lastSumL, lastSumR;         // the last render's Sum tap (P-5 component 12's mid total)
    std::size_t anchorPhaseTapRenders = 0;         // processor renders that carried anchorPhaseTap (plan 5.7 (c), P-11)
    std::vector<std::pair<Steinberg::Vst::ParamID, double>> overrides;   // recorded secondary-claim preconditions
    struct Fidelity {
        bool pass;
        double worstSample;
        double worstMetric;
        std::string tap;
    };
    std::vector<Fidelity> fidelity;                // one per twin render (C-5 fidelity arm)
    std::string stop;                              // Deviation 3: the FR-023 refusal of the last render, else empty
    ProcessorFixture* processor = nullptr;         // optional: render on this processor (nullptr: a fresh one)
};

// -----------------------------------------------------------------------------
// Plan 5.2: the effective script
// -----------------------------------------------------------------------------

/// Plan 5.2: the script, with every carrier filled from the context.
[[nodiscard]] inline auPt::VoiceEngineScript effectiveScriptFor(const auPt::VoiceEngineScript& s,
                                                                const PresetArmContext& ctx) {
    if (ctx.preset == nullptr) {
        return s;   // probe context: the script is authoritative
    }
    using K = auPt::VoiceEngineEventKind;
    using Engine = Krate::DSP::ProfundumEngine;
    const DecodedPresetState& d = *ctx.preset;
    auPt::VoiceEngineScript e = s;

    if (!ctx.macrosFromScript) {
        e.macros.weight = d.macros[0];         // 100
        e.macros.darkBright = d.macros[1];     // 101
        e.macros.tightLoose = d.macros[2];     // 102
        e.macros.cleanViolent = d.macros[3];   // 103
    }
    bool poly = d.voiceMode == 1;
    for (const auto& o : ctx.overrides) {
        if (o.first == ::Profundum::kVoiceModeId) {
            poly = o.second >= 0.5;
        }
    }
    e.mode = poly ? Engine::VoiceMode::Poly : Engine::VoiceMode::Mono;
    e.polyphony = static_cast<std::size_t>(std::max(d.polyphony, 1));
    e.seed = d.seed;

    const auto atZeroIndex = [](K kind, int index) {
        auPt::VoiceEngineEvent ev{};
        ev.sample = 0;
        ev.kind = kind;
        ev.index = index;
        return ev;
    };
    const auto atZeroValue = [](K kind, float value) {
        auPt::VoiceEngineEvent ev{};
        ev.sample = 0;
        ev.kind = kind;
        ev.value = value;
        return ev;
    };
    std::vector<auPt::VoiceEngineEvent> events;
    events.reserve(9u + s.events.size());
    events.push_back(atZeroIndex(K::MonoPriority, d.note.priority));        // 410
    events.push_back(atZeroIndex(K::Legato, d.note.legato));                // 411
    events.push_back(atZeroValue(K::PortamentoTimeMs, d.note.portamentoMs));   // 412
    events.push_back(atZeroIndex(K::PortamentoMode, d.note.portamentoMode));   // 413
    events.push_back(atZeroIndex(K::GlideMode, d.note.glideMode));          // 414
    events.push_back(atZeroIndex(K::NoteMemoryMode, d.note.memory));        // 415
    events.push_back(atZeroIndex(K::NoteRetrigger, d.note.retrigger));      // 416
    events.push_back(atZeroValue(K::MorphRampMs, d.morph.rampMs));          // 1002
    events.push_back(atZeroIndex(K::AnchorMorphMode, d.morph.anchorMode));  // 1000
    events.insert(events.end(), s.events.begin(), s.events.end());   // the script's own events AFTER: they win
    e.events = std::move(events);
    return e;
}

// -----------------------------------------------------------------------------
// Plan 5.3: carriers on the host surface
// -----------------------------------------------------------------------------

/// The parameter point that carries @p ev (plan 5.3 step 3 laws). False for a kind with no parameter carrier
/// (NoteOn / NoteOff are host events; PitchBend, Tempo, Endpoint, VoiceMode, Polyphony, Macro are refused).
[[nodiscard]] inline bool processorCarrierParam(const auPt::VoiceEngineEvent& ev, Steinberg::Vst::ParamID& id,
                                                double& norm) {
    namespace P = ::Profundum;
    using K = auPt::VoiceEngineEventKind;
    const auto indexNorm = [](int index, int maxIndex) {
        return std::clamp(static_cast<double>(index) / static_cast<double>(maxIndex), 0.0, 1.0);
    };
    switch (ev.kind) {
        case K::MorphPosition:
            id = P::kMorphPositionId;
            norm = ppNorm(P::kMorphPositionId, static_cast<double>(ev.value));
            return true;
        case K::MorphRampMs:
            id = P::kMorphRampTimeId;
            norm = P::normalizedFromPlain(P::kMorphRampLaw, static_cast<double>(ev.value));
            return true;
        case K::AnchorMorphMode:
            id = P::kMorphAnchorModeId;
            norm = indexNorm(ev.index, P::kNumAnchorMorphModes - 1);
            return true;
        case K::PortamentoTimeMs:
            id = P::kNotePortamentoId;
            norm = ppNorm(P::kNotePortamentoId, static_cast<double>(ev.value));
            return true;
        case K::NoteMemoryMode:
            id = P::kNoteMemoryId;
            norm = ppNorm(P::kNoteMemoryId, static_cast<double>(ev.index));
            return true;
        case K::NoteRetrigger:
            id = P::kNoteRetriggerId;
            norm = ppNorm(P::kNoteRetriggerId, static_cast<double>(ev.index));
            return true;
        case K::PortamentoMode:
            id = P::kNotePortamentoModeId;
            norm = indexNorm(ev.index, P::kMaxPortamentoModeIndex);
            return true;
        case K::GlideMode:
            id = P::kNoteGlideModeId;
            norm = indexNorm(ev.index, P::kMaxGlideModeIndex);
            return true;
        case K::MonoPriority:
            id = P::kNoteMonoPriorityId;
            norm = indexNorm(ev.index, P::kMaxMonoPriorityIndex);
            return true;
        case K::Legato:
            id = P::kNoteLegatoId;
            norm = ev.index != 0 ? 1.0 : 0.0;   // index / kMaxLegatoIndex (1)
            return true;
        default:
            break;
    }
    return false;
}

/// Plan 5.3 step 3: the ONE parameter-only block before prepare, in order: 105, 100-103, 1, 2 = (polyphony - 1) / 7,
/// every sample-0 carrier event (stable-sorted, so the last same-kind point wins at the block start), 1001 = 0.
[[nodiscard]] inline std::vector<std::pair<Steinberg::Vst::ParamID, double>> processorSampleZeroParams(
    const auPt::VoiceEngineScript& e) {
    namespace P = ::Profundum;
    std::vector<std::pair<Steinberg::Vst::ParamID, double>> out;
    out.emplace_back(P::kMorphPositionId, ppNorm(P::kMorphPositionId, static_cast<double>(e.position)));
    out.emplace_back(P::kMacroWeightId, static_cast<double>(e.macros.weight));
    out.emplace_back(P::kMacroDarkBrightId, static_cast<double>(e.macros.darkBright));
    out.emplace_back(P::kMacroTightLooseId, static_cast<double>(e.macros.tightLoose));
    out.emplace_back(P::kMacroCleanViolentId, static_cast<double>(e.macros.cleanViolent));
    out.emplace_back(P::kVoiceModeId,
                     ppNorm(P::kVoiceModeId, e.mode == Krate::DSP::ProfundumEngine::VoiceMode::Poly ? 1.0 : 0.0));
    out.emplace_back(P::kPolyphonyId,
                     std::clamp((static_cast<double>(e.polyphony) - 1.0) / 7.0, 0.0, 1.0));
    std::vector<auPt::VoiceEngineEvent> events = e.events;
    std::stable_sort(events.begin(), events.end(), [](const auPt::VoiceEngineEvent& x, const auPt::VoiceEngineEvent& y) {
        return x.sample < y.sample;
    });
    for (const auPt::VoiceEngineEvent& ev : events) {
        if (ev.sample != 0u) {
            break;
        }
        Steinberg::Vst::ParamID id = 0;
        double norm = 0.0;
        if (processorCarrierParam(ev, id, norm)) {
            out.emplace_back(id, norm);
        }
    }
    out.emplace_back(P::kMorphEditTargetId, 0.0);
    return out;
}

/// Plan 5.2 / 5.3 refusals (the FR-023 stop): the P-6 pins (preset context), the config rows, the seams, the event
/// kinds, then the endpoint guard (preset context). Empty when the script can be rendered on the processor.
[[nodiscard]] inline std::string processorRenderRefusal(const auPt::VoiceEngineScript& e, const PresetArmContext& ctx) {
    using K = auPt::VoiceEngineEventKind;
    const auto num = [](double v) { return std::to_string(v); };
    if (ctx.preset != nullptr) {
        const DecodedPresetState& d = *ctx.preset;
        const auto off = [](double x, double want) { return !(std::abs(x - want) <= 1.0e-6); };   // NaN -> off
        if (off(static_cast<double>(d.outputGain), 1.0)) {
            return "pin: Output Gain " + num(static_cast<double>(d.outputGain)) +
                   " has no carrier (P-6 pins 1.0, 0 dB)";
        }
        if (off(static_cast<double>(d.outputCeilingDb), -1.0)) {
            return "pin: Output Ceiling " + num(static_cast<double>(d.outputCeilingDb)) +
                   " dBTP has no carrier (P-6 pins -1 dBTP)";
        }
        if (off(static_cast<double>(d.note.bendSt), static_cast<double>(Krate::DSP::ProfundumEngine::kDefaultBendRangeSt))) {
            return "pin: Pitch Bend Range " + num(static_cast<double>(d.note.bendSt)) +
                   " st has no carrier (P-6 pins 2 st)";
        }
        if (d.morph.editTarget != 0) {
            return "pin: Edit Target " + std::to_string(d.morph.editTarget) + " has no carrier (P-6 pins A)";
        }
    }

    const Krate::DSP::ProfundumEngineConfig base = ::Profundum::makeProfundumEngineConfig();
    if (e.config.placement != base.placement || e.config.dynamicsOversampling != base.dynamicsOversampling ||
        e.config.dynamicsOversamplingFactor != base.dynamicsOversamplingFactor) {
        return "no processor carrier: config (placement / dynamicsOversampling / dynamicsOversamplingFactor differ "
               "from makeProfundumEngineConfig())";
    }
    const Krate::DSP::ProfundumEngineSeams& sm = e.config.seams;
    if (sm.macroBypass || sm.anchorDetached || sm.forceLinearCore || sm.disableStealFade || sm.disableDeclick ||
        sm.followNoBypass || sm.lockReadsCore) {
        return "no processor carrier: seam (only anchorPhaseTap reaches the processor, P-11)";
    }
    for (const auPt::VoiceEngineEvent& ev : e.events) {
        if (ev.kind == K::NoteOn || ev.kind == K::NoteOff) {
            continue;
        }
        Steinberg::Vst::ParamID id = 0;
        double norm = 0.0;
        if (!processorCarrierParam(ev, id, norm)) {
            return "no processor carrier: event kind " + std::to_string(static_cast<int>(ev.kind)) + " at sample " +
                   std::to_string(ev.sample);
        }
    }

    if (ctx.preset != nullptr) {
        constexpr const char* kGuard = "script endpoints are not the arm's (a reader hard-codes its base)";
        if (ctx.allowedEndpoints.empty()) {
            return std::string(kGuard) + ": the arm declared no endpoints";
        }
        const ::Profundum::LeafBits ea = ::Profundum::leafBitsOf(e.a);
        const ::Profundum::LeafBits eb = ::Profundum::leafBitsOf(e.b);
        bool found = false;
        for (const auto& pair : ctx.allowedEndpoints) {
            if (::Profundum::leafBitsOf(pair.first) == ea && ::Profundum::leafBitsOf(pair.second) == eb) {
                found = true;
                break;
            }
        }
        if (!found) {
            return std::string(kGuard) + ": no declared pair matches";
        }
    }
    return {};
}

// -----------------------------------------------------------------------------
// Plan 5.3: script -> processor render
// -----------------------------------------------------------------------------

/// Plan 5.3: one processor render of `s`. Taps come from the attached VoiceEngineObserver; outL / outR from the
/// processor. A refusal (Deviation 3) sets ctx.stop and returns empty taps.
[[nodiscard]] inline auPt::VoiceEngineTaps renderScriptThroughProcessor(const auPt::VoiceEngineScript& s, bool twin,
                                                                        int twinVoice, PresetArmContext& ctx) {
    using K = auPt::VoiceEngineEventKind;
    ctx.stop.clear();

    // 1. The effective script; refusals and the 1a endpoint guard.
    const auPt::VoiceEngineScript e = effectiveScriptFor(s, ctx);
    if (std::string why = processorRenderRefusal(e, ctx); !why.empty()) {
        ctx.stop = std::move(why);
        return auPt::VoiceEngineTaps{};
    }
    REQUIRE(e.fs > 0.0);
    REQUIRE(e.hostBlock >= 1u);
    REQUIRE(e.totalSamples > 0u);

    std::unique_ptr<ProcessorFixture> owned;
    ProcessorFixture* fx = ctx.processor;
    if (fx == nullptr) {
        owned = std::make_unique<ProcessorFixture>();
        fx = owned.get();
    } else {
        fx->proc->setActive(false);   // a reused processor: inactive before the state and the re-prepare
    }

    // 2. State: probe -> a fresh processor's stream; preset -> ctx.comp. Endpoints, seed, then setState.
    StateBytes bytes;
    if (ctx.preset == nullptr) {
        if (owned != nullptr) {
            bytes = writeV2Stream(*fx->proc);   // the fresh processor itself
        } else {
            const auto fresh = std::make_unique<ProcessorFixture>();
            bytes = writeV2Stream(*fresh->proc);
        }
    } else {
        bytes.resize(ctx.comp.size());
        if (!ctx.comp.empty()) {
            std::memcpy(bytes.data(), ctx.comp.data(), ctx.comp.size());
        }
    }
    bytes = withEndpoints(std::move(bytes), e.a, e.b);
    patchU32(bytes, kV2SeedAt, e.seed);
    REQUIRE(loadStateBytes(*fx->proc, bytes) == Steinberg::kResultOk);

    // 3. One parameter-only block before prepare (the Phase 11 order, param_path_audibility_helpers.h:584-593).
    {
        const auto params = processorSampleZeroParams(e);
        MultiParamChanges pc;
        pc.reserve(params.size());
        for (const auto& p : params) {
            pc.queueFor(p.first).addTestPoint(0, p.second);
        }
        REQUIRE(fx->processNoOutputs(&pc) == Steinberg::kResultOk);
    }

    // 4. Config (P-11 seam), observer, prepare.
    fx->proc->setEngineSeamsForTest(e.config.seams);
    auPt::VoiceEngineTaps taps;
    auPt::voiceEngineAllocateTaps(taps, e);
    auPt::VoiceEngineObserver observer(taps);
    if (e.attachObserver) {
        fx->proc->setEngineObserverForTest(&observer);
    }
    REQUIRE(fx->prepare(e.fs, static_cast<Steinberg::int32>(e.hostBlock)) == Steinberg::kResultOk);
    enableFTZDAZ();

    // 5. Events: NoteOn / NoteOff as host events, carrier kinds (sample > 0) as parameter points at the start of a
    //    block split at their sample (the processor applies a queue's last point at the block start).
    std::vector<auPt::VoiceEngineEvent> events = e.events;
    std::stable_sort(events.begin(), events.end(), [](const auPt::VoiceEngineEvent& x, const auPt::VoiceEngineEvent& y) {
        return x.sample < y.sample;
    });
    const auto total = static_cast<std::size_t>(e.totalSamples);
    std::vector<std::size_t> splits;   // carrier samples in (0, total), ascending
    for (const auPt::VoiceEngineEvent& ev : events) {
        if (ev.kind == K::NoteOn || ev.kind == K::NoteOff || ev.sample == 0u || ev.sample >= total) {
            continue;
        }
        const auto at = static_cast<std::size_t>(ev.sample);
        if (splits.empty() || splits.back() != at) {
            splits.push_back(at);
        }
    }

    fx->capturedL.clear();
    fx->capturedR.clear();
    fx->reserveCapture(total);
    Krate::Test::EventList hostEvents;
    MultiParamChanges blockParams;
    blockParams.reserve(16);
    std::size_t ei = 0;
    std::size_t si = 0;
    std::size_t pos = 0;
    while (pos < total) {
        std::size_t end = std::min((pos / e.hostBlock + 1u) * e.hostBlock, total);
        while (si < splits.size() && splits[si] <= pos) {
            ++si;
        }
        if (si < splits.size() && splits[si] < end) {
            end = splits[si];
        }
        hostEvents.clear();
        blockParams.clear();
        while (ei < events.size() && events[ei].sample < end) {
            const auPt::VoiceEngineEvent& ev = events[ei++];
            const auto offset = static_cast<Steinberg::int32>(static_cast<std::size_t>(ev.sample) - pos);
            if (ev.kind == K::NoteOn) {
                hostEvents.addNoteOn(static_cast<Steinberg::int16>(ev.note), static_cast<float>(ev.velocity) / 127.0f,
                                     offset);
            } else if (ev.kind == K::NoteOff) {
                hostEvents.addNoteOff(static_cast<Steinberg::int16>(ev.note), offset);
            } else if (ev.sample != 0u) {   // sample-0 carriers went in the step-3 block
                Steinberg::Vst::ParamID id = 0;
                double norm = 0.0;
                REQUIRE(processorCarrierParam(ev, id, norm));
                REQUIRE(offset == 0);   // the block was split at this carrier's sample
                blockParams.queueFor(id).addTestPoint(0, norm);
            }
        }
        REQUIRE(fx->processBlock(end - pos, &hostEvents, &blockParams) == Steinberg::kResultOk);
        pos = end;
    }

    // 6. Capture and detach (Phase 11 step 6: detach after setActive(false)); the seams back to default.
    REQUIRE(fx->capturedL.size() == total);
    REQUIRE(fx->capturedR.size() == total);
    REQUIRE(fx->proc->endpointRejectionsForTest() == 0u);
    taps.outL = fx->capturedL;
    taps.outR = fx->capturedR;
    if (fx->engine() != nullptr) {
        taps.droppedEvents = fx->engine()->droppedEvents();
    }
    fx->proc->setActive(false);
    fx->proc->setEngineObserverForTest(nullptr);
    fx->proc->setEngineSeamsForTest(Krate::DSP::ProfundumEngineSeams{});
    ctx.lastSumL = taps.sumL;
    ctx.lastSumR = taps.sumR;
    if (e.config.seams.anchorPhaseTap) {
        ++ctx.anchorPhaseTapRenders;
    }

    // 7. Twin: engine-direct forceLinearCore render of the effective script (never through the processor).
    if (twin) {
        const auPt::VoiceEngineTaps tw = auPt::voiceEngineRender(auPt::voiceEngineTwinScript(e));
        if (twinVoice < 0) {
            taps.linearCoreL = tw.coreL;
            taps.linearCoreR = tw.coreR;
        } else {
            REQUIRE(static_cast<std::size_t>(twinVoice) < tw.tappedVoices);
            taps.linearCoreL = tw.voices[static_cast<std::size_t>(twinVoice)].coreL;
            taps.linearCoreR = tw.voices[static_cast<std::size_t>(twinVoice)].coreR;
        }
        // Fidelity arm (preset context only, C-5): engine-direct render of the same effective script.
        if (ctx.preset != nullptr) {
            const auPt::VoiceEngineTaps fd = auPt::voiceEngineRender(e);
            PresetArmContext::Fidelity f{.pass = true, .worstSample = 0.0, .worstMetric = 0.0, .tap = "sumL"};
            const auto arm = [&f](const char* name, const std::vector<float>& proc, const std::vector<float>& direct) {
                namespace Tu = Krate::DSP::TestUtils;
                const Tu::FingerprintComparison c =
                    Tu::compareFingerprints(Tu::fingerprintRender(proc), Tu::fingerprintRender(direct),
                                            Tu::kMetricTolerance, Tu::kSampleTolerance);
                const bool ok = c.withinTolerance() && proc.size() == direct.size();
                if (!ok && f.pass) {
                    f.tap = name;   // the first failing channel
                }
                if (f.pass && ok && (static_cast<double>(c.worstSampleError) > f.worstSample ||
                                     c.worstMetricRelativeError > f.worstMetric)) {
                    f.tap = name;   // while passing: the channel with the largest deviation
                }
                f.pass = f.pass && ok;
                f.worstSample = std::max(f.worstSample, static_cast<double>(c.worstSampleError));
                f.worstMetric = std::max(f.worstMetric, c.worstMetricRelativeError);
            };
            arm("sumL", taps.sumL, fd.sumL);
            arm("sumR", taps.sumR, fd.sumR);
            arm("characterL", taps.characterL, fd.characterL);
            arm("characterR", taps.characterR, fd.characterR);
            ctx.fidelity.push_back(std::move(f));
        }
    }
    return taps;
}

/// The hook adapter. Matches auRd::render::ScriptRenderFn; ctx is a PresetArmContext*. A refusal is the FR-023 stop.
[[nodiscard]] inline auPt::VoiceEngineTaps processorRenderHook(const auPt::VoiceEngineScript& s, bool twin, int voice,
                                                               void* ctx) {
    REQUIRE(ctx != nullptr);
    auto& c = *static_cast<PresetArmContext*>(ctx);
    auPt::VoiceEngineTaps taps = renderScriptThroughProcessor(s, twin, voice, c);
    if (!c.stop.empty()) {
        FAIL(c.stop);
    }
    return taps;
}

static_assert(std::is_same_v<decltype(&processorRenderHook), auRd::render::ScriptRenderFn>,
              "processorRenderHook matches the P-1 shim's hook type");

// =============================================================================
// T018: showcase arms per column (plan 5.4; C-5, FR-012, FR-013; P-2, P-3, P-4, P-10)
// =============================================================================

/// One showcase, reverse or probe cell (plan 5.4).
struct ShowcaseArm {
    ::Profundum::PresetDefs::Capability column = ::Profundum::PresetDefs::Capability::Count;
    std::string variant;
    std::uint8_t note = 0;
    double lo = 0.0, hi = 0.0, value = 0.0, bar = 0.0;
    bool pass = false;
    bool fidelityPass = true;
    double windowMs = 0.0;
    std::string overrides;
    std::string descriptor, tap;
    std::string barText;     // T018: the AuCell's bar text (coverage-matrix.csv)
    bool measured = false;   // T018: the moved entry point ran for this cell (false only for P-2's "none")
};

/// A lever: one endpoint edit (applied to both endpoints of S(P), or to A(P)).
using ShowcaseLever = std::function<void(Krate::DSP::MorphState&)>;
/// One cell of a column: calls exactly one moved entry point on states built from the working copy.
using ShowcaseCellFn = std::function<auRd::AuCell(const DecodedPresetState&, std::uint8_t)>;

/// The arm plan of one (preset, column): the working copy of P with the recorded preconditions applied, the
/// declared endpoint pairs (plan 5.3 step 1a), the gated notes and the cell's entry point.
struct ShowcasePlan {
    DecodedPresetState state;                                              // P, preconditions applied
    std::vector<std::string> overrides;                                    // "parameter=value"
    std::vector<std::pair<Steinberg::Vst::ParamID, double>> controlOverrides;   // ctx.overrides (Voice Mode)
    std::vector<std::pair<Krate::DSP::MorphState, Krate::DSP::MorphState>> declared;
    std::vector<std::uint8_t> notes;
    std::string variant, descriptor;
    double windowMs = 0.0;                                                 // setting-defined window (AT-11 / AT-12)
    bool macrosFromScript = false;                                         // AT-47..50
    bool floorRule = false;                                                // auFloorRule after the cell (Field rows)
    bool applicable = true;                                                // false: P-2 "none"
    ShowcaseCellFn cell;
};

namespace preset_showcase_detail {

using MS = Krate::DSP::MorphState;
using Cap = ::Profundum::PresetDefs::Capability;
using Img = Krate::DSP::BassStereoField::ImageMode;
using AMode = Krate::DSP::AnchorOscillator::Mode;
using AWave = Krate::DSP::AnchorOscillator::Waveform;
using Alg = Krate::DSP::BassCharacterStage::Algorithm;
using MSrc = Krate::DSP::MorphState::MotionConfig::Source;
using MTgt = Krate::DSP::MorphState::MotionConfig::Target;
using MPol = Krate::DSP::MorphState::MotionConfig::Polarity;

inline constexpr const char* kShowcaseBase = "P as stored (Phase 13 showcase arm)";

/// S(P): AuSetting{P.a, P.b, P.position, P.macros} (plan 5.4 shorthand).
[[nodiscard]] inline auRd::AuSetting settingOf(const DecodedPresetState& q) {
    auRd::AuSetting s;
    s.a = q.a;
    s.b = q.b;
    s.position = q.morph.position;
    s.macros.weight = q.macros[0];
    s.macros.darkBright = q.macros[1];
    s.macros.tightLoose = q.macros[2];
    s.macros.cleanViolent = q.macros[3];
    return s;
}

/// The lever on both endpoints of @p s.
[[nodiscard]] inline auRd::AuSetting levered(auRd::AuSetting s, const ShowcaseLever& f) {
    if (f) {
        f(s.a);
        f(s.b);
    }
    return s;
}

/// The lever on one state.
[[nodiscard]] inline MS leveredOne(MS s, const ShowcaseLever& f) {
    if (f) {
        f(s);
    }
    return s;
}

[[nodiscard]] inline bool sameValue(float x, float y) noexcept {
    return std::abs(static_cast<double>(x) - static_cast<double>(y)) <= 1.0e-6;
}
template <class T>
[[nodiscard]] bool sameValue(T x, T y) noexcept {
    return x == y;
}

enum class Ends : std::uint8_t { A, Both };

/// A precondition on endpoint leaves: when P differs, set @p want on the working copy and record the override.
template <class T, class Field>
void need(ShowcasePlan& pl, Ends ends, const char* param, const std::string& valueName, T want, Field field) {
    bool differs = false;
    const auto fix = [&](MS& s) {
        T& v = field(s);
        if (!sameValue(v, want)) {
            differs = true;
            v = want;
        }
    };
    fix(pl.state.a);
    if (ends == Ends::Both) {
        fix(pl.state.b);
    }
    if (differs) {
        pl.overrides.push_back(std::string(param) + "=" + valueName);
    }
}

/// P-3: a single-state column reads A(P), which is P as stored only at p = 0.
inline void needPositionZero(ShowcasePlan& pl) {
    if (!(pl.state.morph.position == 0.0f)) {
        pl.state.morph.position = 0.0f;
        pl.overrides.emplace_back("kMorphPositionId=0");
    }
}

inline void needMono(ShowcasePlan& pl) {
    if (pl.state.voiceMode != 0) {
        pl.state.voiceMode = 0;
        pl.controlOverrides.emplace_back(::Profundum::kVoiceModeId, 0.0);
        pl.overrides.emplace_back("kVoiceModeId=Mono");
    }
}

inline void needLegato(ShowcasePlan& pl) {
    if (pl.state.note.legato != 1) {
        pl.state.note.legato = 1;
        pl.overrides.emplace_back("kNoteLegatoId=On");
    }
}

inline void needRetriggerReset(ShowcasePlan& pl) {
    const auto reset = static_cast<std::int32_t>(Krate::DSP::NoteMemory::Retrigger::Reset);
    if (pl.state.note.retrigger != reset) {
        pl.state.note.retrigger = reset;
        pl.overrides.emplace_back("kNoteRetriggerId=Reset");
    }
}

inline void needMemory(ShowcasePlan& pl, Krate::DSP::NoteMemory::Mode m, const char* name) {
    const auto want = static_cast<std::int32_t>(m);
    if (pl.state.note.memory != want) {
        pl.state.note.memory = want;
        pl.overrides.push_back(std::string("kNoteMemoryId=") + name);
    }
}

[[nodiscard]] inline const char* imageModeName(Img m) noexcept {
    switch (m) {
        case Img::Solid: return "Solid";
        case Img::Spread: return "Spread";
        case Img::Wide: return "Wide";
    }
    return "?";
}

[[nodiscard]] inline const char* waveformName(AWave w) noexcept {
    switch (w) {
        case AWave::Sine: return "Sine";
        case AWave::Triangle: return "Triangle";
        case AWave::RoundedSine: return "RoundedSine";
        case AWave::AsymmetricSine: return "AsymmetricSine";
        case AWave::OctaveDownSine: return "OctaveDownSine";
    }
    return "?";
}

[[nodiscard]] inline const char* anchorModeName(AMode m) noexcept {
    switch (m) {
        case AMode::Lock: return "Lock";
        case AMode::Follow: return "Follow";
        case AMode::Punch: return "Punch";
        case AMode::Ghost: return "Ghost";
    }
    return "?";
}

[[nodiscard]] inline const char* motionTargetName(MTgt t) noexcept {
    switch (t) {
        case MTgt::Mass: return "Mass";
        case MTgt::Depth: return "Depth";
        case MTgt::Body: return "Body";
        case MTgt::Edge: return "Edge";
        case MTgt::Shift: return "Shift";
    }
    return "?";
}

/// Field accessors for need().
inline Img& fieldMode(MS& s) { return s.field.mode; }
inline float& fieldSpread(MS& s) { return s.field.spread; }
inline AMode& anchorMode(MS& s) { return s.anchor.mode; }
inline AWave& anchorWaveform(MS& s) { return s.anchor.waveform; }

inline void needImage(ShowcasePlan& pl, Ends ends, Img m) {
    need(pl, ends, "kFieldImageModeId", imageModeName(m), m, fieldMode);
}
inline void needSpreadOne(ShowcasePlan& pl, Ends ends) {
    need(pl, ends, "kFieldSpreadId", "1", 1.0f, fieldSpread);
}
inline void needAnchorMode(ShowcasePlan& pl, Ends ends, AMode m) {
    need(pl, ends, "kAnchorModeId", anchorModeName(m), m, anchorMode);
}
inline void needWaveform(ShowcasePlan& pl, Ends ends, AWave w) {
    need(pl, ends, "kAnchorWaveformId", waveformName(w), w, anchorWaveform);
}

inline void declareS(ShowcasePlan& pl, const auRd::AuSetting& lo, const auRd::AuSetting& hi) {
    pl.declared = {{lo.a, lo.b}, {hi.a, hi.b}};
}

inline void declareOne(ShowcasePlan& pl, const MS& lo, const MS& hi) {
    pl.declared = {{lo, lo}, {hi, hi}};
}

/// auSteadyCell on lo = S(P) with @p loLever and hi = S(P) with @p hiLever (an empty lever leaves S(P)).
inline void steadyPlan(ShowcasePlan& pl, const std::string& row, const std::string& contrast, auRd::AuDesc d,
                       const std::string& tap, double bar, const std::string& barText, const std::string& descriptor,
                       const ShowcaseLever& loLever, const ShowcaseLever& hiLever = {}, bool floorRule = false) {
    const auRd::AuSetting s = settingOf(pl.state);
    declareS(pl, levered(s, loLever), levered(s, hiLever));
    pl.descriptor = descriptor;
    pl.floorRule = floorRule;
    pl.cell = [row, contrast, d, tap, bar, barText, loLever, hiLever](const DecodedPresetState& q, std::uint8_t note) {
        const auRd::AuSetting sq = settingOf(q);
        return auRd::auSteadyCell(row, contrast, kShowcaseBase, note, d, tap, levered(sq, loLever),
                                  levered(sq, hiLever), bar, barText);
    };
}

/// A two-digit AT row name: "AT-04".
[[nodiscard]] inline std::string atRow(std::size_t atId) {
    std::string r = "AT-";
    if (atId < 10u) {
        r += '0';
    }
    r += std::to_string(atId);
    return r;
}

}  // namespace preset_showcase_detail

/// Plan 5.4, one row of the table per column: preconditions on the working copy (recorded), the declared endpoints
/// (plan 5.4 "Endpoint declaration") and the moved entry point the cell calls.
[[nodiscard]] inline ShowcasePlan showcaseArmPlan(const DecodedPresetState& p, ::Profundum::PresetDefs::Capability c) {
    namespace sd = preset_showcase_detail;
    using sd::Ends;
    using sd::MS;
    using C = ::Profundum::PresetDefs::Capability;
    using auRd::AuDesc;

    ShowcasePlan pl;
    pl.state = p;
    pl.notes = {auRd::kAuE1, auRd::kAuC3};
    const auto idx = static_cast<std::size_t>(c);

    switch (c) {
        // ---- Core ----
        case C::AT01:
            sd::need(pl, Ends::Both, "kCoreExcitationDriveId", "1", 1.0f,
                     [](MS& s) -> float& { return s.excitation.drive; });
            sd::steadyPlan(pl, "AT-01", "Excitation: mix 0 -> stored (drive 1)", AuDesc::DNl, "Char / twin",
                           auRd::kAuBarDb, ">= 3 dB", "D_nl", [](MS& s) { s.excitation.mix = 0.0f; });
            break;
        case C::AT02:
            sd::steadyPlan(pl, "AT-02", "SHAPE: 0 -> stored", AuDesc::DBr, "Sum", auRd::kAuBarOct, ">= 0.25 oct",
                           "D_br", [](MS& s) { s.shape = 0.0f; });
            break;
        case C::AT03:
            sd::steadyPlan(pl, "AT-03", "Fold: foldAmount 0 -> stored", AuDesc::DNl, "Char / twin", auRd::kAuBarDb,
                           ">= 3 dB", "D_nl", [](MS& s) { s.foldAmount = 0.0f; });
            break;

        // ---- Character ----
        case C::AT04:
        case C::AT05:
        case C::AT06:
        case C::AT07:
        case C::AT08:
        case C::AT09: {
            const std::size_t i = idx - static_cast<std::size_t>(C::AT04);
            const sd::Alg alg = auRd::kAuAlgs[i];
            sd::need(pl, Ends::Both, "kCharacterId", auRd::kAuAlgNames[i], alg,
                     [](MS& s) -> sd::Alg& { return s.characterAlgorithm; });
            sd::steadyPlan(pl, sd::atRow(4u + i), std::string("Character ") + auRd::kAuAlgNames[i] + ": Amount 0 -> stored",
                           AuDesc::DNl, "Char / twin", auRd::kAuBarDb, ">= 3 dB", "D_nl",
                           [](MS& s) { s.characterAmount = 0.0f; });
            break;
        }

        // ---- Anchor ----
        case C::AT10:
            sd::needWaveform(pl, Ends::Both, sd::AWave::Sine);
            sd::needAnchorMode(pl, Ends::Both, sd::AMode::Lock);
            sd::steadyPlan(pl, "AT-10", "Anchor: level 0 -> stored (Sine, LOCK)", AuDesc::ESub, "Sum",
                           auRd::kAuBarAnchorDb, "dE_sub >= 2 dB", "E_sub", [](MS& s) { s.anchor.level = 0.0f; });
            break;
        case C::AT11:
        case C::AT12: {
            const bool punch = c == C::AT11;
            sd::needPositionZero(pl);
            sd::needAnchorMode(pl, Ends::A, punch ? sd::AMode::Punch : sd::AMode::Ghost);
            const MS hi = pl.state.a;
            MS lo = hi;
            lo.anchor.mode = sd::AMode::Lock;
            sd::declareOne(pl, lo, hi);
            pl.windowMs = static_cast<double>(punch ? hi.anchor.punchTimeMs : hi.anchor.ghostTimeMs);
            pl.descriptor = "E_sub_onset";
            pl.cell = [punch](const DecodedPresetState& q, std::uint8_t note) {
                MS l = q.a;
                l.anchor.mode = sd::AMode::Lock;
                const float w = punch ? q.a.anchor.punchTimeMs : q.a.anchor.ghostTimeMs;
                return auRd::auCellOnsetESub(punch, l, q.a, w, note);
            };
            break;
        }
        case C::AT13: {
            sd::needPositionZero(pl);
            sd::needAnchorMode(pl, Ends::A, sd::AMode::Follow);
            pl.descriptor = "anchor_shift_cents";
            const MS& a = pl.state.a;
            const bool fracture = a.characterAlgorithm == sd::Alg::Fracture;
            const bool rectify = a.characterAlgorithm == sd::Alg::Rectify;
            if (!(fracture || rectify) || !(a.characterAmount >= 1.0f - 1.0e-6f)) {
                pl.applicable = false;   // P-2: no stored variant, and no algorithm override
                pl.variant = "none";
                break;
            }
            const sd::Alg alg = fracture ? sd::Alg::Fracture : sd::Alg::Rectify;
            pl.variant = fracture ? "Fracture" : "Rectify";
            MS lock = a;
            lock.anchor.mode = sd::AMode::Lock;
            sd::declareOne(pl, lock, a);
            pl.cell = [alg](const DecodedPresetState& q, std::uint8_t note) {
                MS l = q.a;
                l.anchor.mode = sd::AMode::Lock;
                return auRd::auCellAt13(alg, l, q.a, note);
            };
            break;
        }
        case C::AT14:
        case C::AT15:
        case C::AT16: {
            const std::size_t i = idx - static_cast<std::size_t>(C::AT14);
            const std::array<sd::AWave, 3> waves{sd::AWave::Triangle, sd::AWave::RoundedSine,
                                                 sd::AWave::AsymmetricSine};
            sd::needWaveform(pl, Ends::Both, waves[i]);
            sd::steadyPlan(pl, sd::atRow(14u + i), std::string("Anchor waveform: Sine -> ") + sd::waveformName(waves[i]),
                           AuDesc::DCx, "Sum", auRd::kAuBarDb, ">= 3 dB", "D_cx",
                           [](MS& s) { s.anchor.waveform = sd::AWave::Sine; });
            break;
        }
        case C::AT17: {
            sd::needPositionZero(pl);
            sd::needWaveform(pl, Ends::A, sd::AWave::OctaveDownSine);
            MS lo = pl.state.a;
            lo.anchor.waveform = sd::AWave::Sine;
            sd::declareOne(pl, lo, pl.state.a);
            pl.descriptor = "P_half";
            pl.cell = [](const DecodedPresetState& q, std::uint8_t note) {
                MS l = q.a;
                l.anchor.waveform = sd::AWave::Sine;
                return auRd::auCellAt17(l, q.a, note);
            };
            break;
        }
        case C::AT18:
            sd::steadyPlan(pl, "AT-18", "anchorFoldFeed: 0 -> stored", AuDesc::DNl, "Char / twin", auRd::kAuBarDb,
                           ">= 3 dB", "D_nl", [](MS& s) { s.anchorFoldFeed = 0.0f; });
            break;

        // ---- Pitch ----
        case C::AT19:
        case C::AT20:
        case C::AT21:
        case C::AT22: {
            sd::needPositionZero(pl);
            if (c != C::AT19) {
                sd::need(pl, Ends::A, "kDropAmountId", "24 st", static_cast<float>(auRd::kAuDropAmountSt),
                         [](MS& s) -> float& { return s.dropAmountSt; });
            }
            ShowcaseLever lever;
            switch (c) {
                case C::AT19: lever = [](MS& s) { s.dropAmountSt = 0.0f; }; pl.descriptor = "drop_start"; break;
                case C::AT20: lever = [](MS& s) { s.dropTimeMs = 60.0f; }; pl.descriptor = "drop_t_s"; break;
                case C::AT21: lever = [](MS& s) { s.dropShape = 0.0f; }; pl.descriptor = "drop_alpha"; break;
                default: lever = [](MS& s) { s.dropRebound = 0.0f; }; pl.descriptor = "drop_rho"; break;
            }
            sd::declareOne(pl, sd::leveredOne(pl.state.a, lever), pl.state.a);
            pl.cell = [c, lever](const DecodedPresetState& q, std::uint8_t note) {
                const MS lo = sd::leveredOne(q.a, lever);
                using D = auPt::VoiceEngineDropDescriptors;
                switch (c) {
                    case C::AT19:
                        return auRd::auDropCell("AT-19", "Drop amount: 0 -> stored", lo, q.a, note,
                                                "start pitch (st, Phase 4 descriptor)",
                                                [](const D& d) { return d.start; }, auRd::kAuBarStartSt, ">= 6 st",
                                                false);
                    case C::AT20:
                        return auRd::auDropCell("AT-20", "Drop time: 60 ms -> stored (amount 24 st)", lo, q.a, note,
                                                "t_s (samples, 1-cent threshold)", [](const D& d) { return d.ts; },
                                                auRd::kAuBarRatio, "ratio >= 2", true);
                    case C::AT21:
                        return auRd::auDropCell("AT-21", "Drop shape: 0 -> stored (amount 24 st)", lo, q.a, note,
                                                "alpha (mean d over [0, N] / A)", [](const D& d) { return d.alpha; },
                                                auRd::kAuBarAlpha, ">= 0.1", false);
                    default:
                        return auRd::auDropCell("AT-22", "Drop rebound: 0 -> stored (amount 24 st)", lo, q.a, note,
                                                "rho (st, the deepest undershoot)", [](const D& d) { return d.rho; },
                                                auRd::kAuBarRhoFrac * auRd::kAuDropAmountSt,
                                                ">= 0.05 A_drop = 1.2 st", false);
                }
            };
            break;
        }
        case C::AT23:
            sd::needPositionZero(pl);
            sd::needMono(pl);
            sd::needLegato(pl);
            pl.declared = {{pl.state.a, pl.state.a}};
            pl.descriptor = "t_50";
            pl.cell = [](const DecodedPresetState& q, std::uint8_t note) {
                return auRd::auCellAt23(q.a, note, 0.0f, q.note.portamentoMs);   // lo: portamento 0 (inside L5)
            };
            break;

        // ---- Envelope ----
        case C::AT24: {
            const ShowcaseLever lever = [](MS& s) { s.ampEnv.attackMs = 0.1f; };
            const auRd::AuSetting s = sd::settingOf(pl.state);
            sd::declareS(pl, sd::levered(s, lever), s);
            pl.descriptor = "t_90";
            pl.cell = [lever](const DecodedPresetState& q, std::uint8_t note) {
                const auRd::AuSetting sq = sd::settingOf(q);
                return auRd::auCellAt24(sd::levered(sq, lever), sq, sd::kShowcaseBase, true, "", note, "");
            };
            break;
        }
        case C::AT25: {
            const ShowcaseLever lever = [](MS& s) { s.ampEnv.releaseMs = 50.0f; };
            const auRd::AuSetting s = sd::settingOf(pl.state);
            sd::declareS(pl, sd::levered(s, lever), s);
            pl.descriptor = "D_t_release";
            pl.cell = [lever](const DecodedPresetState& q, std::uint8_t note) {
                const auRd::AuSetting sq = sd::settingOf(q);
                return auRd::auCellAt25(sd::levered(sq, lever), sq, note, "");
            };
            break;
        }

        // ---- Mass ----
        case C::AT26:
            sd::steadyPlan(pl, "AT-26", "Mass: 0 -> stored", AuDesc::ESub, "Sum", auRd::kAuBarDb, ">= 3 dB", "E_sub",
                           [](MS& s) { s.mass = 0.0f; });
            break;
        case C::AT27: {
            const ShowcaseLever lever = [](MS& s) { s.mass = 0.0f; };
            const auRd::AuSetting s = sd::settingOf(pl.state);
            sd::declareS(pl, sd::levered(s, lever), s);
            pl.descriptor = "dL_K";
            pl.cell = [lever](const DecodedPresetState& q, std::uint8_t note) {
                const auRd::AuSetting sq = sd::settingOf(q);
                return auRd::auCellAt27(sd::levered(sq, lever), sq, sd::kShowcaseBase, note);
            };
            break;
        }
        case C::AT28: {
            const ShowcaseLever lever = [](MS& s) { s.transientAttackDb = 0.0f; };
            const auRd::AuSetting s = sd::settingOf(pl.state);
            sd::declareS(pl, sd::levered(s, lever), s);
            pl.descriptor = "onset_ratio";
            pl.cell = [lever](const DecodedPresetState& q, std::uint8_t note) {
                const auRd::AuSetting sq = sd::settingOf(q);
                return auRd::auCellAt28(sd::levered(sq, lever), sq, note);
            };
            break;
        }

        // ---- Field ----
        case C::AT29:
        case C::AT30:
            sd::needImage(pl, Ends::Both, c == C::AT29 ? sd::Img::Spread : sd::Img::Wide);
            sd::needSpreadOne(pl, Ends::Both);
            sd::steadyPlan(pl, c == C::AT29 ? "AT-29" : "AT-30",
                           c == C::AT29 ? "Image mode: SOLID -> SPREAD (spread 1)" : "Image mode: SOLID -> WIDE (spread 1)",
                           AuDesc::DSt, "field stage tap", auRd::kAuBarDb, ">= 3 dB", "D_st",
                           [](MS& s) { s.field.mode = sd::Img::Solid; }, {}, true);
            break;
        case C::AT31:
            sd::needImage(pl, Ends::Both, sd::Img::Spread);
            sd::steadyPlan(pl, "AT-31", "Spread: 0 -> stored (SPREAD)", AuDesc::DSt, "field stage tap",
                           auRd::kAuBarDb, ">= 3 dB", "D_st", [](MS& s) { s.field.spread = 0.0f; }, {}, true);
            break;
        case C::AT32:
            sd::needImage(pl, Ends::Both, sd::Img::Spread);
            sd::needSpreadOne(pl, Ends::Both);
            sd::steadyPlan(pl, "AT-32", "Gravity: 0 -> stored (SPREAD, spread 1)", AuDesc::DStLow, "field stage tap",
                           auRd::kAuBarDb, ">= 3 dB", "D_st_low", [](MS& s) { s.field.gravity = 0.0f; });
            pl.notes = {auRd::kAuE1};   // E1 only
            break;
        case C::AT33: {
            sd::needPositionZero(pl);
            sd::needImage(pl, Ends::A, sd::Img::Spread);   // P-4: the cell forces these on its hi render
            sd::needSpreadOne(pl, Ends::A);
            sd::need(pl, Ends::A, "kFieldMotionDepthId", "1", 1.0f, [](MS& s) -> float& { return s.field.motionDepth; });
            MS lo = pl.state.a;   // the cell's internally built lo state: depth 0 with the forced SPREAD / spread 1
            lo.field.mode = sd::Img::Spread;
            lo.field.spread = 1.0f;
            lo.field.motionDepth = 0.0f;
            sd::declareOne(pl, lo, pl.state.a);
            pl.descriptor = "D_st_p2p";
            pl.cell = [](const DecodedPresetState& q, std::uint8_t note) {
                return auRd::auFieldMotionCell(q.a, sd::kShowcaseBase, note, true, "");
            };
            break;
        }

        // ---- Motion ----
        case C::AT34:
        case C::AT35:
        case C::AT36:
        case C::AT37:
        case C::AT38:
        case C::AT39:
        case C::AT40: {
            struct MotionRow {
                sd::MSrc source;
                sd::MTgt target;
                auRd::AuFrameDesc d;
                double bar;
                const char* barText;
                const char* descriptor;
                const char* contrast;
            };
            // The auShardMotion rows (AT-34..38 CYCLE on their target, AT-39 ENV and AT-40 DRIFT on Mass).
            const std::array<MotionRow, 7> rows{{
                {sd::MSrc::Cycle, sd::MTgt::Mass, auRd::AuFrameDesc::ESub, auRd::kAuBarDb, ">= 3 dB", "E_sub_p2p",
                 "Motion target Mass: Motion Off -> stored CYCLE"},
                {sd::MSrc::Cycle, sd::MTgt::Depth, auRd::AuFrameDesc::ESub, auRd::kAuBarDb, ">= 3 dB", "E_sub_p2p",
                 "Motion target Depth: Motion Off -> stored CYCLE"},
                {sd::MSrc::Cycle, sd::MTgt::Body, auRd::AuFrameDesc::DCx, auRd::kAuBarDb, ">= 3 dB", "D_cx_p2p",
                 "Motion target Body: Motion Off -> stored CYCLE"},
                {sd::MSrc::Cycle, sd::MTgt::Edge, auRd::AuFrameDesc::DBr, auRd::kAuBarOct, ">= 0.25 oct", "D_br_p2p",
                 "Motion target Edge: Motion Off -> stored CYCLE"},
                {sd::MSrc::Cycle, sd::MTgt::Shift, auRd::AuFrameDesc::DBr, auRd::kAuBarOct, ">= 0.25 oct",
                 "D_br_p2p", "Motion target Shift: Motion Off -> stored CYCLE"},
                {sd::MSrc::Env, sd::MTgt::Mass, auRd::AuFrameDesc::ESub, auRd::kAuBarDb, ">= 3 dB", "E_sub_p2p",
                 "Motion source -> Mass: Off -> stored ENV (retriggered every 2 s)"},
                {sd::MSrc::Drift, sd::MTgt::Mass, auRd::AuFrameDesc::ESub, auRd::kAuBarDb, ">= 3 dB", "E_sub_p2p",
                 "Motion source -> Mass: Off -> stored DRIFT"},
            }};
            const std::size_t i = idx - static_cast<std::size_t>(C::AT34);
            const MotionRow r = rows[i];
            sd::needPositionZero(pl);
            const char* sourceName = r.source == sd::MSrc::Cycle ? "Cycle" : (r.source == sd::MSrc::Env ? "Env" : "Drift");
            sd::need(pl, Ends::A, "kMotionSourceId", sourceName, r.source,
                     [](MS& s) -> sd::MSrc& { return s.motion.source; });
            sd::need(pl, Ends::A, "kMotionTargetId", sd::motionTargetName(r.target), r.target,
                     [](MS& s) -> sd::MTgt& { return s.motion.target; });
            if (r.source == sd::MSrc::Env) {   // C-9: ENV -> Mass is Unipolar
                sd::need(pl, Ends::A, "kMotionPolarityId", "Unipolar", sd::MPol::Unipolar,
                         [](MS& s) -> sd::MPol& { return s.motion.polarity; });
            }
            MS lo = pl.state.a;   // the cell's internally built lo state (source Off)
            lo.motion.source = sd::MSrc::Off;
            sd::declareOne(pl, lo, pl.state.a);
            pl.descriptor = r.descriptor;
            const std::string row = sd::atRow(34u + i);
            // seconds_row: the value auShardMotion passes (cycle rows: max(cycles / rate, 3 s); ENV; DRIFT).
            const double seconds = r.source == sd::MSrc::Cycle
                                       ? std::max(auRd::kAuMotionCycles / auRd::kAuMotionRateHz, 3.0)
                                       : (r.source == sd::MSrc::Env ? auRd::kAuEnvSeconds : auRd::kAuDriftSeconds);
            const bool env = r.source == sd::MSrc::Env;
            pl.cell = [r, row, seconds, env](const DecodedPresetState& q, std::uint8_t note) {
                const std::vector<auPt::VoiceEngineEvent> extra =
                    env ? auRd::auEnvRetriggers(note, seconds) : std::vector<auPt::VoiceEngineEvent>{};
                return auRd::auMotionCell(row, r.contrast, q.a, sd::kShowcaseBase, note, q.a.motion, r.d, seconds,
                                          extra, r.bar, r.barText, true, "");
            };
            break;
        }

        // ---- Morph (performance) ----
        case C::AT41:
        case C::AT42: {
            const float position = c == C::AT41 ? 1.0f : 0.5f;
            const auRd::AuSetting s = sd::settingOf(pl.state);
            pl.declared = {{s.a, s.b}};
            pl.descriptor = "D_nl";
            pl.cell = [c, position](const DecodedPresetState& q, std::uint8_t note) {
                auRd::AuSetting lo = sd::settingOf(q);
                lo.position = 0.0f;
                auRd::AuSetting hi = lo;
                hi.position = position;
                return auRd::auSteadyCell(c == C::AT41 ? "AT-41" : "AT-42",
                                          c == C::AT41 ? "Morph position: 0 -> 1" : "Morph position: 0 -> 0.5",
                                          sd::kShowcaseBase, note, AuDesc::DNl, "Char / twin", lo, hi, auRd::kAuBarDb,
                                          ">= 3 dB");
            };
            break;
        }

        // ---- Note Memory ----
        case C::AT43:
        case C::AT44:
        case C::AT45:
        case C::AT46: {
            const std::size_t i = idx - static_cast<std::size_t>(C::AT43);
            const auRd::AuNoteMemoryRow& r = auRd::auNoteMemoryRows()[i];
            sd::needPositionZero(pl);
            sd::needMono(pl);
            sd::needRetriggerReset(pl);
            sd::needMemory(pl, r.mode, r.modeName);
            pl.declared = {{pl.state.a, pl.state.a}};
            pl.descriptor = "phase_deg";
            pl.cell = [i](const DecodedPresetState& q, std::uint8_t note) {
                const std::uint64_t t1 = auRd::auPickTransition(auRd::auHz(note), auRd::auHz(auRd::auUpOctave(note)));
                return auRd::auCellNoteMemory(q.a, q.a, auRd::auNoteMemoryRows()[i], note, t1);
            };
            break;
        }

        // ---- Macros (performance) ----
        case C::AT47:
        case C::AT48:
        case C::AT49:
        case C::AT50: {
            const auto m = static_cast<Krate::DSP::ProfundumMacro>(idx - static_cast<std::size_t>(C::AT47));
            const auRd::AuSetting s = sd::settingOf(pl.state);
            pl.declared = {{s.a, s.b}};
            pl.macrosFromScript = true;
            switch (auRd::auAxisOf(m)) {
                case auRd::AuAxis::ESub: pl.descriptor = "E_sub"; break;
                case auRd::AuAxis::Dbr: pl.descriptor = "D_br"; break;
                case auRd::AuAxis::Dt: pl.descriptor = "D_t"; break;
                case auRd::AuAxis::Dnl: pl.descriptor = "D_nl"; break;
            }
            pl.cell = [m](const DecodedPresetState& q, std::uint8_t note) {
                const auto set = [m](auRd::AuSetting x, float v) {
                    switch (m) {
                        case Krate::DSP::ProfundumMacro::Weight: x.macros.weight = v; break;
                        case Krate::DSP::ProfundumMacro::DarkBright: x.macros.darkBright = v; break;
                        case Krate::DSP::ProfundumMacro::TightLoose: x.macros.tightLoose = v; break;
                        case Krate::DSP::ProfundumMacro::CleanViolent: x.macros.cleanViolent = v; break;
                        case Krate::DSP::ProfundumMacro::Count: break;
                    }
                    return x;
                };
                const auRd::AuSetting sq = sd::settingOf(q);   // the other three at P's stored values
                return auRd::auMacroCellOn(m, set(sq, 0.0f), set(sq, 1.0f), sd::kShowcaseBase, note, true, "");
            };
            break;
        }

        // ---- Reference reverse arms (FR-013) ----
        case C::RSolid:
            sd::needImage(pl, Ends::Both, sd::Img::Solid);
            sd::steadyPlan(pl, "R-SOLID", "Image mode: stored SOLID -> SPREAD (spread 1), the AT-29 reverse arm",
                           AuDesc::DSt, "field stage tap", auRd::kAuBarDb, ">= 3 dB", "D_st", {},
                           [](MS& s) {
                               s.field.mode = sd::Img::Spread;
                               s.field.spread = 1.0f;
                           },
                           true);
            break;
        case C::RLock: {
            sd::needAnchorMode(pl, Ends::Both, sd::AMode::Lock);
            sd::needPositionZero(pl);
            const ShowcaseLever punch = [](MS& s) {
                s.anchor.mode = sd::AMode::Punch;
                s.anchor.punchBoostDb = 6.0f;
                s.anchor.punchTimeMs = 40.0f;
            };
            sd::declareOne(pl, pl.state.a, sd::leveredOne(pl.state.a, punch));
            pl.windowMs = 40.0;
            pl.descriptor = "E_sub_onset";
            pl.cell = [punch](const DecodedPresetState& q, std::uint8_t note) {
                return auRd::auCellOnsetESub(true, q.a, sd::leveredOne(q.a, punch), 40.0f, note);
            };
            break;
        }
        case C::RSine:
            sd::needWaveform(pl, Ends::Both, sd::AWave::Sine);
            sd::steadyPlan(pl, "R-SINE", "Anchor waveform: stored Sine -> Triangle, the AT-14 reverse arm",
                           AuDesc::DCx, "Sum", auRd::kAuBarDb, ">= 3 dB", "D_cx", {},
                           [](MS& s) { s.anchor.waveform = sd::AWave::Triangle; });
            break;
        case C::Count:
            pl.applicable = false;
            break;
    }
    return pl;
}

/// Plan 5.4: the arms of column @p c for preset @p p (decoded) with Comp bytes @p comp. Every cell runs under
/// ScriptRenderScope(processorRenderHook, &ctx) with the plan's declared endpoints; the AuCell's value / pass / bar
/// are copied, never recomputed. A primary arm with a recorded override fails (SC-007).
[[nodiscard]] inline std::vector<ShowcaseArm> showcaseColumn(const DecodedPresetState& p,
                                                             std::span<const std::uint8_t> comp,
                                                             ::Profundum::PresetDefs::Capability c, bool primary) {
    const ShowcasePlan plan = showcaseArmPlan(p, c);
    std::string overrides;
    for (const std::string& o : plan.overrides) {
        if (!overrides.empty()) {
            overrides += ';';
        }
        overrides += o;
    }
    std::vector<ShowcaseArm> arms;
    arms.reserve(plan.notes.size());
    for (const std::uint8_t note : plan.notes) {
        ShowcaseArm arm;
        arm.column = c;
        arm.variant = plan.variant;
        arm.note = note;
        arm.windowMs = plan.windowMs;
        arm.overrides = overrides;
        arm.descriptor = plan.descriptor;
        if (plan.applicable && plan.cell) {
            PresetArmContext ctx;
            ctx.preset = &plan.state;
            ctx.comp = comp;
            ctx.allowedEndpoints = plan.declared;
            ctx.overrides = plan.controlOverrides;
            ctx.macrosFromScript = plan.macrosFromScript;
            auRd::AuCell cell;
            {
                const auRd::render::ScriptRenderScope scope(&processorRenderHook, &ctx);
                cell = plan.cell(plan.state, note);
            }
            if (plan.floorRule) {
                auRd::auFloorRule(cell);
            }
            arm.lo = cell.lo;
            arm.hi = cell.hi;
            arm.value = cell.value;
            arm.bar = cell.bar;
            arm.barText = cell.barText;
            arm.tap = cell.tap;
            arm.measured = true;
            arm.pass = cell.pass && !(primary && !overrides.empty());
            for (const PresetArmContext::Fidelity& f : ctx.fidelity) {
                arm.fidelityPass = arm.fidelityPass && f.pass;
            }
        }
        arms.push_back(std::move(arm));
    }
    return arms;
}

// =============================================================================
// T018: the C-7 descriptor, distance and threshold (plan 5.8; P-5, P-7)
// =============================================================================

inline constexpr std::size_t kPresetDescriptorComponents = 15;
/// describePreset's seedOverride meaning "render the stored seed".
inline constexpr std::uint32_t kPresetStoredSeed = 0u;

/// The C-7 descriptor of one preset at one note (plan 5.8).
struct PresetDescriptor {
    std::array<double, kPresetDescriptorComponents> x{};
    std::array<bool, kPresetDescriptorComponents> valid{};
    double midTotalDb = -200.0;           // the descriptor window's mid total (bandLevels power sum)
    // T018 additions (reported by the [long] distinctness case, SC-020):
    bool windowValid = false;             // false: the window still reads below -60 dBFS (SC-020 fail)
    std::size_t windowStart = 0, windowLength = 0;
    double t90Ms = 0.0;                   // the sweep render's output-mid onset (fallback window start)
    double steadyMidTotalDb = -200.0;     // components 10-12's window (P-5)
    bool fidelityPass = true;             // every dNlDb twin's fidelity arm
};

namespace preset_descriptor_detail {

inline constexpr double kFloorDb = -60.0;
inline constexpr std::size_t kFallbackLength = 65536;   // 2^16

[[nodiscard]] inline BandLevelReading bandsOver(const std::vector<float>& L, const std::vector<float>& R,
                                                std::size_t start, std::size_t length) {
    REQUIRE(start + length <= L.size());
    REQUIRE(start + length <= R.size());
    return bandLevels(std::span<const float>(L.data() + start, length), std::span<const float>(R.data() + start, length),
                      auRd::kAuFs);
}

/// 10 log10 of the five mid band powers' sum.
[[nodiscard]] inline double midTotalDb(const BandLevelReading& r) {
    double e = 0.0;
    for (std::size_t b = 0; b < kBandLevelBandCount; ++b) {
        e += std::pow(10.0, r.db[0][b] / 10.0);
    }
    return 10.0 * std::log10(std::max(e, 1.0e-30));
}

/// Plan 5.6 t90: the start sample of the first 1 ms frame of the output mid whose RMS is >= 0.9 x the maximum 1 ms
/// frame RMS over [0, noteOff).
[[nodiscard]] inline std::size_t t90Sample(const std::vector<float>& L, const std::vector<float>& R,
                                           std::size_t noteOff) {
    const auto frame = static_cast<std::size_t>(std::llround(0.001 * auRd::kAuFs));
    std::vector<double> rms;
    for (std::size_t s0 = 0; s0 + frame <= noteOff && s0 + frame <= L.size(); s0 += frame) {
        double e = 0.0;
        for (std::size_t i = s0; i < s0 + frame; ++i) {
            const double m = 0.5 * (static_cast<double>(L[i]) + static_cast<double>(R[i]));
            e += m * m;
        }
        rms.push_back(std::sqrt(e / static_cast<double>(frame)));
    }
    double mx = 0.0;
    for (const double v : rms) {
        mx = std::max(mx, v);
    }
    for (std::size_t k = 0; k < rms.size(); ++k) {
        if (mx > 0.0 && rms[k] >= 0.9 * mx) {
            return k * frame;
        }
    }
    return 0;
}

/// P-7 units: dB / 3 (0-9, 10, 14), D_br / 0.25 oct (11), E_sub - mid / 2 dB (12), log2 ms / 1 (13).
[[nodiscard]] inline double unitOf(std::size_t k) noexcept {
    if (k == 11u) {
        return 0.25;
    }
    if (k == 12u) {
        return 2.0;
    }
    if (k == 13u) {
        return 1.0;
    }
    return 3.0;
}

}  // namespace preset_descriptor_detail

/// Plan 5.8: the descriptor of decoded preset @p p (Comp @p comp) at @p note. Components 0-9 and 14 on the sweep
/// render's descriptor window ([1.0 s, 3.0 s), else [t90, t90 + 2^16)), rendered at host block @p hostBlock;
/// 10-12 from auSteady(S(P), note, d) under the preset hook (P-5; 12 = eSubDb minus the mid total of the same steady
/// render's Sum tap over readWindow); 13 = log2 of auRiseMs(S(P), note). @p seedOverride: kPresetStoredSeed or the
/// seed to render with.
[[nodiscard]] inline PresetDescriptor describePreset(const DecodedPresetState& p, std::span<const std::uint8_t> comp,
                                                     std::uint8_t note, std::size_t hostBlock,
                                                     std::uint32_t seedOverride) {
    namespace dd = preset_descriptor_detail;
    REQUIRE(hostBlock >= 1u);
    DecodedPresetState q = p;
    if (seedOverride != kPresetStoredSeed) {
        q.seed = seedOverride;
    }
    const auRd::AuSetting s = preset_showcase_detail::settingOf(q);
    PresetArmContext ctx;
    ctx.preset = &q;
    ctx.comp = comp;
    ctx.allowedEndpoints = {{q.a, q.b}};
    PresetDescriptor out;

    // 1. The sweep render (C-6): note-on at 0, note-off at 3 s, to note-off + 2^16.
    const auto noteOff = static_cast<std::size_t>(std::llround(3.0 * auRd::kAuFs));
    const std::size_t total = noteOff + dd::kFallbackLength;
    auPt::VoiceEngineScript sweep = auRd::auScript(s, note, static_cast<std::uint64_t>(total));
    sweep.hostBlock = hostBlock;
    sweep.events.push_back(auRd::auOff(static_cast<std::uint64_t>(noteOff), note));
    const auPt::VoiceEngineTaps t = renderScriptThroughProcessor(sweep, false, -1, ctx);
    INFO("describePreset sweep render: \"" << ctx.stop << "\"");
    REQUIRE(ctx.stop.empty());
    REQUIRE(t.outL.size() == total);
    REQUIRE(t.sumL.size() == total);

    // 2. The descriptor window.
    const auto heldFrom = static_cast<std::size_t>(std::llround(1.0 * auRd::kAuFs));
    std::size_t start = heldFrom;
    std::size_t length = noteOff - heldFrom;
    BandLevelReading bands = dd::bandsOver(t.outL, t.outR, start, length);
    double mid = dd::midTotalDb(bands);
    const std::size_t t90 = dd::t90Sample(t.outL, t.outR, noteOff);
    out.t90Ms = 1000.0 * static_cast<double>(t90) / auRd::kAuFs;
    if (mid < dd::kFloorDb) {
        start = t90;
        length = dd::kFallbackLength;
        bands = dd::bandsOver(t.outL, t.outR, start, length);
        mid = dd::midTotalDb(bands);
    }
    out.windowStart = start;
    out.windowLength = length;
    out.midTotalDb = mid;
    out.windowValid = mid >= dd::kFloorDb;

    // 3. Components 0-9 (band shape, side / mid) and 14 (frame-wise E_sub p2p on the Sum tap).
    for (std::size_t b = 0; b < kBandLevelBandCount; ++b) {
        out.x[b] = bands.db[0][b] - mid;
        out.valid[b] = out.windowValid && bands.db[0][b] >= dd::kFloorDb;
        out.x[5 + b] = bands.db[1][b] - bands.db[0][b];
        out.valid[5 + b] = out.windowValid && bands.db[1][b] >= dd::kFloorDb;
    }
    const std::vector<double> series =
        auRd::auFrameSeries(t.sumL, t.sumR, auRd::auHz(note), start, start + length, auRd::AuFrameDesc::ESub);
    out.x[14] = series.empty() ? 0.0 : auRd::auP2p(series);
    out.valid[14] = out.windowValid && !series.empty();

    // 4. Components 10-13 under the preset hook (P-5).
    auRd::AuReading eSub;
    auRd::AuReading dnl;
    auRd::AuReading dbr;
    auRd::AuReading rise;
    {
        const auRd::render::ScriptRenderScope scope(&processorRenderHook, &ctx);
        eSub = auRd::auSteady(s, note, auRd::AuDesc::ESub);
        const std::vector<float> sumL = ctx.lastSumL;   // the same steady render's Sum tap
        const std::vector<float> sumR = ctx.lastSumR;
        const Krate::DSP::BassMorphTest::ReadWindow w = Krate::DSP::BassMorphTest::readWindow(sumL.size(), auRd::kAuFs);
        out.steadyMidTotalDb = dd::midTotalDb(dd::bandsOver(sumL, sumR, w.start, w.length));
        dnl = auRd::auSteady(s, note, auRd::AuDesc::DNl);
        dbr = auRd::auSteady(s, note, auRd::AuDesc::DBr);
        rise = auRd::auRiseMs(s, note);
    }
    const bool steadyOk = out.steadyMidTotalDb >= dd::kFloorDb;
    out.x[10] = dnl.value;
    out.valid[10] = steadyOk && dnl.valid;
    out.x[11] = dbr.value;
    out.valid[11] = steadyOk && dbr.valid;
    out.x[12] = eSub.value - out.steadyMidTotalDb;
    out.valid[12] = steadyOk && eSub.valid;
    out.x[13] = rise.valid ? std::log2(rise.value) : 0.0;
    out.valid[13] = rise.valid;
    for (const PresetArmContext::Fidelity& f : ctx.fidelity) {
        out.fidelityPass = out.fidelityPass && f.pass;
    }
    return out;
}

/// P-7: Euclidean distance over the unmasked components of both notes, in the plan 5.8 units, side / mid clamped to
/// [-40, 0] dB before scaling. A (note, component) slot invalid in either preset is masked and counted in @p masked.
[[nodiscard]] inline double presetDistance(const std::array<PresetDescriptor, 2>& p,
                                           const std::array<PresetDescriptor, 2>& q, std::size_t& masked) {
    namespace dd = preset_descriptor_detail;
    masked = 0;
    double sum = 0.0;
    for (std::size_t n = 0; n < p.size(); ++n) {
        for (std::size_t k = 0; k < kPresetDescriptorComponents; ++k) {
            if (!p[n].valid[k] || !q[n].valid[k]) {
                ++masked;
                continue;
            }
            double a = p[n].x[k];
            double b = q[n].x[k];
            if (k >= 5u && k <= 9u) {
                a = std::clamp(a, -40.0, 0.0);
                b = std::clamp(b, -40.0, 0.0);
            }
            const double d = (a - b) / dd::unitOf(k);
            sum += d * d;
        }
    }
    return std::sqrt(sum);
}

/// P-7: F = max(2.5 t_max, 1.0).
[[nodiscard]] inline double presetDistinctnessThreshold(double tMax) noexcept {
    return std::max(2.5 * tMax, 1.0);
}

// =============================================================================
// T019: sweep renders, readings and the render cache (plan 5.5, 5.6, 5.8 window, 5.12; R-4)
// =============================================================================

/// presetReadings variants: the stored state, the FR-041 maximum-Mass prelude, the FR-063 Poly chord.
inline constexpr int kPresetVariantStored = 0;
inline constexpr int kPresetVariantMaxMass = 1;
inline constexpr int kPresetVariantChord = 2;
/// FR-063: E1, B1, E2 (root, fifth, octave).
inline constexpr std::array<std::uint8_t, 3> kPresetChordNotes{28, 35, 40};
static_assert(kPresetChordNotes[0] == auRd::kAuE1, "FR-063: the chord root is E1");
/// The FR-044 / plan 5.8 seed-twin perturbation.
inline constexpr std::uint32_t kPresetSeedTwinXor = 0x5A5Au;
/// Plan 5.6: the mono-sum window length (2^16).
inline constexpr std::size_t kPresetMonoWindow = std::size_t{1} << 16;

/// Plan 5.6: the slim mono-sum observer (modelled on MspObserver, mono_sum_params_test.cpp:93-132). Per voice it
/// counts the samples whose anchorL != anchorR bitwise, and it records the last resolved crossover. It stores no
/// per-sample vectors. Never asserts inside a callback (they are noexcept); the caller reads the record afterwards.
class PresetSweepObserver final : public Krate::DSP::ProfundumEngineObserver {
public:
    bool haveGrid = false;
    float crossoverHz = 0.0f;
    std::size_t voiceChunks = 0;
    std::array<std::size_t, Krate::DSP::ProfundumEngine::kMaxVoices> anchorMismatches{};   // samples, per voice
    std::size_t anchorMismatchesOutOfRange = 0;   // a voice index past kMaxVoices (never expected)
    double anchorPeak = 0.0;

    void onGrid(const Krate::DSP::ProfundumGridRecord& r) noexcept override {
        if (r.resolved == nullptr) {
            return;
        }
        haveGrid = true;
        crossoverHz = r.resolved->state.field.crossoverHz;
    }

    void onVoiceChunk(const Krate::DSP::ProfundumVoiceTaps& t) noexcept override {
        ++voiceChunks;
        std::size_t bad = 0;
        if (t.anchorL.size() < t.n || t.anchorR.size() < t.n) {
            bad = t.n;   // a tap shorter than the chunk counts every sample as a mismatch
        } else {
            for (std::size_t i = 0; i < t.n; ++i) {
                if (std::bit_cast<std::uint32_t>(t.anchorL[i]) != std::bit_cast<std::uint32_t>(t.anchorR[i])) {
                    ++bad;
                }
                anchorPeak = std::max(anchorPeak, std::abs(static_cast<double>(t.anchorL[i])));
            }
        }
        const auto v = static_cast<std::size_t>(t.voice);
        if (v < anchorMismatches.size()) {
            anchorMismatches[v] += bad;
        } else {
            anchorMismatchesOutOfRange += bad;
        }
    }

    [[nodiscard]] std::size_t totalAnchorMismatches() const noexcept {
        std::size_t n = anchorMismatchesOutOfRange;
        for (const std::size_t m : anchorMismatches) {
            n += m;
        }
        return n;
    }
};

/// One C-6 sweep render of a Comp through a fresh processor (plan 5.5).
struct PresetSweepRender {
    SweepTimeline timeline{};
    Steinberg::uint32 tailSamples = 0;
    std::uint8_t lowestNote = 0;
    std::vector<float> L, R;   // >= timeline.total samples (longer when the plan 5.6 fallback window needs it)
    // PresetSweepObserver's record:
    bool haveGrid = false;
    double crossoverHz = 0.0;
    std::size_t voiceChunks = 0;
    std::size_t anchorMismatches = 0;
    double anchorPeak = 0.0;
};

/// The sample from which a SOLID preset reads L == R bitwise (plan 5.6): kLimiterLookaheadSamples +
/// llround(kModeCrossfadeMs * fs / 1000). 544 at 48 kHz.
[[nodiscard]] inline std::size_t presetSolidSettleSample(double fs) {
    return Krate::DSP::ProfundumEngine::kLimiterLookaheadSamples +
           static_cast<std::size_t>(
               std::llround(static_cast<double>(Krate::DSP::BassStereoField::kModeCrossfadeMs) * fs / 1000.0));
}

/// Plan 5.6 "Active endpoints": A alone at p = 0, B alone at p = 1, both for 0 < p < 1; true when every active
/// endpoint stores SOLID.
[[nodiscard]] inline bool presetSolidActive(const DecodedPresetState& d) noexcept {
    using Img = Krate::DSP::BassStereoField::ImageMode;
    const float p = d.morph.position;
    const bool aActive = !(p >= 1.0f);
    const bool bActive = !(p <= 0.0f);
    return (!aActive || d.a.field.mode == Img::Solid) && (!bActive || d.b.field.mode == Img::Solid);
}

/// Plan 5.5: Comp -> setState (the seed word patched when @p seed is set) -> the FR-041 four parameter-only blocks
/// when @p maxMass -> observer -> setupProcessing(fs, 512) -> getTailSamples() -> the notes on at 0 (velocity
/// 100 / 127), off at llround(3.0 fs), rendered to noteOff + tail, and at least to the end of the plan 5.6
/// fallback window (noteOff + 20 ms + 2^16) so every window the readings use exists; a causal render's samples do
/// not depend on its length.
[[nodiscard]] inline PresetSweepRender renderPresetSweep(std::span<const std::uint8_t> comp,
                                                         std::span<const std::uint8_t> notes, double fs, bool maxMass,
                                                         std::optional<std::uint32_t> seed = std::nullopt) {
    namespace P = ::Profundum;
    REQUIRE(!notes.empty());
    REQUIRE(comp.size() == P::kStateV3Bytes);
    REQUIRE(fs > 0.0);
    auto fx = std::make_unique<ProcessorFixture>();

    StateBytes bytes(comp.size());
    std::memcpy(bytes.data(), comp.data(), comp.size());
    if (seed.has_value()) {
        patchU32(bytes, kV2SeedAt, *seed);
    }
    REQUIRE(loadStateBytes(*fx->proc, bytes) == Steinberg::kResultOk);

    if (maxMass) {   // FR-041 / plan 5.5: Mass 100 under Edit Target A and B, Edit Target back to A, Mod Wheel 1
        MultiParamChanges pc;
        pc.reserve(2);
        const auto block = [&fx, &pc](std::initializer_list<std::pair<Steinberg::Vst::ParamID, double>> points) {
            pc.clear();
            for (const auto& p : points) {
                pc.queueFor(p.first).addTestPoint(0, p.second);
            }
            REQUIRE(fx->processNoOutputs(&pc) == Steinberg::kResultOk);
        };
        block({{P::kMorphEditTargetId, 0.0}, {P::kMassId, 1.0}});
        block({{P::kMorphEditTargetId, 1.0}, {P::kMassId, 1.0}});
        block({{P::kMorphEditTargetId, 0.0}});
        block({{P::kModWheelId, 1.0}});
    }

    PresetSweepObserver obs;
    fx->proc->setEngineObserverForTest(&obs);
    constexpr std::size_t kBlock = 512;
    REQUIRE(fx->prepare(fs, static_cast<Steinberg::int32>(kBlock)) == Steinberg::kResultOk);

    PresetSweepRender out;
    out.tailSamples = fx->proc->getTailSamples();   // after setState AND setupProcessing (processor.cpp:574-594)
    out.timeline = makeTimeline(fs, out.tailSamples);
    REQUIRE(out.timeline.block == kBlock);
    out.lowestNote = *std::min_element(notes.begin(), notes.end());
    const std::size_t fallbackEnd =
        out.timeline.noteOff + static_cast<std::size_t>(std::llround(0.02 * fs)) + kPresetMonoWindow;
    const std::size_t total = std::max(out.timeline.total, fallbackEnd);

    using Ev = ProcessorFixture::ScriptedEvent;
    std::vector<Ev> script;
    script.reserve(2u * notes.size());
    for (const std::uint8_t n : notes) {
        script.push_back(Ev{.sample = 0,
                            .kind = Ev::Kind::NoteOn,
                            .pitch = static_cast<Steinberg::int16>(n),
                            .velocity = static_cast<float>(auRd::kAuVelocity) / 127.0f});
    }
    for (const std::uint8_t n : notes) {
        script.push_back(Ev{.sample = static_cast<Steinberg::int64>(out.timeline.noteOff),
                            .kind = Ev::Kind::NoteOff,
                            .pitch = static_cast<Steinberg::int16>(n)});
    }
    fx->renderScript(script, total, kBlock);

    REQUIRE(fx->proc->endpointRejectionsForTest() == 0u);
    fx->proc->setActive(0u);
    fx->proc->setEngineObserverForTest(nullptr);   // detach while inactive (FR-067)
    REQUIRE(fx->capturedL.size() == total);
    REQUIRE(fx->capturedR.size() == total);
    out.L = std::move(fx->capturedL);
    out.R = std::move(fx->capturedR);
    out.haveGrid = obs.haveGrid;
    out.crossoverHz = static_cast<double>(obs.crossoverHz);
    out.voiceChunks = obs.voiceChunks;
    out.anchorMismatches = obs.totalAnchorMismatches();
    out.anchorPeak = obs.anchorPeak;
    return out;
}

/// Sweep reading cache entry (plan 4.7, 10): one render per (preset, note, fs, variant); the audio is dropped.
struct PresetRenderReadings {
    bool allFinite = false;
    double heldRmsDb = -200.0, tailRmsDb = -200.0, truePeakDb = -200.0;
    Krate::DSP::StereoFieldTest::BandReading monoSum{};
    bool anchorBitIdentical = false, solidBitIdentical = false, controlFails = false;
    double midTotalDb = -200.0, t90Ms = 0.0;
    std::size_t windowStart = 0;
    // T019 additions (reported by the [long] cases):
    std::size_t nonFinite = 0;
    Steinberg::uint32 tailSamples = 0;
    std::size_t total = 0;              // the C-6 timeline total (noteOff + tail)
    bool monoMeasured = false;          // plan 5.6 / 5.8 reads exist only on 48 kHz renders
    int monoSumWindow = 0;              // 1: W1, 2: W2 (t90 + 20 ms), 0: neither valid (the preset fails, FR-042)
    std::size_t monoSumStart = 0;
    double crossoverHz = 0.0;
    bool subInBand = false;             // the lowest note's fundamental lies below f_c
    double controlRho = 0.0;            // rho of the R' = -L copy on the same window
    std::size_t anchorMismatches = 0, voiceChunks = 0;
    bool solidActive = false;
    std::size_t solidFrom = 0, solidDiffering = 0;
    bool windowValid = false;           // the descriptor window's mid total is >= -60 dBFS (plan 5.8)
    std::size_t windowLength = 0;
};

namespace preset_sweep_detail {

/// Plan 5.5: 10 log10(mean over [a, b) of (L^2 + R^2) / 2), dBFS; -200 on an empty or silent span.
[[nodiscard]] inline double rmsDbOver(const std::vector<float>& L, const std::vector<float>& R, std::size_t a,
                                      std::size_t b) {
    b = std::min({b, L.size(), R.size()});
    if (a >= b) {
        return -200.0;
    }
    double e = 0.0;
    for (std::size_t i = a; i < b; ++i) {
        const auto l = static_cast<double>(L[i]);
        const auto r = static_cast<double>(R[i]);
        e += 0.5 * (l * l + r * r);
    }
    e /= static_cast<double>(b - a);
    return e > 0.0 ? 10.0 * std::log10(e) : -200.0;
}

[[nodiscard]] inline double noteHz(std::uint8_t note) noexcept {
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

}  // namespace preset_sweep_detail

/// The readings of one sweep render (plan 5.5, 5.6, 5.8 window, 5.12) for decoded state @p d.
[[nodiscard]] inline PresetRenderReadings presetSweepReadings(const PresetSweepRender& r, const DecodedPresetState& d) {
    namespace sd = preset_sweep_detail;
    namespace dd = preset_descriptor_detail;
    namespace sft = Krate::DSP::StereoFieldTest;
    const double fs = r.timeline.fs;
    const std::size_t total = r.timeline.total;
    REQUIRE(r.L.size() >= total);
    REQUIRE(r.R.size() >= total);

    PresetRenderReadings out;
    out.total = total;
    out.tailSamples = r.tailSamples;
    for (std::size_t i = 0; i < total; ++i) {
        if (!presetFloatIsFinite(r.L[i])) {
            ++out.nonFinite;
        }
        if (!presetFloatIsFinite(r.R[i])) {
            ++out.nonFinite;
        }
    }
    out.allFinite = out.nonFinite == 0u;
    out.heldRmsDb = sd::rmsDbOver(r.L, r.R, static_cast<std::size_t>(std::llround(0.2 * fs)), r.timeline.noteOff);
    const auto tailSpan = static_cast<std::size_t>(std::llround(0.25 * fs));
    out.tailRmsDb = sd::rmsDbOver(r.L, r.R, total > tailSpan ? total - tailSpan : 0u, total);
    out.truePeakDb =
        static_cast<double>(Krate::DSP::TestUtils::measureTruePeakDb(r.L.data(), r.R.data(), total, fs));

    out.crossoverHz = r.crossoverHz;
    out.voiceChunks = r.voiceChunks;
    out.anchorMismatches = r.anchorMismatches;
    out.anchorBitIdentical = r.voiceChunks > 0u && r.anchorMismatches == 0u;

    out.solidActive = presetSolidActive(d);
    out.solidFrom = presetSolidSettleSample(fs);
    for (std::size_t i = out.solidFrom; i < total; ++i) {
        if (std::bit_cast<std::uint32_t>(r.L[i]) != std::bit_cast<std::uint32_t>(r.R[i])) {
            ++out.solidDiffering;
        }
    }
    out.solidBitIdentical = out.solidDiffering == 0u;

    // Plan 5.6 and 5.8 read 48 kHz renders only (the band and frame grids are the 48 kHz ones).
    if (!(fs == auRd::kAuFs)) {
        return out;
    }
    out.monoMeasured = true;
    const std::size_t t90 = dd::t90Sample(r.L, r.R, r.timeline.noteOff);
    out.t90Ms = 1000.0 * static_cast<double>(t90) / fs;

    // Plan 5.6: W1 = [1.5 s, 1.5 s + 2^16), else W2 = [t90 + 20 ms, t90 + 20 ms + 2^16).
    out.subInBand = sd::noteHz(r.lowestNote) < r.crossoverHz;
    const std::array<std::size_t, 2> starts{static_cast<std::size_t>(std::llround(1.5 * fs)),
                                            t90 + static_cast<std::size_t>(std::llround(0.02 * fs))};
    std::size_t controlStart = starts[0];
    for (std::size_t w = 0; w < starts.size(); ++w) {
        const std::size_t s = starts[w];
        REQUIRE(s + kPresetMonoWindow <= r.L.size());
        const sft::BandReading br =
            sft::bandReading(r.L.data() + s, r.R.data() + s, kPresetMonoWindow, fs, 0.0, r.crossoverHz);
        if (w == 0u) {
            out.monoSum = br;   // reported even when invalid
            out.monoSumStart = s;
        }
        if (br.valid) {
            out.monoSum = br;
            out.monoSumWindow = static_cast<int>(w) + 1;
            out.monoSumStart = s;
            controlStart = s;
            break;
        }
    }
    // Non-vacuity (plan 5.6): R' = -L on a copy of the same window must fail the rho bar.
    {
        std::vector<float> negL(kPresetMonoWindow);
        for (std::size_t i = 0; i < kPresetMonoWindow; ++i) {
            negL[i] = -r.L[controlStart + i];
        }
        const sft::BandReading c =
            sft::bandReading(r.L.data() + controlStart, negL.data(), kPresetMonoWindow, fs, 0.0, r.crossoverHz);
        out.controlRho = c.rho;
        out.controlFails = !(c.rho >= sft::kSubBandRhoBar);
    }

    // Plan 5.8 / 5.12: the descriptor window, [1.0 s, 3.0 s) if its mid total is >= -60 dBFS, else
    // [t90, t90 + 2^16).
    auto start = static_cast<std::size_t>(std::llround(1.0 * fs));
    std::size_t length = r.timeline.noteOff - start;
    double mid = dd::midTotalDb(dd::bandsOver(r.L, r.R, start, length));
    if (mid < dd::kFloorDb) {
        start = t90;
        length = dd::kFallbackLength;
        mid = dd::midTotalDb(dd::bandsOver(r.L, r.R, start, length));
    }
    out.windowStart = start;
    out.windowLength = length;
    out.midTotalDb = mid;
    out.windowValid = mid >= dd::kFloorDb;
    return out;
}

/// The Comp of library definition @p def (C-4); REQUIREs the build.
[[nodiscard]] inline std::vector<std::uint8_t> presetCompOf(const ::Profundum::PresetDefs::ProfundumPresetDef& def) {
    std::vector<std::uint8_t> comp;
    std::string why;
    const bool built = buildPresetComponentState(def, comp, why);
    INFO("preset " << def.name << ": " << why);
    REQUIRE(built);
    return comp;
}

/// Plan 4.7 / R-4: the render cache. One render per (preset index, note, fs, variant); the readings are kept and
/// the audio is dropped. A function-local static map; test thread only. @p variant: kPresetVariantStored,
/// kPresetVariantMaxMass, or kPresetVariantChord (E1, B1, E2; @p note must be E1).
[[nodiscard]] inline const PresetRenderReadings& presetReadings(std::size_t presetIndex, std::uint8_t note, double fs,
                                                                int variant) {
    using Key = std::tuple<std::size_t, int, long long, int>;
    static std::map<Key, PresetRenderReadings> cache;
    const Key key{presetIndex, static_cast<int>(note), std::llround(fs), variant};
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    REQUIRE((variant == kPresetVariantStored || variant == kPresetVariantMaxMass || variant == kPresetVariantChord));
    const std::vector<::Profundum::PresetDefs::ProfundumPresetDef>& defs = ::Profundum::PresetDefs::allPresets();
    REQUIRE(presetIndex < defs.size());
    const std::vector<std::uint8_t> comp = presetCompOf(defs[presetIndex]);
    const DecodedPresetState d = decodePresetState(comp);
    REQUIRE(d.ok);
    std::vector<std::uint8_t> notes{note};
    if (variant == kPresetVariantChord) {
        REQUIRE(note == auRd::kAuE1);
        notes.assign(kPresetChordNotes.begin(), kPresetChordNotes.end());
    }
    const PresetSweepRender render = renderPresetSweep(comp, notes, fs, variant == kPresetVariantMaxMass);
    return cache.emplace(key, presetSweepReadings(render, d)).first->second;
}

}  // namespace ProfundumPluginTest
