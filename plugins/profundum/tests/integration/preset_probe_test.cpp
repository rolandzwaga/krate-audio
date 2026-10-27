// Profundum Phase 13 preset audibility probe tests (specs/profundum-phase13-presets-release/tasks.md)
//
// Placeholder created by T007 (Deviation 1). Cases written by T012, T015, T016:
// Profundum_EngineSeamsForTest (P-11), render-support arms (FR-021),
// Profundum_PresetAudibilityProbe_<Group> x11 (FR-020 - FR-022; SC-009).
//
// T012 (plan 2 P-11): Profundum_EngineSeamsForTest proves the processor test seam setEngineSeamsForTest reaches the
// engine at the next setupProcessing. With anchorPhaseTap off the voice fills the Anchor phase tap once per render
// chunk (profundum_voice.h:880-888), so the tap is a staircase; with it on, the tap is written per sample.
//
// T015 (FR-021, C-5 twin rule; plan 5.2, 5.3, P-1, P-6, P-11): the render support of preset_render_support.h:
// Profundum_ScriptRenderShim (the P-1 hook), Profundum_ProcessorScriptRender (processor vs engine-direct),
// Profundum_ScriptRenderRefusals (the FR-023 stops, Deviation 3), Profundum_EffectiveScriptCarriers (plan 5.2).
//
// T016 (FR-020 - FR-022; SC-009; plan 5.7): Profundum_PresetAudibilityProbeKeys (per push: the 102 gated keys of the
// Phase 9 table) and the eleven hidden Profundum_PresetAudibilityProbe_<Group> cases, which run each moved Phase 9
// shard through the processor (probe context) and engine-direct, join on the gated keys and write
// evidence/probe-table/<Group>.csv (merged by evidence/merge-csv.js).
//
// Phase 14 T021 (specs/profundum-phase14-morph-modulation; FR-044; SC-017 (a) input; plan 5.7 steps 1-4), appended:
// the 32 hidden Profundum_MorphModProbe_<k>of32 cases. Shard k takes every factory definition i with i % 32 == k
// (Source Off as stored) and renders each source column's C-15 arms (morphModSourceCell's step-3 arms and step 4-6
// reading) for the plan 5.7 step 1 candidate grid at E1 and C3, in preset context under
// ScriptRenderScope(&processorRenderHook, &ctx). Rows go to
// specs/profundum-phase14-morph-modulation/evidence/probe/shard-<k>.csv; evidence/probe/merge.js joins them,
// evidence/probe/select.js applies the ruled conversion rule, evidence/probe/run-probe.sh runs it all.
// Recorded choices (T021 notes):
//   - The forced-Off lo arm of M-ENV / M-CYCLE is rendered once per (P, note) with P's stored block (Source Off) and
//     reused for all 12 of their candidates; morphModSourceCell renders it per call, so the reading is the same
//     code path called on the cached arm (mmpRead mirrors morphModSourceCell steps 4-6 on its own helpers). The
//     M-VEL / M-WHEEL lo arms depend on the candidate (C-15: "as stored, velocity 1" / "wheel 0") and are rendered
//     per candidate. Guard: on each shard's first preset at E1, the first candidate of every column is also read
//     through morphModSourceCell itself and must agree (lo, hi within 1e-6 dB, same pass), which also proves the
//     Off arm does not depend on the candidate's other fields.
//   - t0 is written in seconds (%.6f; the 32-sample grid at 48 kHz is 0.000667 s); t0 / p_lo / p_hi are empty
//     when no window was found (the cell then fails, plan 5.5).

#include <catch2/catch_test_macros.hpp>

#include "preset_render_support.h"
#include "profundum_test_fixture.h"

#include "profundum_voice_engine_test_helpers.h"  // read-only Phase 9 helper (tests/CMakeLists.txt include dir)

#include <krate/dsp/systems/profundum_engine_observer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

namespace esPt = Krate::DSP::ProfundumTest;

constexpr double kEsFs = 48000.0;
constexpr std::size_t kEsBlock = 512;
constexpr std::size_t kEsTotal = 48000;        // 1 s
constexpr std::size_t kEsWindowFrom = 24000;   // [24000, 25024)
constexpr std::size_t kEsWindowLength = 1024;

/// (Re-)prepares @p fx with a fresh observer on 1 tapped voice, renders E1 from sample 0 for 1 s at block 512 and
/// returns the number of n in the window with anchorPhase[n] != anchorPhase[n - 1] on voice 0.
[[nodiscard]] std::size_t esAnchorPhaseChanges(ProcessorFixture& fx) {
    esPt::VoiceEngineScript s;
    s.fs = kEsFs;
    s.hostBlock = kEsBlock;
    s.totalSamples = kEsTotal;
    s.tappedVoices = 1;
    esPt::VoiceEngineEvent on{};
    on.sample = 0;
    on.kind = esPt::VoiceEngineEventKind::NoteOn;
    on.note = esPt::kVoiceEngineNoteE1;
    on.velocity = 100;
    s.events.push_back(on);

    esPt::VoiceEngineTaps taps;
    esPt::voiceEngineAllocateTaps(taps, s);
    esPt::VoiceEngineObserver observer(taps);

    // FR-067: attach while inactive, before setupProcessing; the seam is copied into the engine config there.
    fx.proc->setActive(0u);
    fx.proc->setEngineObserverForTest(&observer);
    REQUIRE(fx.prepare(kEsFs, static_cast<Steinberg::int32>(kEsBlock)) == Steinberg::kResultOk);

    fx.capturedL.clear();
    fx.capturedR.clear();
    ProcessorFixture::ScriptedEvent note{};
    note.sample = 0;
    note.kind = ProcessorFixture::ScriptedEvent::Kind::NoteOn;
    note.pitch = kE1;
    note.velocity = 100.0f / 127.0f;
    const std::array<ProcessorFixture::ScriptedEvent, 1> script{note};
    fx.renderScript(script, kEsTotal, kEsBlock);

    // Detach while inactive (FR-067), before the observer goes out of scope.
    fx.proc->setActive(0u);
    fx.proc->setEngineObserverForTest(nullptr);

    const std::vector<float>& ph = taps.voices[0].anchorPhase;
    REQUIRE(ph.size() >= kEsWindowFrom + kEsWindowLength);
    std::size_t changes = 0;
    for (std::size_t n = kEsWindowFrom; n < kEsWindowFrom + kEsWindowLength; ++n) {
        if (ph[n] != ph[n - 1u]) {
            ++changes;
        }
    }
    return changes;
}

// ---- T015 helpers ----

/// One held E1 from sample 0 (velocity 100), MONO, position 0, neutral macros, the observer attached, no per-voice
/// taps unless asked.
[[nodiscard]] esPt::VoiceEngineScript rsScript(const Krate::DSP::MorphState& a, const Krate::DSP::MorphState& b,
                                               std::uint64_t total, std::size_t block, std::size_t tapped = 0) {
    esPt::VoiceEngineScript s;
    s.fs = kEsFs;
    s.hostBlock = block;
    s.totalSamples = total;
    s.a = a;
    s.b = b;
    s.position = 0.0f;
    s.macros = Krate::DSP::ProfundumMacroValues{};
    s.mode = Krate::DSP::ProfundumEngine::VoiceMode::Mono;
    s.attachObserver = true;
    s.tappedVoices = tapped;
    esPt::VoiceEngineEvent on{};
    on.sample = 0;
    on.kind = esPt::VoiceEngineEventKind::NoteOn;
    on.note = esPt::kVoiceEngineNoteE1;
    on.velocity = 100;
    s.events.push_back(on);
    return s;
}

