// ==============================================================================
// Vorago - processor CPU test (SC-014; FR-067, FR-067a)   Plan section 4.3
// ==============================================================================
// Vorago_ProcessorCpu is HIDDEN ([.perf]) and wall-clock based: run it ALONE,
// nothing else executing (node tools/run-cpu-tests.js vorago_tests).
//
//   Arm P  - ::Vorago::Processor timed through bare proc->process(data) calls.
//   Arm D  - the direct chain the processor wraps: heap VoragoEngine -> heap
//            CavernVerb (in place) -> OnePoleSmoother gain loop at unity ->
//            engine.processOutputStage (limiter last).
//   Gate   - REQUIRE(P_best <= 1.05 * D_best) (FR-067a). P_best / kReferenceNs is
//            WARN-recorded only (FR-067).
//   Arm E  - event-dense worst case (1024 reverse-ordered events in one 2048
//            block), WARN-recorded, not gated.
//
// kReferenceNs comes from the Phase 10 budget header (single source, never
// re-typed), included through VORAGO_PERF_BUDGET_HEADER (tests/CMakeLists.txt).
//
// NAMESPACE HAZARD (vorago_test_fixture.h): plugin types are spelled ::Vorago::,
// the budget constant Krate::DSP::TestUtils::Vorago:: - both fully qualified.
// ==============================================================================

#include "engine/vorago_engine_config.h"
#include "vorago_test_fixture.h"

#include VORAGO_PERF_BUDGET_HEADER

#include <vst_event_list.h>

#include <krate/dsp/effects/cavern_verb.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/vorago_engine.h>

#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstevents.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace {

constexpr double kCpuSampleRate = 48000.0;
constexpr std::size_t kCpuBlock = 512;
constexpr std::size_t kCpuPolyphony = 4;  // == GlobalParams::polyphony default
constexpr std::array<std::uint8_t, 4> kCpuNotes{36, 40, 43, 47};
constexpr std::uint8_t kCpuVelocityMidi = 100;
constexpr float kCpuVelocity = 100.0f / 127.0f;  // quantiseVelocity -> 100

constexpr std::size_t kWarmupBlocks = 100;
constexpr std::size_t kTrials = 16;
constexpr std::size_t kBlocksPerTrial = 100;
constexpr double kWrapperOverheadCeiling = 1.05;  // FR-067a

// Arm E
constexpr std::size_t kDenseBlock = 2048;
constexpr std::size_t kDenseEvents = 1024;  // == Processor::kMaxEventsPerBlock
constexpr std::int16_t kDensePitch = 60;

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsedNs(Clock::time_point t0, Clock::time_point t1) noexcept {
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}

// A stereo ProcessData over caller-owned buffers, built ONCE before timing.
struct BareProcessCall {
    std::vector<float> left;
    std::vector<float> right;
    std::array<float*, 2> channels{};
    Steinberg::Vst::AudioBusBuffers outBus;
    Steinberg::Vst::ProcessData data;

    explicit BareProcessCall(std::size_t n) : left(n, 0.0f), right(n, 0.0f) {
        channels = {left.data(), right.data()};

        outBus.numChannels = 2;
        outBus.silenceFlags = 0;
        outBus.channelBuffers32 = channels.data();

        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(n);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outBus;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = nullptr;
        data.outputEvents = nullptr;
        data.processContext = nullptr;
    }

    BareProcessCall(const BareProcessCall&) = delete;
    BareProcessCall& operator=(const BareProcessCall&) = delete;
};

// Arm D: the processor's renderSlice chain, driven directly (plan 1.2 steps 3-6).
struct DirectChain {
    std::unique_ptr<Krate::DSP::VoragoEngine> engine =
        std::make_unique<Krate::DSP::VoragoEngine>();
    std::unique_ptr<Krate::DSP::CavernVerb> cavern = std::make_unique<Krate::DSP::CavernVerb>();
    Krate::DSP::OnePoleSmoother gain{1.0f};
    std::vector<float> left = std::vector<float>(kCpuBlock, 0.0f);
    std::vector<float> right = std::vector<float>(kCpuBlock, 0.0f);

    DirectChain() {
        engine->setSeed(::Vorago::kEngineSeed);
        engine->prepare(kCpuSampleRate, ::Vorago::makeVoragoEngineConfig(::Vorago::kMaxBlockSamples));
        engine->setPolyphony(kCpuPolyphony);
        cavern->prepare(kCpuSampleRate, ::Vorago::makeVoragoCavernConfig(::Vorago::kMaxBlockSamples));
        gain.configure(::Vorago::kMasterGainSmoothMs, static_cast<float>(kCpuSampleRate));
        gain.snapTo(1.0f);
        for (const std::uint8_t note : kCpuNotes) {
            engine->noteOn(note, kCpuVelocityMidi);
        }
    }

    void processBlock() noexcept {
        float* l = left.data();
        float* r = right.data();
        engine->processStereoBlock(l, r, kCpuBlock);
        cavern->processStereoBlock(l, r, l, r, kCpuBlock);
        for (std::size_t s = 0; s < kCpuBlock; ++s) {
            const float g = gain.process();
            l[s] *= g;
            r[s] *= g;
        }
        engine->processOutputStage(l, r, kCpuBlock);
    }
};

}  // namespace

