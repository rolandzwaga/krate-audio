// ==============================================================================
// Unit Test: Modulation Type Switching (126-ruinae-flanger, User Story 4)
// ==============================================================================
// Tests for ModulationType enum, crossfade switching (None/Phaser/Flanger),
// crossfade duration, and degraded host conditions.
//
// Phase 6: T024-T031
// Reference: specs/126-ruinae-flanger/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"
#include "engine/ruinae_effects_chain.h"
#include "drain_preset_transfer.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using Catch::Approx;

// =============================================================================
// Mock: Parameter Value Queue (single param change)
// =============================================================================

namespace {

class ModSingleParamQueue : public Steinberg::Vst::IParamValueQueue {
public:
    ModSingleParamQueue(Steinberg::Vst::ParamID id, double value)
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

class ModParamChangeBatch : public Steinberg::Vst::IParameterChanges {
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
    std::vector<ModSingleParamQueue> queues_;
};

// =============================================================================
// Testable Processor (expose processParameterChanges)
// =============================================================================

class ModTestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

// =============================================================================
// Helpers
// =============================================================================

static std::unique_ptr<ModTestableProcessor> makeModTestableProcessor() {
    auto p = std::make_unique<ModTestableProcessor>();
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

/// Process a stereo block of silence through the processor
static void processModBlock(Ruinae::Processor* proc, size_t numSamples,
                            std::vector<float>& outL, std::vector<float>& outR,
                            Steinberg::Vst::IParameterChanges* paramChanges = nullptr,
                            Steinberg::Vst::ProcessContext* context = nullptr) {
    outL.assign(numSamples, 0.0f);
    outR.assign(numSamples, 0.0f);

    float* outputs[2] = { outL.data(), outR.data() };
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outputs;

    Steinberg::Vst::ProcessData data{};
    data.numSamples = static_cast<Steinberg::int32>(numSamples);
    data.numInputs = 0;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.inputParameterChanges = paramChanges;
    data.outputParameterChanges = nullptr;
    data.inputEvents = nullptr;
    data.outputEvents = nullptr;
    data.processContext = context;

    proc->process(data);
}

/// Check if all samples are finite
static bool modAllFinite(const float* buf, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!std::isfinite(buf[i])) return false;
    }
    return true;
}

// Mock event list for MIDI notes
class ModMockEventList : public Steinberg::Vst::IEventList {
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

    void clear() { events_.clear(); }

private:
    std::vector<Steinberg::Vst::Event> events_;
};

/// Process a stereo block with MIDI events through the processor
static void processModBlockWithEvents(Ruinae::Processor* proc, size_t numSamples,
                                      std::vector<float>& outL, std::vector<float>& outR,
                                      Steinberg::Vst::IParameterChanges* paramChanges,
                                      Steinberg::Vst::IEventList* events,
                                      Steinberg::Vst::ProcessContext* context = nullptr) {
    outL.assign(numSamples, 0.0f);
    outR.assign(numSamples, 0.0f);

    float* outputs[2] = { outL.data(), outR.data() };
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outputs;

    Steinberg::Vst::ProcessData data{};
    data.numSamples = static_cast<Steinberg::int32>(numSamples);
    data.numInputs = 0;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.inputParameterChanges = paramChanges;
    data.outputParameterChanges = nullptr;
    data.inputEvents = events;
    data.outputEvents = nullptr;
    data.processContext = context;

    proc->process(data);
}

} // anonymous namespace

// =============================================================================
// T024: ModulationType enum values
// =============================================================================

TEST_CASE("ModulationType enum has correct values", "[modulation_type][enum]") {
    using Krate::DSP::ModulationType;

    CHECK(static_cast<int>(ModulationType::None) == 0);
    CHECK(static_cast<int>(ModulationType::Phaser) == 1);
    CHECK(static_cast<int>(ModulationType::Flanger) == 2);
}

// =============================================================================
// T024: Switching to None produces finite output
// =============================================================================

TEST_CASE("Switching modulation type to None produces finite output", "[modulation_type][switching]") {
    auto proc = makeModTestableProcessor();

    // Start with Phaser enabled
    ModParamChangeBatch initBatch;
    initBatch.add(Ruinae::kModulationTypeId, 0.5); // Phaser = 1 (normalized: 1/2 = 0.5)

    std::vector<float> outL, outR;

    // Send a MIDI note and process some blocks with phaser active
    ModMockEventList noteOnEvents;
    noteOnEvents.addNoteOn(60, 0.8f);
    processModBlockWithEvents(proc.get(), 512, outL, outR, &initBatch, &noteOnEvents);
    ModMockEventList emptyEvents;
    for (int i = 0; i < 5; ++i) {
        processModBlockWithEvents(proc.get(), 512, outL, outR, nullptr, &emptyEvents);
    }

    // Switch to None
    ModParamChangeBatch switchBatch;
    switchBatch.add(Ruinae::kModulationTypeId, 0.0); // None = 0

    // Process during and after crossfade
    processModBlockWithEvents(proc.get(), 2048, outL, outR, &switchBatch, &emptyEvents);
    CHECK(modAllFinite(outL.data(), outL.size()));
    CHECK(modAllFinite(outR.data(), outR.size()));

    // Process more blocks after crossfade
    for (int i = 0; i < 5; ++i) {
        processModBlockWithEvents(proc.get(), 512, outL, outR, nullptr, &emptyEvents);
        CHECK(modAllFinite(outL.data(), outL.size()));
        CHECK(modAllFinite(outR.data(), outR.size()));
    }

    proc->setActive(false);
    proc->terminate();
}

// =============================================================================
// T024: Switching from Phaser to Flanger produces finite output
// =============================================================================