[[nodiscard]] std::vector<std::uint8_t> rsBytesOf(const StateBytes& b) {
    std::vector<std::uint8_t> out(b.size());
    if (!b.empty()) {
        std::memcpy(out.data(), b.data(), b.size());
    }
    return out;
}

/// The number of n in the T012 window with anchorPhase[n] != anchorPhase[n - 1] on voice 0 of @p t.
[[nodiscard]] std::size_t rsCountChanges(const esPt::VoiceEngineTaps& t) {
    const std::vector<float>& ph = t.voices[0].anchorPhase;
    REQUIRE(ph.size() >= kEsWindowFrom + kEsWindowLength);
    std::size_t changes = 0;
    for (std::size_t n = kEsWindowFrom; n < kEsWindowFrom + kEsWindowLength; ++n) {
        if (ph[n] != ph[n - 1u]) {
            ++changes;
        }
    }
    return changes;
}

/// Records what the P-1 shim handed a hook.
struct RsHookLog {
    int calls = 0;
    bool twin = false;
    int voice = -2;
};

[[nodiscard]] esPt::VoiceEngineTaps rsCounterHook(const esPt::VoiceEngineScript& /*s*/, bool twin, int voice,
                                                  void* ctx) {
    auto& log = *static_cast<RsHookLog*>(ctx);
    ++log.calls;
    log.twin = twin;
    log.voice = voice;
    esPt::VoiceEngineTaps t;
    t.total = 7;
    t.outL.assign(3, 0.25f);
    t.outR.assign(3, -0.25f);
    return t;
}

[[nodiscard]] Krate::DSP::TestUtils::FingerprintComparison rsCompare(const std::vector<float>& proc,
                                                                    const std::vector<float>& direct) {
    namespace Tu = Krate::DSP::TestUtils;
    return Tu::compareFingerprints(Tu::fingerprintRender(proc), Tu::fingerprintRender(direct), Tu::kMetricTolerance,
                                   Tu::kSampleTolerance);
}

}  // namespace

TEST_CASE("Profundum_EngineSeamsForTest", "[profundum][probe]") {
    auto fx = std::make_unique<ProcessorFixture>();

    // (a) Default: no seam call. One phase value per render chunk -> a staircase.
    const std::size_t defaultChanges = esAnchorPhaseChanges(*fx);

    // (b) The seam on, applied at the re-setupProcessing / setActive.
    fx->proc->setEngineSeamsForTest({.anchorPhaseTap = true});
    const std::size_t seamChanges = esAnchorPhaseChanges(*fx);

    // (c) The seam off again and re-prepared: back to the staircase.
    fx->proc->setEngineSeamsForTest({});
    const std::size_t clearedChanges = esAnchorPhaseChanges(*fx);

    std::cout << "Profundum_EngineSeamsForTest: anchorPhase changes over [" << kEsWindowFrom << ", "
              << (kEsWindowFrom + kEsWindowLength) << "): default " << defaultChanges << ", anchorPhaseTap "
              << seamChanges << ", cleared " << clearedChanges << "\n";

    CHECK(defaultChanges <= kEsWindowLength / 16u);
    CHECK(seamChanges >= 1000u);
    CHECK(clearedChanges <= kEsWindowLength / 16u);
}

// -----------------------------------------------------------------------------
// T015: render support (plan 5.2, 5.3; P-1, P-6, P-11)
// -----------------------------------------------------------------------------

TEST_CASE("Profundum_ScriptRenderShim", "[profundum][probe]") {
    namespace rr = auRd::render;
    const Krate::DSP::MorphState as = auRd::auStateAs();

    // No hook: the shim is the original driver, bit for bit (same binary).
    REQUIRE((rr::scriptRenderHook().fn == nullptr));
    REQUIRE(rr::scriptRenderHook().ctx == nullptr);
    const esPt::VoiceEngineScript s = rsScript(as, as, 12000u, 64u);   // 0.25 s
    const esPt::VoiceEngineTaps viaShim = rr::voiceEngineRender(s);
    const esPt::VoiceEngineTaps direct = esPt::voiceEngineRender(s);
    REQUIRE(viaShim.outL.size() == 12000u);
    REQUIRE(direct.outL.size() == 12000u);
    REQUIRE(viaShim.outR.size() == direct.outR.size());
    CHECK(std::memcmp(viaShim.outL.data(), direct.outL.data(), direct.outL.size() * sizeof(float)) == 0);
    CHECK(std::memcmp(viaShim.outR.data(), direct.outR.data(), direct.outR.size() * sizeof(float)) == 0);
    CHECK(peakOf(direct.outL) > 0.0f);   // the compared render is not silence

    // A scoped hook receives every render; nested scopes restore in LIFO order.
    RsHookLog outer;
    RsHookLog inner;
    {
        const rr::ScriptRenderScope scope(&rsCounterHook, &outer);
        CHECK((rr::scriptRenderHook().fn == &rsCounterHook));
        CHECK(rr::scriptRenderHook().ctx == &outer);
        const esPt::VoiceEngineTaps t = rr::voiceEngineRender(s);
        CHECK(outer.calls == 1);
        CHECK_FALSE(outer.twin);
        CHECK(outer.voice == -1);
        CHECK(t.total == 7u);
        REQUIRE(t.outL.size() == 3u);
        CHECK(t.outL[0] == 0.25f);

        const esPt::VoiceEngineTaps tw = rr::voiceEngineRenderWithTwin(s, 0);
        CHECK(outer.calls == 2);
        CHECK(outer.twin);
        CHECK(outer.voice == 0);
        CHECK(tw.total == 7u);
        {
            const rr::ScriptRenderScope nested(&rsCounterHook, &inner);
            CHECK(rr::scriptRenderHook().ctx == &inner);
            static_cast<void>(rr::voiceEngineRender(s));
            CHECK(inner.calls == 1);
            CHECK(outer.calls == 2);
        }
        CHECK((rr::scriptRenderHook().fn == &rsCounterHook));
        CHECK(rr::scriptRenderHook().ctx == &outer);
    }
    CHECK((rr::scriptRenderHook().fn == nullptr));
    CHECK(rr::scriptRenderHook().ctx == nullptr);
    CHECK(outer.calls == 2);
    CHECK(inner.calls == 1);
}

