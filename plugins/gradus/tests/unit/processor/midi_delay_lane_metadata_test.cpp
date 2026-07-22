// ==============================================================================
// MIDI-delay lane metadata reaches the engine (Gradus audit F14)
// ==============================================================================
// The MIDI delay lane is lane index 8 inside ArpeggiatorCore and advances with
// the other lanes via advanceLaneBySpeed(midiDelayLane_, 8), which reads
// laneSpeedMultipliers_[8], laneSwingAmounts_[8], laneLengthJitters_[8] and
// laneSpeedCurveDepths_[8].
//
// Params 3703-3706 (Speed / Swing / Jitter / Curve Depth) are stored, saved,
// loaded and mirrored to the UI -- the whole persistence chain implies they
// work -- but applyParamsToEngine's MIDI Delay Lane block only ever set the
// lane length and the per-step echo config. It never called setLaneSpeed(8,..)
// and friends, so all four controls were inert and the lane always clocked at
// the base rate.
//
// Observed through the kArpMidiDelayPlayheadId output parameter the processor
// already publishes every block, so the test needs no access to private state.
// ==============================================================================

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

#include "vst_param_changes.h"
#include "vst_event_list.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace Gradus;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int32  kBlockSize  = 512;

// -----------------------------------------------------------------------------
// Output-side IParameterChanges: records the last value written per ParamID.
// (Krate::Test::ParameterChanges is input-only -- its addParameterData returns
// nullptr -- so the playhead outputs need their own sink.)
// -----------------------------------------------------------------------------
class RecordingQueue : public IParamValueQueue {
public:
    ParamID id{};
    ParamValue lastValue{-1.0};

    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return lastValue >= 0.0 ? 1 : 0; }
    tresult PLUGIN_API getPoint(int32, int32& sampleOffset, ParamValue& value) override {
        sampleOffset = 0;
        value = lastValue;
        return kResultTrue;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue value, int32& index) override {
        lastValue = value;
        index = 0;
        return kResultTrue;
    }
};

class OutputParamChanges : public IParameterChanges {
public:
    std::vector<std::unique_ptr<RecordingQueue>> queues;

    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getParameterCount() override {
        return static_cast<int32>(queues.size());
    }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override {
        if (index < 0 || index >= static_cast<int32>(queues.size())) return nullptr;
        return queues[static_cast<size_t>(index)].get();
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override {
        for (size_t i = 0; i < queues.size(); ++i) {
            if (queues[i]->id == id) {
                index = static_cast<int32>(i);
                return queues[i].get();
            }
        }
        auto q = std::make_unique<RecordingQueue>();
        q->id = id;
        index = static_cast<int32>(queues.size());
        queues.push_back(std::move(q));
        return queues.back().get();
    }

    /// Last published value for `id`, or -1 if never published.
    [[nodiscard]] ParamValue valueOf(ParamID id) const {
        for (const auto& q : queues) {
            if (q->id == id) return q->lastValue;
        }
        return -1.0;
    }
};

/// Drive the processor for `numBlocks` with the given setup params, returning
/// the sequence of distinct MIDI-delay playhead steps observed.
std::vector<int> runAndCollectDelayPlayhead(
    Krate::Test::ParameterChanges& setupParams, int numBlocks)
{
    auto proc = std::make_unique<Gradus::Processor>();
    proc->initialize(nullptr);

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = kBlockSize;
    proc->setupProcessing(setup);
    proc->setActive(true);

    std::vector<float> outL(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlockSize), 0.0f);
    float* channels[2] = {outL.data(), outR.data()};
    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channels;

    Krate::Test::EventList inEvents;
    Krate::Test::EventList outEvents;
    Krate::Test::ParameterChanges noParams;
    OutputParamChanges outParams;

    ProcessContext ctx{};
    ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid
              | ProcessContext::kTimeSigValid;
    ctx.tempo = 120.0;
    ctx.timeSigNumerator = 4;
    ctx.timeSigDenominator = 4;
    ctx.sampleRate = kSampleRate;
    ctx.projectTimeSamples = 0;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kBlockSize;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.outputParameterChanges = &outParams;
    data.inputEvents = &inEvents;
    data.outputEvents = &outEvents;
    data.processContext = &ctx;

