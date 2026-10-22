// ==============================================================================
// Profundum_ProcessorLifecycle (Phase 10, plan 4.1 / 4.3)
// ==============================================================================
// T006 ships ONLY the P-5 probe (IdleIsExactZero, plan 1.3 P-5, tasks.md
// deviation 3). It drives ProfundumEngine directly through the fixture's
// DirectEngine, because no processor body exists when it first runs. Later
// tasks (T013, T014, T018) add the processor-path sections to this case.
// T013 adds Latency, NoLatencyMessage, NotReady, SampleSize64 (steps 1-3),
// UnsupportedRate (silence parts), PushInSetup and NoMidiInterfaces.
// T014 adds LatencyIsReal, Guards, Reset, GainSnap and the recovery renders of
// SampleSize64 (step 4) and UnsupportedRate.
// T018 adds ActivateNoAlloc and ProcessNoAlloc (SC-007, SC-017.7) under the P-11
// discipline: every container is built BEFORE the AllocationScope, the scope holds
// only proc->... calls (no Catch macro), the count is read inside and asserted after.
//
// STOP RULE (G3 gate): if either clause is red, the cause lives in dsp/
// (FR-090). Never edit dsp/ to fix it and never weaken the exact-zero bar:
// record the reading and stop for a user ruling.
//
// "Exact zero" is value equality x == 0.0f, which admits -0.0f (P-4).
// ==============================================================================

#include "profundum_test_fixture.h"

#include "controller/controller.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <allocation_detector.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <span>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

// The count of samples in [from, x.size()) that are not exactly zero.
std::size_t nonZeroCount(std::span<const float> x, std::size_t from = 0) noexcept {
    std::size_t count = 0;
    for (std::size_t i = from; i < x.size(); ++i) {
        if (x[i] != 0.0f) {
            ++count;
        }
    }
    return count;
}

// The index of the last sample that is not exactly zero, or -1 if none.
std::int64_t lastNonZero(std::span<const float> x) noexcept {
    for (std::size_t i = x.size(); i > 0; --i) {
        if (x[i - 1] != 0.0f) {
            return static_cast<std::int64_t>(i - 1);
        }
    }
    return -1;
}

// D-1 (user ruling 2026-10-22): the last non-zero sample of the engine's Sum tap (the voices, before the global chain).
struct SumTapObserver final : Krate::DSP::ProfundumEngineObserver {
    // Tap sample indices are the engine's absolute position; the render under test may start after earlier renders
    // on the same engine (clause 1), so the first chunk seen is the origin and readings are relative to it.
    std::int64_t origin = -1;
    std::int64_t lastSumNonZero = -1;
    void onEngineChunk(const Krate::DSP::ProfundumEngineTaps& t) noexcept override {
        if (origin < 0) {
            origin = static_cast<std::int64_t>(t.sample);
        }
        for (std::size_t i = t.n; i > 0; --i) {
            if (t.sumL[i - 1] != 0.0f || t.sumR[i - 1] != 0.0f) {
                lastSumNonZero =
                    std::max(lastSumNonZero, static_cast<std::int64_t>(t.sample + i - 1) - origin);
                return;
            }
        }
    }
};

// The number of samples from `from` whose magnitude exceeds `level`.
std::size_t aboveCount(std::span<const float> x, std::size_t from, float level) noexcept {
    std::size_t n = 0;
    for (std::size_t i = from; i < x.size(); ++i) {
        if (std::fabs(x[i]) > level) {
            ++n;
        }
    }
    return n;
}

// ---- T013 helpers ----

constexpr std::uint32_t kExpectedLatency = 64;
static_assert(kExpectedLatency == Krate::DSP::ProfundumEngine::kLimiterLookaheadSamples);
constexpr std::size_t kBlock = 512;
constexpr float kSentinel = 0.123f;

// setupProcessing with a chosen rate and sample size (no setActive).
Steinberg::tresult setupAt(::Profundum::Processor& p, double sampleRate,
                           Steinberg::int32 sampleSize = Steinberg::Vst::kSample32) {
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = sampleSize;
    setup.maxSamplesPerBlock = 4096;
    setup.sampleRate = sampleRate;
    return p.setupProcessing(setup);
}

void requireLatency64(::Profundum::Processor& p) {
    REQUIRE(p.getLatencySamples() == kExpectedLatency);
    REQUIRE(static_cast<std::size_t>(p.getLatencySamples()) ==
            Krate::DSP::ProfundumEngine::kLimiterLookaheadSamples);
}

