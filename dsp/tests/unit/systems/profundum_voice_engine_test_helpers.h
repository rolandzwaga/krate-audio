// ==============================================================================
// Test helpers - the Profundum voice / engine render fixture and readers
//                (Profundum Phase 9, specs/profundum-phase9-voice-engine)
// ==============================================================================
// tasks.md T009 (plan S10.2, S9.9, S9.11; C-8, C-9; deviation 11). Test-side only: allocation is fine here
// (never inside the driver's optional AllocationScope, which covers the render loop only). Read-only after T009
// (tasks.md file map). Namespace Krate::DSP::ProfundumTest; every name carries the VoiceEngine / voiceEngine
// prefix (the namespace also holds the Phase 1-5 helpers, profundum_core_test_helpers.h:47).
//
// Contents:
//   - VoiceEngineEvent / VoiceEngineScript: a sample-stamped event list plus the engine settings.
//   - VoiceEngineTaps : BassMorphTest::MorphChainTaps, so the Phase 8 readers apply unchanged. Inherited fields:
//       outL/R        the OUTPUT tap (the engine's processBlock output, written directly);
//       sumL/R, dynL/R, stageOutL/R (the field tap), dcL/R   the engine chunk taps (ProfundumEngineTaps);
//       coreL/R, characterL/R, anchorL/R   the per-voice core / Character / Anchor taps summed over voices
//                     (pre g_amp / g_d; initialised to -0.0f, the IEEE additive identity, so with one voice the
//                     sum is that voice's tap bit for bit);
//       linearCoreL/R filled by voiceEngineRenderWithTwin (the forceLinearCore twin's core tap, FR-086);
//       resolvedPerInterval, anchorLevelHanded, advanceSamples, massPerInterval, targetsPerInterval   from the
//                     grid records.
//     Added: per-voice per-sample tap sets and per-chunk records, the copied grid records and the call log.
//   - VoiceEngineObserver: appends into vectors reserved before the render (voiceEngineAllocateTaps).
//   - voiceEngineRender: the driver (C-9: splits each host block at every event's sample and issues the event
//     before rendering that sample). voiceEngineRenderWithTwin: the D_nl twin (FR-086).
//   - Click machinery (S9.9), ported from Phase 4 (profundum_note_behaviour_spectral_test.cpp:1163, :1190, :1293).
//   - Drop descriptors, ported from t22CoreDescriptors (profundum_note_behaviour_spectral_test.cpp:2006-2032).
//   - Frame-wise readers (S9.11).
//
// Recorded decisions (evidence/run-log/T009.txt):
//   - VoiceEngineScript carries two fields beyond the task's list: tappedVoices (per-sample voice taps are stored
//     for voices [0, tappedVoices) only; memory is 13 floats per sample per tapped voice) and countAllocations
//     (an AllocationScope around the render loop, prepare outside it; the count lands in taps.allocations).
//   - The per-voice tap set is named VoiceEngineVoiceTapSet (the task's "VoiceTapSet"), for the prefix rule.
//   - Endpoint events: index 0 / 1 selects endpoint A / B and the state endpointStates[index].
//   - Frame readers: a frame is round(8 fs / f0) samples, which is not a whole number of periods for a non-integer
//     period; the harmonic powers P_DC, P_1 ... P_8 are therefore the least-squares projection of the frame onto
//     {1, cos(n w k), sin(n w k)}_{n=1..8} (the DFT bins exactly, when the frame is whole periods), and P_{>=9} is
//     the residual's mean square, i.e. P_total - P_DC - sum P_n by the projection theorem. A plain DFT on the
//     rounded frame leaks ~1e-4 of h1 into the residual, 60 dB above a -60 dB harmonic.
//   - L_K per frame is TestUtils::kWeightedLevelDb's measurand (k_weighting.h:111-131: one fresh filter from sample
//     0, mean square over the frame), computed with one filter pass per channel instead of one per frame.
//   - Frame D_nl is floored like BassMorphTest::dNlDb (bass_morph_test_helpers.h:771-778): each P_{>=9} at
//     BassMorphTest::floorPower(the twin frame's P_1).
// ==============================================================================

#pragma once

#include "anchor_oscillator_test_helpers.h"
#include "bass_morph_state_helpers.h"
#include "bass_morph_test_helpers.h"
#include "profundum_core_test_helpers.h"

#include "artifact_detection.h"

#include <allocation_detector.h>
#include <enable_ftz_daz.h>
#include <k_weighting.h>

#include <krate/dsp/primitives/retrigger_declick.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/processors/note_memory.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/mass_law.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace Krate::DSP::ProfundumTest {

// -----------------------------------------------------------------------------
// Constants (plan S10.2 "Notes")
// -----------------------------------------------------------------------------

inline constexpr std::uint8_t kVoiceEngineNoteE0 = 16;
inline constexpr std::uint8_t kVoiceEngineNoteE1 = 28;
inline constexpr std::uint8_t kVoiceEngineNoteE2 = 40;
inline constexpr std::uint8_t kVoiceEngineNoteC3 = 48;
inline constexpr std::uint8_t kVoiceEngineNoteA1 = 33;
inline constexpr std::uint8_t kVoiceEngineNoteG1 = 31;
inline constexpr std::uint8_t kVoiceEngineNoteB1 = 35;
inline constexpr std::uint8_t kVoiceEngineNoteE3 = 52;
inline constexpr std::uint8_t kVoiceEngineNoteA2 = 45;

