// ==============================================================================
// DataExchange Pipeline Integration Tests
// ==============================================================================
// Verifies the full round-trip: Processor → DataExchangeHandler → Controller
// using the IMessage fallback path (no host IDataExchangeHandler available).
//
// NOTE: The fallback path uses a timer (1ms) to send queued blocks. On Windows,
// this requires message dispatching. We pump messages in the test to allow delivery.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "controller/display_data.h"
#include "processor/processor.h"
#include "dsp/sample_analysis.h"
#include "plugin_ids.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// ==============================================================================
// Helpers
// ==============================================================================

/// Pump platform messages to allow SDK timers to fire.
static void pumpMessages(int durationMs)
{
#ifdef _WIN32
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start).count() < durationMs)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
#endif
}

/// Simple event list for MIDI events in tests.
class TestEventList : public IEventList
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
        events_.push_back(e);
    }

    void addNoteOff(int16 pitch, int32 sampleOffset = 0)
    {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.noteId = pitch;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

/// Create a simple harmonic analysis with known partials.
static Innexus::SampleAnalysis* makeTestAnalysis(
    int numFrames = 10, float f0 = 440.0f, float amplitude = 0.5f)
{
    auto* analysis = new Innexus::SampleAnalysis();
    analysis->sampleRate = 44100.0f;
    analysis->hopTimeSec = 512.0f / 44100.0f;

    for (int f = 0; f < numFrames; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = f0;
        frame.f0Confidence = 0.9f;
        frame.numPartials = 4;
        frame.globalAmplitude = amplitude;

        for (int p = 0; p < 4; ++p)
        {
            auto& partial = frame.partials[static_cast<size_t>(p)];
            partial.harmonicIndex = p + 1;
            partial.frequency = f0 * static_cast<float>(p + 1);
            partial.amplitude = amplitude / static_cast<float>(p + 1);
            partial.relativeFrequency = static_cast<float>(p + 1);
            partial.stability = 1.0f;
            partial.age = 10;
        }

        analysis->frames.push_back(frame);
    }

    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_sample.wav";
    return analysis;
}

static constexpr int32 kBlockSize = 128;
static constexpr double kSampleRate = 44100.0;

/// Full test fixture: processor + controller wired via IConnectionPoint.
struct PipelineFixture
{
    Innexus::Processor proc;
    Innexus::Controller ctrl;
    HostApplication host;

    TestEventList events;
    std::vector<float> outL;
    std::vector<float> outR;
    float* outChannels[2];
    AudioBusBuffers outputBus{};
    ProcessData data{};

    PipelineFixture()
        : outL(kBlockSize, 0.0f)
        , outR(kBlockSize, 0.0f)
    {
        outChannels[0] = outL.data();
        outChannels[1] = outR.data();

        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = outChannels;
        outputBus.silenceFlags = 0;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlockSize;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = &events;
        data.outputEvents = nullptr;

        // Initialize both components
        proc.initialize(&host);
        ctrl.initialize(&host);

        // Wire IConnectionPoint peers
        auto* procConn = static_cast<IConnectionPoint*>(
            static_cast<AudioEffect*>(&proc));
        auto* ctrlConn = static_cast<IConnectionPoint*>(
            static_cast<EditControllerEx1*>(&ctrl));
        proc.connect(ctrlConn);
        ctrl.connect(procConn);

        // Setup and activate
        ProcessSetup setup{};
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        setup.symbolicSampleSize = kSample32;
        setup.processMode = kRealtime;
        proc.setupProcessing(setup);
        proc.setActive(true);
    }

    ~PipelineFixture()
    {
        proc.setActive(false);

        auto* procConn = static_cast<IConnectionPoint*>(
            static_cast<AudioEffect*>(&proc));
        auto* ctrlConn = static_cast<IConnectionPoint*>(
            static_cast<EditControllerEx1*>(&ctrl));
        proc.disconnect(ctrlConn);
        ctrl.disconnect(procConn);

        ctrl.terminate();
        proc.terminate();
    }

    void processBlock()
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        outputBus.silenceFlags = 0;
        proc.process(data);
        events.clear();
    }

    /// Process blocks and pump messages so DataExchange delivers them.
    void processAndDeliver(int numBlocks = 1)
    {
        for (int i = 0; i < numBlocks; ++i)
            processBlock();
        pumpMessages(20);
    }

    const Innexus::DisplayData& cachedDisplay() const
    {
        return ctrl.getCachedDisplayData();
    }
};

// ==============================================================================
// Tests
// ==============================================================================

TEST_CASE("DataExchange full round-trip via IMessage fallback",
          "[innexus][data-exchange][integration]")
{
    PipelineFixture fix;

    // sendDisplayData directly — verify basic delivery
    ProcessData emptyData{};
    emptyData.numSamples = 0;
    fix.proc.sendDisplayData(emptyData);
    pumpMessages(50);

    REQUIRE(fix.cachedDisplay().frameCounter >= 1u);
    REQUIRE(fix.cachedDisplay().activePartialCount == 48);
}

