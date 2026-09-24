// ==============================================================================
// Vorago - processor lifecycle tests (SC-013, SC-021)
// ==============================================================================
// T013: "ReportedLatency" (SC-013, FR-023 / FR-033) and "DegenerateShapes"
// (SC-021, FR-030 guards + the not-ready zero-fill of plan 2.5.6).
// T016: "SetActiveClearsTail" (SC-026, FR-032) and "ProcessDoesNotAllocate"
// (SC-007 regression guard).
//
// ALLOCATION READING FORM (model: plugins/seraphis/tests/integration/
// effects_perf_test.cpp:848-859): AllocationScope::getAllocationCount() is set
// only in the destructor, so the count is read from the LIVE detector singleton
// INSIDE the scope, stored in a local, and REQUIREd after the scope closes.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "plugin_ids.h"
#include "vorago_test_fixture.h"

#include <allocation_detector.h>
#include <vst_event_list.h>
#include <vst_param_changes.h>

#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace {

constexpr Steinberg::Vst::ParamID kAllParamIds[] = {
    ::Vorago::kMasterGainId,     ::Vorago::kPolyphonyId,     ::Vorago::kMacroDarknessId,
    ::Vorago::kMacroAgeId,       ::Vorago::kMacroDensityId,  ::Vorago::kMacroMovementId,
    ::Vorago::kMacroGravityId,   ::Vorago::kMacroEntropyId,  ::Vorago::kMacroPressureId,
    ::Vorago::kMacroWeightId,    ::Vorago::kMacroFogId,      ::Vorago::kMacroLifeId,
    ::Vorago::kMacroDepthId,     ::Vorago::kMacroMassId,
};
static_assert(sizeof(kAllParamIds) / sizeof(kAllParamIds[0]) == 14);

// A stereo-shaped ProcessData over caller-owned buses; every field set explicitly.
Steinberg::Vst::ProcessData makeData(Steinberg::Vst::AudioBusBuffers* outputs,
                                     Steinberg::int32 numOutputs, Steinberg::int32 numSamples,
                                     Steinberg::Vst::IParameterChanges* pc = nullptr) {
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = numSamples;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = numOutputs;
    data.outputs = outputs;
    data.inputParameterChanges = pc;
    data.outputParameterChanges = nullptr;
    data.inputEvents = nullptr;
    data.outputEvents = nullptr;
    data.processContext = nullptr;
    return data;
}

// Peak over the LAST n captured samples of BOTH channels.
float tailPeak(const VoragoTest::ProcessorFixture& fx, std::size_t n) {
    REQUIRE(fx.capturedL.size() >= n);
    REQUIRE(fx.capturedR.size() >= n);
    const std::span<const float> l(fx.capturedL.data() + (fx.capturedL.size() - n), n);
    const std::span<const float> r(fx.capturedR.data() + (fx.capturedR.size() - n), n);
    return std::max(VoragoTest::peakOf(l), VoragoTest::peakOf(r));
}

Steinberg::Vst::ProcessSetup makeSetup(double sr) {
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = 2048;
    setup.sampleRate = sr;
    return setup;
}

}  // namespace

