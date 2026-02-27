// ==============================================================================
// Arpeggiator Performance & Stress Tests (082-presets-polish)
// ==============================================================================
// Tests for CPU overhead measurement, stress testing under worst-case conditions,
// and note-on/note-off matching under extreme load.
//
// Phase 5 (US3): T054, T055, T056
//
// Reference: specs/082-presets-polish/spec.md FR-016 to FR-019, SC-002, SC-003
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include <krate/dsp/processors/arpeggiator_core.h>

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>

// =============================================================================
// Mock Infrastructure (same pattern as arp_integration_test.cpp)
// =============================================================================

namespace {

class PerfTestEventList : public Steinberg::Vst::IEventList {
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

    void addNoteOn(int16_t pitch, float velocity, int32_t sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = -1;
        e.noteOn.length = 0;
        e.noteOn.tuning = 0.0f;
        events_.push_back(e);
    }

    void addNoteOff(int16_t pitch, int32_t sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = -1;
        e.noteOff.tuning = 0.0f;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Steinberg::Vst::Event> events_;
};

class PerfTestParamQueue : public Steinberg::Vst::IParamValueQueue {
public:
    PerfTestParamQueue(Steinberg::Vst::ParamID id, double value)
        : paramId_(id), value_(value) {}

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return paramId_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }

    Steinberg::tresult PLUGIN_API getPoint(
        Steinberg::int32 index,
        Steinberg::int32& sampleOffset,
        Steinberg::Vst::ParamValue& value) override {
        if (index != 0) return Steinberg::kResultFalse;
        sampleOffset = 0;
        value = value_;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(
        Steinberg::int32, Steinberg::Vst::ParamValue,
        Steinberg::int32&) override {
        return Steinberg::kResultFalse;
    }

private:
    Steinberg::Vst::ParamID paramId_;
    double value_;
};

class PerfTestParamChanges : public Steinberg::Vst::IParameterChanges {
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

    void addChange(Steinberg::Vst::ParamID id, double value) {
        queues_.emplace_back(id, value);
    }

private:
    std::vector<PerfTestParamQueue> queues_;
};

class PerfEmptyParamChanges : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getParameterCount() override { return 0; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32) override { return nullptr; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override { return nullptr; }
};

// =============================================================================
// Helper: check if buffer has any non-zero samples
// =============================================================================

static bool hasNonZeroSamples(const float* buffer, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        if (buffer[i] != 0.0f) return true;
    }
    return false;
}

// =============================================================================
// Performance Test Fixture
// =============================================================================

struct PerfTestFixture {
    Ruinae::Processor processor;
    PerfTestEventList events;
    PerfEmptyParamChanges emptyParams;
    std::vector<float> outL;
    std::vector<float> outR;
    float* channelBuffers[2];
    Steinberg::Vst::AudioBusBuffers outputBus{};
    Steinberg::Vst::ProcessData data{};
    Steinberg::Vst::ProcessContext processContext{};
    static constexpr size_t kBlockSize = 512;
    static constexpr double kSampleRate = 44100.0;

    PerfTestFixture() : outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f) {
        channelBuffers[0] = outL.data();
        channelBuffers[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = &emptyParams;
        data.inputEvents = &events;

        processContext.state = Steinberg::Vst::ProcessContext::kPlaying
                            | Steinberg::Vst::ProcessContext::kTempoValid
                            | Steinberg::Vst::ProcessContext::kTimeSigValid;
        processContext.tempo = 120.0;
        processContext.timeSigNumerator = 4;
        processContext.timeSigDenominator = 4;
        processContext.sampleRate = kSampleRate;
        processContext.projectTimeMusic = 0.0;
        processContext.projectTimeSamples = 0;
        data.processContext = &processContext;

        processor.initialize(nullptr);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~PerfTestFixture() {
        processor.setActive(false);
        processor.terminate();
    }

    void processBlock() {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
        processContext.projectTimeSamples += static_cast<Steinberg::int64>(kBlockSize);
        processContext.projectTimeMusic +=
            static_cast<double>(kBlockSize) / kSampleRate
            * (processContext.tempo / 60.0);
    }

    void processBlockWithParams(PerfTestParamChanges& params) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = &params;
        processor.process(data);
        data.inputParameterChanges = &emptyParams;
        processContext.projectTimeSamples += static_cast<Steinberg::int64>(kBlockSize);
        processContext.projectTimeMusic +=
            static_cast<double>(kBlockSize) / kSampleRate
            * (processContext.tempo / 60.0);
    }

    void clearEvents() {
        events.clear();
    }

    void enableArp() {
        PerfTestParamChanges params;
        params.addChange(Ruinae::kArpEnabledId, 1.0);
        processBlockWithParams(params);
    }

    void disableArp() {
        PerfTestParamChanges params;
        params.addChange(Ruinae::kArpEnabledId, 0.0);
        processBlockWithParams(params);
    }

    void setTransportPlaying(bool playing) {
        if (playing) {
            processContext.state |= Steinberg::Vst::ProcessContext::kPlaying;
        } else {
            processContext.state &= ~static_cast<Steinberg::uint32>(
                Steinberg::Vst::ProcessContext::kPlaying);
        }
    }
};

} // anonymous namespace