TEST_CASE("Profundum_ProcessorScriptRender", "[profundum][probe]") {
    const Krate::DSP::MorphState as = auRd::auStateAs();
    constexpr std::uint64_t kTotal = 72000;   // 1.5 s at 48 kHz
    constexpr std::size_t kBlock = 512;

    SECTION("held E1 on A_s: the processor render through the hook matches engine-direct") {
        auto fx = std::make_unique<ProcessorFixture>();
        PresetArmContext ctx;   // probe context
        ctx.processor = fx.get();
        const esPt::VoiceEngineScript s = rsScript(as, as, kTotal, kBlock);

        esPt::VoiceEngineTaps proc;
        {
            const auRd::render::ScriptRenderScope scope(&processorRenderHook, &ctx);
            proc = auRd::render::voiceEngineRender(s);
        }
        const esPt::VoiceEngineTaps direct = esPt::voiceEngineRender(s);

        REQUIRE(ctx.stop.empty());
        CHECK(fx->proc->endpointRejectionsForTest() == 0u);
        REQUIRE(proc.sumL.size() == kTotal);
        REQUIRE(proc.sumR.size() == kTotal);
        REQUIRE(direct.sumL.size() == kTotal);
        REQUIRE(direct.sumR.size() == kTotal);
        REQUIRE(proc.outL.size() == kTotal);
        CHECK(rmsDb(direct.sumL) > -60.0);   // non-vacuity: the compared tap carries the note
        CHECK(peakOf(proc.outL) > 0.0f);
        const auto cl = rsCompare(proc.sumL, direct.sumL);
        const auto cr = rsCompare(proc.sumR, direct.sumR);
        INFO("sumL: " << cl.detail << " (metric " << cl.worstMetricRelativeError << ", sample " << cl.worstSampleError
                      << ")");
        INFO("sumR: " << cr.detail << " (metric " << cr.worstMetricRelativeError << ", sample " << cr.worstSampleError
                      << ")");
        CHECK(cl.withinTolerance());
        CHECK(cr.withinTolerance());
        CHECK(ctx.lastSumL.size() == kTotal);   // P-5: the last render's Sum tap is kept
        CHECK(ctx.lastSumR.size() == kTotal);
    }

    SECTION("a MorphPosition point at sample 24000 (not a multiple of 512) lands on its sample") {
        auto fx = std::make_unique<ProcessorFixture>();
        PresetArmContext ctx;
        ctx.processor = fx.get();
        esPt::VoiceEngineScript s = rsScript(as, auRd::auStateB(), kTotal, kBlock);
        const esPt::VoiceEngineScript still = s;
        esPt::VoiceEngineEvent move{};
        move.sample = 24000;
        move.kind = esPt::VoiceEngineEventKind::MorphPosition;
        move.value = 1.0f;
        s.events.push_back(move);
        REQUIRE(move.sample % kBlock != 0u);

        const esPt::VoiceEngineTaps proc = renderScriptThroughProcessor(s, false, -1, ctx);
        const esPt::VoiceEngineTaps direct = esPt::voiceEngineRender(s);
        REQUIRE(ctx.stop.empty());
        CHECK(fx->proc->endpointRejectionsForTest() == 0u);
        REQUIRE(proc.sumL.size() == kTotal);
        REQUIRE(proc.sumR.size() == kTotal);
        REQUIRE(direct.sumL.size() == kTotal);
        REQUIRE(direct.sumR.size() == kTotal);
        const auto cl = rsCompare(proc.sumL, direct.sumL);
        const auto cr = rsCompare(proc.sumR, direct.sumR);
        INFO("sumL: " << cl.detail << " (metric " << cl.worstMetricRelativeError << ", sample " << cl.worstSampleError
                      << ")");
        INFO("sumR: " << cr.detail << " (metric " << cr.worstMetricRelativeError << ", sample " << cr.worstSampleError
                      << ")");
        CHECK(cl.withinTolerance());
        CHECK(cr.withinTolerance());

        // Non-vacuity: the morph moves the render, so a dropped point would be seen.
        const esPt::VoiceEngineTaps unmoved = esPt::voiceEngineRender(still);
        CHECK_FALSE(rsCompare(unmoved.sumL, direct.sumL).withinTolerance());
    }
}

TEST_CASE("Profundum_ScriptRenderRefusals", "[profundum][probe]") {
    using K = esPt::VoiceEngineEventKind;
    const Krate::DSP::MorphState as = auRd::auStateAs();
    constexpr std::uint64_t kTotal = 12000;   // 0.25 s
    const esPt::VoiceEngineScript good = rsScript(as, as, kTotal, 512u);

    const auto refused = [](const esPt::VoiceEngineScript& s, PresetArmContext& ctx, const char* needle) {
        const esPt::VoiceEngineTaps t = renderScriptThroughProcessor(s, false, -1, ctx);
        INFO("stop: \"" << ctx.stop << "\", expected to contain \"" << needle << "\"");
        CHECK(t.outL.empty());
        CHECK(t.sumL.empty());
        CHECK(ctx.stop.find(needle) != std::string::npos);
    };

    SECTION("probe context: event kinds, config, seams") {
        PresetArmContext ctx;
        esPt::VoiceEngineScript bend = good;
        esPt::VoiceEngineEvent pb{};
        pb.sample = 4800;
        pb.kind = K::PitchBend;
        pb.value = 0.5f;
        bend.events.push_back(pb);
        refused(bend, ctx, "no processor carrier");

        esPt::VoiceEngineScript placed = good;
        placed.config.placement = Krate::DSP::ProfundumNonlinearPlacement::PostSum;
        refused(placed, ctx, "no processor carrier: config");

        esPt::VoiceEngineScript linear = good;
        linear.config.seams.forceLinearCore = true;
        refused(linear, ctx, "seam");
        CHECK(ctx.anchorPhaseTapRenders == 0u);
    }

    SECTION("preset context: the endpoint guard and the P-6 pins") {
        auto base = std::make_unique<ProcessorFixture>();
        const std::vector<std::uint8_t> comp = rsBytesOf(withEndpoints(writeV2Stream(*base->proc), as, as));
        const DecodedPresetState d = decodePresetState(comp);
        REQUIRE(d.ok);

        PresetArmContext ctx;
        ctx.preset = &d;
        ctx.comp = comp;
        refused(good, ctx, "hard-codes its base");   // nothing declared
        ctx.allowedEndpoints = {{auRd::auStateB(), auRd::auStateB()}};
        refused(good, ctx, "hard-codes its base");   // declared, but not the script's
        ctx.allowedEndpoints = {{as, as}};

        DecodedPresetState gain = d;
        gain.outputGain = 0.5f;
        DecodedPresetState ceiling = d;
        ceiling.outputCeilingDb = -3.0f;
        DecodedPresetState bendRange = d;
        bendRange.note.bendSt = 12.0f;
        DecodedPresetState target = d;
        target.morph.editTarget = 1;
        for (const DecodedPresetState* p : {&gain, &ceiling, &bendRange, &target}) {
            ctx.preset = p;
            refused(good, ctx, "pin");
        }

        // Non-vacuity: the same context with the stored pins renders, with the twin and the fidelity arm.
        ctx.preset = &d;
        const esPt::VoiceEngineTaps t = renderScriptThroughProcessor(good, true, -1, ctx);
        INFO("stop: \"" << ctx.stop << "\"");
        REQUIRE(ctx.stop.empty());
        CHECK(t.outL.size() == kTotal);
        CHECK(t.sumL.size() == kTotal);
        CHECK(t.linearCoreL.size() == kTotal);
        REQUIRE(ctx.fidelity.size() == 1u);
        INFO("fidelity tap " << ctx.fidelity[0].tap << ": sample " << ctx.fidelity[0].worstSample << ", metric "
                             << ctx.fidelity[0].worstMetric);
        CHECK(ctx.fidelity[0].pass);
    }

    SECTION("the anchorPhaseTap seam: counted, reaches the engine, and is reset after the render") {
        auto fx = std::make_unique<ProcessorFixture>();
        PresetArmContext ctx;
        ctx.processor = fx.get();
        esPt::VoiceEngineScript s = rsScript(as, as, kEsTotal, kEsBlock, 1u);
        s.config.seams.anchorPhaseTap = true;
        const esPt::VoiceEngineTaps t = renderScriptThroughProcessor(s, false, -1, ctx);
        REQUIRE(ctx.stop.empty());
        CHECK(ctx.anchorPhaseTapRenders == 1u);
        const std::size_t seamChanges = rsCountChanges(t);
        // The processor was left with default seams: a plain re-prepare renders the staircase again (T012 reader).
        const std::size_t afterChanges = esAnchorPhaseChanges(*fx);
        std::cout << "Profundum_ScriptRenderRefusals: anchorPhase changes, seam render " << seamChanges
                  << ", re-prepared after it " << afterChanges << "\n";
        CHECK(seamChanges >= 1000u);
        CHECK(afterChanges <= kEsWindowLength / 16u);
    }
}