inline constexpr std::size_t kVoiceEngineGrid = ProfundumEngine::kControlInterval;
inline constexpr double kVoiceEngineBoundFactor = 1.1;    // Phase 4 kT22BoundFactor (spectral_test.cpp:1110)
inline constexpr std::size_t kVoiceEngineFramePeriods = 8;   // S9.11
inline constexpr std::size_t kVoiceEngineFrameHop = 960;     // S9.11
inline constexpr std::size_t kVoiceEngineFrameHarmonics = 8; // S9.11: P_1 ... P_8 removed, P_{>=9} kept

/// C-8: base "A" with ampEnv.sustain = 1 (A_s).
[[nodiscard]] inline MorphState voiceEngineAs(const MorphState& a) {
    MorphState s = a;
    s.ampEnv.sustain = 1.0f;
    return s;
}

/// Ruling B10-1 (2026-10-20): base "B" with ampEnv.sustain = 1 (B_s), the per-row base of Dark <-> Bright
/// (harvest, SC-007 sweep, AT-48). The sine-recipe A has no spectral distribution for that macro to move.
[[nodiscard]] inline MorphState voiceEngineBs(const MorphState& b) {
    MorphState s = b;
    s.ampEnv.sustain = 1.0f;
    return s;
}

// -----------------------------------------------------------------------------
// Script (plan S10.2 "Driver")
// -----------------------------------------------------------------------------

enum class VoiceEngineEventKind : std::uint8_t {
    NoteOn, NoteOff, PitchBend, Tempo, MorphPosition, MorphRampMs, AnchorMorphMode, VoiceMode, Polyphony, Macro,
    NoteMemoryMode, NoteRetrigger, PortamentoTimeMs, PortamentoMode, GlideMode, MonoPriority, Legato, Endpoint
};

/// One event, issued to the engine immediately before sample `sample` is rendered (C-9). Float payloads in
/// `value`; enums, counts, bools and the endpoint index in `index`.
struct VoiceEngineEvent {
    std::uint64_t sample = 0;
    VoiceEngineEventKind kind = VoiceEngineEventKind::NoteOn;
    std::uint8_t note = 0;
    std::uint8_t velocity = 100;
    float value = 0.0f;
    int index = 0;
};

struct VoiceEngineScript {
    double fs = 48000.0;
    std::size_t hostBlock = 64;
    std::uint64_t totalSamples = 0;
    std::uint32_t seed = ProfundumEngine::kDefaultSeed;
    ProfundumEngineConfig config{};
    MorphState a{}, b{};
    std::vector<MorphState> endpointStates;   ///< Endpoint events: index 0 / 1 -> endpoint A / B, this state
    float position = 0.0f;
    ProfundumMacroValues macros{};
    ProfundumEngine::VoiceMode mode = ProfundumEngine::VoiceMode::Mono;
    std::size_t polyphony = ProfundumEngine::kDefaultPolyphony;
    std::vector<VoiceEngineEvent> events;     ///< any order; stable-sorted by sample
    bool attachObserver = true;
    std::size_t tappedVoices = ProfundumEngine::kMaxVoices;   ///< per-sample voice taps for voices [0, n)
    bool countAllocations = false;            ///< AllocationScope around the render loop -> taps.allocations
};

// -----------------------------------------------------------------------------
// Taps
// -----------------------------------------------------------------------------

/// Per voice, per rendered chunk (ProfundumVoiceTaps' scalars).
struct VoiceEngineChunkRecord {
    std::uint64_t sample = 0;
    std::size_t n = 0;
    float followRatio = 0.0f, a1 = 0.0f, anchorLevel = 0.0f, feedFrom = 0.0f, feedTo = 0.0f, meterTheta = 0.0f,
          meterFreqHz = 0.0f;
    bool halfWave = false;
};

/// Per voice, per sample (indexed by the absolute sample); 0 where the voice did not render.
struct VoiceEngineVoiceTapSet {
    std::vector<float> coreL, coreR, charL, charR, anchorL, anchorR, gAmp, gD, fRes, fCore, fAnchor, anchorPhase, feed;
    std::vector<VoiceEngineChunkRecord> chunks;
};

/// A copied ProfundumGridRecord (the pointers dereferenced at the callback).
struct VoiceEngineGridRecord {
    std::uint64_t sample = 0;
    BassMorphEngine::Resolved resolved{};
    MorphState afterMacros{};
    MorphState afterMotion{};
    float motionSource = 0.0f, motionOutput = 0.0f, massIn = 0.0f, smoothedMass = 0.0f, anchorMassUsed = 0.0f,
          anchorLevel = 0.0f, compressionHanded = 0.0f, compressorGrDb = 0.0f;
    MassLaw::Targets targets{};
};

struct VoiceEngineTaps : BassMorphTest::MorphChainTaps {
    double fs = 48000.0;
    std::size_t total = 0;
    std::size_t tappedVoices = 0;
    std::array<VoiceEngineVoiceTapSet, ProfundumEngine::kMaxVoices> voices{};
    std::vector<VoiceEngineGridRecord> grid;
    std::vector<ProfundumCallRecord> calls;
    std::uint64_t droppedEvents = 0;
    std::size_t allocations = 0;
};

enum class VoiceEngineTap : std::uint8_t { Sum, Dynamics, Field, Output };

