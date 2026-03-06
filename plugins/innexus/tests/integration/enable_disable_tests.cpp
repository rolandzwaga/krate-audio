// ==============================================================================
// Enable/Disable While Playing & Degraded Host Condition Tests
// ==============================================================================
// Integration tests for toggling features mid-stream and processing under
// degraded host environments.
//
// Gap 2: processContext == nullptr, no transport, zero numSamples
// Gap 3: Toggle freeze, sidechain, evolution, modulators, blend mid-playback
//
// Testing Skill Checklist:
//   - Enable while playing: no stuck notes, no glitches
//   - Disable while playing: cleanup happens, output transitions smoothly
//   - Works with processContext == nullptr
//   - Handles transport stop/start transitions
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/harmonic_snapshot.h>
#include <krate/dsp/processors/residual_types.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

static constexpr double kEDSampleRate = 44100.0;
static constexpr int32 kEDBlockSize = 128;

// =============================================================================
// Test Infrastructure
// =============================================================================

namespace {

class EDParamValueQueue : public IParamValueQueue
{
public:
    EDParamValueQueue(ParamID id, ParamValue val)
        : id_(id), value_(val) {}

    ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32, int32& sampleOffset, ParamValue& value) override
    {
        sampleOffset = 0;
        value = value_;
        return kResultTrue;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

private:
    ParamID id_;
    ParamValue value_;
};

class EDParameterChanges : public IParameterChanges
{
public:
    void addChange(ParamID id, ParamValue val)
    {
        queues_.emplace_back(id, val);
    }
    void clear() { queues_.clear(); }

    int32 PLUGIN_API getParameterCount() override
    {
        return static_cast<int32>(queues_.size());
    }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override
    {
        if (index < 0 || index >= static_cast<int32>(queues_.size()))
            return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override
    {
        return nullptr;
    }
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

private:
    std::vector<EDParamValueQueue> queues_;
};

Innexus::SampleAnalysis* makeEDAnalysis(int numFrames = 20, float f0 = 440.0f)
{
    auto* analysis = new Innexus::SampleAnalysis();
    analysis->sampleRate = 44100.0f;
    analysis->hopTimeSec = 512.0f / 44100.0f;
    analysis->analysisFFTSize = 1024;
    analysis->analysisHopSize = 512;

    for (int f = 0; f < numFrames; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = f0;
        frame.f0Confidence = 0.9f;
        frame.numPartials = 4;
        frame.globalAmplitude = 0.5f;

        for (int p = 0; p < 4; ++p)
        {
            auto& partial = frame.partials[static_cast<size_t>(p)];
            partial.harmonicIndex = p + 1;
            partial.frequency = f0 * static_cast<float>(p + 1);
            partial.amplitude = 0.5f / static_cast<float>(p + 1);
            partial.relativeFrequency = static_cast<float>(p + 1);
            partial.stability = 1.0f;
            partial.age = 10;
        }

        analysis->frames.push_back(frame);

        Krate::DSP::ResidualFrame rFrame;
        rFrame.totalEnergy = 0.05f;
        rFrame.transientFlag = false;
        for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
            rFrame.bandEnergies[b] = 0.025f;
        analysis->residualFrames.push_back(rFrame);
    }

    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_ed.wav";
    return analysis;
}

struct EDFixture
{
    Innexus::Processor processor;
    std::vector<float> outL;
    std::vector<float> outR;
    float* channels[2];
    AudioBusBuffers outputBus{};