TEST_CASE("Vorago_ProcessorCpu", "[vorago][.perf][performance]") {
    // ---------------------------------------------------------------- arm P setup
    VoragoTest::ProcessorFixture fixtureP;
    fixtureP.prepare(kCpuSampleRate, static_cast<Steinberg::int32>(kCpuBlock));
    ::Vorago::Processor* const proc = fixtureP.proc.get();
    REQUIRE(proc != nullptr);

    BareProcessCall callP(kCpuBlock);

    Krate::Test::EventList notesP;
    for (const std::uint8_t note : kCpuNotes) {
        notesP.addNoteOn(static_cast<Steinberg::int16>(note), kCpuVelocity, 0);
    }

    // ---------------------------------------------------------------- arm D setup
    DirectChain chainD;

    // ---------------------------------------------------------------- warm-up (discarded)
    callP.data.inputEvents = &notesP;  // first warm-up block ONLY
    REQUIRE(proc->process(callP.data) == Steinberg::kResultOk);
    callP.data.inputEvents = nullptr;  // detached for every later block
    callP.data.inputParameterChanges = nullptr;
    for (std::size_t b = 1; b < kWarmupBlocks; ++b) {
        proc->process(callP.data);
    }
    for (std::size_t b = 0; b < kWarmupBlocks; ++b) {
        chainD.processBlock();
    }

    // ---------------------------------------------------------------- interleaved trials
    double bestP = std::numeric_limits<double>::max();
    double bestD = std::numeric_limits<double>::max();
    for (std::size_t trial = 0; trial < kTrials; ++trial) {
        const Clock::time_point p0 = Clock::now();
        for (std::size_t b = 0; b < kBlocksPerTrial; ++b) {
            proc->process(callP.data);
        }
        const Clock::time_point p1 = Clock::now();
        bestP = std::min(bestP, elapsedNs(p0, p1));

        const Clock::time_point d0 = Clock::now();
        for (std::size_t b = 0; b < kBlocksPerTrial; ++b) {
            chainD.processBlock();
        }
        const Clock::time_point d1 = Clock::now();
        bestD = std::min(bestD, elapsedNs(d0, d1));
    }

    const double pBestNs = bestP / static_cast<double>(kBlocksPerTrial);
    const double dBestNs = bestD / static_cast<double>(kBlocksPerTrial);
    const double ratioPD = pBestNs / dBestNs;
    const double ratioRef = pBestNs / Krate::DSP::TestUtils::Vorago::kReferenceNs;

    WARN("SC-014 arm P best ns/block (512 @ 48 kHz, poly 4): " << pBestNs);
    WARN("SC-014 arm D best ns/block (512 @ 48 kHz, poly 4): " << dBestNs);
    WARN("SC-014 P/D ratio (gate <= " << kWrapperOverheadCeiling << "): " << ratioPD);
    WARN("SC-014 P / kReferenceNs (" << Krate::DSP::TestUtils::Vorago::kReferenceNs
                                     << " ns, recorded only, FR-067): " << ratioRef);

    REQUIRE(pBestNs <= kWrapperOverheadCeiling * dBestNs);  // FR-067a

    // ---------------------------------------------------------------- arm E (WARN only)
    VoragoTest::ProcessorFixture fixtureE;
    fixtureE.prepare(kCpuSampleRate, static_cast<Steinberg::int32>(kDenseBlock));
    ::Vorago::Processor* const procE = fixtureE.proc.get();
    REQUIRE(procE != nullptr);

    BareProcessCall callE(kDenseBlock);

    Krate::Test::EventList notesE;
    for (const std::uint8_t note : kCpuNotes) {
        notesE.addNoteOn(static_cast<Steinberg::int16>(note), kCpuVelocity, 0);
    }
    callE.data.inputEvents = &notesE;
    REQUIRE(procE->process(callE.data) == Steinberg::kResultOk);  // warm-up

    // 1024 events in strictly REVERSE offset order: 2047 down to 1024,
    // alternating NoteOn(60) / NoteOff(60). Built once, reused by every trial.
    Krate::Test::EventList dense;
    for (std::size_t k = 0; k < kDenseEvents; ++k) {
        const auto offset = static_cast<Steinberg::int32>(kDenseBlock - 1u - k);
        if (k % 2u == 0u) {
            dense.addNoteOn(kDensePitch, kCpuVelocity, offset);
        } else {
            dense.addNoteOff(kDensePitch, offset);
        }
    }
    REQUIRE(dense.getEventCount() == static_cast<Steinberg::int32>(kDenseEvents));
    callE.data.inputEvents = &dense;
    callE.data.inputParameterChanges = nullptr;

    double bestE = std::numeric_limits<double>::max();
    for (std::size_t trial = 0; trial < kTrials; ++trial) {
        const Clock::time_point e0 = Clock::now();
        procE->process(callE.data);
        const Clock::time_point e1 = Clock::now();
        bestE = std::min(bestE, elapsedNs(e0, e1));
    }

    const double denseBudgetNs = (static_cast<double>(kDenseBlock) / kCpuSampleRate) * 1.0e9;
    WARN("SC-014 arm E best ns for one 2048-sample block with 1024 reverse-ordered events: "
         << bestE);
    WARN("SC-014 arm E ratio to real time (2048 / 48000 s = " << denseBudgetNs
                                                              << " ns): " << bestE / denseBudgetNs);
}