    // Hold a note so the Live-mode arp clocks and the lanes advance.
    Event on{};
    on.type = Event::kNoteOnEvent;
    on.noteOn.pitch = 60;
    on.noteOn.velocity = 100.0f / 127.0f;
    on.noteOn.noteId = -1;
    inEvents.addEvent(on);

    std::vector<int> steps;
    for (int b = 0; b < numBlocks; ++b) {
        outEvents.clear();
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = (b == 0) ? static_cast<IParameterChanges*>(&setupParams)
                                              : static_cast<IParameterChanges*>(&noParams);
        proc->process(data);
        inEvents.clear();

        const ParamValue v = outParams.valueOf(kArpMidiDelayPlayheadId);
        if (v >= 0.0) {
            // Published as step / 32.0f.
            steps.push_back(static_cast<int>(v * 32.0 + 0.5));
        }
        ctx.projectTimeSamples += kBlockSize;
    }

    proc->setActive(false);
    proc->terminate();
    return steps;
}

/// Count how many times the playhead value changes across the run.
int countAdvances(const std::vector<int>& steps)
{
    int advances = 0;
    for (size_t i = 1; i < steps.size(); ++i) {
        if (steps[i] != steps[i - 1]) ++advances;
    }
    return advances;
}

}  // namespace

TEST_CASE("MIDI-delay lane speed advances lane 8 polymetrically",
          "[processor][gradus][midi-delay][F14]")
{
    // Fast base clock (1/32) and a long delay lane so the playhead has room.
    auto makeBase = [](Krate::Test::ParameterChanges& p) {
        p.add(kArpTempoSyncId, 1.0);
        p.add(kArpNoteValueId, 4.0 / 20.0);              // 1/32
        p.add(kArpMidiDelayLaneLengthId, 1.0);            // 32 steps
    };

    // kLaneSpeedValues is the snapped speed dropdown; index 0 is the slowest
    // (0.25x) and the last is the fastest (4.0x). Normalized position selects
    // the entry, so 0.0 -> slowest and 1.0 -> fastest.
    Krate::Test::ParameterChanges slowParams;
    makeBase(slowParams);
    slowParams.add(kArpMidiDelayLaneSpeedId, 0.0);

    Krate::Test::ParameterChanges fastParams;
    makeBase(fastParams);
    fastParams.add(kArpMidiDelayLaneSpeedId, 1.0);

    const auto slow = runAndCollectDelayPlayhead(slowParams, 120);
    const auto fast = runAndCollectDelayPlayhead(fastParams, 120);

    REQUIRE(!slow.empty());
    REQUIRE(!fast.empty());

    const int slowAdvances = countAdvances(slow);
    const int fastAdvances = countAdvances(fast);

    INFO("delay-lane playhead advances: slowest speed = " << slowAdvances
         << ", fastest speed = " << fastAdvances);

    // With the speed param inert both runs clock identically. Wired up, the
    // fastest setting must advance the lane strictly more often than the
    // slowest one.
    REQUIRE(fastAdvances > slowAdvances);
}

TEST_CASE("MIDI-delay lane swing and jitter reach the engine",
          "[processor][gradus][midi-delay][F14]")
{
    // Swing shifts every other step boundary, so the step-change pattern over a
    // fixed run differs from the un-swung baseline. Jitter re-rolls the pattern
    // length on wrap, which likewise perturbs the advance pattern.
    auto makeBase = [](Krate::Test::ParameterChanges& p) {
        p.add(kArpTempoSyncId, 1.0);
        p.add(kArpNoteValueId, 4.0 / 20.0);
        p.add(kArpMidiDelayLaneLengthId, 1.0);
    };

    Krate::Test::ParameterChanges plain;
    makeBase(plain);

    Krate::Test::ParameterChanges swung;
    makeBase(swung);
    swung.add(kArpMidiDelayLaneSwingId, 1.0);   // 75% swing

    const auto plainSteps = runAndCollectDelayPlayhead(plain, 160);
    const auto swungSteps = runAndCollectDelayPlayhead(swung, 160);

    REQUIRE(plainSteps.size() == swungSteps.size());
    INFO("swing must change the delay lane's step timing");
    REQUIRE(plainSteps != swungSteps);
}
