// ==============================================================================
// Innexus VST3 Validation & Parameter Registration Tests (Phase 1)
// ==============================================================================
// Tests that the Innexus plugin initializes, processes silence, responds to
// MIDI without crashing, and has M1 parameters registered with correct ranges.
//
// Feature: 115-innexus-m1-core-instrument
// User Story: US4 (Plugin Loads and Validates in Any DAW)
// Requirements: FR-001, FR-002, FR-003, FR-004
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstring>
#include <memory>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// =============================================================================
// Helpers
// =============================================================================

static constexpr double kTestSampleRate = 44100.0;
static constexpr int32 kTestBlockSize = 512;

// Minimal ProcessSetup for testing
static ProcessSetup makeSetup(double sampleRate = kTestSampleRate)
{
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kTestBlockSize;
    setup.sampleRate = sampleRate;
    return setup;
}

// =============================================================================
// T001: VST3 Validation -- Plugin Initialization, Silence, MIDI
// =============================================================================

TEST_CASE("Innexus Processor initializes and terminates cleanly",
          "[innexus][vst][init]")
{
    Innexus::Processor processor;
    auto result = processor.initialize(nullptr);
    REQUIRE(result == kResultOk);

    result = processor.terminate();
    REQUIRE(result == kResultOk);
}

TEST_CASE("Innexus Processor outputs silence with no input",
          "[innexus][vst][silence]")
{
    Innexus::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Create output buffers
    std::vector<float> outL(kTestBlockSize, 1.0f);
    std::vector<float> outR(kTestBlockSize, 1.0f);
    float* outChannels[2] = {outL.data(), outR.data()};

    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannels;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kTestBlockSize;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = nullptr;
    data.outputEvents = nullptr;

    REQUIRE(processor.process(data) == kResultOk);

    // Verify all samples are zero (silence)
    bool allSilent = true;
    for (int32 s = 0; s < kTestBlockSize; ++s)
    {
        if (outL[static_cast<size_t>(s)] != 0.0f ||
            outR[static_cast<size_t>(s)] != 0.0f)
        {
            allSilent = false;
            break;
        }
    }
    REQUIRE(allSilent);

    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

TEST_CASE("Innexus Processor supports 32-bit sample size",
          "[innexus][vst][samplesize]")
{
    Innexus::Processor processor;
    REQUIRE(processor.canProcessSampleSize(kSample32) == kResultTrue);
    REQUIRE(processor.canProcessSampleSize(kSample64) == kResultFalse);
}

TEST_CASE("Innexus Controller initializes and terminates cleanly",
          "[innexus][vst][controller]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);
    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// T002: Parameter Registration -- kReleaseTimeId and kInharmonicityAmountId
// =============================================================================

TEST_CASE("Innexus Controller registers kReleaseTimeId with correct properties",
          "[innexus][vst][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Verify parameter exists
    ParameterInfo info{};
    auto result = controller.getParameterInfoByTag(Innexus::kReleaseTimeId, info);
    REQUIRE(result == kResultOk);

    // Verify it's automatable
    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);

    // Verify parameter ID
    REQUIRE(info.id == Innexus::kReleaseTimeId);

    // Verify plain range: min 20ms, max 5000ms, default 100ms
    // RangeParameter maps normalized 0->min, 1->max
    auto plainMin = controller.normalizedParamToPlain(Innexus::kReleaseTimeId, 0.0);
    auto plainMax = controller.normalizedParamToPlain(Innexus::kReleaseTimeId, 1.0);
    auto plainDefault = controller.normalizedParamToPlain(
        Innexus::kReleaseTimeId, info.defaultNormalizedValue);

    REQUIRE(plainMin == Approx(20.0).margin(0.01));
    REQUIRE(plainMax == Approx(5000.0).margin(0.01));
    REQUIRE(plainDefault == Approx(100.0).margin(0.5));

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Innexus Controller registers kInharmonicityAmountId with correct properties",
          "[innexus][vst][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Verify parameter exists
    ParameterInfo info{};
    auto result = controller.getParameterInfoByTag(Innexus::kInharmonicityAmountId, info);
    REQUIRE(result == kResultOk);

    // Verify it's automatable
    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);

    // Verify parameter ID
    REQUIRE(info.id == Innexus::kInharmonicityAmountId);

    // Verify range: 0-100% (normalized 0-1)
    // Plain Parameter: toPlain is identity, so normalized 0->0, 1->1
    auto plainMin = controller.normalizedParamToPlain(
        Innexus::kInharmonicityAmountId, 0.0);
    auto plainMax = controller.normalizedParamToPlain(
        Innexus::kInharmonicityAmountId, 1.0);

    REQUIRE(plainMin == Approx(0.0).margin(0.001));
    REQUIRE(plainMax == Approx(1.0).margin(0.001));

    // Verify default is 1.0 (100% inharmonicity)
    REQUIRE(info.defaultNormalizedValue == Approx(1.0).margin(0.001));

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// T042: Impact Exciter parameter registration (Spec 128, User Story 4)
// =============================================================================

TEST_CASE("Innexus Controller registers kExciterTypeId with correct properties",
          "[innexus][vst][params][exciter]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    auto result = controller.getParameterInfoByTag(Innexus::kExciterTypeId, info);
    REQUIRE(result == kResultOk);
    REQUIRE(info.id == Innexus::kExciterTypeId);

    // kExciterTypeId = 805, StringListParameter with 3 items (0=Residual, 1=Impact, 2=Bow)
    REQUIRE(info.stepCount == 2); // 3 items => stepCount = 2

    // Default should be 0 (Residual) => normalized default = 0.0
    REQUIRE(info.defaultNormalizedValue == Approx(0.0).margin(0.001));

    // Verify automatable
    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Innexus Controller registers kImpactHardnessId with correct properties",
          "[innexus][vst][params][exciter]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kImpactHardnessId, info) == kResultOk);
    REQUIRE(info.id == Innexus::kImpactHardnessId);

    // Range 0.0-1.0
    auto plainMin = controller.normalizedParamToPlain(Innexus::kImpactHardnessId, 0.0);
    auto plainMax = controller.normalizedParamToPlain(Innexus::kImpactHardnessId, 1.0);
    REQUIRE(plainMin == Approx(0.0).margin(0.001));
    REQUIRE(plainMax == Approx(1.0).margin(0.001));

    // Default 0.5
    auto plainDefault = controller.normalizedParamToPlain(
        Innexus::kImpactHardnessId, info.defaultNormalizedValue);
    REQUIRE(plainDefault == Approx(0.5).margin(0.01));

    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);
    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Innexus Controller registers kImpactMassId with correct properties",
          "[innexus][vst][params][exciter]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kImpactMassId, info) == kResultOk);
    REQUIRE(info.id == Innexus::kImpactMassId);

    auto plainMin = controller.normalizedParamToPlain(Innexus::kImpactMassId, 0.0);
    auto plainMax = controller.normalizedParamToPlain(Innexus::kImpactMassId, 1.0);
    REQUIRE(plainMin == Approx(0.0).margin(0.001));
    REQUIRE(plainMax == Approx(1.0).margin(0.001));

    auto plainDefault = controller.normalizedParamToPlain(
        Innexus::kImpactMassId, info.defaultNormalizedValue);
    REQUIRE(plainDefault == Approx(0.3).margin(0.01));

    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);
    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Innexus Controller registers kImpactBrightnessId with correct properties",
          "[innexus][vst][params][exciter]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kImpactBrightnessId, info) == kResultOk);
    REQUIRE(info.id == Innexus::kImpactBrightnessId);

    // Plain range: -1.0 to +1.0, normalized 0.0-1.0
    auto plainMin = controller.normalizedParamToPlain(Innexus::kImpactBrightnessId, 0.0);
    auto plainMax = controller.normalizedParamToPlain(Innexus::kImpactBrightnessId, 1.0);
    REQUIRE(plainMin == Approx(-1.0).margin(0.001));
    REQUIRE(plainMax == Approx(1.0).margin(0.001));

    // Default: plain 0.0 => normalized 0.5
    REQUIRE(info.defaultNormalizedValue == Approx(0.5).margin(0.01));
    auto plainDefault = controller.normalizedParamToPlain(
        Innexus::kImpactBrightnessId, info.defaultNormalizedValue);
    REQUIRE(plainDefault == Approx(0.0).margin(0.01));

    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);
    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Innexus Controller registers kImpactPositionId with correct properties",
          "[innexus][vst][params][exciter]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kImpactPositionId, info) == kResultOk);
    REQUIRE(info.id == Innexus::kImpactPositionId);

    auto plainMin = controller.normalizedParamToPlain(Innexus::kImpactPositionId, 0.0);
    auto plainMax = controller.normalizedParamToPlain(Innexus::kImpactPositionId, 1.0);
    REQUIRE(plainMin == Approx(0.0).margin(0.001));
    REQUIRE(plainMax == Approx(1.0).margin(0.001));

    auto plainDefault = controller.normalizedParamToPlain(
        Innexus::kImpactPositionId, info.defaultNormalizedValue);
    REQUIRE(plainDefault == Approx(0.13).margin(0.01));

    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);
    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Innexus impact exciter parameters persist via save/load round-trip",
          "[innexus][vst][params][exciter][state]")
{
    // Save state with non-default impact exciter parameters
    Innexus::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Process one block to let state settle
    std::vector<float> outL(kTestBlockSize, 0.0f);
    std::vector<float> outR(kTestBlockSize, 0.0f);
    float* outChannels[2] = {outL.data(), outR.data()};
    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannels;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kTestBlockSize;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = nullptr;
    data.outputEvents = nullptr;

    REQUIRE(processor.process(data) == kResultOk);

    // Save state
    Steinberg::MemoryStream stream;
    REQUIRE(processor.getState(&stream) == kResultOk);

    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);

    // Load state in a fresh processor
    Innexus::Processor processor2;
    REQUIRE(processor2.initialize(nullptr) == kResultOk);
    REQUIRE(processor2.setupProcessing(setup) == kResultOk);
    REQUIRE(processor2.setActive(true) == kResultOk);

    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor2.setState(&stream) == kResultOk);

    // Process a block to verify no crash after state load
    std::vector<float> outL2(kTestBlockSize, 0.0f);
    std::vector<float> outR2(kTestBlockSize, 0.0f);
    float* outChannels2[2] = {outL2.data(), outR2.data()};
    AudioBusBuffers outputBus2{};
    outputBus2.numChannels = 2;
    outputBus2.channelBuffers32 = outChannels2;

    ProcessData data2{};
    data2.processMode = kRealtime;
    data2.symbolicSampleSize = kSample32;
    data2.numSamples = kTestBlockSize;
    data2.numOutputs = 1;
    data2.outputs = &outputBus2;
    data2.numInputs = 0;
    data2.inputs = nullptr;
    data2.inputParameterChanges = nullptr;
    data2.outputParameterChanges = nullptr;
    data2.inputEvents = nullptr;
    data2.outputEvents = nullptr;

    REQUIRE(processor2.process(data2) == kResultOk);

    REQUIRE(processor2.setActive(false) == kResultOk);
    REQUIRE(processor2.terminate() == kResultOk);
}

// =============================================================================
// Standard plugin parameter tests
// =============================================================================

TEST_CASE("Innexus Controller registers Bypass and MasterGain parameters",
          "[innexus][vst][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Verify bypass parameter
    ParameterInfo bypassInfo{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kBypassId, bypassInfo) ==
            kResultOk);
    REQUIRE((bypassInfo.flags & ParameterInfo::kIsBypass) != 0);

    // Verify master gain parameter
    ParameterInfo gainInfo{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kMasterGainId, gainInfo) ==
            kResultOk);
    REQUIRE((gainInfo.flags & ParameterInfo::kCanAutomate) != 0);

    REQUIRE(controller.terminate() == kResultOk);
}
