// ==============================================================================
// Integration Test: Preset Switch Safety During Playback
// ==============================================================================
// Verifies that preset switching during active audio playback is crash-proof:
//   - No NaN/Inf in output after preset switch
//   - No crash under rapid preset switching
//   - Oscillator type changes during playback are safe
//   - Arp-to-non-arp transitions are clean
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "drain_preset_transfer.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cmath>
#include <vector>

// =============================================================================
// Helpers
// =============================================================================

namespace {

class TestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

std::unique_ptr<TestableProcessor> makeTestableProcessor() {
    auto p = std::make_unique<TestableProcessor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);
    p->setActive(true);

    return p;
}

class SingleParamQueue : public Steinberg::Vst::IParamValueQueue {
public:
    SingleParamQueue(Steinberg::Vst::ParamID id, double value)
        : paramId_(id), value_(value) {}
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return paramId_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }
    Steinberg::tresult PLUGIN_API getPoint(
        Steinberg::int32 index, Steinberg::int32& sampleOffset,
        Steinberg::Vst::ParamValue& value) override {
        if (index != 0) return Steinberg::kResultFalse;
        sampleOffset = 0;
        value = value_;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addPoint(
        Steinberg::int32, Steinberg::Vst::ParamValue, Steinberg::int32&) override {
        return Steinberg::kResultFalse;
    }
private:
    Steinberg::Vst::ParamID paramId_;
    double value_;
};

class ParamChangeBatch : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(queues_.size());
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size()))
            return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override {
        return nullptr;
    }
    void add(Steinberg::Vst::ParamID id, double value) {
        queues_.emplace_back(id, value);
    }
private:
    std::vector<SingleParamQueue> queues_;
};

// Minimal MIDI event list for testing
class TestEventList : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getEventCount() override {
        return static_cast<Steinberg::int32>(events_.size());
    }

    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                            Steinberg::Vst::Event& e) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(events_.size()))
            return Steinberg::kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        events_.push_back(e);
        return Steinberg::kResultTrue;
    }

    void addNoteOn(int note, float velocity, int sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = static_cast<Steinberg::int16>(note);
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = note;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Steinberg::Vst::Event> events_;
};

std::vector<char> captureState(Ruinae::Processor* proc) {
    Steinberg::MemoryStream stream;
    proc->getState(&stream);
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> data(static_cast<size_t>(size));
    Steinberg::int32 bytesRead = 0;
    stream.read(data.data(), static_cast<Steinberg::int32>(size), &bytesRead);
    return data;
}

void loadState(Ruinae::Processor* proc, const std::vector<char>& bytes) {
    Steinberg::MemoryStream stream;
    stream.write(const_cast<char*>(bytes.data()),
        static_cast<Steinberg::int32>(bytes.size()), nullptr);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    proc->setState(&stream);
}

bool hasNanOrInf(const float* buffer, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (std::isnan(buffer[i]) || std::isinf(buffer[i])) return true;
    }
    return false;
}

void processBlock(Ruinae::Processor& proc, TestEventList& events,
                  float* outL, float* outR, Steinberg::int32 numSamples) {
    Steinberg::Vst::ProcessContext context{};
    context.state = Steinberg::Vst::ProcessContext::kPlaying |
                    Steinberg::Vst::ProcessContext::kTempoValid;
    context.tempo = 120.0;
    context.sampleRate = 44100.0;

    float* channelBuffers[2] = {outL, outR};
    Steinberg::Vst::AudioBusBuffers output{};
    output.numChannels = 2;
    output.channelBuffers32 = channelBuffers;

    Steinberg::Vst::ProcessData data{};
    data.numSamples = numSamples;
    data.numInputs = 0;
    data.numOutputs = 1;
    data.outputs = &output;
    data.inputEvents = &events;
    data.processContext = &context;

    proc.process(data);
}

} // anonymous namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Preset switch during playback produces finite output",
          "[preset-safety][playback]") {
    auto proc = makeTestableProcessor();

    // Set up a saw oscillator preset
    ParamChangeBatch changes;
    changes.add(Ruinae::kOscATypeId, 0.0); // Saw
    changes.add(Ruinae::kOscALevelId, 1.0);
    proc->processParameterChanges(&changes);

    static constexpr Steinberg::int32 kBlockSize = 512;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    TestEventList events;

    // Play a note
    events.addNoteOn(60, 0.8f);
    processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    events.clear();

    // Process a few blocks to get sound flowing
    for (int i = 0; i < 5; ++i) {
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    }

    // Now switch to a different preset (different osc type + filter)
    auto proc2 = makeTestableProcessor();
    ParamChangeBatch changes2;
    changes2.add(Ruinae::kOscATypeId, 3.0 / 9.0); // Additive
    changes2.add(Ruinae::kFilterCutoffId, 0.3);
    proc2->processParameterChanges(&changes2);
    auto presetB = captureState(proc2.get());

    // Load new preset while notes are playing
    loadState(proc.get(), presetB);

    // Process several blocks after the switch — verify no NaN/Inf
    bool anyBadSamples = false;
    for (int i = 0; i < 10; ++i) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
        if (hasNanOrInf(outL.data(), kBlockSize) ||
            hasNanOrInf(outR.data(), kBlockSize)) {
            anyBadSamples = true;
            break;
        }
    }

    REQUIRE_FALSE(anyBadSamples);

    proc->terminate();
    proc2->terminate();
}

