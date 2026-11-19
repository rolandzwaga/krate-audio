// ==============================================================================
// Unit Test: Processor host contract (T2.26: R-189, R-190, R-105, R-137)
// ==============================================================================
// What the processor tells the host, and what it reads back:
// - IProcessContextRequirements names every ProcessContext field process() reads
// - getTailSamples reports the reverb freeze / delay feedback / release tails
// - process() writes the output silenceFlags
// - a block without kTempoValid keeps the last valid host tempo everywhere
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "vst_event_list.h"
#include "vst_param_changes.h"

#include <cmath>
#include <memory>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr Steinberg::int32 kMaxBlock = 512;

class HostContractProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
    using Ruinae::Processor::engine;
};

std::unique_ptr<HostContractProcessor> makeProcessor() {
    auto p = std::make_unique<HostContractProcessor>();
    p->initialize(nullptr);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = kMaxBlock;
    p->setupProcessing(setup);
    p->setActive(true);
    return p;
}

void setParams(HostContractProcessor& p,
               std::initializer_list<std::pair<Steinberg::Vst::ParamID, double>> values) {
    Krate::Test::ParameterChanges changes;
    for (const auto& [id, v] : values) {
        changes.addChange(id, v);
    }
    p.processParameterChanges(&changes);
}

/// One process() call; returns the output bus silenceFlags the processor left.
Steinberg::uint64 processBlock(HostContractProcessor& p, Steinberg::int32 numSamples,
                               Steinberg::Vst::IEventList* events = nullptr,
                               Steinberg::Vst::ProcessContext* ctx = nullptr,
                               Steinberg::uint64 presetSilenceFlags = 0) {
    std::vector<float> left(static_cast<size_t>(numSamples), 0.0f);
    std::vector<float> right(static_cast<size_t>(numSamples), 0.0f);
    float* channels[2] = {left.data(), right.data()};
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = channels;
    outBus.silenceFlags = presetSilenceFlags;

    Steinberg::Vst::ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.inputEvents = events;
    data.processContext = ctx;
    p.process(data);
    return outBus.silenceFlags;
}

} // namespace

TEST_CASE("Processor_DeclaresProcessContextRequirements", "[processor][host-contract]") {
    auto p = makeProcessor();

    Steinberg::Vst::IProcessContextRequirements* req = nullptr;
    REQUIRE(p->queryInterface(Steinberg::Vst::IProcessContextRequirements::iid,
                              reinterpret_cast<void**>(&req)) == Steinberg::kResultTrue);
    REQUIRE(req != nullptr);
    const Steinberg::uint32 flags = req->getProcessContextRequirements();
    req->release();

    using Req = Steinberg::Vst::IProcessContextRequirements;
    CHECK((flags & Req::kNeedTempo) != 0u);
    CHECK((flags & Req::kNeedTimeSignature) != 0u);
    CHECK((flags & Req::kNeedProjectTimeMusic) != 0u);
    CHECK((flags & Req::kNeedTransportState) != 0u);

    p->setActive(false);
    p->terminate();
}

TEST_CASE("Processor_TailSamples", "[processor][host-contract]") {
    auto p = makeProcessor();

    SECTION("defaults: finite and at least the amp release (200 ms)") {
        const auto tail = p->getTailSamples();
        CHECK(tail != Steinberg::Vst::kInfiniteTail);
        CHECK(tail >= static_cast<Steinberg::uint32>(0.200 * kSampleRate));
    }

    SECTION("a longer amp release lengthens the tail") {
        setParams(*p, {{Ruinae::kAmpEnvReleaseId, 0.5}});  // 0.5^3 * 10 s = 1250 ms
        const auto tail = p->getTailSamples();
        CHECK(tail != Steinberg::Vst::kInfiniteTail);
        CHECK(tail >= static_cast<Steinberg::uint32>(1.250 * kSampleRate));
    }

    SECTION("reverb Freeze on -> infinite") {
        setParams(*p, {{Ruinae::kReverbEnabledId, 1.0}, {Ruinae::kReverbFreezeId, 1.0}});
        CHECK(p->getTailSamples() == Steinberg::Vst::kInfiniteTail);
    }

    SECTION("delay enabled with feedback 1.2 -> infinite") {
        setParams(*p, {{Ruinae::kDelayEnabledId, 1.0}, {Ruinae::kDelayFeedbackId, 1.0}});
        CHECK(p->getTailSamples() == Steinberg::Vst::kInfiniteTail);
    }

    SECTION("feedback 1.2 with the delay disabled stays finite") {
        setParams(*p, {{Ruinae::kDelayEnabledId, 0.0}, {Ruinae::kDelayFeedbackId, 1.0}});
        CHECK(p->getTailSamples() != Steinberg::Vst::kInfiniteTail);
    }

    SECTION("an enabled delay below unity feedback adds its decay") {
        const auto dry = p->getTailSamples();
        // 1 s free time, feedback 0.6: -60 dB after ln(1e-3)/ln(0.6) = 13.5 repeats
        setParams(*p, {{Ruinae::kDelayEnabledId, 1.0}, {Ruinae::kDelaySyncId, 0.0},
                       {Ruinae::kDelayTimeId, (1000.0 - 1.0) / 4999.0},
                       {Ruinae::kDelayFeedbackId, 0.5}});
        const auto withDelay = p->getTailSamples();
        CHECK(withDelay != Steinberg::Vst::kInfiniteTail);
        CHECK(withDelay >= dry + static_cast<Steinberg::uint32>(13.0 * kSampleRate));
    }

    p->setActive(false);
    p->terminate();
}