/// Sizes every per-sample vector to `total` and reserves every record vector from the script's bounds, so the
/// observer never grows a vector during the render. Engine chunks <= intervals + host blocks + events + 1; voice
/// chunks <= 2 x that (a dip's switch splits a chunk); calls <= 32 engine + 32 per voice per interval.
inline void voiceEngineAllocateTaps(VoiceEngineTaps& t, const VoiceEngineScript& s) {
    const auto total = static_cast<std::size_t>(s.totalSamples);
    const std::size_t block = std::max<std::size_t>(s.hostBlock, 1);
    const std::size_t intervals = total / kVoiceEngineGrid + 1;
    const std::size_t engineChunks = intervals + total / block + 1 + s.events.size() + 1;
    t.fs = s.fs;
    t.total = total;
    t.tappedVoices = std::min(s.tappedVoices, ProfundumEngine::kMaxVoices);
    t.outL.assign(total, 0.0f);
    t.outR.assign(total, 0.0f);
    if (!s.attachObserver)
        return;
    for (std::vector<float>* v : {&t.sumL, &t.sumR, &t.dynL, &t.dynR, &t.stageOutL, &t.stageOutR, &t.dcL, &t.dcR})
        v->assign(total, 0.0f);
    for (std::vector<float>* v : {&t.coreL, &t.coreR, &t.characterL, &t.characterR, &t.anchorL, &t.anchorR})
        v->assign(total, -0.0f);
    for (std::size_t v = 0; v < t.tappedVoices; ++v) {
        VoiceEngineVoiceTapSet& p = t.voices[v];
        for (std::vector<float>* x : {&p.coreL, &p.coreR, &p.charL, &p.charR, &p.anchorL, &p.anchorR, &p.gAmp,
                                      &p.gD, &p.fRes, &p.fCore, &p.fAnchor, &p.anchorPhase, &p.feed})
            x->assign(total, 0.0f);
        p.chunks.reserve(2 * engineChunks);
    }
    t.grid.reserve(intervals + 1);
    t.calls.reserve((intervals + 1) * (32 + 32 * ProfundumEngine::kMaxVoices));
    t.resolvedPerInterval.reserve(intervals + 1);
    t.anchorLevelHanded.reserve(intervals + 1);
    t.advanceSamples.reserve(intervals + 1);
    t.massPerInterval.reserve(intervals + 1);
    t.targetsPerInterval.reserve(intervals + 1);
}

// -----------------------------------------------------------------------------
// Observer (plan S8, S10.2 "Taps")
// -----------------------------------------------------------------------------

class VoiceEngineObserver final : public ProfundumEngineObserver {
public:
    explicit VoiceEngineObserver(VoiceEngineTaps& taps) noexcept : t_(taps) {}

    void onGrid(const ProfundumGridRecord& g) noexcept override {
        VoiceEngineGridRecord r;
        r.sample = g.sample;
        if (g.resolved != nullptr)
            r.resolved = *g.resolved;
        if (g.afterMacros != nullptr)
            r.afterMacros = *g.afterMacros;
        if (g.afterMotion != nullptr)
            r.afterMotion = *g.afterMotion;
        r.motionSource = g.motionSource;
        r.motionOutput = g.motionOutput;
        r.massIn = g.massIn;
        r.smoothedMass = g.smoothedMass;
        r.anchorMassUsed = g.anchorMassUsed;
        r.anchorLevel = g.anchorLevel;
        r.compressionHanded = g.compressionHanded;
        r.compressorGrDb = g.compressorGrDb;
        r.targets = g.targets;
        t_.grid.push_back(r);
        t_.resolvedPerInterval.push_back(r.resolved);
        t_.anchorLevelHanded.push_back(g.anchorLevel);
        t_.advanceSamples.push_back(static_cast<std::size_t>(g.sample));
        t_.massPerInterval.push_back(g.smoothedMass);
        t_.targetsPerInterval.push_back(g.targets);
    }

    void onCall(const ProfundumCallRecord& c) noexcept override { t_.calls.push_back(c); }

    void onVoiceChunk(const ProfundumVoiceTaps& v) noexcept override {
        const std::size_t s0 = static_cast<std::size_t>(v.sample);
        if (s0 >= t_.total)
            return;
        const std::size_t m = std::min(v.n, t_.total - s0);
        for (std::size_t i = 0; i < m; ++i) {
            t_.coreL[s0 + i] += v.coreL[i];
            t_.coreR[s0 + i] += v.coreR[i];
            t_.characterL[s0 + i] += v.charL[i];
            t_.characterR[s0 + i] += v.charR[i];
            t_.anchorL[s0 + i] += v.anchorL[i];
            t_.anchorR[s0 + i] += v.anchorR[i];
        }
        if (v.voice >= t_.tappedVoices)
            return;
        VoiceEngineVoiceTapSet& p = t_.voices[v.voice];
        const auto put = [s0, m](std::vector<float>& dst, std::span<const float> src) noexcept {
            std::copy_n(src.begin(), m, dst.begin() + static_cast<std::ptrdiff_t>(s0));
        };
        put(p.coreL, v.coreL);
        put(p.coreR, v.coreR);
        put(p.charL, v.charL);
        put(p.charR, v.charR);
        put(p.anchorL, v.anchorL);
        put(p.anchorR, v.anchorR);
        put(p.gAmp, v.gAmp);
        put(p.gD, v.gD);
        put(p.fRes, v.fRes);
        put(p.fCore, v.fCore);
        put(p.fAnchor, v.fAnchor);
        put(p.anchorPhase, v.anchorPhase);
        put(p.feed, v.feed);
        p.chunks.push_back(VoiceEngineChunkRecord{.sample = v.sample,
                                                  .n = v.n,
                                                  .followRatio = v.followRatio,
                                                  .a1 = v.a1,
                                                  .anchorLevel = v.anchorLevel,
                                                  .feedFrom = v.feedFrom,
                                                  .feedTo = v.feedTo,
                                                  .meterTheta = v.meterTheta,
                                                  .meterFreqHz = v.meterFreqHz,
                                                  .halfWave = v.halfWave});
    }

