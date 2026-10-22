// ==============================================================================
// Profundum - parameter flow and audibility (Phase 10 T016, SC-016)
// ==============================================================================
// Profundum_ParamFlowReachesEngine (FR-024.4, FR-027, FR-028, FR-041, FR-043;
// plan 4.3 SC-016):
//   ReadBack           one block with points on all eight IDs -> the engine's
//                      pending read-backs equal the denormalised atomics
//   Audibility         every control moves a 2 s render past its floor
//   Tempo              kTempoValid forwards, no flag / null context keeps
//   MacrosLive         macro points reach macros(); all-0.5 == DirectEngine
//   StateWhileRunning  setState mid-run reaches the engine with no param point
//                      (the unconditional per-block push, FR-024.4)
//   GainRamp           the output gain is smoothed per sample (FR-028)
//
// No bit-exact float golden (FR-065): ratios, level bars, the limiter's own
// ceiling expression and explicit mismatch counts only. A red section is fixed
// at its root cause in processor.cpp; a floor is never moved.
// ==============================================================================

#include "profundum_test_fixture.h"

#include "engine/profundum_engine_config.h"
#include "parameters/global_params.h"
#include "parameters/macro_params.h"
#include "plugin_ids.h"

#include <vst_event_list.h>

#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/profundum_engine.h>

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

using ProfEngine = Krate::DSP::ProfundumEngine;
using Scripted = ProcessorFixture::ScriptedEvent;
using Steinberg::Vst::ParamID;

constexpr double kFs = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kTotal = 96000;  // 2 s at 48 kHz
constexpr float kVelocity = 100.0f / 127.0f;
constexpr std::uint8_t kMidiVelocity = 100;  // quantiseVelocity(100/127) = uint8(100.5)

struct Stereo {
    std::vector<float> l;
    std::vector<float> r;
};

using ParamPoint = std::pair<ParamID, double>;

Scripted noteOnAt(Steinberg::int64 sample, Steinberg::int16 pitch) {
    Scripted s;
    s.sample = sample;
    s.kind = Scripted::Kind::NoteOn;
    s.pitch = pitch;
    s.velocity = kVelocity;
    return s;
}

Scripted paramAt(Steinberg::int64 sample, ParamID id, double value) {
    Scripted s;
    s.sample = sample;
    s.kind = Scripted::Kind::Param;
    s.id = id;
    s.value = value;
    return s;
}

// Parameter points at sample 0, then a NoteOn at sample 0 for each pitch.
std::vector<Scripted> scriptOf(std::initializer_list<ParamPoint> params,
                               std::span<const Steinberg::int16> pitches) {
    std::vector<Scripted> script;
    for (const ParamPoint& p : params) {
        script.push_back(paramAt(0, p.first, p.second));
    }
    for (const Steinberg::int16 pitch : pitches) {
        script.push_back(noteOnAt(0, pitch));
    }
    return script;
}

// A fresh fixture prepared at 48 kHz, 2 s rendered in 512-sample host blocks.
Stereo renderProcessor(const std::vector<Scripted>& script) {
    ProcessorFixture f;
    REQUIRE(f.prepare(kFs) == Steinberg::kResultOk);
    f.renderScript(script, kTotal, kBlock);
    REQUIRE(f.capturedL.size() == kTotal);
    REQUIRE(f.capturedR.size() == kTotal);
    return Stereo{.l = std::move(f.capturedL), .r = std::move(f.capturedR)};
}

float stereoPeak(const Stereo& s) { return std::max(peakOf(s.l), peakOf(s.r)); }

float stereoMaxAbsDiff(const Stereo& a, const Stereo& b) {
    return std::max(maxAbsDiff(a.l, b.l), maxAbsDiff(a.r, b.r));
}

// RMS (dBFS) of x - ref over both channels.
double diffRmsDb(const Stereo& x, const Stereo& ref) {
    REQUIRE(x.l.size() == ref.l.size());
    REQUIRE(x.r.size() == ref.r.size());
    std::vector<float> d;
    d.reserve(x.l.size() + x.r.size());
    for (std::size_t i = 0; i < x.l.size(); ++i) {
        d.push_back(x.l[i] - ref.l[i]);
    }
    for (std::size_t i = 0; i < x.r.size(); ++i) {
        d.push_back(x.r[i] - ref.r[i]);
    }
    return rmsDb(d);
}

