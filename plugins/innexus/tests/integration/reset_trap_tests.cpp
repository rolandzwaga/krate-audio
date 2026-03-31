// ==============================================================================
// Reset Trap Regression Tests
// ==============================================================================
// Verifies that calling sub-component setters every block with the same value
// does NOT reset internal state (phase, position, etc.).
//
// Motivation: The testing guide's INTEGRATION-TESTING.md documents the "Reset
// Trap" as the #1 integration bug pattern. Setters that internally call reset()
// silently break state advancement when called from applyParamsToEngine() 86x/s.
//
// These tests lock in current safety and catch future regressions.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"
#include "dsp/harmonic_modulator.h"
#include "dsp/evolution_engine.h"
#include "dsp/live_analysis_pipeline.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/harmonic_snapshot.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

static constexpr double kRTSampleRate = 44100.0;
static constexpr int32 kRTBlockSize = 512;

// =============================================================================
// Level 1: Sub-Component Direct Tests
// =============================================================================

TEST_CASE("ResetTrap: setWaveform() every block does not reset modulator phase",
          "[innexus][reset_trap][integration]")
{
    using namespace Innexus;

    HarmonicModulator mod;
    mod.prepare(kRTSampleRate);
    mod.setRate(1.0f);    // 1 Hz
    mod.setDepth(1.0f);

    // --- Control run: setWaveform called once ---
    std::vector<float> controlPhases;
    for (int block = 0; block < 100; ++block)
    {
        for (int s = 0; s < kRTBlockSize; ++s)
            mod.advance();
        controlPhases.push_back(mod.getPhase());
    }

    // --- Test run: setWaveform called every block ---
    HarmonicModulator modSpam;
    modSpam.prepare(kRTSampleRate);
    modSpam.setRate(1.0f);
    modSpam.setDepth(1.0f);

    std::vector<float> spamPhases;
    for (int block = 0; block < 100; ++block)
    {
        modSpam.setWaveform(ModulatorWaveform::Sine); // Same value every block
        for (int s = 0; s < kRTBlockSize; ++s)
            modSpam.advance();
        spamPhases.push_back(modSpam.getPhase());
    }

    // Phase trajectories must match exactly
    for (size_t i = 0; i < controlPhases.size(); ++i)
    {
        REQUIRE(spamPhases[i] == Approx(controlPhases[i]).margin(1e-6f));
    }
}

TEST_CASE("ResetTrap: setMode() every block does not reset evolution engine phase",
          "[innexus][reset_trap][integration]")
{
    using namespace Innexus;

    // Build 3 waypoints
    std::array<Krate::DSP::MemorySlot, 8> slots{};
    for (int i = 0; i < 3; ++i)
    {
        slots[static_cast<size_t>(i)].occupied = true;
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = 440.0f;
        frame.numPartials = 2;
        frame.partials[0].frequency = 440.0f * static_cast<float>(i + 1);
        frame.partials[0].amplitude = 0.5f;
        frame.partials[0].harmonicIndex = 1;
        Krate::DSP::ResidualFrame rframe{};
        slots[static_cast<size_t>(i)].snapshot =
            Krate::DSP::captureSnapshot(frame, rframe);
    }

    // --- Control run ---
    EvolutionEngine engCtrl;
    engCtrl.prepare(kRTSampleRate);
    engCtrl.setMode(EvolutionMode::Cycle);
    engCtrl.setSpeed(1.0f);
    engCtrl.setDepth(1.0f);
    engCtrl.updateWaypoints(slots);

    std::vector<float> controlPositions;
    for (int block = 0; block < 100; ++block)
    {
        for (int s = 0; s < kRTBlockSize; ++s)
            engCtrl.advance();
        controlPositions.push_back(engCtrl.getPosition());
    }

    // --- Test run: setMode every block ---
    EvolutionEngine engSpam;
    engSpam.prepare(kRTSampleRate);
    engSpam.setMode(EvolutionMode::Cycle);
    engSpam.setSpeed(1.0f);
    engSpam.setDepth(1.0f);
    engSpam.updateWaypoints(slots);

    std::vector<float> spamPositions;
    for (int block = 0; block < 100; ++block)
    {
        engSpam.setMode(EvolutionMode::Cycle); // Same value every block
        for (int s = 0; s < kRTBlockSize; ++s)
            engSpam.advance();
        spamPositions.push_back(engSpam.getPosition());
    }

    for (size_t i = 0; i < controlPositions.size(); ++i)
    {
        REQUIRE(spamPositions[i] == Approx(controlPositions[i]).margin(1e-6f));
    }
}