    void onEngineChunk(const ProfundumEngineTaps& e) noexcept override {
        const std::size_t s0 = static_cast<std::size_t>(e.sample);
        if (s0 >= t_.total)
            return;
        const std::size_t m = std::min(e.n, t_.total - s0);
        const auto put = [s0, m](std::vector<float>& dst, std::span<const float> src) noexcept {
            std::copy_n(src.begin(), m, dst.begin() + static_cast<std::ptrdiff_t>(s0));
        };
        put(t_.sumL, e.sumL);
        put(t_.sumR, e.sumR);
        put(t_.dynL, e.dynL);
        put(t_.dynR, e.dynR);
        put(t_.stageOutL, e.fieldL);
        put(t_.stageOutR, e.fieldR);
        put(t_.dcL, e.dcL);
        put(t_.dcR, e.dcR);
    }

private:
    VoiceEngineTaps& t_;
};

// -----------------------------------------------------------------------------
// Driver (plan S10.2, C-9)
// -----------------------------------------------------------------------------

/// Issues one event (C-9: the engine latches it at its current sample).
inline void voiceEngineIssue(ProfundumEngine& e, const VoiceEngineEvent& ev, const VoiceEngineScript& s) noexcept {
    using K = VoiceEngineEventKind;
    switch (ev.kind) {
        case K::NoteOn: e.noteOn(ev.note, ev.velocity); break;
        case K::NoteOff: e.noteOff(ev.note); break;
        case K::PitchBend: e.setPitchBend(ev.value); break;
        case K::Tempo: e.setTempoBpm(ev.value); break;
        case K::MorphPosition: e.setMorphPosition(ev.value); break;
        case K::MorphRampMs: e.setMorphRampMs(ev.value); break;
        case K::AnchorMorphMode:
            e.setAnchorMorphMode(static_cast<BassMorphEngine::AnchorMorphMode>(ev.index));
            break;
        case K::VoiceMode: e.setVoiceMode(static_cast<ProfundumEngine::VoiceMode>(ev.index)); break;
        case K::Polyphony: e.setPolyphony(static_cast<std::size_t>(std::max(ev.index, 0))); break;
        case K::Macro: e.setMacro(static_cast<ProfundumMacro>(ev.index), ev.value); break;
        case K::NoteMemoryMode: e.setNoteMemoryMode(static_cast<NoteMemory::Mode>(ev.index)); break;
        case K::NoteRetrigger: e.setNoteRetrigger(static_cast<NoteMemory::Retrigger>(ev.index)); break;
        case K::PortamentoTimeMs: e.setPortamentoTimeMs(ev.value); break;
        case K::PortamentoMode: e.setPortamentoMode(static_cast<PortaMode>(ev.index)); break;
        case K::GlideMode: e.setGlideMode(static_cast<GlideMode>(ev.index)); break;
        case K::MonoPriority: e.setMonoPriority(static_cast<MonoMode>(ev.index)); break;
        case K::Legato: e.setLegato(ev.index != 0); break;
        case K::Endpoint: {
            const auto i = static_cast<std::size_t>(ev.index);   // validated before the render
            const BassMorphEngine::Endpoint which =
                i == 0 ? BassMorphEngine::Endpoint::A : BassMorphEngine::Endpoint::B;
            static_cast<void>(e.setEndpoint(which, s.endpointStates[i]));
            break;
        }
    }
}

/// Renders the script: std::make_unique<ProfundumEngine>(); seed, config, endpoints, position, macros, mode and
/// polyphony set; prepare(fs, config); then host blocks of `hostBlock`, each split at every event's sample, the
/// event issued before that sample is rendered (C-9).
[[nodiscard]] inline VoiceEngineTaps voiceEngineRender(const VoiceEngineScript& s) {
    REQUIRE(s.fs > 0.0);
    REQUIRE(s.hostBlock >= 1u);
    REQUIRE(s.totalSamples > 0u);
    std::vector<VoiceEngineEvent> events = s.events;
    std::stable_sort(events.begin(), events.end(),
                     [](const VoiceEngineEvent& x, const VoiceEngineEvent& y) { return x.sample < y.sample; });
    for (const VoiceEngineEvent& ev : events) {
        if (ev.kind == VoiceEngineEventKind::Endpoint) {
            REQUIRE((ev.index == 0 || ev.index == 1));
            REQUIRE(static_cast<std::size_t>(ev.index) < s.endpointStates.size());
            REQUIRE(isValidMorphState(s.endpointStates[static_cast<std::size_t>(ev.index)]));
        }
    }

    VoiceEngineTaps taps;
    voiceEngineAllocateTaps(taps, s);
    VoiceEngineObserver observer(taps);

    auto e = std::make_unique<ProfundumEngine>();
    e->setSeed(s.seed);
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, s.a));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, s.b));
    e->setMorphPosition(s.position);
    e->setMacros(s.macros);
    e->setVoiceMode(s.mode);
    e->setPolyphony(s.polyphony);
    if (s.attachObserver)
        e->setObserver(&observer);
    e->prepare(s.fs, s.config);

    const auto total = static_cast<std::size_t>(s.totalSamples);
    const auto render = [&]() noexcept {
        std::size_t pos = 0;
        std::size_t ei = 0;
        while (pos < total) {
            const std::size_t blockEnd = std::min(pos + s.hostBlock, total);
            while (pos < blockEnd) {
                while (ei < events.size() && events[ei].sample <= pos)
                    voiceEngineIssue(*e, events[ei++], s);
                std::size_t next = blockEnd;
                if (ei < events.size() && events[ei].sample < next)
                    next = static_cast<std::size_t>(events[ei].sample);
                e->processBlock(taps.outL.data() + pos, taps.outR.data() + pos, next - pos);
                pos = next;
            }
        }
    };
    if (s.countAllocations) {
        const ::TestHelpers::AllocationScope scope;
        render();
        taps.allocations = ::TestHelpers::AllocationDetector::instance().getAllocationCount();
    } else {
        render();
    }
    taps.droppedEvents = e->droppedEvents();
    e->setObserver(nullptr);
    return taps;
}

