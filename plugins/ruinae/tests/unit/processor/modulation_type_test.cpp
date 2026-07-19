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
#include "vst_param_changes.h"
#include "vst_event_list.h"

using Catch::Approx;

// =============================================================================
// Mock: Parameter Value Queue (single param change)
// =============================================================================

namespace {

// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using ModSingleParamQueue = Krate::Test::ParamValueQueue;
using ModParamChangeBatch = Krate::Test::ParameterChanges;




// =============================================================================
// Testable Processor (expose processParameterChanges)
// =============================================================================

class ModTestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
    using Ruinae::Processor::engine;
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
// IEventList mock consolidated into tests/test_helpers/vst_event_list.h
using ModMockEventList = Krate::Test::EventList;


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
    initBatch.add(Ruinae::kModulationTypeId, 1.0 / 3.0); // Phaser = 1 (normalized: 1/3)

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
    initBatch.add(Ruinae::kModulationTypeId, 1.0 / 3.0); // Phaser

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
    switchBatch.add(Ruinae::kModulationTypeId, 2.0 / 3.0); // Flanger = 2 (normalized: 2/3)

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
    initBatch.add(Ruinae::kModulationTypeId, 2.0 / 3.0); // Flanger

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
    switchBatch.add(Ruinae::kModulationTypeId, 2.0 / 3.0); // Flanger

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

// =============================================================================
// Flanger and chorus must be re-applied from the loaded preset state
// =============================================================================
// Every other effect re-applies its parameters from the atomics in
// applyParamsToEngine() every block. Flanger and chorus were driven only by live
// parameter-change events through an inline switch, and applyParamsToEngine()
// never touched them. Loading a preset calls engine_.reset(), which returns the
// flanger and chorus DSP to their defaults -- and nothing then pushed the loaded
// values back down, so both ran at reset defaults until the user moved a control.

namespace {

void setNormalizedParam(Ruinae::Processor* proc, Steinberg::Vst::ParamID id, double value) {
    Krate::Test::ParameterChanges changes;
    changes.addChange(id, value);
    std::vector<float> l;
    std::vector<float> r;
    processModBlock(proc, 32, l, r, &changes);
}

std::vector<char> grabState(Ruinae::Processor* proc) {
    Steinberg::MemoryStream stream;
    proc->getState(&stream);
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> data(static_cast<size_t>(size));
    Steinberg::int32 read = 0;
    stream.read(data.data(), static_cast<Steinberg::int32>(size), &read);
    return data;
}

void putState(Ruinae::Processor* proc, const std::vector<char>& bytes) {
    Steinberg::MemoryStream stream;
    stream.write(const_cast<char*>(bytes.data()),
                 static_cast<Steinberg::int32>(bytes.size()), nullptr);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    proc->setState(&stream);
}

} // namespace

TEST_CASE("Loading a preset restores flanger and chorus DSP values",
          "[ruinae][processor][flanger][chorus][regression]") {
    auto source = makeModTestableProcessor();

    // Drive the parameters well away from their defaults, then capture.
    setNormalizedParam(source.get(), Ruinae::kFlangerRateId, 0.9);
    setNormalizedParam(source.get(), Ruinae::kFlangerDepthId, 0.8);
    setNormalizedParam(source.get(), Ruinae::kChorusRateId, 0.9);
    setNormalizedParam(source.get(), Ruinae::kChorusDepthId, 0.8);

    const float expectedFlangerRate =
        source->engine().effectsChain().flanger().getRate();
    const float expectedFlangerDepth =
        source->engine().effectsChain().flanger().getDepth();
    const float expectedChorusRate =
        source->engine().effectsChain().chorus().getRate();
    const float expectedChorusDepth =
        source->engine().effectsChain().chorus().getDepth();

    const auto preset = grabState(source.get());

    auto target = makeModTestableProcessor();
    putState(target.get(), preset);
    drainPresetTransfer(target.get());

    // One ordinary block, with no parameter changes -- exactly what happens in a
    // host after a preset is recalled and before the user touches anything.
    std::vector<float> l;
    std::vector<float> r;
    processModBlock(target.get(), 32, l, r);

    CHECK(target->engine().effectsChain().flanger().getRate()
          == Approx(expectedFlangerRate).margin(1e-4));
    CHECK(target->engine().effectsChain().flanger().getDepth()
          == Approx(expectedFlangerDepth).margin(1e-4));
    CHECK(target->engine().effectsChain().chorus().getRate()
          == Approx(expectedChorusRate).margin(1e-4));
    CHECK(target->engine().effectsChain().chorus().getDepth()
          == Approx(expectedChorusDepth).margin(1e-4));
}

// =============================================================================
// Each envelope's parameters must reach its own envelope
// =============================================================================
// The amp, filter and mod envelope blocks in applyParamsToEngine are generated
// from one body with the setAmp*/setFilter*/setMod* prefix pasted in. Crossing
// two prefixes still compiles -- every family has the same setter names -- so
// this drives the three attack times to different values and checks that each
// envelope responded to its own.

TEST_CASE("Amp, filter and mod envelope parameters reach their own envelopes",
          "[ruinae][processor][envelope][regression]") {
    auto proc = makeModTestableProcessor();

    ModParamChangeBatch params;
    params.add(Ruinae::kAmpEnvAttackId, 1.0);    // longest attack
    params.add(Ruinae::kFilterEnvAttackId, 0.0); // instant
    params.add(Ruinae::kModEnvAttackId, 0.0);    // instant

    ModMockEventList noteOn;
    noteOn.addNoteOn(60, 0.8f);

    std::vector<float> l;
    std::vector<float> r;
    processModBlockWithEvents(proc.get(), 512, l, r, &params, &noteOn);

    const float amp = proc->engine().getVoiceAmpEnvelope(0).getOutput();
    const float filter = proc->engine().getVoiceFilterEnvelope(0).getOutput();
    const float mod = proc->engine().getVoiceModEnvelope(0).getOutput();

    // A few milliseconds into the note the two instant envelopes are already up
    // while the slow one has barely moved. Swap any two prefixes and this flips.
    CHECK(filter > 0.5f);
    CHECK(mod > 0.5f);
    CHECK(amp < filter);
}
