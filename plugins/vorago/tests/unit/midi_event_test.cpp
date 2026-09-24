// ==============================================================================
// Vorago - MIDI event translation tests (SC-022, SC-008 (1))
// ==============================================================================
// Vorago_MidiEventTranslation (T015). Reads the engine through engineForTest()
// (FR-026a). Nine SC-022 clauses plus SC-008 (1)'s block-size invariance.
//
// Clause 8 (OutOfOrderEventsAreSorted) is the P-3 detector: an unsorted copy of
// the host list fires the @400 note at cursor 0. Clause 9
// (OverflowQueueDropsNothing) covers plan 3.2 step 3: events past the 1024-slot
// sorted queue are read in list order at non-decreasing effective offsets.
//
// Every render comparison is a max-abs tolerance (1e-5), never a bit-exact
// golden; each equality carries a >= 1e-4 peak precondition (P-6) and the
// timing clause a negative control. If a precondition misses, LENGTHEN the
// render - never touch a threshold.
// ==============================================================================

#include "vorago_test_fixture.h"

#include <render_fingerprint.h>  // SC-008 (1): WARN-only secondary check
#include <vst_event_list.h>

#include <krate/dsp/systems/voice_allocator.h>
#include <krate/dsp/systems/vorago_engine.h>

#include <pluginterfaces/vst/ivstevents.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::VoiceState;
using Krate::DSP::VoragoEngine;

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr float kVelocity100 = 100.0f / 127.0f;
constexpr float kEqualTolerance = 1.0e-5f;
constexpr float kAudibleFloor = 1.0e-4f;

// Clause 5 / 7 / 8 window: [3072 latency + 300 offset, end). The render runs
// 1.5 s past it (the spec's minimum is 1 s; lengthen further if the peak
// precondition ever misses).
constexpr std::size_t kWindowStart = 3072 + 300;
constexpr std::size_t kTimingSamples = kWindowStart + 72000;

// SC-008 (1)
constexpr std::size_t kInvarianceSamples = 192000;  // 4 s at 48 kHz
constexpr std::size_t kLatency = 3072;
static_assert(65 % 64 != 0, "SC-008 (1): a 65-sample partition boundary falls inside a 64-sample control chunk");

struct Stereo {
    std::vector<float> l;
    std::vector<float> r;
};

[[nodiscard]] Steinberg::Vst::Event makeNoteOn(Steinberg::int16 pitch, float velocity) {
    Steinberg::Vst::Event e{};
    e.type = Steinberg::Vst::Event::kNoteOnEvent;
    e.noteOn.channel = 0;
    e.noteOn.pitch = pitch;
    e.noteOn.velocity = velocity;
    e.noteOn.noteId = -1;
    return e;
}

[[nodiscard]] Steinberg::Vst::Event makeNoteOff(Steinberg::int16 pitch) {
    Steinberg::Vst::Event e{};
    e.type = Steinberg::Vst::Event::kNoteOffEvent;
    e.noteOff.channel = 0;
    e.noteOff.pitch = pitch;
    e.noteOff.noteId = -1;
    return e;
}

[[nodiscard]] const VoragoEngine& engineOf(const VoragoTest::ProcessorFixture& fx) {
    const VoragoEngine* engine = fx.proc->engineForTest();
    REQUIRE(engine != nullptr);
    return *engine;
}

[[nodiscard]] std::size_t countSlots(const VoragoEngine& engine, VoiceState state) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
        if (engine.getVoiceState(i) == state) {
            ++n;
        }
    }
    return n;
}

[[nodiscard]] std::array<VoiceState, VoragoEngine::kMaxVoices> slotStates(const VoragoEngine& engine) {
    std::array<VoiceState, VoragoEngine::kMaxVoices> states{};
    for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
        states[i] = engine.getVoiceState(i);
    }
    return states;
}

// The single slot reading `state`; REQUIREs there is exactly one.
[[nodiscard]] std::size_t onlySlot(const VoragoEngine& engine, VoiceState state) {
    REQUIRE(countSlots(engine, state) == 1u);
    for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
        if (engine.getVoiceState(i) == state) {
            return i;
        }
    }
    return VoragoEngine::kMaxVoices;
}

// SC-022 (2) precondition. VoragoVoice retires on its level detector ALONE
// (vorago_voice.h:938-940): the quiescent counter starts AT the retire count
// (clearRunState, :1571) and is only reset by a chunk whose level reaches
// kTailSilenceThreshold (updateLevel, :2057-2061). A note released while the 20 s
// attack is still below -90 dBFS is therefore retired at the next control step,
// so "Releasing with the count unchanged" is only observable once the voice has
// been audible. Holds the note in 512 blocks until the slot's level clears the
// threshold (capped at kMaxHoldBlocks) and REQUIREs that it did.
constexpr std::size_t kMaxHoldBlocks = 375;  // 4 s at 48 kHz / 512