/// FR-086: the twin of a script, identical but for config.seams.forceLinearCore = true.
[[nodiscard]] inline VoiceEngineScript voiceEngineTwinScript(const VoiceEngineScript& s) {
    VoiceEngineScript t = s;
    t.config.seams.forceLinearCore = true;
    t.attachObserver = true;
    t.countAllocations = false;
    return t;
}

/// Renders the script and its forceLinearCore twin (same seed, settings and events) and copies the twin's core
/// tap into linearCoreL/R: summed over voices (voice < 0) or of voice `voice`, so BassMorphTest::dNlDb reads D_nl
/// (Character tap over the linear core) unchanged.
[[nodiscard]] inline VoiceEngineTaps voiceEngineRenderWithTwin(const VoiceEngineScript& s, int voice = -1) {
    REQUIRE(s.attachObserver);
    VoiceEngineTaps taps = voiceEngineRender(s);
    const VoiceEngineTaps twin = voiceEngineRender(voiceEngineTwinScript(s));
    if (voice < 0) {
        taps.linearCoreL = twin.coreL;
        taps.linearCoreR = twin.coreR;
    } else {
        REQUIRE(static_cast<std::size_t>(voice) < twin.tappedVoices);
        taps.linearCoreL = twin.voices[static_cast<std::size_t>(voice)].coreL;
        taps.linearCoreR = twin.voices[static_cast<std::size_t>(voice)].coreR;
    }
    return taps;
}

/// The Phase 8 readers' view of one tap: a copy of the MorphChainTaps part with outL/R replaced by the chosen
/// tap (Sum, Dynamics, Field = the safety stage output, Output), for dStDb / dBrOct / lKDb.
[[nodiscard]] inline BassMorphTest::MorphChainTaps asFieldTaps(const VoiceEngineTaps& taps, VoiceEngineTap t) {
    BassMorphTest::MorphChainTaps m = static_cast<const BassMorphTest::MorphChainTaps&>(taps);
    switch (t) {
        case VoiceEngineTap::Sum:
            m.outL = taps.sumL;
            m.outR = taps.sumR;
            break;
        case VoiceEngineTap::Dynamics:
            m.outL = taps.dynL;
            m.outR = taps.dynR;
            break;
        case VoiceEngineTap::Field:
            m.outL = taps.stageOutL;
            m.outR = taps.stageOutR;
            break;
        case VoiceEngineTap::Output:
            break;
    }
    return m;
}

// -----------------------------------------------------------------------------
// Click machinery (plan S9.9; SC-005(b), SC-006(b))
// -----------------------------------------------------------------------------

/// L_d = ceil(ProfundumVoice::kDeclickMs fs / 1000) (ruling R-B: the voice's 3 ms, not Phase 4's 1.5 ms default):
/// ported from t22DeclickLength (profundum_note_behaviour_spectral_test.cpp:1163, retrigger_declick.h prepare()).
[[nodiscard]] inline std::size_t voiceEngineDeclickLength(double fs) {
    return static_cast<std::size_t>(std::ceil(static_cast<double>(ProfundumVoice::kDeclickMs) * fs / 1000.0));
}

/// pi / (2 L_d): the raised-cosine dip's analytic peak per-sample slope, ported from t22DeclickSlope
/// (profundum_note_behaviour_spectral_test.cpp:1190). 0.02182 at 48 kHz (L_d = 72).
[[nodiscard]] inline double voiceEngineDeclickSlope(double fs) {
    return 3.14159265358979323846 / (2.0 * static_cast<double>(voiceEngineDeclickLength(fs)));
}

/// S9.9 Drop term's slope: |A| max|d'| ln2 / 12, with max|d'| the Drop's analytic peak per-sample slope of its
/// normalised shape (Phase 4 SC-010(b)). The bound adds it times P_max.
[[nodiscard]] inline double voiceEngineDropSlope(double amountSt, double maxShapeSlopePerSample) {
    return std::abs(amountSt) * std::abs(maxShapeSlopePerSample) * std::log(2.0) / 12.0;
}

/// S9.9 glide term (Phase 4 SC-010(c), spectral_test.cpp:1079-1080): the one-semitone frequency step on a
/// phase-continuous oscillator is covered by the steady D at the upper step's frequency; this is that key.
[[nodiscard]] inline double voiceEngineGlideStepKeyHz(double fHz) {
    return fHz * std::exp2(1.0 / 12.0);
}

/// Window W (SC-005): [the grid point the steal / restart is applied at, the fade / dip end + one period of the
/// slower of the old and new f0).
struct VoiceEngineWindow {
    std::size_t begin = 0;
    std::size_t end = 0;
};

[[nodiscard]] inline VoiceEngineWindow voiceEngineWindow(std::size_t stealGrid, std::size_t fadeEnd, double f0Old,
                                                         double f0New, double fs) {
    REQUIRE(f0Old > 0.0);
    REQUIRE(f0New > 0.0);
    REQUIRE(fadeEnd >= stealGrid);
    const double slow = std::min(f0Old, f0New);
    const auto period = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(fs / slow)));
    return {stealGrid, fadeEnd + period};
}

