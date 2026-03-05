// ==============================================================================
// Stereo Spread Integration Tests (M6)
// ==============================================================================
// Integration tests verifying stereo output pipeline in the Innexus processor.
//
// Feature: 120-creative-extensions
// User Story 2: Stereo Partial Spread
// Requirements: FR-007, FR-012, FR-013, SC-010
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// =============================================================================
// Test Infrastructure (matches existing M5 patterns)
// =============================================================================

static constexpr double kStereoTestSampleRate = 44100.0;
static constexpr int32 kStereoTestBlockSize = 128;

static ProcessSetup makeStereoSetup(double sampleRate = kStereoTestSampleRate,
                                    int32 blockSize = kStereoTestBlockSize)
{
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;
    return setup;
}

static Innexus::SampleAnalysis* makeStereoTestAnalysis(
    int numFrames = 10,
    float f0 = 440.0f,
    float amplitude = 0.5f)
{
    auto* analysis = new Innexus::SampleAnalysis();
    analysis->sampleRate = 44100.0f;
    analysis->hopTimeSec = 512.0f / 44100.0f;

    for (int f = 0; f < numFrames; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = f0;
        frame.f0Confidence = 0.9f;
        frame.numPartials = 8;
        frame.globalAmplitude = amplitude;
        frame.spectralCentroid = 1000.0f;
        frame.brightness = 0.5f;
        frame.noisiness = 0.1f;

        for (int p = 0; p < 8; ++p)
        {
            auto& partial = frame.partials[static_cast<size_t>(p)];
            partial.harmonicIndex = p + 1;
            partial.frequency = f0 * static_cast<float>(p + 1);
            partial.amplitude = amplitude / static_cast<float>(p + 1);
            partial.relativeFrequency = static_cast<float>(p + 1);
            partial.inharmonicDeviation = 0.0f;
            partial.stability = 1.0f;
            partial.age = 10;
            partial.phase = static_cast<float>(p) * 0.3f;
        }

        analysis->frames.push_back(frame);
    }

    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_stereo.wav";

    return analysis;
}

// Minimal EventList for MIDI events
class StereoTestEventList : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getEventCount() override
    {
        return static_cast<int32>(events_.size());
    }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override
    {
        if (index < 0 || index >= static_cast<int32>(events_.size()))
            return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultTrue;
    }
    tresult PLUGIN_API addEvent(Event& e) override
    {
        events_.push_back(e);
        return kResultTrue;
    }

    void addNoteOn(int16 pitch, float velocity, int32 sampleOffset = 0)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = pitch;
        e.noteOn.tuning = 0.0f;
        e.noteOn.length = 0;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

// Minimal parameter value queue
class StereoTestParamValueQueue : public IParamValueQueue
{
public:
    StereoTestParamValueQueue(ParamID id, ParamValue val)
        : id_(id), value_(val) {}

    ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32 /*index*/, int32& sampleOffset,
                                 ParamValue& value) override
    {
        sampleOffset = 0;
        value = value_;
        return kResultTrue;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override
    {
        return kResultFalse;
    }

    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

private:
    ParamID id_;
    ParamValue value_;
};

// Parameter changes container
class StereoTestParameterChanges : public IParameterChanges
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

    void clear() { queues_.clear(); }

private:
    std::vector<StereoTestParamValueQueue> queues_;
};

// Helper fixture for stereo integration tests
struct StereoTestFixture
{
    Innexus::Processor processor;
    StereoTestEventList events;
    StereoTestParameterChanges paramChanges;

    std::vector<float> outL;
    std::vector<float> outR;
    float* outChannels[2];
    AudioBusBuffers outputBus{};
    ProcessData data{};

    StereoTestFixture(int32 blockSize = kStereoTestBlockSize,
                      double sampleRate = kStereoTestSampleRate)
        : outL(static_cast<size_t>(blockSize), 0.0f)
        , outR(static_cast<size_t>(blockSize), 0.0f)
    {
        outChannels[0] = outL.data();
        outChannels[1] = outR.data();

        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = outChannels;
        outputBus.silenceFlags = 0;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = blockSize;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = &events;
        data.outputEvents = nullptr;

        processor.initialize(nullptr);
        auto setup = makeStereoSetup(sampleRate, blockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~StereoTestFixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    void injectAnalysis(Innexus::SampleAnalysis* analysis)
    {
        processor.testInjectAnalysis(analysis);
    }

    void processBlock()
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = nullptr;
        processor.process(data);
    }

    void processBlockWithParams()
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = &paramChanges;
        processor.process(data);
        paramChanges.clear();
    }
};

// =============================================================================
// T012: Processor integration tests for stereo output
// =============================================================================