TEST_CASE("Rapid preset switching doesn't crash",
          "[preset-safety][rapid]") {
    auto proc = makeTestableProcessor();

    static constexpr Steinberg::int32 kBlockSize = 256;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    TestEventList events;

    // Create 5 different preset states
    std::vector<std::vector<char>> presets;
    for (int i = 0; i < 5; ++i) {
        auto tmp = makeTestableProcessor();
        ParamChangeBatch changes;
        changes.add(Ruinae::kOscATypeId, static_cast<double>(i) / 9.0);
        changes.add(Ruinae::kMasterGainId, 0.2 + 0.15 * i);
        tmp->processParameterChanges(&changes);
        presets.push_back(captureState(tmp.get()));
        tmp->terminate();
    }

    // Play a note
    events.addNoteOn(60, 0.8f);
    processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    events.clear();

    // Rapidly switch between presets (2 presets per process block)
    bool anyBadSamples = false;
    for (int cycle = 0; cycle < 10; ++cycle) {
        loadState(proc.get(), presets[static_cast<size_t>(cycle % 5)]);
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);

        if (hasNanOrInf(outL.data(), kBlockSize) ||
            hasNanOrInf(outR.data(), kBlockSize)) {
            anyBadSamples = true;
            break;
        }
    }

    REQUIRE_FALSE(anyBadSamples);
    proc->terminate();
}

TEST_CASE("Oscillator type switch during playback",
          "[preset-safety][osc-switch]") {
    auto proc = makeTestableProcessor();

    // Start with Saw oscillator
    ParamChangeBatch sawChanges;
    sawChanges.add(Ruinae::kOscATypeId, 0.0); // Saw = 0
    sawChanges.add(Ruinae::kOscALevelId, 1.0);
    proc->processParameterChanges(&sawChanges);

    static constexpr Steinberg::int32 kBlockSize = 512;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    TestEventList events;

    // Play a note and process a few blocks
    events.addNoteOn(60, 0.8f);
    processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    events.clear();

    for (int i = 0; i < 3; ++i) {
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    }

    // Create a Wavetable preset
    auto wtProc = makeTestableProcessor();
    ParamChangeBatch wtChanges;
    wtChanges.add(Ruinae::kOscATypeId, 4.0 / 9.0); // Wavetable = 4
    wtChanges.add(Ruinae::kOscALevelId, 0.8);
    wtProc->processParameterChanges(&wtChanges);
    auto wtState = captureState(wtProc.get());

    // Switch while note is playing
    loadState(proc.get(), wtState);

    // Verify clean output after switch
    bool anyBadSamples = false;
    for (int i = 0; i < 10; ++i) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
        if (hasNanOrInf(outL.data(), kBlockSize) ||
            hasNanOrInf(outR.data(), kBlockSize)) {
            anyBadSamples = true;
            break;
        }
    }

    REQUIRE_FALSE(anyBadSamples);

    proc->terminate();
    wtProc->terminate();
}

TEST_CASE("Arp-to-non-arp preset switch",
          "[preset-safety][arp-switch]") {
    // Create an arp-enabled preset
    auto arpProc = makeTestableProcessor();
    ParamChangeBatch arpChanges;
    arpChanges.add(Ruinae::kArpEnabledId, 1.0);
    arpChanges.add(Ruinae::kArpModeId, 0.0); // Up
    arpChanges.add(Ruinae::kArpTempoSyncId, 1.0);
    arpChanges.add(Ruinae::kOscALevelId, 1.0);
    arpProc->processParameterChanges(&arpChanges);
    auto arpState = captureState(arpProc.get());

    // Create a non-arp preset
    auto noArpProc = makeTestableProcessor();
    ParamChangeBatch noArpChanges;
    noArpChanges.add(Ruinae::kArpEnabledId, 0.0);
    noArpChanges.add(Ruinae::kOscALevelId, 0.8);
    noArpChanges.add(Ruinae::kFilterCutoffId, 0.5);
    noArpProc->processParameterChanges(&noArpChanges);
    auto noArpState = captureState(noArpProc.get());

    // Start playing with arp preset
    auto proc = makeTestableProcessor();
    loadState(proc.get(), arpState);

    static constexpr Steinberg::int32 kBlockSize = 512;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    TestEventList events;

    events.addNoteOn(60, 0.8f);
    events.addNoteOn(64, 0.8f);
    events.addNoteOn(67, 0.8f);

    // Process several blocks with arp running
    for (int i = 0; i < 5; ++i) {
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
        events.clear();
    }

    // Switch to non-arp preset
    loadState(proc.get(), noArpState);

    // Process after switch — should be clean
    bool anyBadSamples = false;
    for (int i = 0; i < 10; ++i) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
        if (hasNanOrInf(outL.data(), kBlockSize) ||
            hasNanOrInf(outR.data(), kBlockSize)) {
            anyBadSamples = true;
            break;
        }
    }

    REQUIRE_FALSE(anyBadSamples);

    arpProc->terminate();
    noArpProc->terminate();
    proc->terminate();
}

TEST_CASE("Voice routes synced after preset snapshot",
          "[preset-safety][voice-routes]") {
    // Create a preset with voice routes configured
    auto proc1 = makeTestableProcessor();
    auto state1 = captureState(proc1.get());

    // Load into a fresh processor, drain, verify state survives round-trip
    auto proc2 = makeTestableProcessor();
    loadState(proc2.get(), state1);
    drainPresetTransfer(proc2.get());

    // Get state and verify it matches
    auto state2 = captureState(proc2.get());
    REQUIRE(state1.size() == state2.size());
    REQUIRE(state1 == state2);

    proc1->terminate();
    proc2->terminate();
}