/// max |y[n] - y[n-1]| over n in W (n >= 1, n < y.size()).
[[nodiscard]] inline double voiceEngineMaxDelta(std::span<const float> y, VoiceEngineWindow w) {
    double m = 0.0;
    const std::size_t hi = std::min(w.end, y.size());
    for (std::size_t n = std::max<std::size_t>(w.begin, 1); n < hi; ++n)
        m = std::max(m, std::abs(static_cast<double>(y[n]) - static_cast<double>(y[n - 1])));
    return m;
}

/// SC-005(a): max |dy| over W of y over that of the reference.
[[nodiscard]] inline double voiceEngineDeltaRatio(std::span<const float> y, std::span<const float> ref,
                                                  VoiceEngineWindow w) {
    const double r = voiceEngineMaxDelta(ref, w);
    REQUIRE(r > 0.0);
    return voiceEngineMaxDelta(y, w) / r;
}

/// A steady reference: D_max = max |dw|, P_max = peak |w|.
struct VoiceEngineSteady {
    double dMax = 0.0;
    double pMax = 0.0;
};

[[nodiscard]] inline VoiceEngineSteady voiceEngineSteadyOf(std::span<const float> w) {
    VoiceEngineSteady s;
    for (std::size_t n = 0; n < w.size(); ++n) {
        s.pMax = std::max(s.pMax, std::abs(static_cast<double>(w[n])));
        if (n > 0)
            s.dMax = std::max(s.dMax, std::abs(static_cast<double>(w[n]) - static_cast<double>(w[n - 1])));
    }
    return s;
}

/// S9.9: the steady reference of one voice's unit-gain output (Character + Anchor, g_amp = g_d = 1) over
/// [from, to), both channels, from a steady render's per-voice taps.
[[nodiscard]] inline VoiceEngineSteady voiceEngineUnitGainSteady(const VoiceEngineVoiceTapSet& v, std::size_t from,
                                                                 std::size_t to) {
    REQUIRE(from < to);
    REQUIRE(to <= v.charL.size());
    VoiceEngineSteady s;
    for (std::size_t n = from; n < to; ++n) {
        const double l = static_cast<double>(v.charL[n]) + static_cast<double>(v.anchorL[n]);
        const double r = static_cast<double>(v.charR[n]) + static_cast<double>(v.anchorR[n]);
        s.pMax = std::max({s.pMax, std::abs(l), std::abs(r)});
        if (n > from) {
            const double lp = static_cast<double>(v.charL[n - 1]) + static_cast<double>(v.anchorL[n - 1]);
            const double rp = static_cast<double>(v.charR[n - 1]) + static_cast<double>(v.anchorR[n - 1]);
            s.dMax = std::max({s.dMax, std::abs(l - lp), std::abs(r - rp)});
        }
    }
    return s;
}

/// S9.9 (Phase 3 SC-006(c) form, ported from t22DeltaBound, profundum_note_behaviour_spectral_test.cpp:1293-1347):
/// 1.1 * (G_max D_max + S_max P_max + dropSlope P_max), with D_max and P_max the maxima over the steady references
/// of every note involved (separate steady renders, never the transition trace), G_max <= 1 the amp-envelope peak,
/// S_max the dip slope (voiceEngineDeclickSlope) and dropSlope voiceEngineDropSlope (0 without a Drop).
[[nodiscard]] inline double voiceEngineDeltaBound(std::span<const VoiceEngineSteady> steady, double sMax,
                                                  double dropSlope = 0.0, double gMax = 1.0) {
    REQUIRE(!steady.empty());
    double d = 0.0;
    double p = 0.0;
    for (const VoiceEngineSteady& s : steady) {
        d = std::max(d, s.dMax);
        p = std::max(p, s.pMax);
    }
    return kVoiceEngineBoundFactor * (gMax * d + sMax * p + dropSlope * p);
}

// -----------------------------------------------------------------------------
// Drop descriptors (deviation 11): ported from t22CoreDescriptors (profundum_note_behaviour_spectral_test.cpp:
// 2006-2032), verbatim
// -----------------------------------------------------------------------------

struct VoiceEngineDropDescriptors {
    double start = 0.0;   // st
    double ts = 0.0;      // samples: the last sample with |d| > 1 cent
    double alpha = 0.0;   // the sample mean of d over [0, round(N)] / A
    double eps = 0.0;     // st: the undershoot after landing
    double rho = 0.0;     // st: the deepest undershoot overall
};

/// Descriptors on d_core (st per sample; Phase 4 spec "Definitions"; t_s threshold 1 cent).
[[nodiscard]] inline VoiceEngineDropDescriptors voiceEngineDropDescriptors(const std::vector<double>& d, double A,
                                                                           double N) {
    VoiceEngineDropDescriptors out;
    out.start = d.empty() ? 0.0 : d[0];
    std::size_t landing = d.size();
    double minAll = 0.0;
    for (std::size_t n = 0; n < d.size(); ++n) {
        if (std::abs(d[n]) > 0.01)
            out.ts = static_cast<double>(n);
        if (landing == d.size() && d[n] <= 0.0)
            landing = n;
        minAll = std::min(minAll, d[n]);
    }
    double minAfter = 0.0;
    for (std::size_t n = landing; n < d.size(); ++n)
        minAfter = std::min(minAfter, d[n]);
    out.eps = -minAfter;
    out.rho = -minAll;
    if (A > 0.0 && !d.empty()) {
        const auto last = std::min(static_cast<std::size_t>(std::llround(N)), d.size() - 1);
        double sum = 0.0;
        for (std::size_t n = 0; n <= last; ++n)
            sum += d[n];
        out.alpha = sum / (static_cast<double>(last + 1) * A);
    }
    return out;
}

