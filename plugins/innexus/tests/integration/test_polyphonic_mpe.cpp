// Polyphonic MPE Integration Tests

#include <catch2/catch_test_macros.hpp>
#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"
#include <krate/dsp/processors/harmonic_types.h>
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <memory>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static constexpr int32 kBlk = 128;

static Innexus::SampleAnalysis* makeAnalysis()
{
    auto* a = new Innexus::SampleAnalysis();
    a->sampleRate = 44100.0f;
    a->hopTimeSec = 512.0f / 44100.0f;
    for (int f = 0; f < 50; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = 440.0f;
        frame.f0Confidence = 0.9f;
        frame.numPartials = 4;
        frame.globalAmplitude = 0.5f;
        for (int p = 0; p < 4; ++p)
        {
            auto& pt = frame.partials[static_cast<size_t>(p)];
            pt.harmonicIndex = p + 1;
            pt.frequency = 440.0f * static_cast<float>(p + 1);
            pt.amplitude = 0.5f / static_cast<float>(p + 1);
            pt.relativeFrequency = static_cast<float>(p + 1);
            pt.stability = 1.0f;
            pt.age = 10;
        }
        a->frames.push_back(frame);
    }
    a->totalFrames = a->frames.size();
    a->filePath = "test.wav";
    return a;
}

TEST_CASE("Poly: mono mode backward compatible",
          "[innexus][poly][integration]")
{
    auto proc = std::make_unique<Innexus::Processor>();
    proc->initialize(nullptr);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kBlk;
    setup.sampleRate = 44100.0;
    proc->setupProcessing(setup);
    proc->setActive(true);
    proc->testInjectAnalysis(makeAnalysis());

    std::vector<float> outL(kBlk, 0.0f), outR(kBlk, 0.0f);
    float* ch[2] = {outL.data(), outR.data()};
    AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = ch;
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kBlk;
    data.numOutputs = 1;
    data.outputs = &bus;

    proc->onNoteOn(60, 0.8f);
    for (int i = 0; i < 10; ++i)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        bus.silenceFlags = 0;
        proc->process(data);
    }

    float sumSq = 0.0f;
    for (int s = 0; s < kBlk; ++s)
        sumSq += outL[static_cast<size_t>(s)] * outL[static_cast<size_t>(s)];
    float rms = std::sqrt(sumSq / kBlk);
    REQUIRE(rms > 0.01f);

    proc->setActive(false);
    proc->terminate();
}

TEST_CASE("Poly: two simultaneous notes both produce output",
          "[innexus][poly][integration]")
{
    auto proc = std::make_unique<Innexus::Processor>();
    proc->initialize(nullptr);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kBlk;
    setup.sampleRate = 44100.0;
    proc->setupProcessing(setup);
    proc->setActive(true);
    proc->testInjectAnalysis(makeAnalysis());

    std::vector<float> outL(kBlk, 0.0f), outR(kBlk, 0.0f);
    float* ch[2] = {outL.data(), outR.data()};
    AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = ch;
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kBlk;
    data.numOutputs = 1;
    data.outputs = &bus;

    // Set 4-voice mode via parameter change
    {
        class VoiceModeQueue : public IParamValueQueue {
        public:
            tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
            uint32 PLUGIN_API addRef() override { return 1; }
            uint32 PLUGIN_API release() override { return 1; }
            ParamID PLUGIN_API getParameterId() override { return Innexus::kVoiceModeId; }
            int32 PLUGIN_API getPointCount() override { return 1; }
            tresult PLUGIN_API getPoint(int32, int32& o, ParamValue& v) override { o=0; v=0.5; return kResultTrue; }
            tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }
        };
        class VoiceModeChanges : public IParameterChanges {
        public:
            tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
            uint32 PLUGIN_API addRef() override { return 1; }
            uint32 PLUGIN_API release() override { return 1; }
            int32 PLUGIN_API getParameterCount() override { return 1; }
            IParamValueQueue* PLUGIN_API getParameterData(int32 i) override { return i==0 ? &q_ : nullptr; }
            IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override { return nullptr; }
        private:
            VoiceModeQueue q_;
        } params;
        data.inputParameterChanges = &params;
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        bus.silenceFlags = 0;
        proc->process(data);
        data.inputParameterChanges = nullptr;
    }

    // Play note 1 (C4)
    proc->onNoteOn(60, 0.8f, 1);
    for (int i = 0; i < 5; ++i)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        bus.silenceFlags = 0;
        proc->process(data);
    }

    // Measure single note
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    bus.silenceFlags = 0;
    proc->process(data);
    float singleSumSq = 0.0f;
    for (int s = 0; s < kBlk; ++s)
        singleSumSq += outL[static_cast<size_t>(s)] * outL[static_cast<size_t>(s)];
    float singleRms = std::sqrt(singleSumSq / kBlk);
    REQUIRE(singleRms > 0.001f);

    // Play note 2 (E4) simultaneously
    proc->onNoteOn(64, 0.8f, 2);
    for (int i = 0; i < 5; ++i)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        bus.silenceFlags = 0;
        proc->process(data);
    }

    // Measure two notes
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    bus.silenceFlags = 0;
    proc->process(data);
    float twoSumSq = 0.0f;
    for (int s = 0; s < kBlk; ++s)
        twoSumSq += outL[static_cast<size_t>(s)] * outL[static_cast<size_t>(s)];
    float twoRms = std::sqrt(twoSumSq / kBlk);

    INFO("singleRms = " << singleRms);
    INFO("twoRms = " << twoRms);
    // Two notes should produce meaningful output
    REQUIRE(twoRms > singleRms * 0.4f);

    proc->setActive(false);
    proc->terminate();
}