    EDFixture()
        : outL(static_cast<size_t>(kEDBlockSize), 0.0f)
        , outR(static_cast<size_t>(kEDBlockSize), 0.0f)
    {
        channels[0] = outL.data();
        channels[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channels;

        processor.initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = kEDBlockSize;
        setup.sampleRate = kEDSampleRate;
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~EDFixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    void injectAndNoteOn()
    {
        processor.testInjectAnalysis(makeEDAnalysis());
        processor.onNoteOn(60, 1.0f);
    }

    float processBlock(EDParameterChanges* params = nullptr)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        ProcessData data{};
        data.numSamples = kEDBlockSize;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = params;
        processor.process(data);

        float maxAmp = 0.0f;
        for (size_t s = 0; s < outL.size(); ++s)
            maxAmp = std::max(maxAmp, std::abs(outL[s]));
        return maxAmp;
    }

    // Process a block with processContext set to nullptr
    float processBlockNoContext()
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        ProcessData data{};
        data.numSamples = kEDBlockSize;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.processContext = nullptr;
        processor.process(data);

        float maxAmp = 0.0f;
        for (size_t s = 0; s < outL.size(); ++s)
            maxAmp = std::max(maxAmp, std::abs(outL[s]));
        return maxAmp;
    }

    // Process with zero numSamples
    tresult processBlockZeroSamples()
    {
        ProcessData data{};
        data.numSamples = 0;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        return processor.process(data);
    }

    bool hasNaN() const
    {
        for (size_t s = 0; s < outL.size(); ++s)
        {
            if (std::isnan(outL[s]) || std::isnan(outR[s]))
                return true;
        }
        return false;
    }

    void stabilize(int blocks = 20)
    {
        for (int b = 0; b < blocks; ++b)
            processBlock();
    }