// -----------------------------------------------------------------------------
// Frame-wise readers (plan S9.11; SC-009, AT-33 ... AT-40)
// -----------------------------------------------------------------------------

/// round(8 fs / f0) samples.
[[nodiscard]] inline std::size_t voiceEngineFrameLength(double f0, double fs) {
    REQUIRE(f0 > 0.0);
    return static_cast<std::size_t>(std::llround(static_cast<double>(kVoiceEngineFramePeriods) * fs / f0));
}

/// Least-squares projection of a frame onto {1, cos(n w k), sin(n w k)}, n = 1 .. min(8, below Nyquist): the
/// basis and the Cholesky factor of its Gram matrix are built once per (length, f0, fs).
class VoiceEngineHarmonicFit {
public:
    struct Powers {
        double pTotal = 0.0;    // mean square of the frame
        double pDc = 0.0;       // c_0^2
        std::array<double, kVoiceEngineFrameHarmonics> pHarm{};   // (a_n^2 + b_n^2) / 2
        double pAbove8 = 0.0;   // the residual's mean square: P_{>=9}
    };

    VoiceEngineHarmonicFit(std::size_t length, double f0, double fs) : len_(length) {
        REQUIRE(length > 2 * kVoiceEngineFrameHarmonics + 1);
        REQUIRE(f0 > 0.0);
        nh_ = 0;
        for (std::size_t n = 1; n <= kVoiceEngineFrameHarmonics; ++n) {
            if (static_cast<double>(n) * f0 < 0.5 * fs)
                nh_ = n;
        }
        nb_ = 1 + 2 * nh_;
        basis_.assign(nb_ * len_, 0.0);
        const double w = kTwoPi * f0 / fs;
        for (std::size_t k = 0; k < len_; ++k) {
            basis_[k] = 1.0;
            for (std::size_t n = 1; n <= nh_; ++n) {
                const double ph = w * static_cast<double>(n) * static_cast<double>(k);
                basis_[(2 * n - 1) * len_ + k] = std::cos(ph);
                basis_[(2 * n) * len_ + k] = std::sin(ph);
            }
        }
        // Gram matrix, then its lower Cholesky factor in place.
        chol_.assign(nb_ * nb_, 0.0);
        for (std::size_t i = 0; i < nb_; ++i) {
            for (std::size_t j = 0; j <= i; ++j) {
                double s = 0.0;
                for (std::size_t k = 0; k < len_; ++k)
                    s += basis_[i * len_ + k] * basis_[j * len_ + k];
                chol_[i * nb_ + j] = s;
            }
        }
        for (std::size_t j = 0; j < nb_; ++j) {
            double d = chol_[j * nb_ + j];
            for (std::size_t p = 0; p < j; ++p)
                d -= chol_[j * nb_ + p] * chol_[j * nb_ + p];
            REQUIRE(d > 0.0);
            const double ljj = std::sqrt(d);
            chol_[j * nb_ + j] = ljj;
            for (std::size_t i = j + 1; i < nb_; ++i) {
                double s = chol_[i * nb_ + j];
                for (std::size_t p = 0; p < j; ++p)
                    s -= chol_[i * nb_ + p] * chol_[j * nb_ + p];
                chol_[i * nb_ + j] = s / ljj;
            }
        }
    }

    [[nodiscard]] std::size_t length() const noexcept { return len_; }

    [[nodiscard]] Powers fit(const float* x) const {
        REQUIRE(x != nullptr);
        std::vector<double> b(nb_, 0.0);
        double sq = 0.0;
        for (std::size_t k = 0; k < len_; ++k) {
            const double v = static_cast<double>(x[k]);
            sq += v * v;
            for (std::size_t i = 0; i < nb_; ++i)
                b[i] += v * basis_[i * len_ + k];
        }
        // L y = b, then L^T c = y.
        std::vector<double> c(nb_, 0.0);
        for (std::size_t i = 0; i < nb_; ++i) {
            double s = b[i];
            for (std::size_t p = 0; p < i; ++p)
                s -= chol_[i * nb_ + p] * c[p];
            c[i] = s / chol_[i * nb_ + i];
        }
        for (std::size_t ii = nb_; ii-- > 0;) {
            double s = c[ii];
            for (std::size_t p = ii + 1; p < nb_; ++p)
                s -= chol_[p * nb_ + ii] * c[p];
            c[ii] = s / chol_[ii * nb_ + ii];
        }
        Powers out;
        const auto L = static_cast<double>(len_);
        out.pTotal = sq / L;
        out.pDc = c[0] * c[0];
        for (std::size_t n = 1; n <= nh_; ++n)
            out.pHarm[n - 1] = 0.5 * (c[2 * n - 1] * c[2 * n - 1] + c[2 * n] * c[2 * n]);
        double r2 = 0.0;
        for (std::size_t k = 0; k < len_; ++k) {
            double r = static_cast<double>(x[k]);
            for (std::size_t i = 0; i < nb_; ++i)
                r -= c[i] * basis_[i * len_ + k];
            r2 += r * r;
        }
        out.pAbove8 = r2 / L;
        return out;
    }

private:
    std::size_t len_ = 0;
    std::size_t nh_ = 0;
    std::size_t nb_ = 1;
    std::vector<double> basis_;
    std::vector<double> chol_;
};