TEST_CASE("Vorago_ProcessorLifecycle", "[vorago][processor]") {
    SECTION("ReportedLatency") {  // SC-013
        VoragoTest::ProcessorFixture fx;
        ::Vorago::Processor& p = *fx.proc;

        // Recorded, not asserted (plan 2.5.9: expected 0 + 1024 before any prepare).
        WARN("latency before prepare = " << p.getLatencySamples());

        constexpr std::array<double, 5> kRates{44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
        bool first = true;
        for (const double sr : kRates) {
            CAPTURE(sr);
            Steinberg::Vst::ProcessSetup setup = makeSetup(sr);
            REQUIRE(p.setupProcessing(setup) == Steinberg::kResultOk);

            REQUIRE(p.engineForTest() != nullptr);
            REQUIRE(p.cavernForTest() != nullptr);
            if (first) {
                // FR-023.4: polyphony pushed from the parameter; the call is counted.
                REQUIRE(p.engineForTest()->getPolyphony() == 4u);
                REQUIRE(p.setPolyphonyCallCountForTest() == 1u);
                first = false;
            }

            REQUIRE(p.getLatencySamples() == 3072u);
            REQUIRE(p.getLatencySamples() ==
                    p.engineForTest()->getLatencySamples() +
                        p.cavernForTest()->getLatencySamples());
        }

        // Re-prepare at a different rate, bounce activation, render one block
        // with every parameter changed: latency does not move.
        fx.prepare(96000.0, 2048);
        fx.prepare(48000.0, 2048);
        REQUIRE(p.setActive(false) == Steinberg::kResultOk);
        REQUIRE(p.setActive(true) == Steinberg::kResultOk);

        Krate::Test::ParameterChanges pc;
        for (const Steinberg::Vst::ParamID id : kAllParamIds) {
            pc.addChange(id, 0.8);
        }
        fx.reserveCapture(512);
        REQUIRE(fx.processBlock(512, nullptr, &pc) == Steinberg::kResultOk);
        REQUIRE(p.getLatencySamples() == 3072u);
    }

    SECTION("DegenerateShapes") {  // SC-021
        VoragoTest::ProcessorFixture fx;
        fx.prepare(48000.0, 2048);
        ::Vorago::Processor& p = *fx.proc;

        constexpr Steinberg::int32 kN = 512;
        std::vector<float> left(static_cast<std::size_t>(kN), 0.0f);
        std::vector<float> right(static_cast<std::size_t>(kN), 0.0f);

        SECTION("NoOutputs") {
            Steinberg::Vst::ProcessData data = makeData(nullptr, 0, kN);
            REQUIRE(p.process(data) == Steinberg::kResultOk);
        }

        SECTION("NullChannelBuffers") {
            Steinberg::Vst::AudioBusBuffers bus{};
            bus.numChannels = 2;
            bus.channelBuffers32 = nullptr;
            Steinberg::Vst::ProcessData data = makeData(&bus, 1, kN);
            REQUIRE(p.process(data) == Steinberg::kResultOk);
        }

        SECTION("MonoBus") {
            // Exactly ONE pointer: a read of element [1] would be out of bounds.
            std::array<float*, 1> channels{left.data()};
            Steinberg::Vst::AudioBusBuffers bus{};
            bus.numChannels = 1;
            bus.channelBuffers32 = channels.data();
            Steinberg::Vst::ProcessData data = makeData(&bus, 1, kN);
            REQUIRE(p.process(data) == Steinberg::kResultOk);
        }

        SECTION("ZeroSamples") {
            std::array<float*, 2> channels{left.data(), right.data()};
            Steinberg::Vst::AudioBusBuffers bus{};
            bus.numChannels = 2;
            bus.channelBuffers32 = channels.data();
            Steinberg::Vst::ProcessData data = makeData(&bus, 1, 0);
            REQUIRE(p.process(data) == Steinberg::kResultOk);
        }

        SECTION("NullRightChannel") {
            std::array<float*, 2> channels{left.data(), nullptr};
            Steinberg::Vst::AudioBusBuffers bus{};
            bus.numChannels = 2;
            bus.channelBuffers32 = channels.data();
            Steinberg::Vst::ProcessData data = makeData(&bus, 1, kN);
            REQUIRE(p.process(data) == Steinberg::kResultOk);
        }

        SECTION("GainChangeWithZeroSamples") {
            Krate::Test::ParameterChanges pc;
            pc.addChange(::Vorago::kMasterGainId, 0.25);
            std::array<float*, 2> channels{left.data(), right.data()};
            Steinberg::Vst::AudioBusBuffers bus{};
            bus.numChannels = 2;
            bus.channelBuffers32 = channels.data();
            Steinberg::Vst::ProcessData data = makeData(&bus, 1, 0, &pc);
            REQUIRE(p.process(data) == Steinberg::kResultOk);
            REQUIRE(p.globalParamsForTest().masterGain.load() == Catch::Approx(0.5f));
        }
    }

    SECTION("NotReadyZeroFills") {  // SC-021: process() before setupProcessing()
        VoragoTest::ProcessorFixture fx;  // initialised, NOT prepared
        ::Vorago::Processor& p = *fx.proc;

        constexpr Steinberg::int32 kN = 512;
        std::vector<float> left(static_cast<std::size_t>(kN), 0.5f);
        std::vector<float> right(static_cast<std::size_t>(kN), 0.5f);
        std::array<float*, 2> channels{left.data(), right.data()};

        Steinberg::Vst::AudioBusBuffers bus{};
        bus.numChannels = 2;
        bus.silenceFlags = 0;
        bus.channelBuffers32 = channels.data();
        Steinberg::Vst::ProcessData data = makeData(&bus, 1, kN);

        REQUIRE(p.process(data) == Steinberg::kResultOk);
        for (std::size_t i = 0; i < left.size(); ++i) {
            REQUIRE(left[i] == 0.0f);
            REQUIRE(right[i] == 0.0f);
        }
        REQUIRE(bus.silenceFlags == 3u);
    }

    SECTION("SetActiveClearsTail") {  // SC-026, FR-032
        constexpr double kSr = 48000.0;
        constexpr std::size_t kSecond = 48000;
        constexpr std::size_t kHold = 8u * kSecond;  // 750 host blocks of 512
        constexpr std::array<std::size_t, 1> kPattern{512};

        Steinberg::Vst::Event noteOn{};
        noteOn.type = Steinberg::Vst::Event::kNoteOnEvent;
        noteOn.noteOn.channel = 0;
        noteOn.noteOn.pitch = 48;
        noteOn.noteOn.velocity = 1.0f;
        noteOn.noteOn.noteId = -1;
        const std::array<VoragoTest::ProcessorFixture::ScriptedEvent, 1> script{{{.at = 0u, .e = noteOn}}};
        const std::span<const VoragoTest::ProcessorFixture::ScriptedEvent> noEvents{};

        // ---- Negative control: identical script, NO setActive(false/true) ----
        {
            VoragoTest::ProcessorFixture ctl;
            ctl.prepare(kSr, 2048);
            ctl.renderScript(script, kHold, kPattern);
            REQUIRE(tailPeak(ctl, kSecond) >= 1.0e-4f);  // precondition
            ctl.renderScript(noEvents, kSecond, kPattern);
            REQUIRE(tailPeak(ctl, kSecond) >= 1.0e-4f);  // the tail really persists
        }

        // ---- Treated: deactivate, then reactivate without allocating ---------
        VoragoTest::ProcessorFixture fx;
        fx.prepare(kSr, 2048);
        fx.renderScript(script, kHold, kPattern);
        REQUIRE(tailPeak(fx, kSecond) >= 1.0e-4f);  // precondition

        REQUIRE(fx.proc->setActive(false) == Steinberg::kResultOk);

        Steinberg::tresult activated = Steinberg::kResultFalse;
        std::size_t allocs = 0;
        {
            TestHelpers::AllocationScope scope;
            activated = fx.proc->setActive(static_cast<Steinberg::TBool>(true));
            allocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        REQUIRE(activated == Steinberg::kResultOk);
        REQUIRE(allocs == 0u);

        fx.renderScript(noEvents, kSecond, kPattern);
        const std::span<const float> l(fx.capturedL.data() + (fx.capturedL.size() - kSecond),
                                       kSecond);
        const std::span<const float> r(fx.capturedR.data() + (fx.capturedR.size() - kSecond),
                                       kSecond);
        REQUIRE(VoragoTest::peakOf(l) < 1.0e-6f);
        REQUIRE(VoragoTest::peakOf(r) < 1.0e-6f);
    }

    SECTION("ProcessDoesNotAllocate") {  // SC-007 regression guard
        // Liveness probe - a SEPARATE, never nested scope. `volatile` is
        // load-bearing: a compiler may elide an unobserved new/delete pair.
        std::size_t probe = 0;
        {
            TestHelpers::AllocationScope scope;
            int* volatile deliberate = new int(1);
            probe = TestHelpers::AllocationDetector::instance().getAllocationCount();
            delete deliberate;
        }
        REQUIRE(probe >= 1u);

        constexpr std::size_t kBlock = 512;
        constexpr std::size_t kBlocks = 188;  // ~2 s at 48 kHz

        VoragoTest::ProcessorFixture fx;
        fx.prepare(48000.0, 2048);
        fx.reserveCapture(kBlock * (kBlocks + 1u));
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);  // warm-up

        // Pre-build every event list and parameter-change set OUTSIDE the scope.
        std::vector<Krate::Test::EventList> events(kBlocks);
        std::vector<VoragoTest::MultiParamChanges> params(kBlocks);
        for (std::size_t b = 0; b < kBlocks; ++b) {
            const auto off = [b](std::size_t salt) {
                return static_cast<Steinberg::int32>((b * salt + 17u) % kBlock);
            };

            Krate::Test::EventList& ev = events[b];
            if (b % 3u == 0u) {
                ev.addNoteOn(static_cast<Steinberg::int16>(36u + (b % 24u)),
                             0.25f + 0.05f * static_cast<float>(b % 10u), off(37u));
            }
            if (b % 3u == 2u) {  // releases the note started two blocks earlier
                ev.addNoteOff(static_cast<Steinberg::int16>(36u + ((b - 2u) % 24u)), off(53u));
            }
            if (b % 7u == 1u) {  // an in-block note-on / note-off pair
                ev.addNoteOn(72, 0.9f, 10);
                ev.addNoteOff(72, 400);
            }

            VoragoTest::MultiParamChanges& pc = params[b];
            pc.reserve(4);
            if (b % 10u == 3u) {
                pc.addQueue(::Vorago::kPolyphonyId).addTestPoint(off(11u), 0.0);
            }
            if (b % 10u == 8u) {
                pc.addQueue(::Vorago::kPolyphonyId).addTestPoint(off(11u), 1.0);
            }
            if (b % 4u == 1u) {
                VoragoTest::MultiPointParamValueQueue& q = pc.addQueue(::Vorago::kMasterGainId);
                q.addTestPoint(0, 0.3);
                q.addTestPoint(off(29u), 0.3 + 0.05 * static_cast<double>(b % 8u));
            }
            if (b % 2u == 0u) {
                const auto id = static_cast<Steinberg::Vst::ParamID>(::Vorago::kMacroDarknessId) +
                                static_cast<Steinberg::Vst::ParamID>(b % 12u);
                pc.addQueue(id).addTestPoint(off(41u), static_cast<double>(b % 7u) / 7.0);
            }
        }

        std::size_t allocs = 0;
        std::size_t okBlocks = 0;
        {
            TestHelpers::AllocationScope scope;
            for (std::size_t b = 0; b < kBlocks; ++b) {
                if (fx.processBlock(kBlock, &events[b], &params[b]) == Steinberg::kResultOk) {
                    ++okBlocks;
                }
            }
            allocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        REQUIRE(okBlocks == kBlocks);
        REQUIRE(allocs == 0u);

        // Non-vacuity: the script sounded and the polyphony flips reached the engine.
        REQUIRE(tailPeak(fx, kBlock * kBlocks) > 0.0f);
        REQUIRE(fx.proc->setPolyphonyCallCountForTest() >= 3u);
    }
}