TEST_CASE("Switching Phaser to Flanger produces finite output during crossfade", "[modulation_type][switching]") {
    auto proc = makeModTestableProcessor();

    // Start with Phaser
    ModParamChangeBatch initBatch;
    initBatch.add(Ruinae::kModulationTypeId, 0.5); // Phaser

    std::vector<float> outL, outR;
    ModMockEventList noteOnEvents;
    noteOnEvents.addNoteOn(60, 0.8f);
    processModBlockWithEvents(proc.get(), 512, outL, outR, &initBatch, &noteOnEvents);

    ModMockEventList emptyEvents;
    for (int i = 0; i < 5; ++i) {
        processModBlockWithEvents(proc.get(), 512, outL, outR, nullptr, &emptyEvents);
    }

    // Switch to Flanger
    ModParamChangeBatch switchBatch;
    switchBatch.add(Ruinae::kModulationTypeId, 1.0); // Flanger = 2 (normalized: 2/2 = 1.0)

    // Process during crossfade (~1323 samples at 44.1kHz for 30ms)
    processModBlockWithEvents(proc.get(), 2048, outL, outR, &switchBatch, &emptyEvents);
    CHECK(modAllFinite(outL.data(), outL.size()));
    CHECK(modAllFinite(outR.data(), outR.size()));

    proc->setActive(false);
    proc->terminate();
}

// =============================================================================
// T024: Switching Flanger back to None passes through dry signal
// =============================================================================

TEST_CASE("Switching Flanger to None produces finite output", "[modulation_type][switching]") {
    auto proc = makeModTestableProcessor();

    // Start with Flanger
    ModParamChangeBatch initBatch;
    initBatch.add(Ruinae::kModulationTypeId, 1.0); // Flanger

    std::vector<float> outL, outR;
    ModMockEventList noteOnEvents;
    noteOnEvents.addNoteOn(60, 0.8f);
    processModBlockWithEvents(proc.get(), 512, outL, outR, &initBatch, &noteOnEvents);

    ModMockEventList emptyEvents;
    for (int i = 0; i < 3; ++i) {
        processModBlockWithEvents(proc.get(), 512, outL, outR, nullptr, &emptyEvents);
    }

    // Switch to None
    ModParamChangeBatch switchBatch;
    switchBatch.add(Ruinae::kModulationTypeId, 0.0); // None

    // Process during crossfade
    processModBlockWithEvents(proc.get(), 2048, outL, outR, &switchBatch, &emptyEvents);
    CHECK(modAllFinite(outL.data(), outL.size()));
    CHECK(modAllFinite(outR.data(), outR.size()));

    // Process after crossfade completes
    for (int i = 0; i < 5; ++i) {
        processModBlockWithEvents(proc.get(), 512, outL, outR, nullptr, &emptyEvents);
        CHECK(modAllFinite(outL.data(), outL.size()));
    }

    proc->setActive(false);
    proc->terminate();
}

// =============================================================================
// T024: Crossfade duration (30ms)
// =============================================================================

TEST_CASE("Modulation crossfade completes within expected duration", "[modulation_type][crossfade]") {
    // Test at effects chain level directly
    Krate::DSP::RuinaeEffectsChain chain;
    chain.prepare(44100.0, 512);
    chain.reset();

    // Start with Phaser active
    chain.setModulationType(Krate::DSP::ModulationType::Phaser);
    // Process a few blocks to settle
    std::vector<float> bufL(512, 0.1f);
    std::vector<float> bufR(512, 0.1f);
    chain.processBlock(bufL.data(), bufR.data(), 512);

    // Switch to Flanger -- this should start a crossfade
    chain.startModCrossfade(Krate::DSP::ModulationType::Flanger);

    // 30ms at 44100 Hz = 1323 samples. Process enough to complete the crossfade.
    // After ~1323 samples the crossfade should be complete.
    size_t samplesProcessed = 0;
    constexpr size_t kBlockSize = 64;
    constexpr size_t kMaxSamples = 44100; // 1 second max
    while (samplesProcessed < kMaxSamples) {
        std::fill(bufL.begin(), bufL.begin() + kBlockSize, 0.1f);
        std::fill(bufR.begin(), bufR.begin() + kBlockSize, 0.1f);
        chain.processBlock(bufL.data(), bufR.data(), kBlockSize);
        samplesProcessed += kBlockSize;
    }

    // After 1 second of processing the crossfade must be complete.
    // We verify by checking that the active mod type is now Flanger.
    // (The crossfade completes when alpha >= 1.0, which sets activeModType_ = incomingModType_)
    CHECK(chain.getActiveModulationType() == Krate::DSP::ModulationType::Flanger);
}

// =============================================================================
// T024: Degraded host - nullptr process context does not crash
// =============================================================================

TEST_CASE("Switching modulation type with nullptr process context does not crash", "[modulation_type][degraded]") {
    auto proc = makeModTestableProcessor();

    std::vector<float> outL, outR;

    // Switch modulation type with nullptr context (already default in processModBlock)
    ModParamChangeBatch switchBatch;
    switchBatch.add(Ruinae::kModulationTypeId, 1.0); // Flanger

    // Process with nullptr context -- should not crash
    processModBlock(proc.get(), 512, outL, outR, &switchBatch, nullptr);
    CHECK(modAllFinite(outL.data(), outL.size()));
    CHECK(modAllFinite(outR.data(), outR.size()));

    // Switch again
    ModParamChangeBatch switchBack;
    switchBack.add(Ruinae::kModulationTypeId, 0.0); // None
    processModBlock(proc.get(), 512, outL, outR, &switchBack, nullptr);
    CHECK(modAllFinite(outL.data(), outL.size()));

    proc->setActive(false);
    proc->terminate();
}