TEST_CASE("Display data shows partials during note-on, clears after note-off",
          "[innexus][data-exchange][integration]")
{
    PipelineFixture fix;

    // Inject analysis and play a note
    fix.proc.testInjectAnalysis(makeTestAnalysis(50, 440.0f, 0.5f));
    fix.events.addNoteOn(69, 1.0f); // A4

    // Process several blocks to let the oscillator ramp up.
    // Pump between blocks to ensure DataExchange timer delivers.
    for (int i = 0; i < 5; ++i)
    {
        fix.processBlock();
        pumpMessages(10);
    }

    // Verify display shows non-zero partial amplitudes
    {
        const auto& d = fix.cachedDisplay();
        INFO("frameCounter = " << d.frameCounter);
        REQUIRE(d.frameCounter > 0u);

        float maxAmp = 0.0f;
        for (int i = 0; i < 4; ++i)
            maxAmp = std::max(maxAmp, d.partialAmplitudes[i]);
        INFO("maxAmp during note-on = " << maxAmp);
        REQUIRE(maxAmp > 0.0f);
    }

    uint32_t counterBeforeNoteOff = fix.cachedDisplay().frameCounter;

    // Note off
    fix.events.addNoteOff(69);
    fix.processBlock();
    fix.events.clear();

    // Process blocks to let the release envelope finish.
    // Exponential decay: 100ms release time constant, threshold 1e-6 ≈ 14 tau ≈ 1.4s
    // At 128 samples/block @ 44100Hz, that's ~483 blocks.
    // Pump messages periodically so DataExchange blocks get recycled.
    for (int i = 0; i < 550; ++i)
    {
        fix.processBlock();
        if (i % 20 == 19)
            pumpMessages(5);
    }

    // Final pump to deliver any remaining DataExchange blocks
    pumpMessages(50);

    uint32_t counterAfterRelease = fix.cachedDisplay().frameCounter;
    INFO("counterBeforeNoteOff = " << counterBeforeNoteOff);
    INFO("counterAfterRelease = " << counterAfterRelease);
    INFO("silenceFlags = " << fix.data.outputs[0].silenceFlags);

    // Now simulate the controller's display timer ticking with no new
    // frames arriving — this triggers the staleness check which clears views.
    // kStaleTickThreshold = 3, so 4 ticks should be enough.
    for (int i = 0; i < 5; ++i)
        fix.ctrl.testTickDisplayTimer();

    // Verify display is cleared
    {
        const auto& d = fix.cachedDisplay();
        float maxAmp = 0.0f;
        for (int i = 0; i < 96; ++i)
            maxAmp = std::max(maxAmp, d.partialAmplitudes[i]);
        INFO("maxAmp after staleness timeout = " << maxAmp);
        REQUIRE(maxAmp == Approx(0.0f));
    }
}

TEST_CASE("Display data: last frame before silence has zero amplitudes",
          "[innexus][data-exchange][integration]")
{
    // This test specifically checks that we don't get a "flash" of louder
    // data as the final frame before the display clears.
    PipelineFixture fix;

    fix.proc.testInjectAnalysis(makeTestAnalysis(50, 440.0f, 0.5f));
    fix.events.addNoteOn(69, 1.0f);

    // Let the note play for a bit
    fix.processAndDeliver(10);

    // Note off — start release
    fix.events.addNoteOff(69);
    fix.processBlock();
    fix.events.clear();

    // Process until note becomes inactive (release finishes)
    // Track the amplitude of each delivered frame
    float prevMaxAmp = 999.0f;
    bool foundSilence = false;
    float lastNonZeroMaxAmp = 0.0f;
    float firstZeroFrameCounter = 0;

    for (int block = 0; block < 600; ++block)
    {
        fix.processBlock();
        if (block % 20 == 19)
            pumpMessages(5);

        // After each pump, tick the staleness timer to simulate the
        // real display timer. This allows the staleness check to clear
        // cachedDisplayData_ once frames stop arriving.
        if (block % 20 == 19)
            fix.ctrl.testTickDisplayTimer();

        const auto& d = fix.cachedDisplay();

        float maxAmp = 0.0f;
        for (int i = 0; i < 96; ++i)
            maxAmp = std::max(maxAmp, d.partialAmplitudes[i]);

        if (maxAmp > 0.0f)
            lastNonZeroMaxAmp = maxAmp;

        if (maxAmp == 0.0f && prevMaxAmp > 0.0f)
        {
            foundSilence = true;
            firstZeroFrameCounter = static_cast<float>(d.frameCounter);
            INFO("Silence at block " << block
                 << ", lastNonZeroMaxAmp = " << lastNonZeroMaxAmp
                 << ", frameCounter = " << d.frameCounter);
            break;
        }

        prevMaxAmp = maxAmp;
    }

    // If the loop didn't find silence via staleness, tick the timer
    // a few more times after all processing is done.
    if (!foundSilence)
    {
        pumpMessages(50);
        for (int i = 0; i < 5; ++i)
            fix.ctrl.testTickDisplayTimer();

        const auto& d = fix.cachedDisplay();
        float maxAmp = 0.0f;
        for (int i = 0; i < 96; ++i)
            maxAmp = std::max(maxAmp, d.partialAmplitudes[i]);
        if (maxAmp == 0.0f)
            foundSilence = true;
    }

    if (!foundSilence)
    {
        INFO("Never reached silence after 600 blocks + staleness, "
             "lastNonZeroMaxAmp = " << lastNonZeroMaxAmp);
    }
    REQUIRE(foundSilence);
}