void holdUntilAudible(VoragoTest::ProcessorFixture& fx, std::size_t slot) {
    std::size_t held = 0;
    while (engineOf(fx).getVoiceLevel(slot) < Krate::DSP::VoragoVoice::kTailSilenceThreshold
           && held < kMaxHoldBlocks) {
        REQUIRE(fx.processBlock(kBlock, nullptr) == Steinberg::kResultOk);
        ++held;
    }
    INFO("held blocks = " << held << ", level = " << engineOf(fx).getVoiceLevel(slot));
    REQUIRE(engineOf(fx).getVoiceLevel(slot) >= Krate::DSP::VoragoVoice::kTailSilenceThreshold);
    REQUIRE(engineOf(fx).getVoiceState(slot) == VoiceState::Active);
}

[[nodiscard]] Stereo takeCapture(VoragoTest::ProcessorFixture& fx) {
    return Stereo{.l = std::move(fx.capturedL), .r = std::move(fx.capturedR)};
}

// Fresh prepared processor (48k / 512); `firstEvents` rides on block 0 only;
// `total` samples in 512 blocks (last one truncated).
[[nodiscard]] Stereo renderWithFirstBlockEvents(Steinberg::Vst::IEventList* firstEvents,
                                                std::size_t total) {
    VoragoTest::ProcessorFixture fx;
    fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
    fx.reserveCapture(total);
    std::size_t done = 0;
    bool first = true;
    while (done < total) {
        const std::size_t n = std::min(kBlock, total - done);
        REQUIRE(fx.processBlock(n, first ? firstEvents : nullptr) == Steinberg::kResultOk);
        first = false;
        done += n;
    }
    return takeCapture(fx);
}

[[nodiscard]] float windowPeak(const Stereo& s, std::size_t from) {
    const std::span<const float> l(s.l);
    const std::span<const float> r(s.r);
    return std::max(VoragoTest::peakOf(l.subspan(from)), VoragoTest::peakOf(r.subspan(from)));
}

[[nodiscard]] float windowDiff(const Stereo& a, const Stereo& b, std::size_t from,
                               std::size_t to = VoragoTest::kToEnd) {
    const float dl = VoragoTest::maxAbsDiff(std::span<const float>(a.l), std::span<const float>(b.l),
                                            from, to);
    const float dr = VoragoTest::maxAbsDiff(std::span<const float>(a.r), std::span<const float>(b.r),
                                            from, to);
    return std::max(dl, dr);
}

}  // namespace