/// One frame (S9.11): E_sub = 10 log10(P_1) (RMS dB: a sine of amplitude a reads 20 log10(a / sqrt 2), the
/// BassMorphTest::eSubDb convention), P_1 and P_{>=9} of the mid, L_K = the mean of the K-weighted levels of L and R.
struct VoiceEngineFrame {
    std::size_t start = 0;
    std::size_t length = 0;
    double eSubDb = 0.0;
    double pH1 = 0.0;
    double pAbove8 = 0.0;
    double lKDb = 0.0;
};

/// K-weighted signal of x (one fresh TestUtils::KWeightingFilter from sample 0; k_weighting.h:111-131).
[[nodiscard]] inline std::vector<double> voiceEngineKWeighted(const std::vector<float>& x, double fs) {
    TestUtils::KWeightingFilter f;
    f.prepare(fs);
    std::vector<double> y(x.size(), 0.0);
    for (std::size_t i = 0; i < x.size(); ++i)
        y[i] = f.process(static_cast<double>(x[i]));
    return y;
}

/// TestUtils::kWeightedLevelDb's value on y[start, start + n) of a K-weighted signal.
[[nodiscard]] inline double voiceEngineKLevelDb(const std::vector<double>& y, std::size_t start, std::size_t n) {
    REQUIRE(n > 0u);
    REQUIRE(start + n <= y.size());
    double sumSq = 0.0;
    for (std::size_t i = start; i < start + n; ++i)
        sumSq += y[i] * y[i];
    const double meanSq = sumSq / static_cast<double>(n);
    return meanSq <= 0.0 ? -144.0 : -0.691 + 10.0 * std::log10(meanSq);
}

/// Frames of 8 periods of f0, hop 960, starting at `from`, while the frame ends at or before `to`.
[[nodiscard]] inline std::vector<VoiceEngineFrame> voiceEngineFrames(
    const std::vector<float>& L, const std::vector<float>& R, double f0, double fs, std::size_t from = 0,
    std::size_t to = std::numeric_limits<std::size_t>::max(), bool withLK = true) {
    REQUIRE(L.size() == R.size());
    to = std::min(to, L.size());
    const std::size_t len = voiceEngineFrameLength(f0, fs);
    std::vector<VoiceEngineFrame> frames;
    if (from + len > to)
        return frames;
    const std::vector<float> mid = BassMorphTest::midOf(L, R);
    const VoiceEngineHarmonicFit fit(len, f0, fs);
    std::vector<double> kL;
    std::vector<double> kR;
    if (withLK) {
        kL = voiceEngineKWeighted(L, fs);
        kR = voiceEngineKWeighted(R, fs);
    }
    for (std::size_t s = from; s + len <= to; s += kVoiceEngineFrameHop) {
        const VoiceEngineHarmonicFit::Powers p = fit.fit(mid.data() + s);
        VoiceEngineFrame f;
        f.start = s;
        f.length = len;
        f.pH1 = p.pHarm[0];
        f.pAbove8 = p.pAbove8;
        f.eSubDb = 10.0 * std::log10(std::max(p.pHarm[0], 1e-300));
        if (withLK)
            f.lKDb = 0.5 * (voiceEngineKLevelDb(kL, s, len) + voiceEngineKLevelDb(kR, s, len));
        frames.push_back(f);
    }
    return frames;
}

/// Frame D_nl (S9.11): 10 log10(P_{>=9}(Character) / P_{>=9}(twin core)) per frame pair, each power floored at
/// BassMorphTest::floorPower(the twin frame's P_1) (the dNlDb floor, bass_morph_test_helpers.h:771-778).
[[nodiscard]] inline std::vector<double> voiceEngineFrameDnlDb(const std::vector<VoiceEngineFrame>& character,
                                                               const std::vector<VoiceEngineFrame>& twinCore) {
    REQUIRE(character.size() == twinCore.size());
    std::vector<double> out(character.size(), 0.0);
    for (std::size_t i = 0; i < character.size(); ++i) {
        REQUIRE(character[i].start == twinCore[i].start);
        const double fl = BassMorphTest::floorPower(twinCore[i].pH1);
        out[i] = 10.0 * std::log10(std::max(character[i].pAbove8, fl) / std::max(twinCore[i].pAbove8, fl));
    }
    return out;
}

/// Frame D_nl of a render made by voiceEngineRenderWithTwin: the Character tap over linearCoreL/R.
[[nodiscard]] inline std::vector<double> voiceEngineFrameDnlDb(const VoiceEngineTaps& taps, double f0, double fs,
                                                               std::size_t from = 0) {
    REQUIRE(taps.linearCoreL.size() == taps.characterL.size());
    return voiceEngineFrameDnlDb(voiceEngineFrames(taps.characterL, taps.characterR, f0, fs, from,
                                                   std::numeric_limits<std::size_t>::max(), false),
                                 voiceEngineFrames(taps.linearCoreL, taps.linearCoreR, f0, fs, from,
                                                   std::numeric_limits<std::size_t>::max(), false));
}

/// The boxcar's analytic attenuation at modulation rate fm: |sinc(pi fm T_frame)|, T_frame = 8 / f0.
[[nodiscard]] inline double voiceEngineBoxcarAttenuation(double fmHz, double f0) {
    REQUIRE(f0 > 0.0);
    const double x = 0.5 * kTwoPi * fmHz * static_cast<double>(kVoiceEngineFramePeriods) / f0;
    return x == 0.0 ? 1.0 : std::abs(std::sin(x) / x);
}

}  // namespace Krate::DSP::ProfundumTest