TEST_CASE("Profundum_EffectiveScriptCarriers", "[profundum][probe]") {
    using K = esPt::VoiceEngineEventKind;
    namespace P = ::Profundum;
    const Krate::DSP::MorphState as = auRd::auStateAs();

    DecodedPresetState d{};
    d.ok = true;
    d.outputGain = 1.0f;
    d.outputCeilingDb = -1.0f;
    d.voiceMode = 1;   // Poly
    d.polyphony = 4;
    d.macros = {0.1f, 0.2f, 0.3f, 0.4f};
    d.note.priority = 2;
    d.note.legato = 1;
    d.note.portamentoMs = 250.0f;
    d.note.portamentoMode = 1;
    d.note.glideMode = 2;
    d.note.memory = 3;
    d.note.retrigger = 1;
    d.note.bendSt = 2.0f;
    d.morph.position = 0.0f;
    d.morph.rampMs = 200.0f;
    d.morph.anchorMode = 2;
    d.morph.editTarget = 0;
    d.seed = 0x5EEDu;
    d.a = as;
    d.b = as;

    esPt::VoiceEngineScript s = rsScript(as, as, 48000u, 512u);
    s.polyphony = 2;
    s.seed = 0x1234u;
    esPt::VoiceEngineEvent porta{};
    porta.sample = 0;
    porta.kind = K::PortamentoTimeMs;
    porta.value = 123.0f;
    esPt::VoiceEngineEvent move{};
    move.sample = 4800;
    move.kind = K::MorphPosition;
    move.value = 0.5f;
    const esPt::VoiceEngineEvent noteOn = s.events.front();
    s.events = {porta, noteOn, move};   // PortamentoTimeMs, NoteOn (both at 0), MorphPosition at 4800

    SECTION("probe context: the script is returned unchanged") {
        const PresetArmContext probe{};
        const esPt::VoiceEngineScript e = effectiveScriptFor(s, probe);
        CHECK(e.events.size() == s.events.size());
        CHECK(e.seed == s.seed);
        CHECK(e.polyphony == s.polyphony);
        CHECK(e.macros.weight == s.macros.weight);
    }

    SECTION("preset context: decoded carriers replace the script's; sample-0 events precede the script's") {
        PresetArmContext ctx;
        ctx.preset = &d;
        const esPt::VoiceEngineScript e = effectiveScriptFor(s, ctx);
        CHECK(e.macros.weight == 0.1f);
        CHECK(e.macros.darkBright == 0.2f);
        CHECK(e.macros.tightLoose == 0.3f);
        CHECK(e.macros.cleanViolent == 0.4f);
        CHECK(e.mode == Krate::DSP::ProfundumEngine::VoiceMode::Poly);
        CHECK(e.polyphony == 4u);
        CHECK(e.seed == 0x5EEDu);

        struct Want {
            K kind;
            std::uint64_t sample;
            int index;
            float value;
        };
        const std::array<Want, 12> want{{
            {.kind=K::MonoPriority, .sample=0, .index=2, .value=0.0f},
            {.kind=K::Legato, .sample=0, .index=1, .value=0.0f},
            {.kind=K::PortamentoTimeMs, .sample=0, .index=0, .value=250.0f},
            {.kind=K::PortamentoMode, .sample=0, .index=1, .value=0.0f},
            {.kind=K::GlideMode, .sample=0, .index=2, .value=0.0f},
            {.kind=K::NoteMemoryMode, .sample=0, .index=3, .value=0.0f},
            {.kind=K::NoteRetrigger, .sample=0, .index=1, .value=0.0f},
            {.kind=K::MorphRampMs, .sample=0, .index=0, .value=200.0f},
            {.kind=K::AnchorMorphMode, .sample=0, .index=2, .value=0.0f},
            {.kind=K::PortamentoTimeMs, .sample=0, .index=0, .value=123.0f},
            {.kind=K::NoteOn, .sample=0, .index=0, .value=0.0f},
            {.kind=K::MorphPosition, .sample=4800, .index=0, .value=0.5f},
        }};
        std::vector<esPt::VoiceEngineEvent> sorted = e.events;
        std::stable_sort(sorted.begin(), sorted.end(), [](const esPt::VoiceEngineEvent& x,
                                                          const esPt::VoiceEngineEvent& y) {
            return x.sample < y.sample;
        });
        REQUIRE(e.events.size() == want.size());
        REQUIRE(sorted.size() == want.size());
        for (std::size_t i = 0; i < want.size(); ++i) {
            INFO("event " << i);
            CHECK(sorted[i].kind == want[i].kind);
            CHECK(sorted[i].sample == want[i].sample);
            if (want[i].kind == K::NoteOn) {
                CHECK(sorted[i].note == esPt::kVoiceEngineNoteE1);
            } else if (want[i].kind == K::PortamentoTimeMs || want[i].kind == K::MorphRampMs ||
                       want[i].kind == K::MorphPosition) {
                CHECK(sorted[i].value == want[i].value);
            } else {
                CHECK(sorted[i].index == want[i].index);
            }
        }

        // The script's sample-0 PortamentoTimeMs is the one applied: it is the LAST 412 point of the step-3 block
        // (the processor applies a queue's last point at the block start).
        const auto params = processorSampleZeroParams(e);
        std::vector<double> porta412;
        for (const auto& p : params) {
            if (p.first == P::kNotePortamentoId) {
                porta412.push_back(p.second);
            }
        }
        REQUIRE(porta412.size() == 2u);
        CHECK(porta412[0] == ppNorm(P::kNotePortamentoId, 250.0));
        CHECK(porta412[1] == ppNorm(P::kNotePortamentoId, 123.0));
        CHECK(porta412[0] != porta412[1]);
        REQUIRE(!params.empty());
        CHECK(params.back().first == P::kMorphEditTargetId);
        CHECK(params.back().second == 0.0);
        bool polyphonyOk = false;
        for (const auto& p : params) {
            if (p.first == P::kPolyphonyId) {
                polyphonyOk = p.second == 3.0 / 7.0;
            }
        }
        CHECK(polyphonyOk);
    }

    SECTION("macrosFromScript keeps the script's macros; a recorded Voice Mode override wins") {
        PresetArmContext ctx;
        ctx.preset = &d;
        ctx.macrosFromScript = true;
        ctx.overrides = {{P::kVoiceModeId, 0.0}};
        const esPt::VoiceEngineScript e = effectiveScriptFor(s, ctx);
        CHECK(e.macros.weight == s.macros.weight);
        CHECK(e.macros.cleanViolent == s.macros.cleanViolent);
        CHECK(e.mode == Krate::DSP::ProfundumEngine::VoiceMode::Mono);
        CHECK(e.seed == 0x5EEDu);
    }
}

// -----------------------------------------------------------------------------
// T016: the audibility probe (FR-020 - FR-022; SC-009; plan 5.7)
// -----------------------------------------------------------------------------

namespace {

/// One gated cell of the Phase 9 table, keyed by (row, note, base text); contrast disambiguates a key that the
/// Phase 9 shard emits twice (AT-12's ghostTimeMs 15 and 100 cells share row, note and base).
struct PbGatedKey {
    std::string shard, row, contrast, base, note, barText;
    double value = 0.0;
};

/// The per-shard gated key counts of plan 1 (that session's parse of the Phase 9 table).
struct PbGroupCount {
    const char* group;
    std::size_t keys;
};
constexpr std::array<PbGroupCount, 11> kPbGroupCounts{{
    {.group="Anchor", .keys=20},
    {.group="Character", .keys=12},
    {.group="Core", .keys=6},
    {.group="Envelope", .keys=4},
    {.group="Field", .keys=9},
    {.group="Macros", .keys=8},
    {.group="Mass", .keys=7},
    {.group="Morph", .keys=4},
    {.group="Motion", .keys=14},
    {.group="NoteMemory", .keys=8},
    {.group="Pitch", .keys=10},
}};
constexpr std::size_t kPbGatedTotal = 102;
constexpr std::size_t kPbAt13TapRendersPerNote = 4;   // lock + follow per variant (plan 5.7 (c))

[[nodiscard]] std::size_t pbExpectedKeys(const std::string& group) {
    for (const PbGroupCount& g : kPbGroupCounts) {
        if (group == g.group) {
            return g.keys;
        }
    }
    FAIL("unknown probe group " << group);
    return 0;
}

[[nodiscard]] std::string pbPhase9TablePath() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase9-voice-engine/evidence/audibility-table.csv";
}