TEST_CASE("Vorago_MidiEventTranslation", "[vorago][processor][midi]") {
    SECTION("NoteOnActivates") {  // SC-022 (1)
        VoragoTest::ProcessorFixture fx;
        fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
        fx.reserveCapture(kBlock);

        Krate::Test::EventList ev;
        ev.addNoteOn(48, kVelocity100, 0);
        REQUIRE(fx.processBlock(kBlock, &ev) == Steinberg::kResultOk);

        const VoragoEngine& engine = engineOf(fx);
        REQUIRE(engine.getActiveVoiceCount() == 1u);
        REQUIRE(countSlots(engine, VoiceState::Active) == 1u);
    }

    SECTION("NoteOffReleases") {  // SC-022 (2)
        SECTION("NoteOff event") {
            VoragoTest::ProcessorFixture fx;
            fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
            fx.reserveCapture((kMaxHoldBlocks + 2) * kBlock);

            Krate::Test::EventList on;
            on.addNoteOn(48, kVelocity100, 0);
            REQUIRE(fx.processBlock(kBlock, &on) == Steinberg::kResultOk);
            const std::size_t slot = onlySlot(engineOf(fx), VoiceState::Active);
            holdUntilAudible(fx, slot);

            Krate::Test::EventList off;
            off.addNoteOff(48, 0);
            REQUIRE(fx.processBlock(kBlock, &off) == Steinberg::kResultOk);

            const VoragoEngine& engine = engineOf(fx);
            REQUIRE(engine.getVoiceState(slot) == VoiceState::Releasing);
            REQUIRE(engine.getActiveVoiceCount() == 1u);  // unchanged inside the 45 s release
        }
        SECTION("NoteOn with velocity 0") {
            VoragoTest::ProcessorFixture fx;
            fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
            fx.reserveCapture((kMaxHoldBlocks + 2) * kBlock);

            Krate::Test::EventList on;
            on.addNoteOn(48, kVelocity100, 0);
            REQUIRE(fx.processBlock(kBlock, &on) == Steinberg::kResultOk);
            const std::size_t slot = onlySlot(engineOf(fx), VoiceState::Active);
            holdUntilAudible(fx, slot);

            Krate::Test::EventList zero;
            zero.addNoteOn(48, 0.0f, 0);
            REQUIRE(fx.processBlock(kBlock, &zero) == Steinberg::kResultOk);

            const VoragoEngine& engine = engineOf(fx);
            REQUIRE(engine.getVoiceState(slot) == VoiceState::Releasing);
            REQUIRE(engine.getActiveVoiceCount() == 1u);
        }
    }

    SECTION("OutOfRangePitchDropped") {  // SC-022 (3)
        VoragoTest::ProcessorFixture fx;
        fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
        fx.reserveCapture(kBlock);

        Krate::Test::EventList ev;
        ev.addNoteOn(128, kVelocity100, 0);
        ev.addNoteOn(-1, kVelocity100, 10);
        REQUIRE(fx.processBlock(kBlock, &ev) == Steinberg::kResultOk);

        const VoragoEngine& engine = engineOf(fx);
        REQUIRE(engine.getActiveVoiceCount() == 0u);
        REQUIRE(countSlots(engine, VoiceState::Idle) == VoragoEngine::kMaxVoices);
    }

    SECTION("NonNoteEventsIgnored") {  // SC-022 (4)
        VoragoTest::ProcessorFixture fx;
        fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
        fx.reserveCapture(2 * kBlock);

        Krate::Test::EventList on;
        on.addNoteOn(48, kVelocity100, 0);
        REQUIRE(fx.processBlock(kBlock, &on) == Steinberg::kResultOk);

        const std::size_t countBefore = engineOf(fx).getActiveVoiceCount();
        const auto statesBefore = slotStates(engineOf(fx));
        REQUIRE(countBefore == 1u);

        Steinberg::Vst::Event pressure{};
        pressure.type = Steinberg::Vst::Event::kPolyPressureEvent;
        pressure.sampleOffset = 10;
        pressure.polyPressure.channel = 0;
        pressure.polyPressure.pitch = 48;
        pressure.polyPressure.pressure = 0.5f;
        pressure.polyPressure.noteId = -1;

        Steinberg::Vst::Event dataEvent{};
        dataEvent.type = Steinberg::Vst::Event::kDataEvent;
        dataEvent.sampleOffset = 20;
        dataEvent.data.size = 0;
        dataEvent.data.type = Steinberg::Vst::DataEvent::kMidiSysEx;
        dataEvent.data.bytes = nullptr;

        Krate::Test::EventList ev;
        ev.addEvent(pressure);
        ev.addEvent(dataEvent);
        REQUIRE(fx.processBlock(kBlock, &ev) == Steinberg::kResultOk);

        const VoragoEngine& engine = engineOf(fx);
        REQUIRE(engine.getActiveVoiceCount() == countBefore);
        REQUIRE((slotStates(engine) == statesBefore));
    }

    SECTION("OffsetTiming") {  // SC-022 (5)
        Krate::Test::EventList evA;
        evA.addNoteOn(48, kVelocity100, 300);
        const Stereo a = renderWithFirstBlockEvents(&evA, kTimingSamples);

        // B: a 300-sample block, then the note @0 of the next 512 block, then 512s.
        Stereo b;
        {
            VoragoTest::ProcessorFixture fx;
            fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
            fx.reserveCapture(kTimingSamples);
            REQUIRE(fx.processBlock(300) == Steinberg::kResultOk);
            Krate::Test::EventList evB;
            evB.addNoteOn(48, kVelocity100, 0);
            std::size_t done = 300;
            bool first = true;
            while (done < kTimingSamples) {
                const std::size_t n = std::min(kBlock, kTimingSamples - done);
                REQUIRE(fx.processBlock(n, first ? &evB : nullptr) == Steinberg::kResultOk);
                first = false;
                done += n;
            }
            b = takeCapture(fx);
        }

        Krate::Test::EventList evC;
        evC.addNoteOn(48, kVelocity100, 0);
        const Stereo c = renderWithFirstBlockEvents(&evC, kTimingSamples);

        const float peakA = windowPeak(a, kWindowStart);
        const float diffAB = windowDiff(a, b, kWindowStart);
        const float diffAC = windowDiff(a, c, kWindowStart);
        WARN("SC-022 (5) peak(A)=" << peakA << " maxAbsDiff(A,B)=" << diffAB
                                   << " maxAbsDiff(A,C)=" << diffAC);
        REQUIRE(peakA >= kAudibleFloor);        // P-6 precondition
        REQUIRE(diffAB <= kEqualTolerance);
        REQUIRE(diffAC > kEqualTolerance);      // negative control: a 300-sample error shows
    }

    SECTION("TinyVelocityIsNoteOn") {  // SC-022 (6)
        VoragoTest::ProcessorFixture fx;
        fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
        fx.reserveCapture(kBlock);

        Krate::Test::EventList ev;
        ev.addNoteOn(48, 0.003f, 0);
        REQUIRE(fx.processBlock(kBlock, &ev) == Steinberg::kResultOk);

        const VoragoEngine& engine = engineOf(fx);
        REQUIRE(countSlots(engine, VoiceState::Active) == 1u);
        REQUIRE(countSlots(engine, VoiceState::Releasing) == 0u);
    }

    SECTION("OffsetClamping") {  // SC-022 (7), FR-025
        SECTION("past the end clamps to the last sample") {
            Krate::Test::EventList ev600;
            ev600.addNoteOn(48, kVelocity100, 600);
            const Stereo late = renderWithFirstBlockEvents(&ev600, kTimingSamples);

            Krate::Test::EventList ev511;
            ev511.addNoteOn(48, kVelocity100, 511);
            const Stereo last = renderWithFirstBlockEvents(&ev511, kTimingSamples);

            const float peak = windowPeak(last, kWindowStart);
            const float diff = windowDiff(late, last, kWindowStart);
            WARN("SC-022 (7) @600 vs @511: peak=" << peak << " maxAbsDiff=" << diff);
            REQUIRE(peak >= kAudibleFloor);
            REQUIRE(diff <= kEqualTolerance);
        }
        SECTION("negative clamps to zero") {
            Krate::Test::EventList evNeg;
            evNeg.addNoteOn(48, kVelocity100, -5);
            const Stereo neg = renderWithFirstBlockEvents(&evNeg, kTimingSamples);

            Krate::Test::EventList ev0;
            ev0.addNoteOn(48, kVelocity100, 0);
            const Stereo zero = renderWithFirstBlockEvents(&ev0, kTimingSamples);

            const float peak = windowPeak(zero, kWindowStart);
            const float diff = windowDiff(neg, zero, kWindowStart);
            WARN("SC-022 (7) @-5 vs @0: peak=" << peak << " maxAbsDiff=" << diff);
            REQUIRE(peak >= kAudibleFloor);
            REQUIRE(diff <= kEqualTolerance);
        }
    }

    SECTION("OutOfOrderEventsAreSorted") {  // SC-022 (8), the P-3 detector
        Krate::Test::EventList unsorted;
        unsorted.addNoteOn(55, kVelocity100, 400);
        unsorted.addNoteOn(48, kVelocity100, 100);
        const Stereo u = renderWithFirstBlockEvents(&unsorted, kTimingSamples);

        Krate::Test::EventList sorted;
        sorted.addNoteOn(48, kVelocity100, 100);
        sorted.addNoteOn(55, kVelocity100, 400);
        const Stereo s = renderWithFirstBlockEvents(&sorted, kTimingSamples);

        const float peak = windowPeak(s, kWindowStart);
        const float diff = windowDiff(u, s, kWindowStart);
        WARN("SC-022 (8) peak=" << peak << " maxAbsDiff(unsorted, sorted)=" << diff);
        REQUIRE(peak >= kAudibleFloor);
        REQUIRE(diff <= kEqualTolerance);
    }

    SECTION("OverflowQueueDropsNothing") {  // SC-022 (9), plan 3.2 step 3
        constexpr std::size_t kOverflowBlock = 2048;
        VoragoTest::ProcessorFixture fx;
        fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kOverflowBlock));  // polyphony 4
        fx.reserveCapture(kOverflowBlock);

        Krate::Test::EventList ev;
        // Sorted segment [0, 1024): NoteOn(60) / NoteOff(60) at offset = index.
        for (std::size_t i = 0; i < ::Vorago::Processor::kMaxEventsPerBlock; ++i) {
            const auto offset = static_cast<Steinberg::int32>(i);
            if (i % 2u == 0u) {
                ev.addNoteOn(60, 1.0f, offset);
            } else {
                ev.addNoteOff(60, offset);  // index 1023 is a NoteOff
            }
        }
        // Overflow segment [1024, 1100).
        ev.addNoteOn(62, 1.0f, 1100);  // index 1024
        ev.addNoteOn(64, 1.0f, 5);     // index 1025: below the running max -> 1100, no rewind
        for (std::size_t i = 1026; i < 1100; ++i) {
            const auto offset = static_cast<Steinberg::int32>(1101u + (i - 1026u));  // 1101..1174
            if (i % 2u == 0u) {
                ev.addNoteOn(60, 1.0f, offset);
            } else {
                ev.addNoteOff(60, offset);  // index 1099 is a NoteOff
            }
        }
        REQUIRE(ev.getEventCount() == 1100);

        REQUIRE(fx.processBlock(kOverflowBlock, &ev) == Steinberg::kResultOk);  // canaries checked inside
        REQUIRE(VoragoTest::allFinite(std::span<const float>(fx.capturedL)));
        REQUIRE(VoragoTest::allFinite(std::span<const float>(fx.capturedR)));

        const VoragoEngine& engine = engineOf(fx);
        REQUIRE(countSlots(engine, VoiceState::Active) == 2u);  // notes 62 and 64
        REQUIRE(engine.getActiveVoiceCount() == 3u);             // + pitch 60 Releasing
    }

    SECTION("BlockSizeInvariance") {  // SC-008 (1)
        const auto start = std::chrono::steady_clock::now();

        using Scripted = VoragoTest::ProcessorFixture::ScriptedEvent;
        const std::array<Scripted, 4> script{{
            Scripted{0, makeNoteOn(48, 1.0f)},
            Scripted{37111, makeNoteOn(55, 0.8f)},
            Scripted{100003, makeNoteOff(48)},
            Scripted{150007, makeNoteOn(60, 0.9f)},
        }};

        const auto renderAt = [&script](std::size_t block, bool probeSlices) {
            VoragoTest::ProcessorFixture fx;
            fx.prepare(kSampleRate, static_cast<Steinberg::int32>(block));
            const std::array<std::size_t, 1> pattern{block};
            fx.renderScript(std::span<const Scripted>(script), kInvarianceSamples,
                            std::span<const std::size_t>(pattern));
            REQUIRE(fx.capturedL.size() == kInvarianceSamples);
            if (probeSlices) {
                // FR-026's seam: one event-free 4096 block renders as 2 slices.
                fx.reserveCapture(fx.capturedL.size() + block);
                REQUIRE(fx.processBlock(block) == Steinberg::kResultOk);
                REQUIRE(fx.proc->lastSliceCountForTest() == 2u);
            }
            return takeCapture(fx);
        };

        const Stereo reference = renderAt(kBlock, false);
        const float refPeak = windowPeak(reference, kLatency);
        WARN("SC-008 (1) reference (512) peak over [3072, end) = " << refPeak);
        REQUIRE(refPeak >= kAudibleFloor);  // P-6 precondition

        const auto refFpL = Krate::DSP::TestUtils::fingerprintRender(std::span<const float>(reference.l));
        const auto refFpR = Krate::DSP::TestUtils::fingerprintRender(std::span<const float>(reference.r));

        constexpr std::array<std::size_t, 6> kPartitions{1, 7, 64, 65, 2048, 4096};
        for (const std::size_t block : kPartitions) {
            const Stereo run = renderAt(block, block == 4096u);
            const float dl = VoragoTest::maxAbsDiff(std::span<const float>(run.l),
                                                    std::span<const float>(reference.l), 0,
                                                    kInvarianceSamples);
            const float dr = VoragoTest::maxAbsDiff(std::span<const float>(run.r),
                                                    std::span<const float>(reference.r), 0,
                                                    kInvarianceSamples);
            INFO("block " << block << " maxAbsDiff L=" << dl << " R=" << dr);
            REQUIRE(dl <= kEqualTolerance);
            REQUIRE(dr <= kEqualTolerance);

            // Secondary, WARN-only.
            const std::span<const float> runL(run.l.data(), kInvarianceSamples);
            const std::span<const float> runR(run.r.data(), kInvarianceSamples);
            const auto cmpL = Krate::DSP::TestUtils::compareFingerprints(
                Krate::DSP::TestUtils::fingerprintRender(runL), refFpL);
            const auto cmpR = Krate::DSP::TestUtils::compareFingerprints(
                Krate::DSP::TestUtils::fingerprintRender(runR), refFpR);
            if (!cmpL.withinTolerance() || !cmpR.withinTolerance()) {
                WARN("SC-008 (1) fingerprint drift at block " << block << ": L " << cmpL.detail
                                                              << " | R " << cmpR.detail);
            }
        }

        const double wall =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        WARN("SC-008 (1) BlockSizeInvariance wall time = " << wall << " s");
    }
}