// =============================================================================
// T054: CPU overhead of arp is less than 0.1%
// =============================================================================

TEST_CASE("Arp CPU overhead is less than 0.1% of a single core at 44.1kHz",
          "[arp performance]") {
    // Measure the difference in processing time between arp disabled vs enabled.
    // The overhead is expressed as a percentage of the real-time budget:
    //   budget_per_block_ms = (512 / 44100) * 1000 = ~11.6ms
    //   overhead% = (arp_time - noarp_time) / (N * budget_per_block_ms) * 100

    constexpr int N = 10000;
    constexpr double budgetPerBlockMs = (512.0 / 44100.0) * 1000.0;

    // --- Measure with arp DISABLED ---
    {
        PerfTestFixture f;
        // Send a chord so the synth engine has work to do
        f.events.addNoteOn(60, 0.8f);
        f.events.addNoteOn(64, 0.8f);
        f.events.addNoteOn(67, 0.8f);
        f.processBlock();
        f.clearEvents();

        // Warm up
        for (int i = 0; i < 100; ++i) {
            f.processBlock();
        }
    }

    double noArpTimeMs = 0.0;
    {
        PerfTestFixture f;
        // Arp disabled (default)
        f.events.addNoteOn(60, 0.8f);
        f.events.addNoteOn(64, 0.8f);
        f.events.addNoteOn(67, 0.8f);
        f.processBlock();
        f.clearEvents();

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            f.processBlock();
        }
        auto end = std::chrono::high_resolution_clock::now();
        noArpTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    }

    double arpTimeMs = 0.0;
    {
        PerfTestFixture f;
        // Enable arp with a moderate pattern (Basic Up 1/16)
        {
            PerfTestParamChanges params;
            params.addChange(Ruinae::kArpEnabledId, 1.0);
            params.addChange(Ruinae::kArpModeId, 0.0);              // Up
            params.addChange(Ruinae::kArpNoteValueId, 7.0 / 20.0);  // 1/16 (index 7)
            f.processBlockWithParams(params);
        }

        // Send a chord
        f.events.addNoteOn(60, 0.8f);
        f.events.addNoteOn(64, 0.8f);
        f.events.addNoteOn(67, 0.8f);
        f.processBlock();
        f.clearEvents();

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            f.processBlock();
        }
        auto end = std::chrono::high_resolution_clock::now();
        arpTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    }

    double overheadPct = (arpTimeMs - noArpTimeMs) / (N * budgetPerBlockMs) * 100.0;

    INFO("No-arp total time: " << noArpTimeMs << " ms");
    INFO("Arp total time: " << arpTimeMs << " ms");
    INFO("Delta: " << (arpTimeMs - noArpTimeMs) << " ms");
    INFO("Budget per block: " << budgetPerBlockMs << " ms");
    INFO("Overhead: " << overheadPct << "%");

    // The arp overhead should be negligible -- well under 0.1% of real-time budget
    CHECK(overheadPct < 0.1);
}

// =============================================================================
// T055: Stress test -- worst-case scenario
// =============================================================================

TEST_CASE("Stress test: 10 notes, ratchet=4 all steps, all lanes active, "
          "spice=100%, 200 BPM, 1/32",
          "[arp performance]") {
    PerfTestFixture f;

    // Set tempo to 200 BPM
    f.processContext.tempo = 200.0;

    // Configure the arp with worst-case settings
    {
        PerfTestParamChanges params;
        // Enable arp, mode = Up, rate = 1/32 (index 4)
        params.addChange(Ruinae::kArpEnabledId, 1.0);
        params.addChange(Ruinae::kArpModeId, 0.0);              // Up
        params.addChange(Ruinae::kArpNoteValueId, 4.0 / 20.0);  // 1/32 (index 4)

        // All 6 lane lengths = 32
        params.addChange(Ruinae::kArpVelocityLaneLengthId, 31.0 / 31.0); // len=32
        params.addChange(Ruinae::kArpGateLaneLengthId, 31.0 / 31.0);     // len=32
        params.addChange(Ruinae::kArpPitchLaneLengthId, 31.0 / 31.0);    // len=32
        params.addChange(Ruinae::kArpModifierLaneLengthId, 31.0 / 31.0); // len=32
        params.addChange(Ruinae::kArpRatchetLaneLengthId, 31.0 / 31.0);  // len=32
        params.addChange(Ruinae::kArpConditionLaneLengthId, 31.0 / 31.0);// len=32

        // Ratchet = 4 on all 32 steps (normalized: (4-1)/3 = 1.0)
        for (int i = 0; i < 32; ++i) {
            params.addChange(
                static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpRatchetLaneStep0Id + i),
                1.0);  // ratchet = 4
        }

        // Spice = 1.0 (100%)
        params.addChange(Ruinae::kArpSpiceId, 1.0);

        f.processBlockWithParams(params);
    }

    // Send 10 held MIDI notes (C3 to A3)
    for (int i = 0; i < 10; ++i) {
        f.events.addNoteOn(static_cast<int16_t>(48 + i), 0.8f);
    }
    f.processBlock();
    f.clearEvents();

    // Run process() for 10 seconds worth of blocks at 44.1kHz with 512-sample blocks
    // 10 seconds = 441000 samples = 441000/512 = ~861 blocks
    constexpr int kBlocksFor10Seconds = static_cast<int>(
        (10.0 * 44100.0) / 512.0) + 1;  // ~862

    for (int i = 0; i < kBlocksFor10Seconds; ++i) {
        f.processBlock();
    }

    // If we reach here without assertion failures or crashes, the test passes
    SUCCEED("Stress test completed without crashes or assertion failures");
}