[[nodiscard]] std::filesystem::path pbProbeTableDir() {
    return {std::string(KRATE_DSP_TESTS_DIR) +
                                 "/../../specs/profundum-phase13-presets-release/evidence/probe-table"};
}

/// RFC-4180 style parse: quoted fields, doubled quotes, embedded commas / newlines; CR dropped outside quotes.
[[nodiscard]] std::vector<std::vector<std::string>> pbParseCsv(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    bool any = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == '"') {
            quoted = true;
            any = true;
        } else if (c == ',') {
            row.push_back(field);
            field.clear();
            any = true;
        } else if (c == '\n') {
            row.push_back(field);
            rows.push_back(std::move(row));
            row = {};
            field.clear();
            any = false;
        } else if (c != '\r') {
            field += c;
            any = true;
        }
    }
    if (any) {
        row.push_back(field);
        rows.push_back(std::move(row));
    }
    return rows;
}

/// Every `gated` cell of the Phase 9 audibility table, in file order.
[[nodiscard]] std::vector<PbGatedKey> pbGatedKeys() {
    const std::string path = pbPhase9TablePath();
    INFO("Phase 9 table: " << path);
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::vector<std::vector<std::string>> rows = pbParseCsv(ss.str());
    REQUIRE(rows.size() > 1u);
    const std::vector<std::string>& header = rows.front();
    const auto col = [&header](const char* name) {
        const auto it = std::find(header.begin(), header.end(), std::string(name));
        INFO("column " << name);
        REQUIRE(it != header.end());
        return static_cast<std::size_t>(it - header.begin());
    };
    const std::size_t cShard = col("shard");
    const std::size_t cRow = col("row");
    const std::size_t cContrast = col("contrast");
    const std::size_t cBase = col("base");
    const std::size_t cNote = col("note");
    const std::size_t cValue = col("value");
    const std::size_t cBar = col("bar");
    const std::size_t cGated = col("gated");
    std::vector<PbGatedKey> keys;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        const std::vector<std::string>& r = rows[i];
        INFO("Phase 9 table line " << (i + 1));
        REQUIRE(r.size() == header.size());
        if (r[cGated] != "gated") {
            continue;
        }
        PbGatedKey k;
        k.shard = r[cShard];
        k.row = r[cRow];
        k.contrast = r[cContrast];
        k.base = r[cBase];
        k.note = r[cNote];
        k.barText = r[cBar];
        k.value = std::stod(r[cValue]);
        keys.push_back(std::move(k));
    }
    return keys;
}

/// "AT-13 (Fracture)" -> "AT-13"; "AT-10" -> "AT-10".
[[nodiscard]] std::string pbAtId(const std::string& row) {
    return row.substr(0, row.find(' '));
}

/// "AT-13 (Fracture)" -> "Fracture"; no parenthesis -> "".
[[nodiscard]] std::string pbVariant(const std::string& row) {
    const std::size_t open = row.find('(');
    const std::size_t close = row.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return {};
    }
    return row.substr(open + 1, close - open - 1);
}

/// The one cell of @p cells on key (row, note, base); contrast breaks a tie. nullptr when none or still ambiguous.
/// The join never reads a cell's `gated` flag: the shards decide it from the readings (auGateOnEither /
/// auGateOnFirst), so the processor path may flag a different candidate than Phase 9 did.
[[nodiscard]] const auRd::AuCell* pbFind(const std::vector<auRd::AuCell>& cells, const PbGatedKey& k) {
    std::vector<const auRd::AuCell*> hits;
    for (const auRd::AuCell& c : cells) {
        if (c.row == k.row && c.note == k.note && c.base == k.base) {
            hits.push_back(&c);
        }
    }
    if (hits.size() > 1u) {
        std::vector<const auRd::AuCell*> byContrast;
        for (const auRd::AuCell* c : hits) {
            if (c->contrast == k.contrast) {
                byContrast.push_back(c);
            }
        }
        hits = std::move(byContrast);
    }
    return hits.size() == 1u ? hits.front() : nullptr;
}

using PbShard = void (*)(std::vector<auRd::AuCell>&, std::uint8_t);

/// One joined probe row (plan 5.7 step 4).
struct PbRow {
    const PbGatedKey* key = nullptr;
    const auRd::AuCell* proc = nullptr;
    const auRd::AuCell* live = nullptr;
    double relDev = 0.0;
    std::string tapText;
};

/// Plan 5.7 steps 1-5 for one Phase 9 shard.
void pbRunProbe(const std::string& group, PbShard shard) {
    enableFTZDAZ();
    std::vector<PbGatedKey> keys;
    for (PbGatedKey& k : pbGatedKeys()) {
        if (k.shard == group) {
            keys.push_back(std::move(k));
        }
    }
    const std::size_t expected = pbExpectedKeys(group);
    REQUIRE(keys.size() == expected);

    PresetArmContext probeCtx;   // probe context: preset == nullptr, a fresh processor per render
    std::vector<auRd::AuCell> procCells;
    std::vector<auRd::AuCell> liveCells;
    std::map<std::string, std::size_t> tapRenders;   // note name -> processor renders that carried anchorPhaseTap
    for (const std::uint8_t note : auRd::kAuNotes) {
        REQUIRE((auRd::render::scriptRenderHook().fn == nullptr));
        // (1) Processor path: the moved shard, unchanged, every render through the processor.
        const std::size_t before = probeCtx.anchorPhaseTapRenders;
        {
            const auRd::render::ScriptRenderScope scope(&processorRenderHook, &probeCtx);
            shard(procCells, note);
        }
        tapRenders[auRd::auNoteName(note)] = probeCtx.anchorPhaseTapRenders - before;
        // (2) Engine-direct: the same call with no hook (the live Phase 9 cells in this executable).
        REQUIRE((auRd::render::scriptRenderHook().fn == nullptr));
        shard(liveCells, note);
    }

    // (4) Join on the gated keys and write evidence/probe-table/<Group>.csv before any gate (a red gate keeps it).
    std::vector<PbRow> joined;
    for (const PbGatedKey& k : keys) {
        const auRd::AuCell* proc = pbFind(procCells, k);
        const auRd::AuCell* live = pbFind(liveCells, k);
        INFO(group << ": key " << k.row << " " << k.note << " (base " << k.base << ")");
        REQUIRE(proc != nullptr);
        REQUIRE(live != nullptr);
        PbRow r;
        r.key = &k;
        r.proc = proc;
        r.live = live;
        r.relDev = std::abs(live->value - k.value) / std::max(std::abs(k.value), 1e-12);
        r.tapText = pbAtId(k.row) == "AT-13" ? std::to_string(tapRenders[k.note]) : std::string("n/a");
        joined.push_back(std::move(r));
    }
    REQUIRE(joined.size() == expected);

    std::ostringstream os;
    os << std::setprecision(6);
    os << "at_id,variant,note,base,processor_value,bar,pass,live_value,csv_value,rel_dev,anchor_phase_tap_renders\n";
    for (const PbRow& r : joined) {
        os << auRd::auCsvField(pbAtId(r.key->row)) << ',' << auRd::auCsvField(pbVariant(r.key->row)) << ','
           << r.key->note << ',' << auRd::auCsvField(r.key->base) << ',' << r.proc->value << ','
           << auRd::auCsvField(r.proc->barText) << ',' << (r.proc->pass ? "PASS" : "FAIL") << ',' << r.live->value
           << ',' << r.key->value << ',' << r.relDev << ',' << r.tapText << '\n';
    }
    const std::string csv = os.str();
    std::cout << "# Profundum_PresetAudibilityProbe_" << group << "\n" << csv << std::flush;
    const std::filesystem::path dir = pbProbeTableDir();
    std::filesystem::create_directories(dir);
    {
        std::ofstream out(dir / (group + ".csv"), std::ios::binary);
        out << csv;
        CHECK(out.good());
    }

    // (5) Gates. Every deviation is reported; the 10 % is never widened.
    for (const PbRow& r : joined) {
        const std::string atId = pbAtId(r.key->row);
        const std::string variant = pbVariant(r.key->row);
        INFO(group << ": " << r.key->row << " " << r.key->note << " (base " << r.key->base << "): processor "
                   << r.proc->value << " bar " << r.proc->barText << ", live " << r.live->value << ", csv "
                   << r.key->value << ", rel_dev " << r.relDev);
        // (a) The processor reading passes its bar; a miss is the FR-023 stop.
        if (!r.proc->pass) {
            std::cout << "FR-023 STOP " << atId << ' ' << r.key->note << ' ' << r.proc->value << ' '
                      << r.proc->barText << (variant.empty() ? std::string() : " variant=" + variant) << "\n"
                      << std::flush;
        }
        CHECK(r.proc->pass);
        // (b) The live engine-direct value is within 10 % (relative) of the Phase 9 CSV value.
        CHECK(r.relDev <= kPpPortTolerance);
    }
    // (c) Anchor only: the AT-13 renders carried the anchorPhaseTap seam, 4 per note.
    if (group == "Anchor") {
        for (const std::uint8_t note : auRd::kAuNotes) {
            const std::string n = auRd::auNoteName(note);
            INFO("Anchor: anchorPhaseTap processor renders at " << n);
            CHECK(tapRenders[n] == kPbAt13TapRendersPerNote);
        }
    }
}

}  // namespace