// Linear RMS ratio x / ref over [from, to) of one channel.
double rmsRatio(std::span<const float> x, std::span<const float> ref, std::size_t from,
                std::size_t to) {
    return std::pow(10.0, (rmsDb(x, from, to) - rmsDb(ref, from, to)) / 20.0);
}

void requireMacrosEqualAtomics(const ::Profundum::Processor& p) {
    const Krate::DSP::ProfundumMacroValues m = p.engineForTest()->macros();
    const ::Profundum::MacroParams& a = p.macroParamsForTest();
    REQUIRE(m.weight == a.weight.load(std::memory_order_relaxed));
    REQUIRE(m.darkBright == a.darkBright.load(std::memory_order_relaxed));
    REQUIRE(m.tightLoose == a.tightLoose.load(std::memory_order_relaxed));
    REQUIRE(m.cleanViolent == a.cleanViolent.load(std::memory_order_relaxed));
}

// Precondition shared by the 0-mismatch comparisons: the render is audible.
void requireAudible(const Stereo& s) {
    REQUIRE(rmsDb(s.l) >= -40.0);
    REQUIRE(rmsDb(s.r) >= -40.0);
}

}  // namespace

TEST_CASE("Profundum_ParamFlowReachesEngine", "[profundum][params][audio]") {
    // -------------------------------------------------------------------------
    SECTION("ReadBack") {  // SC-016.1, FR-024.4, FR-041, FR-043
        ProcessorFixture f;
        REQUIRE(f.prepare(kFs) == Steinberg::kResultOk);
        f.reserveCapture(kBlock);

        Krate::Test::ParameterChanges pc;
        pc.addChange(::Profundum::kOutputGainId, 0.3);
        pc.addChange(::Profundum::kVoiceModeId, 1.0);
        pc.addChange(::Profundum::kPolyphonyId, 0.6);
        pc.addChange(::Profundum::kOutputCeilingId, 0.4);
        pc.addChange(::Profundum::kMacroWeightId, 0.2);
        pc.addChange(::Profundum::kMacroDarkBrightId, 0.7);
        pc.addChange(::Profundum::kMacroTightLooseId, 0.9);
        pc.addChange(::Profundum::kMacroCleanViolentId, 0.1);
        REQUIRE(f.processBlock(kBlock, nullptr, &pc) == Steinberg::kResultOk);

        const ::Profundum::GlobalParams& g = f.proc->globalParamsForTest();
        const int mode = g.voiceMode.load(std::memory_order_relaxed);
        const int poly = g.polyphony.load(std::memory_order_relaxed);
        const float ceilingDb = g.outputCeilingDb.load(std::memory_order_relaxed);

        // Non-vacuity: every atomic moved off its default.
        REQUIRE(g.outputGain.load(std::memory_order_relaxed) == ::Profundum::outputGainFromNormalized(0.3));
        REQUIRE(mode == 1);
        REQUIRE(poly == ::Profundum::polyphonyFromNormalized(0.6));
        REQUIRE(poly != ::Profundum::kDefaultPolyphony);
        REQUIRE(ceilingDb == ::Profundum::ceilingFromNormalized(0.4));
        REQUIRE(ceilingDb != ::Profundum::kDefaultCeilingDb);

        const ProfEngine* e = f.proc->engineForTest();
        REQUIRE(e != nullptr);
        WARN("ReadBack: voiceMode " << static_cast<int>(e->voiceMode()) << ", polyphony " << e->polyphony()
                                    << ", ceiling " << e->outputCeilingDb() << " dBTP, macros {"
                                    << e->macros().weight << ", " << e->macros().darkBright << ", "
                                    << e->macros().tightLoose << ", " << e->macros().cleanViolent << "}");
        REQUIRE(e->voiceMode() == (mode == 1 ? ProfEngine::VoiceMode::Poly : ProfEngine::VoiceMode::Mono));
        REQUIRE(e->polyphony() == static_cast<std::size_t>(poly));
        REQUIRE(e->outputCeilingDb() == ceilingDb);
        requireMacrosEqualAtomics(*f.proc);
        REQUIRE(e->macros().weight == 0.2f);
        REQUIRE(e->macros().darkBright == 0.7f);
        REQUIRE(e->macros().tightLoose == 0.9f);
        REQUIRE(e->macros().cleanViolent == 0.1f);
    }

    // -------------------------------------------------------------------------
    SECTION("Audibility") {  // SC-016.2
        const std::span<const Steinberg::int16> e1{&kE1, 1};

        // ---- output gain (E1, Mono) ----
        const Stereo def = renderProcessor(scriptOf({}, e1));
        requireAudible(def);

        const Stereo g09 = renderProcessor(scriptOf({{::Profundum::kOutputGainId, 0.9}}, e1));
        const double ratioL = rmsRatio(g09.l, def.l, 2400, kTotal);
        const double ratioR = rmsRatio(g09.r, def.r, 2400, kTotal);
        WARN("gain 0.9: rms ratio over [2400, 96000) L = " << ratioL << ", R = " << ratioR
                                                           << " (bar [0.491, 0.511])");
        REQUIRE(ratioL >= 0.491);
        REQUIRE(ratioL <= 0.511);
        REQUIRE(ratioR >= 0.491);
        REQUIRE(ratioR <= 0.511);

        const Stereo g0 = renderProcessor(scriptOf({{::Profundum::kOutputGainId, 0.0}}, e1));
        std::size_t nonZeroAfterRamp = 0;
        for (std::size_t i = 960; i < kTotal; ++i) {
            if (g0.l[i] != 0.0f || g0.r[i] != 0.0f) {  // value equality admits -0.0f (plan P-4)
                ++nonZeroAfterRamp;
            }
        }
        WARN("gain 0.0: non-zero samples from index 960 = " << nonZeroAfterRamp << ", peak = " << stereoPeak(g0));
        REQUIRE(nonZeroAfterRamp == 0u);

        const Stereo g1 = renderProcessor(scriptOf({{::Profundum::kOutputGainId, 1.0}}, e1));
        const std::size_t g1MisL = mismatches(g1.l, def.l);
        const std::size_t g1MisR = mismatches(g1.r, def.r);
        WARN("gain 1.0: mismatches vs default L = " << g1MisL << ", R = " << g1MisR);
        REQUIRE(g1MisL == 0u);
        REQUIRE(g1MisR == 0u);

        // ---- voice mode: 28 held from 0, 40 held from 24000 ----
        const std::vector<Scripted> overlapMono{noteOnAt(0, 28), noteOnAt(24000, 40)};
        const std::vector<Scripted> overlapPoly{paramAt(0, ::Profundum::kVoiceModeId, 1.0), noteOnAt(0, 28),
                                                noteOnAt(24000, 40)};
        const Stereo mono = renderProcessor(overlapMono);
        const Stereo polyOverlap = renderProcessor(overlapPoly);
        const float modeDiff = stereoMaxAbsDiff(mono, polyOverlap);
        WARN("voice mode Mono vs Poly: maxAbsDiff = " << modeDiff << " (bar > 1e-3)");
        REQUIRE(modeDiff > 1e-3f);

        // ---- polyphony 1 vs 4, Poly, kChord4 ----
        const Stereo poly1 = renderProcessor(
            scriptOf({{::Profundum::kVoiceModeId, 1.0}, {::Profundum::kPolyphonyId, 0.0}}, kChord4));
        const Stereo poly4 = renderProcessor(
            scriptOf({{::Profundum::kVoiceModeId, 1.0}, {::Profundum::kPolyphonyId, 3.0 / 7.0}}, kChord4));
        const double poly1L = rmsDb(poly1.l);
        const double poly4L = rmsDb(poly4.l);
        const double poly1R = rmsDb(poly1.r);
        const double poly4R = rmsDb(poly4.r);
        WARN("polyphony 1 vs 4 (kChord4): rmsDb L " << poly1L << " vs " << poly4L << ", R " << poly1R << " vs "
                                                    << poly4R << " (bar |diff| > 1 dB)");
        REQUIRE(std::abs(poly1L - poly4L) > 1.0);
        REQUIRE(std::abs(poly1R - poly4R) > 1.0);

        // ---- ceiling -12 dBTP, Poly, polyphony 8, kChord8 ----
        const float ceil12 = std::pow(10.0f, -12.0f * 0.05f);  // the limiter's own expression
        const Stereo ceilDefault = renderProcessor(
            scriptOf({{::Profundum::kVoiceModeId, 1.0}, {::Profundum::kPolyphonyId, 1.0}}, kChord8));
        const Stereo ceilLow = renderProcessor(scriptOf(
            {{::Profundum::kVoiceModeId, 1.0}, {::Profundum::kPolyphonyId, 1.0}, {::Profundum::kOutputCeilingId, 0.0}},
            kChord8));
        const float peakDefault = stereoPeak(ceilDefault);
        const float peakLow = stereoPeak(ceilLow);
        WARN("ceiling (kChord8): default peak = " << peakDefault << ", -12 dBTP peak = " << peakLow << " (bar "
                                                  << ceil12 << ")");
        REQUIRE(peakLow <= ceil12);
        REQUIRE(peakDefault > ceil12);

        // ---- each macro at 0 and at 1 vs 0.5 (E1, Mono) ----
        constexpr std::array<ParamID, 4> kMacroIds{::Profundum::kMacroWeightId, ::Profundum::kMacroDarkBrightId,
                                                   ::Profundum::kMacroTightLooseId,
                                                   ::Profundum::kMacroCleanViolentId};
        for (const ParamID id : kMacroIds) {
            for (const double v : {0.0, 1.0}) {
                const Stereo m = renderProcessor(scriptOf({{id, v}}, e1));
                const double d = diffRmsDb(m, def);
                // D-2 (user ruling 2026-10-22, spec SC-016.2): Clean <-> Violent at 0 is inert by construction on the
                // default patch (excitation mix, Fold and Character Amount already sit at 0 and the row's offsets clamp
                // there), so that arm is recorded, not gated; every other arm is gated.
                const bool recorded = id == ::Profundum::kMacroCleanViolentId && v == 0.0;
                WARN("macro " << id << " = " << v << ": rmsDb(x - default) = " << d
                             << (recorded ? " dBFS (recorded: inert by construction on the default patch, D-2)"
                                          : " dBFS (bar > -60)"));
                if (!recorded) {
                    REQUIRE(d > -60.0);
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    SECTION("Tempo") {  // SC-016.3, FR-027
        ProcessorFixture f;
        REQUIRE(f.prepare(kFs) == Steinberg::kResultOk);
        f.reserveCapture(3 * kBlock);
        const ProfEngine* e = f.proc->engineForTest();
        REQUIRE(e != nullptr);

        REQUIRE(e->tempoBpm() == 120.0f);

        Steinberg::Vst::ProcessContext ctx{};
        ctx.state = Steinberg::Vst::ProcessContext::kTempoValid;
        ctx.tempo = 90.0;
        ctx.sampleRate = kFs;
        REQUIRE(f.processBlock(kBlock, nullptr, nullptr, &ctx) == Steinberg::kResultOk);
        const float afterValid = e->tempoBpm();

        ctx.state = 0;
        ctx.tempo = 140.0;
        REQUIRE(f.processBlock(kBlock, nullptr, nullptr, &ctx) == Steinberg::kResultOk);
        const float afterNoFlag = e->tempoBpm();

        REQUIRE(f.processBlock(kBlock, nullptr, nullptr, nullptr) == Steinberg::kResultOk);
        const float afterNull = e->tempoBpm();

        WARN("Tempo: after kTempoValid 90 = " << afterValid << ", after state 0 / 140 = " << afterNoFlag
                                              << ", after null context = " << afterNull);
        REQUIRE(afterValid == 90.0f);
        REQUIRE(afterNoFlag == 90.0f);
        REQUIRE(afterNull == 90.0f);
    }

    // -------------------------------------------------------------------------
    SECTION("MacrosLive") {  // FR-041, SC-016.4
        {
            ProcessorFixture f;
            REQUIRE(f.prepare(kFs) == Steinberg::kResultOk);
            f.reserveCapture(2 * kBlock);
            REQUIRE(f.processBlock(kBlock) == Steinberg::kResultOk);
            requireMacrosEqualAtomics(*f.proc);

            Krate::Test::ParameterChanges pc;
            pc.addChange(::Profundum::kMacroTightLooseId, 0.8);
            REQUIRE(f.processBlock(kBlock, nullptr, &pc) == Steinberg::kResultOk);
            const float tl = f.proc->engineForTest()->macros().tightLoose;
            WARN("MacrosLive: tightLoose after a 0.8 point = " << tl);
            REQUIRE(tl == 0.8f);
            requireMacrosEqualAtomics(*f.proc);
        }

        // All four at 0.5 (explicit points): the processor equals a bare engine.
        const std::span<const Steinberg::int16> e1{&kE1, 1};
        const Stereo proc = renderProcessor(scriptOf({{::Profundum::kMacroWeightId, 0.5},
                                                      {::Profundum::kMacroDarkBrightId, 0.5},
                                                      {::Profundum::kMacroTightLooseId, 0.5},
                                                      {::Profundum::kMacroCleanViolentId, 0.5}},
                                                     e1));
        requireAudible(proc);

        DirectEngine direct(kFs);
        DirectEvent on;
        on.sample = 0;
        on.kind = DirectEvent::Kind::NoteOn;
        on.note = static_cast<std::uint8_t>(kE1);
        on.velocity = kMidiVelocity;
        const std::array<DirectEvent, 1> directScript{on};
        direct.render(directScript, kTotal);

        const std::size_t misL = mismatches(proc.l, direct.outL);
        const std::size_t misR = mismatches(proc.r, direct.outR);
        WARN("MacrosLive all-0.5 vs DirectEngine: mismatches L = " << misL << ", R = " << misR);
        REQUIRE(misL == 0u);
        REQUIRE(misR == 0u);
    }

    // -------------------------------------------------------------------------
    SECTION("StateWhileRunning") {  // FR-024.4, FR-045
        ProcessorFixture f;
        REQUIRE(f.prepare(kFs) == Steinberg::kResultOk);
        f.reserveCapture(2 * kBlock);

        Krate::Test::EventList ev;
        ev.addNoteOn(kE1, kVelocity, 0);
        REQUIRE(f.processBlock(kBlock, &ev) == Steinberg::kResultOk);

        Steinberg::IPtr<Steinberg::MemoryStream> stream = Steinberg::owned(new Steinberg::MemoryStream());
        {
            Steinberg::IBStreamer s(stream, kLittleEndian);
            REQUIRE(s.writeInt32(::Profundum::kCurrentStateVersion));
            REQUIRE(s.writeFloat(1.0f));     // output gain
            REQUIRE(s.writeInt32(1));        // voice mode Poly
            REQUIRE(s.writeInt32(2));        // polyphony
            REQUIRE(s.writeFloat(-6.0f));    // ceiling dBTP
            REQUIRE(s.writeFloat(0.1f));     // Weight
            REQUIRE(s.writeFloat(0.2f));     // Dark/Bright
            REQUIRE(s.writeFloat(0.3f));     // Tight/Loose
            REQUIRE(s.writeFloat(0.4f));     // Clean/Violent
        }
        REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
        REQUIRE(f.proc->setState(stream) == Steinberg::kResultOk);

        // No parameter changes, no events.
        REQUIRE(f.processBlock(kBlock) == Steinberg::kResultOk);

        const ProfEngine* e = f.proc->engineForTest();
        const Krate::DSP::ProfundumMacroValues m = e->macros();
        WARN("StateWhileRunning: voiceMode " << static_cast<int>(e->voiceMode()) << ", polyphony "
                                             << e->polyphony() << ", ceiling " << e->outputCeilingDb()
                                             << ", macros {" << m.weight << ", " << m.darkBright << ", "
                                             << m.tightLoose << ", " << m.cleanViolent << "}");
        REQUIRE(e->voiceMode() == ProfEngine::VoiceMode::Poly);
        REQUIRE(e->polyphony() == 2u);
        REQUIRE(e->outputCeilingDb() == -6.0f);
        REQUIRE(m.weight == 0.1f);
        REQUIRE(m.darkBright == 0.2f);
        REQUIRE(m.tightLoose == 0.3f);
        REQUIRE(m.cleanViolent == 0.4f);
    }

    // -------------------------------------------------------------------------
    SECTION("GainRamp") {  // FR-028: per-sample smoothing
        ProcessorFixture f;
        REQUIRE(f.prepare(kFs) == Steinberg::kResultOk);
        f.reserveCapture(2 * kBlock);

        Krate::Test::EventList ev;
        ev.addNoteOn(kE1, kVelocity, 0);
        REQUIRE(f.processBlock(kBlock, &ev) == Steinberg::kResultOk);

        Krate::Test::ParameterChanges pc;
        pc.addChange(::Profundum::kOutputGainId, 0.9);
        REQUIRE(f.processBlock(kBlock, nullptr, &pc) == Steinberg::kResultOk);

        // The unity reference: both blocks at gain 1.0.
        DirectEngine direct(kFs);
        DirectEvent on;
        on.sample = 0;
        on.kind = DirectEvent::Kind::NoteOn;
        on.note = static_cast<std::uint8_t>(kE1);
        on.velocity = kMidiVelocity;
        const std::array<DirectEvent, 1> directScript{on};
        direct.render(directScript, 2 * kBlock);

        const std::span<const float> outL{f.capturedL};
        const std::span<const float> outR{f.capturedR};
        const std::span<const float> dirL{direct.outL};
        const std::span<const float> dirR{direct.outR};

        // Block 0: bit-identical (SC-008).
        const std::size_t mis0L = mismatches(outL.first(kBlock), dirL.first(kBlock));
        const std::size_t mis0R = mismatches(outR.first(kBlock), dirR.first(kBlock));

        // Smoother state after block 1, and its prediction.
        const float gainNow = f.proc->outputGainValueForTest();
        Krate::DSP::OnePoleSmoother s{1.0f};
        s.configure(::Profundum::kOutputGainSmoothMs, 48000.0f);
        s.setTarget(0.5011872f);
        for (std::size_t i = 0; i < kBlock; ++i) {
            static_cast<void>(s.process());
        }
        const float predicted = s.getCurrentValue();

        // Ratio over block 1 at every index where the reference is audible.
        struct RatioStats {
            std::size_t count = 0;
            std::size_t increases = 0;
            double first = 0.0;
            double last = 0.0;
        };
        const auto ratioStats = [&](std::span<const float> out, std::span<const float> dir) {
            RatioStats st;
            double prev = 0.0;
            for (std::size_t i = kBlock; i < 2 * kBlock; ++i) {
                if (std::fabs(dir[i]) <= 1e-4f) {
                    continue;
                }
                const double r = static_cast<double>(out[i]) / static_cast<double>(dir[i]);
                if (st.count == 0) {
                    st.first = r;
                } else if (r > prev + 1e-6) {
                    ++st.increases;
                }
                prev = r;
                st.last = r;
                ++st.count;
            }
            return st;
        };
        const RatioStats rl = ratioStats(outL, dirL);
        const RatioStats rr = ratioStats(outR, dirR);

        WARN("GainRamp: block 0 mismatches L = " << mis0L << ", R = " << mis0R << "; gain after block 1 = "
                                                 << gainNow << ", predicted = " << predicted << " (|diff| "
                                                 << std::abs(gainNow - predicted) << ")");
        WARN("GainRamp ratio L: count " << rl.count << ", increases " << rl.increases << ", first " << rl.first
                                        << ", last " << rl.last << "; R: count " << rr.count << ", increases "
                                        << rr.increases << ", first " << rr.first << ", last " << rr.last);

        REQUIRE(mis0L == 0u);
        REQUIRE(mis0R == 0u);
        REQUIRE(gainNow > 0.5011872f);
        REQUIRE(gainNow < 1.0f);
        REQUIRE(std::abs(gainNow - predicted) <= 1e-5f);

        REQUIRE(rl.count >= 2u);
        REQUIRE(rr.count >= 2u);
        REQUIRE(rl.increases == 0u);
        REQUIRE(rr.increases == 0u);
        REQUIRE(rl.first - rl.last > 0.1);
        REQUIRE(rr.first - rr.last > 0.1);
    }
}