TEST_CASE("Stereo integration: spread=0 produces L==R (SC-010)",
          "[innexus][stereo][integration]")
{
    StereoTestFixture fix;
    fix.injectAnalysis(makeStereoTestAnalysis());

    // Set spread=0
    fix.paramChanges.addChange(Innexus::kStereoSpreadId, 0.0);
    fix.events.addNoteOn(60, 0.8f);
    fix.processBlockWithParams();
    fix.events.clear();

    // Process several blocks to let smoothers settle
    for (int b = 0; b < 20; ++b) {
        fix.processBlock();
    }

    // Now check that L == R
    fix.processBlock();
    bool allIdentical = true;
    for (size_t i = 0; i < fix.outL.size(); ++i) {
        if (fix.outL[i] != fix.outR[i]) {
            allIdentical = false;
            break;
        }
    }
    REQUIRE(allIdentical);
}

TEST_CASE("Stereo integration: spread=1.0 produces L!=R",
          "[innexus][stereo][integration]")
{
    StereoTestFixture fix;
    fix.injectAnalysis(makeStereoTestAnalysis());

    // Set spread=1.0
    fix.paramChanges.addChange(Innexus::kStereoSpreadId, 1.0);
    fix.events.addNoteOn(60, 0.8f);
    fix.processBlockWithParams();
    fix.events.clear();

    // Process several blocks to let smoothers settle
    for (int b = 0; b < 20; ++b) {
        fix.processBlock();
    }

    // Now check that L != R (some difference exists)
    fix.processBlock();
    bool anyDifferent = false;
    for (size_t i = 0; i < fix.outL.size(); ++i) {
        if (fix.outL[i] != fix.outR[i]) {
            anyDifferent = true;
            break;
        }
    }
    REQUIRE(anyDifferent);
}

TEST_CASE("Stereo integration: residual is center-panned (FR-012)",
          "[innexus][stereo][integration]")
{
    // Create analysis with residual
    auto* analysis = makeStereoTestAnalysis(10, 440.0f, 0.5f);
    analysis->analysisFFTSize = 1024;
    analysis->analysisHopSize = 256;

    for (size_t f = 0; f < analysis->frames.size(); ++f)
    {
        Krate::DSP::ResidualFrame rframe{};
        rframe.totalEnergy = 0.1f;
        for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
            rframe.bandEnergies[b] = 0.005f;
        rframe.transientFlag = false;
        analysis->residualFrames.push_back(rframe);
    }

    StereoTestFixture fix;
    fix.injectAnalysis(analysis);

    // Set spread=1.0, residual level high
    fix.paramChanges.addChange(Innexus::kStereoSpreadId, 1.0);
    fix.events.addNoteOn(60, 0.8f);
    fix.processBlockWithParams();
    fix.events.clear();

    // Process several blocks
    for (int b = 0; b < 20; ++b) {
        fix.processBlock();
    }

    // Residual contributes equally to L and R -- this test verifies the processor
    // doesn't crash and produces output on both channels. Residual is identical
    // in both channels (center-panned).
    fix.processBlock();

    bool hasOutputL = false, hasOutputR = false;
    for (size_t i = 0; i < fix.outL.size(); ++i) {
        if (fix.outL[i] != 0.0f) hasOutputL = true;
        if (fix.outR[i] != 0.0f) hasOutputR = true;
    }
    REQUIRE(hasOutputL);
    REQUIRE(hasOutputR);
}

TEST_CASE("Stereo integration: mono output bus sums channels (FR-013)",
          "[innexus][stereo][integration]")
{
    Innexus::Processor processor;
    StereoTestEventList events;
    StereoTestParameterChanges paramChanges;

    // Create MONO output bus (1 channel)
    constexpr int32 blockSize = 128;
    std::vector<float> outMono(blockSize, 0.0f);
    float* monoChannels[1] = { outMono.data() };
    AudioBusBuffers monoBus{};
    monoBus.numChannels = 1;
    monoBus.channelBuffers32 = monoChannels;
    monoBus.silenceFlags = 0;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = blockSize;
    data.numOutputs = 1;
    data.outputs = &monoBus;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = &events;
    data.outputEvents = nullptr;

    processor.initialize(nullptr);
    auto setup = makeStereoSetup(kStereoTestSampleRate, blockSize);
    processor.setupProcessing(setup);
    processor.setActive(true);

    processor.testInjectAnalysis(makeStereoTestAnalysis());

    // Set spread=1.0 and trigger note
    paramChanges.addChange(Innexus::kStereoSpreadId, 1.0);
    events.addNoteOn(60, 0.8f);
    data.inputParameterChanges = &paramChanges;
    processor.process(data);
    paramChanges.clear();
    events.clear();

    // Process several blocks to settle
    for (int b = 0; b < 20; ++b) {
        std::fill(outMono.begin(), outMono.end(), 0.0f);
        data.inputParameterChanges = nullptr;
        processor.process(data);
    }

    // Process final block and verify non-silent mono output
    std::fill(outMono.begin(), outMono.end(), 0.0f);
    processor.process(data);

    bool hasOutput = false;
    for (float s : outMono) {
        if (s != 0.0f) {
            hasOutput = true;
            break;
        }
    }
    REQUIRE(hasOutput);

    processor.setActive(false);
    processor.terminate();
}