// SC-014: the Latency sequence from "after initialize" up to (not including) terminate().
void runLatencySequence(ProcessorFixture& f) {
    ::Profundum::Processor& p = *f.proc;
    requireLatency64(p);  // after initialize, before any setup

    constexpr std::array<double, 6> kRates{22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
    for (const double sr : kRates) {
        INFO("fs = " << sr);
        REQUIRE(setupAt(p, sr) == Steinberg::kResultOk);
        requireLatency64(p);
        REQUIRE(p.engineForTest() != nullptr);
        REQUIRE(static_cast<std::size_t>(p.getLatencySamples()) == p.engineForTest()->latencySamples());
    }

    REQUIRE(setupAt(p, 44100.0) == Steinberg::kResultOk);  // a second setup
    requireLatency64(p);
    REQUIRE(static_cast<std::size_t>(p.getLatencySamples()) == p.engineForTest()->latencySamples());

    p.setActive(0u);
    p.setActive(1u);
    requireLatency64(p);

    constexpr std::array<Steinberg::Vst::ParamID, 8> kIds{
        ::Profundum::kOutputGainId,    ::Profundum::kVoiceModeId,      ::Profundum::kPolyphonyId,
        ::Profundum::kOutputCeilingId, ::Profundum::kMacroWeightId,    ::Profundum::kMacroDarkBrightId,
        ::Profundum::kMacroTightLooseId, ::Profundum::kMacroCleanViolentId,
    };
    Krate::Test::ParameterChanges pc;
    for (const Steinberg::Vst::ParamID id : kIds) {
        INFO("param id = " << id);
        pc.clear();
        pc.addChange(id, 0.7);
        REQUIRE(f.processNoOutputs(&pc) == Steinberg::kResultOk);
        requireLatency64(p);
    }

    REQUIRE(setupAt(p, 384000.0) == Steinberg::kResultFalse);  // a refused rate
    requireLatency64(p);
}

// One 512 block carrying NoteOn(E1) @0 with both channels pre-filled with the sentinel. Requires the not-ready
// path: kResultOk, every sample exact zero, no sentinel bits left, silenceFlags == 3, droppedEvents() unchanged.
void requireNotReadyBlock(ProcessorFixture& f) {
    std::fill_n(f.audioL(), kBlock, kSentinel);
    std::fill_n(f.audioR(), kBlock, kSentinel);
    f.reserveCapture(f.capturedL.size() + kBlock);

    REQUIRE(f.proc->engineForTest() != nullptr);
    const std::uint64_t droppedBefore = f.proc->engineForTest()->droppedEvents();

    Krate::Test::EventList ev;
    ev.addNoteOn(kE1, 100.0f / 127.0f, 0);
    REQUIRE(f.processBlock(kBlock, &ev) == Steinberg::kResultOk);

    const auto sentinelBits = std::bit_cast<std::uint32_t>(kSentinel);
    std::size_t sentinelHits = 0;
    std::size_t nonZero = 0;
    const float* l = f.audioL();
    const float* r = f.audioR();
    for (std::size_t i = 0; i < kBlock; ++i) {
        for (const float x : {l[i], r[i]}) {
            if (std::bit_cast<std::uint32_t>(x) == sentinelBits) {
                ++sentinelHits;
            }
            if (x != 0.0f) {
                ++nonZero;
            }
        }
    }
    REQUIRE(sentinelHits == 0u);
    REQUIRE(nonZero == 0u);
    REQUIRE(f.lastSilenceFlags == 3u);
    REQUIRE(f.proc->engineForTest()->droppedEvents() == droppedBefore);
}

// ---- T014 helpers ----

constexpr float kVelocity100 = 100.0f / 127.0f;

// Recovery arm (SC-017 SampleSize64 step 4, UnsupportedRate): a kSample32 setup at 48 kHz is accepted, and E1 held
// 1 s from the next block renders RMS >= -40 dBFS over [0.5 s, 1 s) on each channel.
void requireRecoveryRender(ProcessorFixture& f) {
    REQUIRE(setupAt(*f.proc, 48000.0) == Steinberg::kResultOk);
    const std::size_t start = f.capturedL.size();
    std::array<ProcessorFixture::ScriptedEvent, 1> script{};
    script[0].sample = 0;
    script[0].kind = ProcessorFixture::ScriptedEvent::Kind::NoteOn;
    script[0].pitch = kE1;
    script[0].velocity = kVelocity100;
    constexpr std::size_t kOneSecond = 48000;
    f.renderScript(script, kOneSecond, kBlock);
    REQUIRE(f.capturedL.size() == start + kOneSecond);
    const std::span<const float> l = std::span<const float>(f.capturedL).subspan(start);
    const std::span<const float> r = std::span<const float>(f.capturedR).subspan(start);
    const double rmsL = rmsDb(l, 24000, 48000);
    const double rmsR = rmsDb(r, 24000, 48000);
    INFO("recovery RMS [0.5 s, 1 s) L = " << rmsL << " dBFS, R = " << rmsR << " dBFS");
    REQUIRE(rmsL >= -40.0);
    REQUIRE(rmsR >= -40.0);
}

// A v1 state stream (plan 3.4: gain, voice mode, polyphony, ceiling, four macros), positioned at 0.
Steinberg::IPtr<Steinberg::MemoryStream> makeStateStream(float gain, std::int32_t voiceMode, std::int32_t polyphony,
                                                         float ceilingDb, const std::array<float, 4>& macros) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer s(stream, kLittleEndian);
        REQUIRE(s.writeInt32(::Profundum::kCurrentStateVersion));
        REQUIRE(s.writeFloat(gain));
        REQUIRE(s.writeInt32(voiceMode));
        REQUIRE(s.writeInt32(polyphony));
        REQUIRE(s.writeFloat(ceilingDb));
        for (const float m : macros) {
            REQUIRE(s.writeFloat(m));
        }
    }
    REQUIRE(std::cmp_equal(stream->getSize(), ::Profundum::kStateV1Bytes));
    REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
    return stream;
}

// The engine read-backs Guards requires unchanged (engine.h:398-416: the pending value of every setter).
struct EngineReadBack {
    Krate::DSP::ProfundumEngine::VoiceMode mode = Krate::DSP::ProfundumEngine::VoiceMode::Mono;
    std::size_t polyphony = 0;
    float ceilingDb = 0.0f;
    Krate::DSP::ProfundumMacroValues macros{};
    std::uint64_t dropped = 0;
};

