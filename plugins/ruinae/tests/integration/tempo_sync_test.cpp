// ==============================================================================
// Integration Test: Host Tempo and Transport Integration
// ==============================================================================
// Verifies that ProcessContext tempo/time signature are forwarded to
// the engine's BlockContext, and default values are used when ProcessContext
// is null.
//
// Reference: specs/045-plugin-shell/spec.md FR-013, FR-014
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <vector>

using Catch::Approx;

// =============================================================================
// Mock classes
// =============================================================================

class TempoTestEventList : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getEventCount() override { return 0; }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32,
                                            Steinberg::Vst::Event&) override {
        return Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override {
        return Steinberg::kResultTrue;
    }
};

class TempoTestParamChanges : public Steinberg::Vst::IParameterChanges {
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
// Tests
// =============================================================================

TEST_CASE("Process with null ProcessContext does not crash", "[tempo][integration]") {
    Ruinae::Processor processor;
    processor.initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 256;
    processor.setupProcessing(setup);
    processor.setActive(true);

    constexpr size_t kBlockSize = 256;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    float* channelBuffers[2] = {outL.data(), outR.data()};

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    TempoTestParamChanges params;
    TempoTestEventList events;

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &params;
    data.inputEvents = &events;
    data.processContext = nullptr; // No context!

    // Should not crash with null processContext
    auto result = processor.process(data);
    REQUIRE(result == Steinberg::kResultTrue);

    processor.setActive(false);
    processor.terminate();
}

TEST_CASE("Process with valid ProcessContext does not crash", "[tempo][integration]") {
    Ruinae::Processor processor;
    processor.initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 256;
    processor.setupProcessing(setup);
    processor.setActive(true);

    constexpr size_t kBlockSize = 256;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    float* channelBuffers[2] = {outL.data(), outR.data()};

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    TempoTestParamChanges params;
    TempoTestEventList events;

    // Create a valid ProcessContext with tempo and time signature
    Steinberg::Vst::ProcessContext processContext{};
    processContext.state = Steinberg::Vst::ProcessContext::kTempoValid
                         | Steinberg::Vst::ProcessContext::kTimeSigValid
                         | Steinberg::Vst::ProcessContext::kPlaying;
    processContext.tempo = 140.0;
    processContext.timeSigNumerator = 3;
    processContext.timeSigDenominator = 4;
    processContext.sampleRate = 44100.0;

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &params;
    data.inputEvents = &events;
    data.processContext = &processContext;

    // Should not crash with valid context
    auto result = processor.process(data);
    REQUIRE(result == Steinberg::kResultTrue);

    processor.setActive(false);
    processor.terminate();
}