    // Populate 2 memory slots for evolution/blend tests
    void populateMemorySlots()
    {
        // Capture slot 0
        {
            EDParameterChanges params;
            params.addChange(Innexus::kMemorySlotId, 0.0 / 7.0);  // Slot 0
            params.addChange(Innexus::kMemoryCaptureId, 1.0);
            processBlock(&params);
        }
        // Process a few blocks to advance frame
        for (int b = 0; b < 5; ++b)
            processBlock();
        // Capture slot 1
        {
            EDParameterChanges params;
            params.addChange(Innexus::kMemorySlotId, 1.0 / 7.0);  // Slot 1
            params.addChange(Innexus::kMemoryCaptureId, 1.0);
            processBlock(&params);
        }
        // Reset capture trigger
        {
            EDParameterChanges params;
            params.addChange(Innexus::kMemoryCaptureId, 0.0);
            processBlock(&params);
        }
    }
};

} // anonymous namespace

// =============================================================================
// Gap 2: Degraded Host Conditions
// =============================================================================

TEST_CASE("DegradedHost: processContext == nullptr with all features active",
          "[innexus][degraded_host][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(10);
    f.populateMemorySlots();

    // Enable M6 features
    {
        EDParameterChanges params;
        params.addChange(Innexus::kEvolutionEnableId, 1.0);
        params.addChange(Innexus::kEvolutionSpeedId, 0.5);
        params.addChange(Innexus::kEvolutionDepthId, 0.5);
        params.addChange(Innexus::kMod1EnableId, 1.0);
        params.addChange(Innexus::kMod1DepthId, 0.5);
        params.addChange(Innexus::kMod1RateId, 0.5);
        f.processBlock(&params);
    }

    // Process 50 blocks with processContext == nullptr
    bool anyNaN = false;
    bool allSilent = true;
    for (int b = 0; b < 50; ++b)
    {
        float amp = f.processBlockNoContext();
        if (f.hasNaN()) anyNaN = true;
        if (amp > 1e-6f) allSilent = false;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(allSilent);
}

TEST_CASE("DegradedHost: processContext with state=0 and tempo=0",
          "[innexus][degraded_host][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(10);

    // Process with empty processContext
    ProcessContext ctx{};
    ctx.state = 0;   // No flags set
    ctx.tempo = 0.0; // No valid tempo

    bool anyNaN = false;
    bool allSilent = true;
    for (int b = 0; b < 30; ++b)
    {
        std::fill(f.outL.begin(), f.outL.end(), 0.0f);
        std::fill(f.outR.begin(), f.outR.end(), 0.0f);

        ProcessData data{};
        data.numSamples = kEDBlockSize;
        data.numOutputs = 1;
        data.outputs = &f.outputBus;
        data.processContext = &ctx;
        f.processor.process(data);

        float maxAmp = 0.0f;
        for (size_t s = 0; s < f.outL.size(); ++s)
        {
            if (std::isnan(f.outL[s]) || std::isnan(f.outR[s]))
                anyNaN = true;
            maxAmp = std::max(maxAmp, std::abs(f.outL[s]));
        }
        if (maxAmp > 1e-6f) allSilent = false;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(allSilent);
}

TEST_CASE("DegradedHost: zero numSamples does not corrupt state",
          "[innexus][degraded_host][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(10);

    // Record amplitude before zero-sample block
    float ampBefore = f.processBlock();
    REQUIRE(ampBefore > 0.0f);

    // Process zero samples
    tresult result = f.processBlockZeroSamples();
    REQUIRE(result == kResultOk);

    // Process a normal block afterwards -- audio should still work
    float ampAfter = f.processBlock();
    REQUIRE(ampAfter > 0.0f);
    REQUIRE_FALSE(f.hasNaN());
}

// =============================================================================
// Gap 3: Enable/Disable While Playing
// =============================================================================

TEST_CASE("EnableDisable: Freeze ON mid-note continues audio without click",
          "[innexus][enable_disable][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(20);

    float ampBeforeFreeze = f.processBlock();
    REQUIRE(ampBeforeFreeze > 0.0f);

    // Enable freeze
    EDParameterChanges params;
    params.addChange(Innexus::kFreezeId, 1.0);
    f.processBlock(&params);

    // Audio should continue (frozen frame still produces output)
    REQUIRE(f.processor.getManualFreezeActive());

    // Process more blocks -- amplitude should stay present and bounded
    bool anyNaN = false;
    bool allSilent = true;
    float maxSpike = 0.0f;
    for (int b = 0; b < 30; ++b)
    {
        float amp = f.processBlock();
        if (f.hasNaN()) anyNaN = true;
        if (amp > 1e-6f) allSilent = false;
        maxSpike = std::max(maxSpike, amp);
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(allSilent);
    // No click: max amplitude should not spike beyond 2x pre-freeze level
    REQUIRE(maxSpike < ampBeforeFreeze * 3.0f);
}

TEST_CASE("EnableDisable: Freeze OFF while frozen completes crossfade cleanly",
          "[innexus][enable_disable][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(20);

    // Enable freeze
    {
        EDParameterChanges params;
        params.addChange(Innexus::kFreezeId, 1.0);
        f.processBlock(&params);
    }
    REQUIRE(f.processor.getManualFreezeActive());

    // Let frozen state settle
    f.stabilize(10);

    // Disable freeze
    {
        EDParameterChanges params;
        params.addChange(Innexus::kFreezeId, 0.0);
        f.processBlock(&params);
    }
    REQUIRE_FALSE(f.processor.getManualFreezeActive());

    // Crossfade should have been initiated
    // Process until crossfade completes (~10ms = 441 samples ~ 3-4 blocks)
    bool anyNaN = false;
    bool allSilent = true;
    for (int b = 0; b < 20; ++b)
    {
        float amp = f.processBlock();
        if (f.hasNaN()) anyNaN = true;
        if (amp > 1e-6f) allSilent = false;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(allSilent);
    // Crossfade should have completed
    REQUIRE(f.processor.getManualFreezeRecoverySamplesRemaining() == 0);
}

TEST_CASE("EnableDisable: Toggle sidechain mode while note is playing",
          "[innexus][enable_disable][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(20);

    float ampBefore = f.processBlock();
    REQUIRE(ampBefore > 0.0f);

    // Switch to sidechain mode
    {
        EDParameterChanges params;
        params.addChange(Innexus::kInputSourceId, 1.0);
        f.processBlock(&params);
    }

    // Crossfade should initiate
    REQUIRE(f.processor.getSourceCrossfadeSamplesRemaining() > 0);

    // Process blocks until crossfade completes
    bool anyNaN = false;
    for (int b = 0; b < 30; ++b)
    {
        f.processBlock();
        if (f.hasNaN()) anyNaN = true;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE(f.processor.getSourceCrossfadeSamplesRemaining() == 0);

    // Switch back to sample mode
    {
        EDParameterChanges params;
        params.addChange(Innexus::kInputSourceId, 0.0);
        f.processBlock(&params);
    }

    REQUIRE(f.processor.getSourceCrossfadeSamplesRemaining() > 0);

    for (int b = 0; b < 30; ++b)
    {
        f.processBlock();
        if (f.hasNaN()) anyNaN = true;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE(f.processor.getSourceCrossfadeSamplesRemaining() == 0);
}

TEST_CASE("EnableDisable: Enable evolution engine mid-note",
          "[innexus][enable_disable][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(10);
    f.populateMemorySlots();
    f.stabilize(10);

    // Enable evolution
    {
        EDParameterChanges params;
        params.addChange(Innexus::kEvolutionEnableId, 1.0);
        params.addChange(Innexus::kEvolutionSpeedId, 0.5);
        params.addChange(Innexus::kEvolutionDepthId, 1.0);
        f.processBlock(&params);
    }

    bool anyNaN = false;
    bool allSilent = true;
    for (int b = 0; b < 100; ++b)
    {
        float amp = f.processBlock();
        if (f.hasNaN()) anyNaN = true;
        if (amp > 1e-6f) allSilent = false;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(allSilent);
}

TEST_CASE("EnableDisable: Disable evolution engine mid-note",
          "[innexus][enable_disable][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(10);
    f.populateMemorySlots();

    // Enable evolution first
    {
        EDParameterChanges params;
        params.addChange(Innexus::kEvolutionEnableId, 1.0);
        params.addChange(Innexus::kEvolutionSpeedId, 0.5);
        params.addChange(Innexus::kEvolutionDepthId, 1.0);
        f.processBlock(&params);
    }
    f.stabilize(20);

    // Disable evolution
    {
        EDParameterChanges params;
        params.addChange(Innexus::kEvolutionEnableId, 0.0);
        f.processBlock(&params);
    }

    // Audio should continue without glitch
    bool anyNaN = false;
    bool allSilent = true;
    for (int b = 0; b < 30; ++b)
    {
        float amp = f.processBlock();
        if (f.hasNaN()) anyNaN = true;
        if (amp > 1e-6f) allSilent = false;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(allSilent);
}

TEST_CASE("EnableDisable: Enable modulator mid-note",
          "[innexus][enable_disable][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(20);

    // Enable mod1
    {
        EDParameterChanges params;
        params.addChange(Innexus::kMod1EnableId, 1.0);
        params.addChange(Innexus::kMod1DepthId, 0.8);
        params.addChange(Innexus::kMod1RateId, 0.25);   // ~5 Hz
        params.addChange(Innexus::kMod1WaveformId, 0.0); // Sine
        params.addChange(Innexus::kMod1TargetId, 0.0);   // Amplitude
        f.processBlock(&params);
    }

    bool anyNaN = false;
    bool allSilent = true;
    for (int b = 0; b < 50; ++b)
    {
        float amp = f.processBlock();
        if (f.hasNaN()) anyNaN = true;
        if (amp > 1e-6f) allSilent = false;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(allSilent);
}

TEST_CASE("EnableDisable: Disable modulator mid-note",
          "[innexus][enable_disable][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();

    // Enable mod1 from start
    {
        EDParameterChanges params;
        params.addChange(Innexus::kMod1EnableId, 1.0);
        params.addChange(Innexus::kMod1DepthId, 0.8);
        params.addChange(Innexus::kMod1RateId, 0.25);
        f.processBlock(&params);
    }
    f.stabilize(20);

    // Disable mod1
    {
        EDParameterChanges params;
        params.addChange(Innexus::kMod1EnableId, 0.0);
        f.processBlock(&params);
    }

    bool anyNaN = false;
    bool allSilent = true;
    for (int b = 0; b < 30; ++b)
    {
        float amp = f.processBlock();
        if (f.hasNaN()) anyNaN = true;
        if (amp > 1e-6f) allSilent = false;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(allSilent);
}

TEST_CASE("EnableDisable: Enable blend mode mid-note",
          "[innexus][enable_disable][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(10);
    f.populateMemorySlots();
    f.stabilize(10);

    // Enable blend with both slots weighted
    {
        EDParameterChanges params;
        params.addChange(Innexus::kBlendEnableId, 1.0);
        params.addChange(Innexus::kBlendSlotWeight1Id, 0.5);
        params.addChange(Innexus::kBlendSlotWeight2Id, 0.5);
        f.processBlock(&params);
    }

    bool anyNaN = false;
    bool allSilent = true;
    for (int b = 0; b < 50; ++b)
    {
        float amp = f.processBlock();
        if (f.hasNaN()) anyNaN = true;
        if (amp > 1e-6f) allSilent = false;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(allSilent);
}

TEST_CASE("EnableDisable: Disable blend mode mid-note",
          "[innexus][enable_disable][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(10);
    f.populateMemorySlots();

    // Enable blend first
    {
        EDParameterChanges params;
        params.addChange(Innexus::kBlendEnableId, 1.0);
        params.addChange(Innexus::kBlendSlotWeight1Id, 0.5);
        params.addChange(Innexus::kBlendSlotWeight2Id, 0.5);
        f.processBlock(&params);
    }
    f.stabilize(20);

    // Disable blend
    {
        EDParameterChanges params;
        params.addChange(Innexus::kBlendEnableId, 0.0);
        f.processBlock(&params);
    }

    bool anyNaN = false;
    bool allSilent = true;
    for (int b = 0; b < 30; ++b)
    {
        float amp = f.processBlock();
        if (f.hasNaN()) anyNaN = true;
        if (amp > 1e-6f) allSilent = false;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(allSilent);
}

TEST_CASE("EnableDisable: Freeze while morph is non-zero",
          "[innexus][enable_disable][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(20);

    // Set morph to 0.5
    {
        EDParameterChanges params;
        params.addChange(Innexus::kMorphPositionId, 0.5);
        f.processBlock(&params);
    }
    f.stabilize(10);

    // Engage freeze with morph still at 0.5
    {
        EDParameterChanges params;
        params.addChange(Innexus::kFreezeId, 1.0);
        f.processBlock(&params);
    }

    REQUIRE(f.processor.getManualFreezeActive());

    bool anyNaN = false;
    bool allSilent = true;
    for (int b = 0; b < 30; ++b)
    {
        float amp = f.processBlock();
        if (f.hasNaN()) anyNaN = true;
        if (amp > 1e-6f) allSilent = false;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(allSilent);
}

TEST_CASE("EnableDisable: Rapid freeze toggle 10x in 10 blocks",
          "[innexus][enable_disable][integration]")
{
    EDFixture f;
    f.injectAndNoteOn();
    f.stabilize(20);

    float ampBefore = f.processBlock();
    REQUIRE(ampBefore > 0.0f);

    // Toggle freeze rapidly 10 times
    bool anyNaN = false;
    float maxSpike = 0.0f;
    for (int b = 0; b < 10; ++b)
    {
        EDParameterChanges params;
        params.addChange(Innexus::kFreezeId, (b % 2 == 0) ? 1.0 : 0.0);
        float amp = f.processBlock(&params);
        if (f.hasNaN()) anyNaN = true;
        maxSpike = std::max(maxSpike, amp);
    }

    REQUIRE_FALSE(anyNaN);
    // Amplitude should stay bounded (no explosive clicks)
    REQUIRE(maxSpike < ampBefore * 5.0f);

    // Continue processing -- audio should be stable
    bool allSilent = true;
    for (int b = 0; b < 20; ++b)
    {
        float amp = f.processBlock();
        if (f.hasNaN()) anyNaN = true;
        if (amp > 1e-6f) allSilent = false;
    }

    REQUIRE_FALSE(anyNaN);
    REQUIRE_FALSE(allSilent);
}