// =============================================================================
// T056: Stress test -- all note-on events have matching note-off events
// =============================================================================
// Uses ArpeggiatorCore directly to collect all output ArpEvents and verify that
// the cumulative note-on count equals the cumulative note-off count after
// transport is stopped. This ensures no stuck notes under worst-case load.

TEST_CASE("Stress test: all note-on events have matching note-off events",
          "[arp performance]") {
    using namespace Krate::DSP;

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    ArpeggiatorCore arp;
    arp.prepare(kSampleRate, kBlockSize);
    arp.reset();

    // Enable arp, mode = Up, rate = 1/32
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::ThirtySecond, NoteModifier::None);

    // All 6 lane lengths = 32
    arp.velocityLane().setLength(32);
    arp.gateLane().setLength(32);
    arp.pitchLane().setLength(32);
    arp.modifierLane().setLength(32);
    arp.ratchetLane().setLength(32);
    arp.conditionLane().setLength(32);

    // Set modifier lane: all steps active
    for (size_t i = 0; i < 32; ++i) {
        arp.modifierLane().setStep(i, static_cast<uint8_t>(kStepActive));
    }

    // Ratchet = 4 on all 32 steps
    for (size_t i = 0; i < 32; ++i) {
        arp.ratchetLane().setStep(i, static_cast<uint8_t>(4));
    }

    // Spice = 100%
    arp.setSpice(1.0f);

    // Feed 10 held notes (C3 to A3)
    for (uint8_t i = 0; i < 10; ++i) {
        arp.noteOn(static_cast<uint8_t>(48 + i), 100);
    }

    // Prepare block context: 200 BPM, transport playing
    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 200.0;
    ctx.timeSignatureNumerator = 4;
    ctx.timeSignatureDenominator = 4;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Collect all ArpEvents over 10 seconds of processing
    // 10 seconds at 44.1kHz / 512 samples per block = ~862 blocks
    constexpr int kBlocksFor10Seconds = static_cast<int>(
        (10.0 * kSampleRate) / static_cast<double>(kBlockSize)) + 1;

    size_t totalNoteOns = 0;
    size_t totalNoteOffs = 0;
    std::array<ArpEvent, 128> blockEvents{};

    for (int i = 0; i < kBlocksFor10Seconds; ++i) {
        size_t count = arp.processBlock(ctx, blockEvents);
        for (size_t j = 0; j < count; ++j) {
            if (blockEvents[j].type == ArpEvent::Type::NoteOn) {
                ++totalNoteOns;
            } else if (blockEvents[j].type == ArpEvent::Type::NoteOff) {
                ++totalNoteOffs;
            }
        }
        ctx.transportPositionSamples += static_cast<int64_t>(kBlockSize);
    }

    // Stop transport (not disable arp) -- this should trigger note-off flush
    // for all currently sounding arp notes (FR-031 transport stop handling)
    ctx.isPlaying = false;

    // Process additional blocks after transport stop to collect remaining note-offs.
    // A few blocks should be enough for all pending note-offs to be emitted.
    constexpr int kDrainBlocks = 20;
    for (int i = 0; i < kDrainBlocks; ++i) {
        size_t count = arp.processBlock(ctx, blockEvents);
        for (size_t j = 0; j < count; ++j) {
            if (blockEvents[j].type == ArpEvent::Type::NoteOn) {
                ++totalNoteOns;
            } else if (blockEvents[j].type == ArpEvent::Type::NoteOff) {
                ++totalNoteOffs;
            }
        }
        ctx.transportPositionSamples += static_cast<int64_t>(kBlockSize);
    }

    INFO("Total note-ons: " << totalNoteOns);
    INFO("Total note-offs: " << totalNoteOffs);

    // Under stress conditions (10 notes, ratchet=4, 1/32 at 200 BPM, spice=100%),
    // the arp must produce a matched note-on/note-off count. Any mismatch means
    // stuck notes (FR-024, FR-025).
    REQUIRE(totalNoteOns > 0);
    REQUIRE(totalNoteOns == totalNoteOffs);
}