TEST_CASE("ResetTrap: setRange() every block does not affect modulation output",
          "[innexus][reset_trap][integration]")
{
    using namespace Innexus;

    auto runTest = [](bool spamRange) {
        HarmonicModulator mod;
        mod.prepare(kRTSampleRate);
        mod.setRate(2.0f);
        mod.setDepth(0.8f);
        mod.setRange(1, 4);
        mod.setTarget(ModulatorTarget::Amplitude);
        mod.setWaveform(ModulatorWaveform::Sine);

        Krate::DSP::HarmonicFrame frame{};
        frame.numPartials = 4;
        for (int p = 0; p < 4; ++p)
        {
            frame.partials[static_cast<size_t>(p)].harmonicIndex = p + 1;
            frame.partials[static_cast<size_t>(p)].amplitude = 0.5f;
        }

        float totalAmp = 0.0f;
        for (int block = 0; block < 50; ++block)
        {
            if (spamRange)
                mod.setRange(1, 4); // Same value every block

            for (int s = 0; s < kRTBlockSize; ++s)
                mod.advance();

            // Apply to a copy of the frame
            auto frameCopy = frame;
            mod.applyAmplitudeModulation(frameCopy);
            for (int p = 0; p < 4; ++p)
                totalAmp += frameCopy.partials[static_cast<size_t>(p)].amplitude;
        }
        return totalAmp;
    };

    float controlTotal = runTest(false);
    float spamTotal = runTest(true);

    REQUIRE(spamTotal == Approx(controlTotal).margin(1e-4f));
}

TEST_CASE("ResetTrap: setTarget() every block does not affect modulation output",
          "[innexus][reset_trap][integration]")
{
    using namespace Innexus;

    auto runTest = [](bool spamTarget) {
        HarmonicModulator mod;
        mod.prepare(kRTSampleRate);
        mod.setRate(2.0f);
        mod.setDepth(0.8f);
        mod.setRange(1, 4);
        mod.setTarget(ModulatorTarget::Amplitude);
        mod.setWaveform(ModulatorWaveform::Triangle);

        Krate::DSP::HarmonicFrame frame{};
        frame.numPartials = 4;
        for (int p = 0; p < 4; ++p)
        {
            frame.partials[static_cast<size_t>(p)].harmonicIndex = p + 1;
            frame.partials[static_cast<size_t>(p)].amplitude = 0.5f;
        }

        float totalAmp = 0.0f;
        for (int block = 0; block < 50; ++block)
        {
            if (spamTarget)
                mod.setTarget(ModulatorTarget::Amplitude); // Same value every block

            for (int s = 0; s < kRTBlockSize; ++s)
                mod.advance();

            auto frameCopy = frame;
            mod.applyAmplitudeModulation(frameCopy);
            for (int p = 0; p < 4; ++p)
                totalAmp += frameCopy.partials[static_cast<size_t>(p)].amplitude;
        }
        return totalAmp;
    };

    float controlTotal = runTest(false);
    float spamTotal = runTest(true);

    REQUIRE(spamTotal == Approx(controlTotal).margin(1e-4f));
}

TEST_CASE("ResetTrap: setResponsiveness() every block does not reset live pipeline",
          "[innexus][reset_trap][integration]")
{
    using namespace Innexus;

    auto runTest = [](bool spamResp) {
        LiveAnalysisPipeline pipeline;
        pipeline.prepare(kRTSampleRate, LatencyMode::LowLatency);

        // Generate a simple sine tone
        std::vector<float> buffer(static_cast<size_t>(kRTBlockSize));
        for (size_t s = 0; s < buffer.size(); ++s)
        {
            float phase = static_cast<float>(s) / static_cast<float>(kRTSampleRate);
            buffer[s] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * phase);
        }

        int framesProduced = 0;
        for (int block = 0; block < 200; ++block)
        {
            if (spamResp)
                pipeline.setResponsiveness(0.5f); // Same value every block

            pipeline.pushSamples(buffer.data(), buffer.size());
            if (pipeline.hasNewFrame())
                ++framesProduced;
        }
        return framesProduced;
    };

    int controlFrames = runTest(false);
    int spamFrames = runTest(true);

    // Both runs should produce the same number of frames
    REQUIRE(spamFrames == controlFrames);
}

// =============================================================================
// Level 2: Full Processor Parameter Spam
// =============================================================================