EngineReadBack readBack(const ::Profundum::Processor& p) {
    const Krate::DSP::ProfundumEngine* e = p.engineForTest();
    REQUIRE(e != nullptr);
    EngineReadBack r;
    r.mode = e->voiceMode();
    r.polyphony = e->polyphony();
    r.ceilingDb = e->outputCeilingDb();
    r.macros = e->macros();
    r.dropped = e->droppedEvents();
    return r;
}

void requireSameReadBack(const EngineReadBack& a, const EngineReadBack& b) {
    REQUIRE(a.mode == b.mode);
    REQUIRE(a.polyphony == b.polyphony);
    REQUIRE(a.ceilingDb == b.ceilingDb);
    REQUIRE(a.macros.weight == b.macros.weight);
    REQUIRE(a.macros.darkBright == b.macros.darkBright);
    REQUIRE(a.macros.tightLoose == b.macros.tightLoose);
    REQUIRE(a.macros.cleanViolent == b.macros.cleanViolent);
    REQUIRE(a.dropped == b.dropped);
}

// The six FR-030 guard shapes (SC-017.1). Each process() call carries the events and must return kResultOk.
enum class GuardShape : std::uint8_t { NoOutputs, NullOutputs, NullChannelBuffers, OneChannel, ZeroSamples, NullChannel };

constexpr std::array<GuardShape, 6> kGuardShapes{GuardShape::NoOutputs,          GuardShape::NullOutputs,
                                                 GuardShape::NullChannelBuffers, GuardShape::OneChannel,
                                                 GuardShape::ZeroSamples,        GuardShape::NullChannel};

const char* guardName(GuardShape g) noexcept {
    switch (g) {
        case GuardShape::NoOutputs:
            return "numOutputs 0";
        case GuardShape::NullOutputs:
            return "outputs == nullptr";
        case GuardShape::NullChannelBuffers:
            return "channelBuffers32 == nullptr";
        case GuardShape::OneChannel:
            return "numChannels == 1";
        case GuardShape::ZeroSamples:
            return "numSamples == 0";
        case GuardShape::NullChannel:
            return "one null channel pointer";
    }
    return "?";
}

Steinberg::tresult processGuardShape(ProcessorFixture& f, GuardShape g, Steinberg::Vst::IEventList* ev) {
    std::array<float*, 2> channels{f.audioL(), f.audioR()};
    if (g == GuardShape::NullChannel) {
        channels[1] = nullptr;
    }

    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = (g == GuardShape::OneChannel) ? 1 : 2;
    outBus.silenceFlags = 0;
    outBus.channelBuffers32 = (g == GuardShape::NullChannelBuffers) ? nullptr : channels.data();

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = (g == GuardShape::ZeroSamples) ? 0 : static_cast<Steinberg::int32>(kBlock);
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = (g == GuardShape::NoOutputs) ? 0 : 1;
    data.outputs = (g == GuardShape::NullOutputs) ? nullptr : &outBus;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = ev;
    data.outputEvents = nullptr;
    data.processContext = nullptr;
    return f.proc->process(data);
}

// One event-free 512 block through the fixture; returns the block's start index in the capture.
std::size_t renderEventFreeBlock(ProcessorFixture& f) {
    const std::size_t start = f.capturedL.size();
    f.reserveCapture(start + kBlock);
    REQUIRE(f.processBlock(kBlock) == Steinberg::kResultOk);
    return start;
}

}  // namespace