TEST_CASE("Profundum_PresetAudibilityProbeKeys", "[profundum][probe]") {
    const std::vector<PbGatedKey> keys = pbGatedKeys();
    REQUIRE(keys.size() == kPbGatedTotal);

    // The keys are unique (the join is well defined).
    std::set<std::string> unique;
    for (const PbGatedKey& k : keys) {
        INFO("duplicate key " << k.row << " " << k.note << " " << k.base);
        CHECK(unique.insert(k.row + "|" + k.note + "|" + k.base).second);
    }

    // They cover exactly AT-01 ... AT-50.
    std::map<std::string, std::vector<const PbGatedKey*>> byId;
    for (const PbGatedKey& k : keys) {
        byId[pbAtId(k.row)].push_back(&k);
    }
    std::set<std::string> want;
    for (int i = 1; i <= 50; ++i) {
        want.insert(std::string("AT-") + (i < 10 ? "0" : "") + std::to_string(i));
    }
    std::set<std::string> got;
    for (const auto& entry : byId) {
        got.insert(entry.first);
    }
    CHECK(got == want);

    for (const auto& [id, list] : byId) {
        INFO(id << ": " << list.size() << " gated cells");
        std::multiset<std::string> notes;
        for (const PbGatedKey* k : list) {
            notes.insert(k->note);
        }
        if (id == "AT-32") {
            REQUIRE(list.size() == 1u);
            CHECK(list.front()->note == "E1");
        } else if (id == "AT-13") {
            REQUIRE(list.size() == 4u);
            std::set<std::string> cells;
            for (const PbGatedKey* k : list) {
                cells.insert(pbVariant(k->row) + "/" + k->note);
            }
            CHECK(cells == std::set<std::string>{"Fracture/E1", "Fracture/C3", "Rectify/E1", "Rectify/C3"});
        } else if (id == "AT-26") {
            CHECK(list.size() == 3u);
        } else {
            CHECK(list.size() == 2u);
            CHECK(notes == std::multiset<std::string>{"C3", "E1"});
        }
    }

    // The per-shard key counts of plan 1.
    std::map<std::string, std::size_t> perShard;
    for (const PbGatedKey& k : keys) {
        ++perShard[k.shard];
    }
    CHECK(perShard.size() == kPbGroupCounts.size());
    std::size_t sum = 0;
    for (const PbGroupCount& g : kPbGroupCounts) {
        INFO("shard " << g.group);
        CHECK(perShard[g.group] == g.keys);
        sum += g.keys;
    }
    CHECK(sum == kPbGatedTotal);
}

TEST_CASE("Profundum_PresetAudibilityProbe_Core", "[profundum][probe][.probe]") {
    pbRunProbe("Core", &auRd::auShardCore);
}

TEST_CASE("Profundum_PresetAudibilityProbe_Character", "[profundum][probe][.probe]") {
    pbRunProbe("Character", &auRd::auShardCharacter);
}

TEST_CASE("Profundum_PresetAudibilityProbe_Anchor", "[profundum][probe][.probe]") {
    pbRunProbe("Anchor", &auRd::auShardAnchor);
}

TEST_CASE("Profundum_PresetAudibilityProbe_Pitch", "[profundum][probe][.probe]") {
    pbRunProbe("Pitch", &auRd::auShardPitch);
}

TEST_CASE("Profundum_PresetAudibilityProbe_Envelope", "[profundum][probe][.probe]") {
    pbRunProbe("Envelope", &auRd::auShardEnvelope);
}

TEST_CASE("Profundum_PresetAudibilityProbe_Mass", "[profundum][probe][.probe]") {
    pbRunProbe("Mass", &auRd::auShardMass);
}

TEST_CASE("Profundum_PresetAudibilityProbe_Field", "[profundum][probe][.probe]") {
    pbRunProbe("Field", &auRd::auShardField);
}

TEST_CASE("Profundum_PresetAudibilityProbe_Motion", "[profundum][probe][.probe]") {
    pbRunProbe("Motion", &auRd::auShardMotion);
}

TEST_CASE("Profundum_PresetAudibilityProbe_Morph", "[profundum][probe][.probe]") {
    pbRunProbe("Morph", &auRd::auShardMorph);
}

TEST_CASE("Profundum_PresetAudibilityProbe_NoteMemory", "[profundum][probe][.probe]") {
    pbRunProbe("NoteMemory", &auRd::auShardNoteMemory);
}

TEST_CASE("Profundum_PresetAudibilityProbe_Macros", "[profundum][probe][.probe]") {
    pbRunProbe("Macros", &auRd::auShardMacros);
}

// -----------------------------------------------------------------------------
// Phase 14 T021: the Morph Mod probe (FR-044; SC-017 (a) input; plan 5.7 steps 1-4)
// -----------------------------------------------------------------------------