// Minimal parameter changes infrastructure (duplicated from residual tests
// to keep each test file self-contained)
namespace {

class RTParamValueQueue : public IParamValueQueue
{
public:
    RTParamValueQueue(ParamID id, ParamValue val)
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

class RTParameterChanges : public IParameterChanges
{
public:
    void addChange(ParamID id, ParamValue val)
    {
        queues_.emplace_back(id, val);
    }

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
    std::vector<RTParamValueQueue> queues_;
};

Innexus::SampleAnalysis* makeRTAnalysis()
{
    auto* analysis = new Innexus::SampleAnalysis();
    analysis->sampleRate = 44100.0f;
    analysis->hopTimeSec = 512.0f / 44100.0f;
    analysis->analysisFFTSize = 1024;
    analysis->analysisHopSize = 512;

    for (int f = 0; f < 20; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = 440.0f;
        frame.f0Confidence = 0.9f;
        frame.numPartials = 4;
        frame.globalAmplitude = 0.5f;

        for (int p = 0; p < 4; ++p)
        {
            auto& partial = frame.partials[static_cast<size_t>(p)];
            partial.harmonicIndex = p + 1;
            partial.frequency = 440.0f * static_cast<float>(p + 1);
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
    analysis->filePath = "test_rt.wav";
    return analysis;
}

} // anonymous namespace

TEST_CASE("ResetTrap: Full processor -- same-value params every block does not break audio",
          "[innexus][reset_trap][integration]")
{
    using namespace Innexus;

    Processor proc;
    proc.initialize(nullptr);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kRTBlockSize;
    setup.sampleRate = kRTSampleRate;
    proc.setupProcessing(setup);
    proc.setActive(true);

    proc.testInjectAnalysis(makeRTAnalysis());
    proc.onNoteOn(60, 1.0f);

    std::vector<float> outL(static_cast<size_t>(kRTBlockSize), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kRTBlockSize), 0.0f);
    float* channels[2] = {outL.data(), outR.data()};

    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = channels;

    // Stabilize for 20 blocks (no param spam)
    for (int b = 0; b < 20; ++b)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        ProcessData data{};
        data.numSamples = kRTBlockSize;
        data.numOutputs = 1;
        data.outputs = &outBus;
        proc.process(data);
    }

    // Record amplitude of clean block
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    {
        ProcessData data{};
        data.numSamples = kRTBlockSize;
        data.numOutputs = 1;
        data.outputs = &outBus;
        proc.process(data);
    }
    float cleanMax = 0.0f;
    for (size_t s = 0; s < outL.size(); ++s)
        cleanMax = std::max(cleanMax, std::abs(outL[s]));

    REQUIRE(cleanMax > 0.0f);

    // Now spam ALL parameters at their defaults for 50 blocks
    bool anyNaN = false;
    float spamMax = 0.0f;
    float spamMin = 1.0f;

    for (int b = 0; b < 50; ++b)
    {
        RTParameterChanges params;
        // M4 params at defaults
        params.addChange(Innexus::kFreezeId, 0.0);
        params.addChange(Innexus::kMorphPositionId, 0.0);
        params.addChange(Innexus::kHarmonicFilterTypeId, 0.0);
        params.addChange(Innexus::kResponsivenessId, 0.5);
        // M6 params at defaults
        params.addChange(Innexus::kStereoSpreadId, 0.0);
        params.addChange(Innexus::kEvolutionEnableId, 0.0);
        params.addChange(Innexus::kMod1EnableId, 0.0);
        params.addChange(Innexus::kMod2EnableId, 0.0);
        params.addChange(Innexus::kBlendEnableId, 0.0);
        params.addChange(Innexus::kDetuneSpreadId, 0.0);

        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        ProcessData data{};
        data.numSamples = kRTBlockSize;
        data.numOutputs = 1;
        data.outputs = &outBus;
        data.inputParameterChanges = &params;
        proc.process(data);

        float blockMax = 0.0f;
        for (size_t s = 0; s < outL.size(); ++s)
        {
            if (std::isnan(outL[s]) || std::isnan(outR[s]))
                anyNaN = true;
            blockMax = std::max(blockMax, std::abs(outL[s]));
        }
        spamMax = std::max(spamMax, blockMax);
        spamMin = std::min(spamMin, blockMax);
    }

    REQUIRE_FALSE(anyNaN);
    // Audio should remain present and stable (no drops to zero)
    REQUIRE(spamMin > 0.0f);
    // Amplitude should be within reasonable range of clean block
    REQUIRE(spamMax < cleanMax * 3.0f);

    proc.setActive(false);
    proc.terminate();
}