TEST_CASE("Profundum_ProcessorLifecycle", "[profundum][processor]") {
    SECTION("IdleIsExactZero") {
        constexpr double kFs = 48000.0;
        DirectEngine d{kFs};

        // Clause 1: 4096 event-free samples are exact zero on both channels.
        constexpr std::size_t kIdleSamples = 4096;
        d.render(std::span<const DirectEvent>{}, kIdleSamples);
        REQUIRE(d.outL.size() == kIdleSamples);
        REQUIRE(d.outR.size() == kIdleSamples);
        const std::size_t idleNonZeroL = nonZeroCount(d.outL);
        const std::size_t idleNonZeroR = nonZeroCount(d.outR);
        WARN("P-5 idle: non-zero samples L = " << idleNonZeroL << ", R = " << idleNonZeroR
                                               << " of " << kIdleSamples);
        REQUIRE(idleNonZeroL == 0u);
        REQUIRE(idleNonZeroR == 0u);

        // Clause 2 (D-1, user ruling 2026-10-22): NoteOn(E1, 100) at 0, NoteOff at 24 000, render 96 000 (2 s).
        // The voices (Sum tap) are exactly zero by 0.125 s after the note-off (the 100 ms default release plus the
        // 32-sample grid latch and the declick; measured 0.100 s). The output then carries only the global 5 Hz DC
        // blocker's own decay of the last voice sample (tau 31.8 ms, flushed to exact zero by its denormal guard;
        // measured: < 1e-6 at 0.35 s, < 1e-12 at 0.79 s, exactly 0 at 1.011 s plus the 64-sample look-ahead; Phase 9's
        // VoiceEngine_ModeSwitch read the same tail at 1.3 s). Bars: |out| <= 1e-6 from 0.5 s after the note-off, and
        // exact zero from 1.25 s after it. The hidden Profundum_IdleTailProbe below prints every tap's decay.
        constexpr std::size_t kNoteOffAt = 24000;
        constexpr std::size_t kTotal = 96000;
        constexpr std::size_t kSumZeroBy = kNoteOffAt + 6000;     // 0.125 s
        constexpr std::size_t kLevelFrom = kNoteOffAt + 24000;    // 0.5 s
        constexpr float kLevel = 1.0e-6f;
        constexpr std::size_t kTailFrom = kNoteOffAt + 60000;     // 1.25 s
        SumTapObserver sumTap;
        d.e->setObserver(&sumTap);

        std::array<DirectEvent, 2> script{};
        script[0].sample = 0;
        script[0].kind = DirectEvent::Kind::NoteOn;
        script[0].note = static_cast<std::uint8_t>(kE1);
        script[0].velocity = 100;
        script[1].sample = kNoteOffAt;
        script[1].kind = DirectEvent::Kind::NoteOff;
        script[1].note = static_cast<std::uint8_t>(kE1);

        d.render(script, kTotal);
        REQUIRE(d.outL.size() == kTotal);
        REQUIRE(d.outR.size() == kTotal);

        // Non-vacuity arm: the held note must actually sound, or an all-zero
        // tail proves nothing.
        const double heldRmsDb = rmsDb(d.outL, 0, kNoteOffAt);
        WARN("P-5 held E1 RMS (L, 0 .. 0.5 s) = " << heldRmsDb << " dBFS");
        REQUIRE(heldRmsDb > -200.0);

        d.e->setObserver(nullptr);
        const std::int64_t lastL = lastNonZero(d.outL);
        const std::int64_t lastR = lastNonZero(d.outR);
        WARN("P-5 release: last non-zero Sum-tap sample = " << sumTap.lastSumNonZero << " (bar < " << kSumZeroBy
             << "); samples above 1e-6 from " << kLevelFrom << ": L " << aboveCount(d.outL, kLevelFrom, kLevel)
             << ", R " << aboveCount(d.outR, kLevelFrom, kLevel) << "; last non-zero output sample L = " << lastL
             << ", R = " << lastR << " (exact-zero tail starts at " << kTailFrom << ")");
        REQUIRE(sumTap.lastSumNonZero >= 0);
        REQUIRE(std::cmp_less(sumTap.lastSumNonZero, kSumZeroBy));
        REQUIRE(aboveCount(d.outL, kLevelFrom, kLevel) == 0u);
        REQUIRE(aboveCount(d.outR, kLevelFrom, kLevel) == 0u);
        REQUIRE(nonZeroCount(d.outL, kTailFrom) == 0u);
        REQUIRE(nonZeroCount(d.outR, kTailFrom) == 0u);
    }

    // ---- T013: setupProcessing, latency, setActive, guards, not-ready path ----

    SECTION("Latency") {  // SC-014, FR-032
        auto bare = std::make_unique<::Profundum::Processor>();
        REQUIRE(bare->getLatencySamples() == 0u);  // before initialize: no engine
        bare.reset();

        ProcessorFixture f;
        runLatencySequence(f);
        REQUIRE(f.proc->terminate() == Steinberg::kResultOk);
        REQUIRE(f.proc->getLatencySamples() == 0u);
    }

    SECTION("NoLatencyMessage") {  // SC-014: latency is never announced by IMessage
        // Both outlive the fixture (declared first), so a failed REQUIRE never leaves the processor holding a
        // dangling host or peer.
        Steinberg::Vst::HostApplication host;  // hostclasses.h:32
        CountingConnectionPoint peer;
        ProcessorFixture f(&host);
        REQUIRE(f.proc->connect(&peer) == Steinberg::kResultOk);

        runLatencySequence(f);
        REQUIRE(peer.notifications == 0);

        // Positive control: the connection carries a message when one is sent.
        REQUIRE(f.proc->sendTextMessage("probe") == Steinberg::kResultOk);
        REQUIRE(peer.notifications == 1);

        REQUIRE(f.proc->disconnect(&peer) == Steinberg::kResultOk);
        REQUIRE(f.proc->terminate() == Steinberg::kResultOk);
        REQUIRE(f.proc->getLatencySamples() == 0u);
        REQUIRE(peer.notifications == 1);
    }

    SECTION("NotReady") {  // SC-017.9 silence part, FR-030 guard 7
        ProcessorFixture f;  // initialised, never prepared
        requireNotReadyBlock(f);
    }

    SECTION("SampleSize64") {  // P-12, steps 1-3 (step 4, the recovery render, is T014's)
        ProcessorFixture neverPrepared;
        ProcessorFixture prepared;
        REQUIRE(prepared.prepare(48000.0) == Steinberg::kResultOk);

        for (ProcessorFixture* f : {&neverPrepared, &prepared}) {
            INFO((f == &prepared ? "prepared at 48 kHz" : "never prepared"));
            REQUIRE(setupAt(*f->proc, 48000.0, Steinberg::Vst::kSample64) == Steinberg::kResultFalse);
            requireNotReadyBlock(*f);
            requireLatency64(*f->proc);
            requireRecoveryRender(*f);  // step 4 (T014)
        }
    }

    SECTION("UnsupportedRate") {  // FR-023 step 2 (Q6), silence parts (the recovery render is T014's)
        const std::array<double, 3> kRefused{384000.0, 11025.0, makeNaND()};
        for (const double sr : kRefused) {
            ProcessorFixture neverPrepared;
            ProcessorFixture prepared;
            REQUIRE(prepared.prepare(48000.0) == Steinberg::kResultOk);
            for (ProcessorFixture* f : {&neverPrepared, &prepared}) {
                INFO("rate " << sr << (f == &prepared ? ", prepared at 48 kHz" : ", never prepared"));
                REQUIRE(setupAt(*f->proc, sr) == Steinberg::kResultFalse);
                requireNotReadyBlock(*f);
                requireLatency64(*f->proc);
                requireRecoveryRender(*f);  // the recovery arm (T014)
            }
        }

        ProcessorFixture edges;
        REQUIRE(setupAt(*edges.proc, 22050.0) == Steinberg::kResultOk);
        REQUIRE(setupAt(*edges.proc, 192000.0) == Steinberg::kResultOk);
    }

    SECTION("PushInSetup") {  // SC-017.6, FR-023.5: setupProcessing pushes the latched values with no process()
        ProcessorFixture f;  // never prepared
        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        {
            Steinberg::IBStreamer s(stream, kLittleEndian);
            REQUIRE(s.writeInt32(::Profundum::kCurrentStateVersion));
            REQUIRE(s.writeFloat(1.0f));     // gain
            REQUIRE(s.writeInt32(1));        // voice mode: Poly
            REQUIRE(s.writeInt32(2));        // polyphony
            REQUIRE(s.writeFloat(-6.0f));    // ceiling dBTP
            REQUIRE(s.writeFloat(0.1f));     // macros
            REQUIRE(s.writeFloat(0.2f));
            REQUIRE(s.writeFloat(0.3f));
            REQUIRE(s.writeFloat(0.4f));
        }
        REQUIRE(std::cmp_equal(stream->getSize(), ::Profundum::kStateV1Bytes));
        REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
        REQUIRE(f.proc->setState(stream) == Steinberg::kResultOk);

        REQUIRE(setupAt(*f.proc, 48000.0) == Steinberg::kResultOk);  // no process() call follows

        const Krate::DSP::ProfundumEngine* e = f.proc->engineForTest();
        REQUIRE(e != nullptr);
        REQUIRE(e->polyphony() == 2u);
        REQUIRE(e->voiceMode() == Krate::DSP::ProfundumEngine::VoiceMode::Poly);
        REQUIRE(e->outputCeilingDb() == -6.0f);
        const Krate::DSP::ProfundumMacroValues m = e->macros();
        REQUIRE(m.weight == 0.1f);
        REQUIRE(m.darkBright == 0.2f);
        REQUIRE(m.tightLoose == 0.3f);
        REQUIRE(m.cleanViolent == 0.4f);
    }

    SECTION("NoMidiInterfaces") {  // SC-017.8, FR-019: regression guard (controller.h declares neither base)
        Steinberg::IPtr<::Profundum::Controller> ctl = Steinberg::owned(new ::Profundum::Controller());
        REQUIRE(ctl->initialize(nullptr) == Steinberg::kResultOk);
        void* obj = nullptr;
        REQUIRE(ctl->queryInterface(Steinberg::Vst::IMidiMapping::iid, &obj) == Steinberg::kNoInterface);
        obj = nullptr;
        REQUIRE(ctl->queryInterface(Steinberg::Vst::INoteExpressionController::iid, &obj) ==
                Steinberg::kNoInterface);
        REQUIRE(ctl->terminate() == Steinberg::kResultOk);
    }

    // ---- T014: the render loop ----

    SECTION("LatencyIsReal") {  // SC-014 reality check: the onset lags the NoteOn by the 64-sample look-ahead
        ProcessorFixture f;
        REQUIRE(f.prepare(48000.0) == Steinberg::kResultOk);
        f.reserveCapture(kBlock);
        Krate::Test::EventList ev;
        ev.addNoteOn(kE1, kVelocity100, 0);
        REQUIRE(f.processBlock(kBlock, &ev) == Steinberg::kResultOk);
        REQUIRE(f.capturedL.size() == kBlock);

        std::int64_t onset = -1;
        for (std::size_t i = 0; i < kBlock; ++i) {
            if (f.capturedL[i] != 0.0f || f.capturedR[i] != 0.0f) {
                onset = static_cast<std::int64_t>(i);
                break;
            }
        }
        WARN("SC-014 LatencyIsReal: first non-zero sample index = " << onset << " (bar [64, 68])");
        REQUIRE(onset >= 64);
        REQUIRE(onset <= 68);
    }

    SECTION("Guards") {  // SC-017.1, FR-030 guards 2-6
        ProcessorFixture f;
        REQUIRE(f.prepare(48000.0) == Steinberg::kResultOk);
        static_cast<void>(renderEventFreeBlock(f));  // warm: no note sounding
        const EngineReadBack before = readBack(*f.proc);

        Krate::Test::EventList ev;
        ev.addNoteOn(kE1, kVelocity100, 0);
        for (const GuardShape g : kGuardShapes) {
            INFO("guard shape: " << guardName(g));
            REQUIRE(processGuardShape(f, g, &ev) == Steinberg::kResultOk);
            const std::size_t start = renderEventFreeBlock(f);
            REQUIRE(nonZeroCount(std::span<const float>(f.capturedL), start) == 0u);
            REQUIRE(nonZeroCount(std::span<const float>(f.capturedR), start) == 0u);
            requireSameReadBack(readBack(*f.proc), before);
        }

        // Non-vacuity arm: the same NoteOn in a VALID block makes the next event-free block sound.
        f.reserveCapture(f.capturedL.size() + kBlock);
        REQUIRE(f.processBlock(kBlock, &ev) == Steinberg::kResultOk);
        const std::size_t start = renderEventFreeBlock(f);
        const float peak = std::max(peakOf(std::span<const float>(f.capturedL).subspan(start)),
                                    peakOf(std::span<const float>(f.capturedR).subspan(start)));
        WARN("SC-017.1 Guards control: peak of the block after a valid NoteOn = " << peak);
        REQUIRE(peak > 1.0e-4f);
    }

    SECTION("Reset") {  // SC-017.4, FR-031
        constexpr std::size_t kHoldSamples = 47 * kBlock;  // ~0.5 s
        std::array<ProcessorFixture::ScriptedEvent, 1> script{};
        script[0].sample = 0;
        script[0].kind = ProcessorFixture::ScriptedEvent::Kind::NoteOn;
        script[0].pitch = kE1;
        script[0].velocity = kVelocity100;

        ProcessorFixture f;
        REQUIRE(f.prepare(48000.0) == Steinberg::kResultOk);
        f.renderScript(script, kHoldSamples, kBlock);
        REQUIRE(f.proc->setActive(false) == Steinberg::kResultOk);
        REQUIRE(f.proc->setActive(true) == Steinberg::kResultOk);
        const std::size_t start = renderEventFreeBlock(f);
        const std::size_t nzL = nonZeroCount(std::span<const float>(f.capturedL), start);
        const std::size_t nzR = nonZeroCount(std::span<const float>(f.capturedR), start);

        // Non-vacuity: without the setActive pair the same block is NOT silent.
        ProcessorFixture control;
        REQUIRE(control.prepare(48000.0) == Steinberg::kResultOk);
        control.renderScript(script, kHoldSamples, kBlock);
        const std::size_t cStart = renderEventFreeBlock(control);
        const float controlPeak = std::max(peakOf(std::span<const float>(control.capturedL).subspan(cStart)),
                                           peakOf(std::span<const float>(control.capturedR).subspan(cStart)));
        WARN("SC-017.4 Reset: non-zero samples after reset L = " << nzL << ", R = " << nzR
                                                                  << "; control block peak = " << controlPeak);
        REQUIRE(nzL == 0u);
        REQUIRE(nzR == 0u);
        REQUIRE(f.proc->engineForTest() != nullptr);
        REQUIRE(f.proc->engineForTest()->stateFinite());
        REQUIRE(controlPeak > 1.0e-4f);
    }

    SECTION("GainSnap") {  // SC-017.5, plan 3.1: snap on the first block after setupProcessing / setActive(true)
        ProcessorFixture f;  // never prepared
        REQUIRE(f.proc->setState(makeStateStream(0.5f, 0, 4, -1.0f, {0.5f, 0.5f, 0.5f, 0.5f})) ==
                Steinberg::kResultOk);
        REQUIRE(setupAt(*f.proc, 48000.0) == Steinberg::kResultOk);
        static_cast<void>(renderEventFreeBlock(f));
        REQUIRE(f.proc->outputGainValueForTest() == 0.5f);

        REQUIRE(f.proc->setState(makeStateStream(0.25f, 0, 4, -1.0f, {0.5f, 0.5f, 0.5f, 0.5f})) ==
                Steinberg::kResultOk);
        REQUIRE(f.proc->setActive(false) == Steinberg::kResultOk);
        REQUIRE(f.proc->setActive(true) == Steinberg::kResultOk);
        static_cast<void>(renderEventFreeBlock(f));
        REQUIRE(f.proc->outputGainValueForTest() == 0.25f);
    }

    // ---- T018: zero allocation (FR-029, FR-031) ----

    SECTION("ActivateNoAlloc") {  // SC-017.7: setActive(true) allocates nothing
        ProcessorFixture f;
        REQUIRE(f.prepare(48000.0) == Steinberg::kResultOk);
        REQUIRE(f.proc->setActive(false) == Steinberg::kResultOk);

        ::Profundum::Processor& p = *f.proc;
        Steinberg::tresult activated = Steinberg::kResultFalse;
        std::size_t allocations = 0;
        {
            TestHelpers::AllocationScope scope;
            activated = p.setActive(1u);
            allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        WARN("SC-017.7 ActivateNoAlloc: allocations in setActive(true) = " << allocations);
        REQUIRE(activated == Steinberg::kResultOk);
        REQUIRE(allocations == 0u);
    }

    SECTION("ProcessNoAlloc") {  // SC-007, FR-029: 2 s of notes, automation and tempo allocate nothing
        // 1. Liveness probe: the detector counts in this binary (test_main.cpp includes the operator overrides).
        //    The pointer is held in a volatile so the new/delete pair cannot be elided ([expr.new]/10).
        std::size_t probeCount = 0;
        {
            TestHelpers::AllocationScope s;
            int* volatile probe = new int(7);  // NOLINT(cppcoreguidelines-owning-memory)
            probeCount = TestHelpers::AllocationDetector::instance().getAllocationCount();
            delete probe;  // NOLINT(cppcoreguidelines-owning-memory)
        }
        REQUIRE(probeCount >= 1u);

        // 2. Prepare and render one warm-up block.
        ProcessorFixture f;
        REQUIRE(f.prepare(48000.0) == Steinberg::kResultOk);
        static_cast<void>(renderEventFreeBlock(f));

        // 3. Build 188 blocks of 512 (~2 s at 48 kHz) BEFORE the scope.
        constexpr std::size_t kBlocks = 188;
        std::vector<Krate::Test::EventList> events(kBlocks);
        std::vector<MultiParamChanges> params(kBlocks);
        for (MultiParamChanges& pc : params) {
            pc.reserve(8);
        }
        const auto point = [&params](std::size_t block, Steinberg::Vst::ParamID id, Steinberg::int32 offset,
                                     double value) { params[block].queueFor(id).addTestPoint(offset, value); };
        const auto chordOn = [&events](std::size_t block, Steinberg::int32 offset) {
            for (std::size_t k = 0; k < kChord4.size(); ++k) {
                events[block].addNoteOn(kChord4[k], kVelocity100, offset + static_cast<Steinberg::int32>(10 * k));
            }
        };
        const auto chordOff = [&events](std::size_t block, Steinberg::int32 offset) {
            for (const Steinberg::int16 pitch : kChord4) {
                events[block].addNoteOff(pitch, offset);
            }
        };
        const auto allMacros = [&point](std::size_t block, Steinberg::int32 offset, double value) {
            point(block, ::Profundum::kMacroWeightId, offset, value);
            point(block, ::Profundum::kMacroDarkBrightId, offset, value);
            point(block, ::Profundum::kMacroTightLooseId, offset, value);
            point(block, ::Profundum::kMacroCleanViolentId, offset, value);
        };

        // Mono E1; Poly x4 chord under automation; back to Mono with the chord (legato) and E1.
        events[0].addNoteOn(kE1, kVelocity100, 0);
        allMacros(10, 128, 0.75);
        point(10, ::Profundum::kOutputGainId, 200, 0.5);
        events[20].addNoteOff(kE1, 100);
        point(20, ::Profundum::kVoiceModeId, 0, 1.0);        // Poly
        point(20, ::Profundum::kPolyphonyId, 0, 3.0 / 7.0);  // 4 voices
        chordOn(21, 0);
        point(40, ::Profundum::kOutputCeilingId, 64, 0.25);
        point(40, ::Profundum::kOutputCeilingId, 300, 0.75);  // the last point wins
        allMacros(50, 0, 1.0);
        point(50, ::Profundum::kPolyphonyId, 256, 1.0);       // 8 voices while the chord sounds
        point(60, ::Profundum::kOutputGainId, 0, 1.0);
        chordOff(80, 256);
        point(90, ::Profundum::kVoiceModeId, 0, 0.0);         // Mono
        events[91].addNoteOn(kE1, kVelocity100, 17);
        allMacros(100, 300, 0.25);
        events[120].addNoteOff(kE1, 0);
        chordOn(121, 5);                                      // mono legato across the chord
        point(130, ::Profundum::kPolyphonyId, 0, 3.0 / 7.0);
        point(130, ::Profundum::kOutputCeilingId, 0, 1.0);
        chordOff(160, 400);
        events[170].addNoteOn(kE1, kVelocity100, 511);
        events[186].addNoteOff(kE1, 0);

        // A ProcessContext with kTempoValid: 90 BPM for the first half, then 140.
        std::vector<Steinberg::Vst::ProcessContext> contexts(kBlocks);
        for (std::size_t b = 0; b < kBlocks; ++b) {
            Steinberg::Vst::ProcessContext& c = contexts[b];
            c.state = Steinberg::Vst::ProcessContext::kTempoValid;
            c.sampleRate = 48000.0;
            c.tempo = (b < kBlocks / 2) ? 90.0 : 140.0;
            c.projectTimeSamples = static_cast<Steinberg::Vst::TSamples>(b * kBlock);
        }

        std::array<float*, 2> channels{f.audioL(), f.audioR()};
        std::vector<Steinberg::Vst::AudioBusBuffers> buses(kBlocks);
        std::vector<Steinberg::Vst::ProcessData> blocks(kBlocks);
        for (std::size_t b = 0; b < kBlocks; ++b) {
            buses[b].numChannels = 2;
            buses[b].silenceFlags = 0;
            buses[b].channelBuffers32 = channels.data();
            Steinberg::Vst::ProcessData& d = blocks[b];
            d.processMode = Steinberg::Vst::kRealtime;
            d.symbolicSampleSize = Steinberg::Vst::kSample32;
            d.numSamples = static_cast<Steinberg::int32>(kBlock);
            d.numInputs = 0;
            d.inputs = nullptr;
            d.numOutputs = 1;
            d.outputs = &buses[b];
            d.inputParameterChanges = &params[b];
            d.outputParameterChanges = nullptr;
            d.inputEvents = &events[b];
            d.outputEvents = nullptr;
            d.processContext = &contexts[b];
        }

        // 4. One scope around all 188 process() calls. No Catch macro inside.
        ::Profundum::Processor& p = *f.proc;
        const float* outL = f.audioL();
        const float* outR = f.audioR();
        std::size_t notOk = 0;
        float peak = 0.0f;
        std::size_t allocations = 0;
        {
            TestHelpers::AllocationScope scope;
            for (std::size_t b = 0; b < kBlocks; ++b) {
                if (p.process(blocks[b]) != Steinberg::kResultOk) {
                    ++notOk;
                }
                for (std::size_t i = 0; i < kBlock; ++i) {
                    peak = std::max({peak, std::fabs(outL[i]), std::fabs(outR[i])});
                }
            }
            allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }

        WARN("SC-007 ProcessNoAlloc: allocations over " << kBlocks << " x " << kBlock << " samples = "
                                                         << allocations << " (probe " << probeCount
                                                         << "); output peak = " << peak);
        REQUIRE(allocations == 0u);
        REQUIRE(notOk == 0u);
        // Non-vacuity: the blocks rendered sound and the automation reached the engine.
        REQUIRE(peak > 1.0e-4f);
        const Krate::DSP::ProfundumEngine* e = p.engineForTest();
        REQUIRE(e != nullptr);
        REQUIRE(e->voiceMode() == Krate::DSP::ProfundumEngine::VoiceMode::Mono);
        REQUIRE(e->polyphony() == 4u);
        const Krate::DSP::ProfundumMacroValues m = e->macros();
        REQUIRE(m.weight == 0.25f);
        REQUIRE(m.darkBright == 0.25f);
        REQUIRE(m.tightLoose == 0.25f);
        REQUIRE(m.cleanViolent == 0.25f);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// D-1 probe (hidden): the engine taps after NoteOff — which stage still carries the tail at 1 s, and its decay shape.
// ---------------------------------------------------------------------------------------------------------------------
namespace {

struct TailObserver final : Krate::DSP::ProfundumEngineObserver {
    static constexpr std::array<float, 7> kLevels{1e-3f, 1e-6f, 1e-12f, 1e-20f, 1e-30f, 1e-36f, 0.0f};
    struct Tap { std::array<std::int64_t, 7> last{}; Tap() { last.fill(-1); } };
    Tap sum, dyn, field, dc, out, gAmp, core, anchor;
    std::int64_t lastVoiceChunk = -1;

    static void update(Tap& t, std::span<const float> x, std::uint64_t base) noexcept {
        for (std::size_t k = 0; k < kLevels.size(); ++k) {
            for (std::size_t i = x.size(); i > 0; --i) {
                const float a = std::fabs(x[i - 1]);
                if (a > kLevels[k]) { t.last[k] = std::max(t.last[k], static_cast<std::int64_t>(base + i - 1)); break; }
            }
        }
    }
    void onVoiceChunk(const Krate::DSP::ProfundumVoiceTaps& t) noexcept override {
        update(gAmp, t.gAmp, t.sample); update(core, t.coreL, t.sample); update(anchor, t.anchorL, t.sample);
        lastVoiceChunk = std::max(lastVoiceChunk, static_cast<std::int64_t>(t.sample + t.n - 1));
    }
    void onEngineChunk(const Krate::DSP::ProfundumEngineTaps& t) noexcept override {
        update(sum, t.sumL, t.sample); update(dyn, t.dynL, t.sample); update(field, t.fieldL, t.sample);
        update(dc, t.dcL, t.sample); update(out, t.outL, t.sample);
    }
    static void print(const char* name, const Tap& t) {
        std::cout << "  " << name << ": last >1e-3 " << t.last[0] << ", >1e-6 " << t.last[1] << ", >1e-12 " << t.last[2]
                  << ", >1e-20 " << t.last[3] << ", >1e-30 " << t.last[4] << ", >1e-36 " << t.last[5] << ", !=0 " << t.last[6] << "\n";
    }
};

}  // namespace

TEST_CASE("Profundum_IdleTailProbe", "[.][profundum][probe]") {
    constexpr double kFs = 48000.0;
    DirectEngine d{kFs};
    TailObserver obs;
    d.e->setObserver(&obs);
    constexpr std::size_t kNoteOffAt = 24000;
    constexpr std::size_t kTotal = 96000;
    std::array<DirectEvent, 2> script{};
    script[0].sample = 0; script[0].kind = DirectEvent::Kind::NoteOn; script[0].note = static_cast<std::uint8_t>(kE1); script[0].velocity = 100;
    script[1].sample = kNoteOffAt; script[1].kind = DirectEvent::Kind::NoteOff; script[1].note = static_cast<std::uint8_t>(kE1);
    d.render(script, kTotal);
    std::cout << "D-1 PROBE NoteOff at " << kNoteOffAt << ", render " << kTotal << ", last voice chunk sample " << obs.lastVoiceChunk << "\n";
    TailObserver::print("voice gAmp ", obs.gAmp);
    TailObserver::print("voice coreL", obs.core);
    TailObserver::print("voice anchL", obs.anchor);
    TailObserver::print("engine sumL", obs.sum);
    TailObserver::print("engine dynL", obs.dyn);
    TailObserver::print("engine fldL", obs.field);
    TailObserver::print("engine dcL ", obs.dc);
    TailObserver::print("engine outL", obs.out);
    TailObserver out2;
    TailObserver::update(out2.out, std::span<const float>(d.outL), 0);
    TailObserver::print("gained outL", out2.out);
    // Decay shape of the output: |x| at a few samples past the voice's last chunk.
    const std::int64_t v = obs.lastVoiceChunk;
    for (std::int64_t k : {0, 64, 128, 1000, 10000, 20000, 30000, 40000, 47000, 48000, 48400, 48500}) {
        const std::int64_t i = v + k;
        if (i >= 0 && std::cmp_less(i, kTotal))
            std::cout << "  out[" << i << "] = " << d.outL[static_cast<std::size_t>(i)] << "\n";
    }
    CHECK(true);
}