namespace {

namespace mmpPd = ::Profundum::PresetDefs;
using MmpMod = DecodedPresetState::MorphMod;
using MmpSource = Krate::DSP::ProfundumMorphModulator::Source;

constexpr std::size_t kMmpShards = 32;
constexpr double kMmpGuardDb = 1.0e-6;   // the morphModSourceCell guard (same binary, separately inlined call sites)

/// One source column of the probe: the CSV id (the T023 capability label's prefix), the source and the row label.
struct MmpColumn {
    const char* id;
    MmpSource source;
    const char* row;
};
constexpr std::array<MmpColumn, 4> kMmpColumns{{
    {.id = "M-ENV", .source = MmpSource::Envelope, .row = "M-ENV Morph Mod Envelope"},
    {.id = "M-CYCLE", .source = MmpSource::Cycle, .row = "M-CYCLE Morph Mod Cycle"},
    {.id = "M-VEL", .source = MmpSource::Velocity, .row = "M-VEL Morph Mod Velocity"},
    {.id = "M-WHEEL", .source = MmpSource::ModWheel, .row = "M-WHEEL Morph Mod Wheel"},
}};

[[nodiscard]] std::int32_t mmpSourceIndex(MmpSource s) noexcept {
    return static_cast<std::int32_t>(s);
}

/// One candidate of plan 5.7 step 1: the column index into kMmpColumns and the nine C-10 fields.
struct MmpCandidate {
    std::size_t column = 0;
    MmpMod mod;
};

/// Plan 5.7 step 1, in this order: M-ENV Amount sigma * {1, 0.6} x Attack {0.1, 150} ms x Sustain {1, 0.6}, Decay
/// 600 ms (8); M-CYCLE Amount {+1, -1} x Shape {Square, S&H}, Rate 0.05 Hz free (4); M-VEL Amount sigma * {1, 0.6}
/// (2); M-WHEEL Amount sigma * {1, 0.6} (2). sigma = +1 if P's stored position <= 0.5, else -1. Fields a candidate
/// does not name keep P's stored values.
[[nodiscard]] std::vector<MmpCandidate> mmpCandidates(const DecodedPresetState& d) {
    const float sigma = d.morph.position <= 0.5f ? 1.0f : -1.0f;
    const auto base = [&d](MmpSource s) {
        MmpMod m = d.morphMod;
        m.source = mmpSourceIndex(s);
        return m;
    };
    std::vector<MmpCandidate> out;
    for (const float amount : {1.0f, 0.6f}) {
        for (const float attack : {0.1f, 150.0f}) {
            for (const float sustain : {1.0f, 0.6f}) {
                MmpMod m = base(MmpSource::Envelope);
                m.amount = sigma * amount;
                m.attackMs = attack;
                m.decayMs = 600.0f;
                m.sustain = sustain;
                out.push_back({.column = 0, .mod = m});
            }
        }
    }
    for (const float amount : {1.0f, -1.0f}) {
        for (const Krate::DSP::Waveform shape : {Krate::DSP::Waveform::Square, Krate::DSP::Waveform::SampleHold}) {
            MmpMod m = base(MmpSource::Cycle);
            m.amount = amount;
            m.cycleShape = static_cast<std::int32_t>(shape);
            m.cycleRateHz = 0.05f;
            m.cycleSync = 0;
            out.push_back({.column = 1, .mod = m});
        }
    }
    for (const std::size_t column : {std::size_t{2}, std::size_t{3}}) {
        for (const float amount : {1.0f, 0.6f}) {
            MmpMod m = base(kMmpColumns[column].source);
            m.amount = sigma * amount;
            out.push_back({.column = column, .mod = m});
        }
    }
    REQUIRE(out.size() == 16u);
    return out;
}

/// True iff P claims (primary or secondary) a column whose showcaseArmPlan on P with morph.position = 0.5 records
/// the override "kMorphPositionId=0" (a single-state column, Phase 13 P-3, preset_render_support.h needPositionZero).
[[nodiscard]] bool mmpSingleState(const DecodedPresetState& d, const mmpPd::ProfundumPresetDef& def) {
    DecodedPresetState q = d;
    q.morph.position = 0.5f;
    std::vector<mmpPd::Capability> claims = def.secondaries;
    claims.push_back(def.primary);
    return std::ranges::any_of(claims, [&q](const mmpPd::Capability c) {
        const ShowcasePlan plan = showcaseArmPlan(q, c);
        return std::ranges::find(plan.overrides, std::string("kMorphPositionId=0")) != plan.overrides.end();
    });
}

/// One arm of @p source exactly as morphModSourceCell step 3 builds it (lo: hi == false); the context's two levers
/// are restored on return.
[[nodiscard]] auPt::VoiceEngineTaps mmpArm(PresetArmContext& ctx, const auPt::VoiceEngineScript& sc,
                                           MmpSource source, bool hi) {
    const std::optional<std::int32_t> savedSource = ctx.forceMorphModSource;
    const std::optional<float> savedWheel = ctx.morphModWheel;
    auPt::VoiceEngineScript s = sc;
    switch (source) {
        case MmpSource::Envelope:
        case MmpSource::Cycle:
            ctx.forceMorphModSource = hi ? std::optional<std::int32_t>{} : std::optional<std::int32_t>{0};
            break;
        case MmpSource::Velocity: s.events[0].velocity = hi ? std::uint8_t{127} : std::uint8_t{1}; break;
        default: ctx.morphModWheel = hi ? 1.0f : 0.0f; break;
    }
    auPt::VoiceEngineTaps t = auRd::render::voiceEngineRenderWithTwin(s);
    ctx.forceMorphModSource = savedSource;
    ctx.morphModWheel = savedWheel;
    return t;
}

/// One reading: morphModSourceCell steps 4-6 (window, D_nl on shifted taps, contrast, pass).
struct MmpReading {
    bool found = false;
    std::size_t t0 = 0;
    double pLo = 0.0, pHi = 0.0, dLo = 0.0, dHi = 0.0, contrast = 0.0;
    bool pass = false;
};

[[nodiscard]] MmpReading mmpRead(const auPt::VoiceEngineTaps& lo, const auPt::VoiceEngineTaps& hi,
                                 std::uint8_t note) {
    namespace md = morph_mod_reader_detail;
    const double fs = auRd::kAuFs;
    const md::MmArmWindow win = md::mmPlaceWindow(lo.grid, hi.grid, fs);
    const auto settle = static_cast<std::size_t>(std::llround(1.0 * fs));
    const std::size_t shift = win.found ? win.t0 - settle : 0u;
    const double f0 = auRd::auHz(note);
    MmpReading r;
    r.found = win.found;
    r.t0 = win.t0;
    r.pLo = win.pLo;
    r.pHi = win.pHi;
    r.dLo = auBm::dNlDb(md::mmShiftedChainTaps(lo, shift), f0, fs);
    r.dHi = auBm::dNlDb(md::mmShiftedChainTaps(hi, shift), f0, fs);
    r.contrast = std::abs(r.dHi - r.dLo);
    const bool valid = auRd::auFinite(r.dLo) && auRd::auFinite(r.dHi) && auRd::auFinite(r.contrast);
    r.pass = win.found && valid && r.contrast >= auRd::kAuBarDb;
    return r;
}

[[nodiscard]] std::string mmpFixed(double v, int decimals) {
    std::array<char, 48> buf{};
    std::snprintf(buf.data(), buf.size(), "%.*f", decimals, v);
    return {buf.data()};
}

[[nodiscard]] std::string mmpNum(double v) {
    std::array<char, 48> buf{};
    std::snprintf(buf.data(), buf.size(), "%.9g", v);
    return {buf.data()};
}

[[nodiscard]] std::filesystem::path mmpProbeDir() {
    return {std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase14-morph-modulation/evidence/probe"};
}

/// Plan 5.7 step 1 for shard @p k: every definition i with i % kMmpShards == k.
void mmpRunShard(std::size_t k) {
    enableFTZDAZ();
    const auto start = std::chrono::steady_clock::now();
    const std::vector<mmpPd::ProfundumPresetDef>& defs = mmpPd::allPresets();
    const auto& table = mmpPd::capabilityTable();

    std::ostringstream os;
    os << "preset,defIndex,column,source,amount,attackMs,decayMs,sustain,cycleShape,cycleRateHz,cycleSync,"
          "cycleSyncRate,note,t0,p_lo,p_hi,dnl_lo,dnl_hi,contrast,pass,primary,singleState\n";
    std::size_t presets = 0;
    std::size_t rows = 0;
    std::size_t renders = 0;
    bool guardPending = true;   // the first preset of the shard, at E1

    for (std::size_t i = k; i < defs.size(); i += kMmpShards) {
        const mmpPd::ProfundumPresetDef& def = defs[i];
        INFO("preset " << def.name << " (definition " << i << ")");
        const std::vector<std::uint8_t> comp = presetCompOf(def);
        const DecodedPresetState d = decodePresetState(comp);
        REQUIRE(d.ok);
        REQUIRE(d.morphMod.present);
        const bool single = mmpSingleState(d, def);
        const std::string primary(table[static_cast<std::size_t>(def.primary)].label);
        const std::vector<MmpCandidate> candidates = mmpCandidates(d);

        PresetArmContext ctx;
        ctx.preset = &d;
        ctx.comp = comp;
        ctx.allowedEndpoints = {{d.a, d.b}};
        REQUIRE((auRd::render::scriptRenderHook().fn == nullptr));
        const auRd::render::ScriptRenderScope scope(&processorRenderHook, &ctx);

        for (const std::uint8_t note : auRd::kAuNotes) {
            INFO("note " << auRd::auNoteName(note));
            const auPt::VoiceEngineScript sc =
                auRd::auScript(preset_showcase_detail::settingOf(d), note, kMorphModArmTotal);
            REQUIRE(!sc.events.empty());
            REQUIRE(sc.events[0].kind == auRd::AuKind::NoteOn);

            // The forced-Off lo arm of M-ENV / M-CYCLE: P as stored (Source Off), once per (P, note).
            ctx.morphModCandidate.reset();
            const auPt::VoiceEngineTaps offLo = mmpArm(ctx, sc, MmpSource::Envelope, false);
            ++renders;

            const bool guardNote = guardPending && note == auRd::kAuE1;
            std::array<bool, kMmpColumns.size()> guarded{};
            for (const MmpCandidate& cand : candidates) {
                const MmpColumn& col = kMmpColumns[cand.column];
                ctx.morphModCandidate = cand.mod;
                MmpReading r;
                if (col.source == MmpSource::Envelope || col.source == MmpSource::Cycle) {
                    const auPt::VoiceEngineTaps hi = mmpArm(ctx, sc, col.source, true);
                    ++renders;
                    r = mmpRead(offLo, hi, note);
                } else {
                    const auPt::VoiceEngineTaps lo = mmpArm(ctx, sc, col.source, false);
                    const auPt::VoiceEngineTaps hi = mmpArm(ctx, sc, col.source, true);
                    renders += 2u;
                    r = mmpRead(lo, hi, note);
                }

                if (guardNote && !guarded[cand.column]) {
                    // The same candidate through morphModSourceCell itself (its lo arm carries the candidate's
                    // fields with Source forced Off): the cached-arm reading must agree.
                    guarded[cand.column] = true;
                    const auRd::AuCell cell = morphModSourceCell(mmpSourceIndex(col.source), col.row, d, note);
                    renders += 2u;
                    INFO("guard " << col.id << ": cell lo " << cell.lo << " hi " << cell.hi << " pass " << cell.pass
                                  << " (" << cell.remark << "); probe lo " << r.dLo << " hi " << r.dHi << " pass "
                                  << r.pass);
                    CHECK(std::abs(cell.lo - r.dLo) <= kMmpGuardDb);
                    CHECK(std::abs(cell.hi - r.dHi) <= kMmpGuardDb);
                    CHECK(cell.pass == r.pass);
                }

                const MmpMod& m = cand.mod;
                os << auRd::auCsvField(std::string(def.name)) << ',' << i << ',' << col.id << ',' << m.source << ','
                   << mmpNum(m.amount) << ',' << mmpNum(m.attackMs) << ',' << mmpNum(m.decayMs) << ','
                   << mmpNum(m.sustain) << ',' << m.cycleShape << ',' << mmpNum(m.cycleRateHz) << ',' << m.cycleSync
                   << ',' << m.cycleSyncRate << ',' << auRd::auNoteName(note) << ',';
                if (r.found) {
                    os << mmpFixed(static_cast<double>(r.t0) / auRd::kAuFs, 6) << ',' << mmpNum(r.pLo) << ','
                       << mmpNum(r.pHi) << ',';
                } else {
                    os << ",,,";
                }
                os << mmpNum(r.dLo) << ',' << mmpNum(r.dHi) << ',' << mmpNum(r.contrast) << ','
                   << (r.pass ? "PASS" : "FAIL") << ',' << auRd::auCsvField(primary) << ','
                   << (single ? "true" : "false") << '\n';
                ++rows;
            }
            ctx.morphModCandidate.reset();
            if (guardNote) {
                guardPending = false;
            }
        }
        ++presets;
    }

    const std::string csv = os.str();
    std::cout << "# Profundum_MorphModProbe_" << k << "of" << kMmpShards << "\n" << csv << std::flush;
    const std::filesystem::path dir = mmpProbeDir();
    std::filesystem::create_directories(dir);
    {
        std::ofstream out(dir / ("shard-" + std::to_string(k) + ".csv"), std::ios::binary);
        out << csv;
        CHECK(out.good());
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "MORPHMOD PROBE SHARD " << k << " presets=" << presets << " rows=" << rows << " renders=" << renders
              << " wall=" << mmpFixed(wall, 1) << " s\n"
              << std::flush;
    CHECK(rows == presets * 16u * auRd::kAuNotes.size());
}

}  // namespace

TEST_CASE("Profundum_MorphModProbe_0of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(0); }
TEST_CASE("Profundum_MorphModProbe_1of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(1); }
TEST_CASE("Profundum_MorphModProbe_2of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(2); }
TEST_CASE("Profundum_MorphModProbe_3of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(3); }
TEST_CASE("Profundum_MorphModProbe_4of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(4); }
TEST_CASE("Profundum_MorphModProbe_5of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(5); }
TEST_CASE("Profundum_MorphModProbe_6of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(6); }
TEST_CASE("Profundum_MorphModProbe_7of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(7); }
TEST_CASE("Profundum_MorphModProbe_8of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(8); }
TEST_CASE("Profundum_MorphModProbe_9of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(9); }
TEST_CASE("Profundum_MorphModProbe_10of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(10); }
TEST_CASE("Profundum_MorphModProbe_11of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(11); }
TEST_CASE("Profundum_MorphModProbe_12of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(12); }
TEST_CASE("Profundum_MorphModProbe_13of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(13); }
TEST_CASE("Profundum_MorphModProbe_14of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(14); }
TEST_CASE("Profundum_MorphModProbe_15of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(15); }
TEST_CASE("Profundum_MorphModProbe_16of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(16); }
TEST_CASE("Profundum_MorphModProbe_17of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(17); }
TEST_CASE("Profundum_MorphModProbe_18of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(18); }
TEST_CASE("Profundum_MorphModProbe_19of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(19); }
TEST_CASE("Profundum_MorphModProbe_20of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(20); }
TEST_CASE("Profundum_MorphModProbe_21of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(21); }
TEST_CASE("Profundum_MorphModProbe_22of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(22); }
TEST_CASE("Profundum_MorphModProbe_23of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(23); }
TEST_CASE("Profundum_MorphModProbe_24of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(24); }
TEST_CASE("Profundum_MorphModProbe_25of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(25); }
TEST_CASE("Profundum_MorphModProbe_26of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(26); }
TEST_CASE("Profundum_MorphModProbe_27of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(27); }
TEST_CASE("Profundum_MorphModProbe_28of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(28); }
TEST_CASE("Profundum_MorphModProbe_29of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(29); }
TEST_CASE("Profundum_MorphModProbe_30of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(30); }
TEST_CASE("Profundum_MorphModProbe_31of32", "[profundum][morphmod][probe][.probe]") { mmpRunShard(31); }