TEST_CASE("Processor_WritesSilenceFlags", "[processor][host-contract]") {
    auto p = makeProcessor();
    constexpr Steinberg::uint64 kBothSilent = 3;

    SECTION("a sounding note clears a stale silent bit") {
        Krate::Test::EventList events;
        events.addNoteOn(60, 0.8f);
        (void)processBlock(*p, kMaxBlock, &events, nullptr, kBothSilent);
        const auto flags = processBlock(*p, kMaxBlock, nullptr, nullptr, kBothSilent);
        CHECK(flags == 0u);
    }

    SECTION("no note and the effects idle -> both channels flagged silent") {
        Steinberg::uint64 flags = 0;
        for (int i = 0; i < 8; ++i) {
            flags = processBlock(*p, kMaxBlock, nullptr, nullptr, 0);
        }
        CHECK(flags == kBothSilent);
    }

    p->setActive(false);
    p->terminate();
}

TEST_CASE("Processor_TempoDropout_UsesLastValidTempo", "[processor][host-contract]") {
    // Two instances, same patch, same held note. A gets kTempoValid at 90 BPM on
    // every block; B on every other block. No project time, so the trance gate
    // free-runs on the tempo it is given: its step sequence must match A's.
    auto a = makeProcessor();
    auto b = makeProcessor();
    for (auto* p : {a.get(), b.get()}) {
        setParams(*p, {{Ruinae::kTranceGateEnabledId, 1.0},
                       {Ruinae::kTranceGateDepthId, 1.0},
                       {Ruinae::kTranceGateTempoSyncId, 1.0}});
    }

    Steinberg::Vst::ProcessContext ctxA{};
    ctxA.sampleRate = kSampleRate;
    ctxA.tempo = 90.0;
    ctxA.timeSigNumerator = 4;
    ctxA.timeSigDenominator = 4;
    ctxA.state = Steinberg::Vst::ProcessContext::kTempoValid
               | Steinberg::Vst::ProcessContext::kTimeSigValid
               | Steinberg::Vst::ProcessContext::kPlaying;
    Steinberg::Vst::ProcessContext ctxB = ctxA;

    constexpr Steinberg::int32 kBlock = 32;
    constexpr int kBlocks = static_cast<int>(4.0 * kSampleRate) / kBlock;  // 4 s
    std::vector<int> stepsA;
    std::vector<int> stepsB;
    int transitionsA = 0;
    for (int i = 0; i < kBlocks; ++i) {
        Krate::Test::EventList evA;
        Krate::Test::EventList evB;
        if (i == 0) {
            evA.addNoteOn(60, 0.8f);
            evB.addNoteOn(60, 0.8f);
        }
        ctxB.state = (i % 2 == 0)
            ? ctxA.state
            : (ctxA.state & ~static_cast<Steinberg::uint32>(Steinberg::Vst::ProcessContext::kTempoValid));
        (void)processBlock(*a, kBlock, &evA, &ctxA);
        (void)processBlock(*b, kBlock, &evB, &ctxB);
        stepsA.push_back(a->engine().getTranceGateCurrentStep());
        stepsB.push_back(b->engine().getTranceGateCurrentStep());
        if (i > 0 && stepsA[static_cast<size_t>(i)] != stepsA[static_cast<size_t>(i - 1)]) {
            ++transitionsA;
        }
    }

    REQUIRE(transitionsA >= 4);  // the gate really stepped
    int firstDiff = -1;
    for (size_t i = 0; i < stepsA.size(); ++i) {
        if (stepsA[i] != stepsB[i]) {
            firstDiff = static_cast<int>(i);
            break;
        }
    }
    INFO("first block where the gate step differs: " << firstDiff
         << " (A transitions: " << transitionsA << ")");
    CHECK(firstDiff == -1);

    for (auto* p : {a.get(), b.get()}) {
        p->setActive(false);
        p->terminate();
    }
}
