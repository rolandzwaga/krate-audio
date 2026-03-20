// ==============================================================================
// DataExchange Spectrum Pipeline Integration Test
// ==============================================================================
// Verifies the full round-trip: Processor sends audio samples via DataExchange
// → Controller receives them → local FIFOs are populated → SpectrumAnalyzer
// can read the data.
//
// This test exists because migrating from raw pointer IMessage to DataExchange
// API broke the visualizer — no data was being displayed.
//
// NOTE: The fallback path uses a timer (1ms) to send queued blocks. On Windows,
// this requires message dispatching. We pump messages in the test to allow delivery.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "controller/spectrum_block.h"
#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
#include <memory>
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

/// Pump platform messages to allow SDK timers to fire (DataExchange fallback).
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

/// Generate sine wave samples into a buffer.
static void generateSine(float* buffer, int numSamples, float freq,
                          double sampleRate, int offset = 0)
{
    constexpr double twoPi = 6.283185307179586;
    for (int i = 0; i < numSamples; ++i) {
        buffer[i] = static_cast<float>(
            std::sin(twoPi * freq * static_cast<double>(i + offset) / sampleRate));
    }
}

static constexpr int32 kBlockSize = 512;
static constexpr double kSampleRate = 44100.0;

// ==============================================================================
// PipelineFixture: Processor + Controller wired via IConnectionPoint
// ==============================================================================

struct PipelineFixture
{
    // Heap-allocate processor and controller to avoid stack overflow
    // (Controller contains 2 × SpectrumFIFO<8192> = 64KB of arrays)
    std::unique_ptr<Disrumpo::Processor> proc;
    std::unique_ptr<Disrumpo::Controller> ctrl;
    HostApplication host;

    std::vector<float> inL, inR, outL, outR;
    float* inChannels[2];
    float* outChannels[2];
    AudioBusBuffers inputBus{};
    AudioBusBuffers outputBus{};
    ProcessData data{};

    PipelineFixture()
        : proc(std::make_unique<Disrumpo::Processor>())
        , ctrl(std::make_unique<Disrumpo::Controller>())
        , inL(kBlockSize, 0.0f)
        , inR(kBlockSize, 0.0f)
        , outL(kBlockSize, 0.0f)
        , outR(kBlockSize, 0.0f)
    {
        inChannels[0] = inL.data();
        inChannels[1] = inR.data();
        outChannels[0] = outL.data();
        outChannels[1] = outR.data();

        inputBus.numChannels = 2;
        inputBus.channelBuffers32 = inChannels;
        inputBus.silenceFlags = 0;

        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = outChannels;
        outputBus.silenceFlags = 0;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlockSize;
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &inputBus;
        data.outputs = &outputBus;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = nullptr;
        data.outputEvents = nullptr;

        // Initialize both components
        proc->initialize(&host);
        ctrl->initialize(&host);

        // Wire IConnectionPoint peers (enables DataExchange)
        auto* procConn = static_cast<IConnectionPoint*>(
            static_cast<AudioEffect*>(proc.get()));
        auto* ctrlConn = static_cast<IConnectionPoint*>(
            static_cast<EditControllerEx1*>(ctrl.get()));
        proc->connect(ctrlConn);
        ctrl->connect(procConn);

        // Setup and activate
        ProcessSetup setup{};
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        setup.symbolicSampleSize = kSample32;
        setup.processMode = kRealtime;
        proc->setupProcessing(setup);
        proc->setActive(true);
    }

    ~PipelineFixture()
    {
        proc->setActive(false);

        auto* procConn = static_cast<IConnectionPoint*>(
            static_cast<AudioEffect*>(proc.get()));
        auto* ctrlConn = static_cast<IConnectionPoint*>(
            static_cast<EditControllerEx1*>(ctrl.get()));
        proc->disconnect(ctrlConn);
        ctrl->disconnect(procConn);

        ctrl->terminate();
        proc->terminate();
    }

    /// Fill input buffers with sine wave and process one block.
    void processBlockWithSine(float freq = 440.0f, int sampleOffset = 0)
    {
        generateSine(inL.data(), kBlockSize, freq, kSampleRate, sampleOffset);
        generateSine(inR.data(), kBlockSize, freq, kSampleRate, sampleOffset);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        outputBus.silenceFlags = 0;
        proc->process(data);
    }

    /// Process multiple blocks with sine input and pump messages for delivery.
    void processAndDeliver(int numBlocks, float freq = 440.0f)
    {
        for (int i = 0; i < numBlocks; ++i)
            processBlockWithSine(freq, i * kBlockSize);
        pumpMessages(50);
    }
};

// ==============================================================================
// Tests
// ==============================================================================

TEST_CASE("SpectrumBlock is trivially copyable",
          "[disrumpo][data-exchange]")
{
    STATIC_REQUIRE(std::is_trivially_copyable_v<Disrumpo::SpectrumBlock>);
}

TEST_CASE("DataExchange delivers audio samples to controller local FIFOs",
          "[disrumpo][data-exchange][integration]")
{
    PipelineFixture fix;

    // Verify no data before processing
    REQUIRE_FALSE(fix.ctrl->isSpectrumDataAvailable());
    REQUIRE(fix.ctrl->getLocalInputFIFO().totalWritten() == 0);
    REQUIRE(fix.ctrl->getLocalOutputFIFO().totalWritten() == 0);

    // Process several blocks of sine wave audio and pump messages
    fix.processAndDeliver(10, 1000.0f);

    // Verify controller received spectrum data
    INFO("spectrumDataAvailable = " << fix.ctrl->isSpectrumDataAvailable());
    INFO("inputFIFO totalWritten = " << fix.ctrl->getLocalInputFIFO().totalWritten());
    INFO("outputFIFO totalWritten = " << fix.ctrl->getLocalOutputFIFO().totalWritten());

    REQUIRE(fix.ctrl->isSpectrumDataAvailable());
    REQUIRE(fix.ctrl->getLocalInputFIFO().totalWritten() > 0);
    REQUIRE(fix.ctrl->getLocalOutputFIFO().totalWritten() > 0);

    // We should receive a reasonable amount of data. With numBlocks=2 and
    // IMessage fallback, not every block will be delivered (blocks can be
    // dropped when the producer outruns the consumer). But we should get
    // at least some data.
    const size_t totalInput = fix.ctrl->getLocalInputFIFO().totalWritten();
    const size_t totalOutput = fix.ctrl->getLocalOutputFIFO().totalWritten();
    INFO("totalInput = " << totalInput << ", totalOutput = " << totalOutput);
    REQUIRE(totalInput >= static_cast<size_t>(kBlockSize));
    REQUIRE(totalOutput >= static_cast<size_t>(kBlockSize));

    // Verify the data is actually non-zero (sine wave content)
    const size_t readSize = std::min(totalInput, size_t(512));
    std::vector<float> readBuf(readSize);
    size_t read = fix.ctrl->getLocalInputFIFO().readLatest(readBuf.data(), readSize);
    REQUIRE(read == readSize);

    // Check that at least some samples are non-zero
    float maxVal = 0.0f;
    for (size_t i = 0; i < read; ++i)
        maxVal = std::max(maxVal, std::abs(readBuf[i]));
    INFO("maxVal in input FIFO = " << maxVal);
    REQUIRE(maxVal > 0.01f);
}

TEST_CASE("Processor lifecycle without connect - sendSpectrumBlock is no-op",
          "[disrumpo][data-exchange]")
{
    auto proc = std::make_unique<Disrumpo::Processor>();
    HostApplication host;
    REQUIRE(proc->initialize(&host) == kResultOk);

    ProcessSetup setup{};
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    setup.symbolicSampleSize = kSample32;
    setup.processMode = kRealtime;
    REQUIRE(proc->setupProcessing(setup) == kResultOk);
    REQUIRE(proc->setActive(true) == kResultOk);

    // Process a block without connect — sendSpectrumBlock should be a no-op
    std::vector<float> inL(512, 0.5f), inR(512, 0.5f);
    std::vector<float> outL(512, 0.0f), outR(512, 0.0f);
    float* inCh[2] = { inL.data(), inR.data() };
    float* outCh[2] = { outL.data(), outR.data() };

    AudioBusBuffers inputBus{};
    inputBus.numChannels = 2;
    inputBus.channelBuffers32 = inCh;
    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outCh;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = 512;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inputBus;
    data.outputs = &outputBus;

    // Should not crash — process without DataExchange connect() should be a no-op
    // for spectrum sending (Tier 3 shared FIFO still works)
    auto result = proc->process(data);
    REQUIRE(result == kResultOk);

    REQUIRE(proc->setActive(false) == kResultOk);
    REQUIRE(proc->terminate() == kResultOk);
}

TEST_CASE("Controller can be initialized",
          "[disrumpo][data-exchange][lifecycle]")
{
    HostApplication host;
    Disrumpo::Controller ctrl;
    REQUIRE(ctrl.initialize(&host) == kResultOk);
    REQUIRE(ctrl.terminate() == kResultOk);
}

TEST_CASE("Processor init with host",
          "[disrumpo][data-exchange][lifecycle]")
{
    HostApplication host;
    auto proc = std::make_unique<Disrumpo::Processor>();
    REQUIRE(proc->initialize(&host) == kResultOk);
    REQUIRE(proc->terminate() == kResultOk);
}

TEST_CASE("Processor connect to controller",
          "[disrumpo][data-exchange][lifecycle]")
{
    HostApplication host;

    auto proc = std::make_unique<Disrumpo::Processor>();
    REQUIRE(proc->initialize(&host) == kResultOk);

    auto ctrl = std::make_unique<Disrumpo::Controller>();
    REQUIRE(ctrl->initialize(&host) == kResultOk);

    auto* ctrlConn = static_cast<IConnectionPoint*>(
        static_cast<EditControllerEx1*>(ctrl.get()));

    REQUIRE(proc->connect(ctrlConn) == kResultOk);
    REQUIRE(proc->disconnect(ctrlConn) == kResultOk);

    REQUIRE(ctrl->terminate() == kResultOk);
    REQUIRE(proc->terminate() == kResultOk);
}

TEST_CASE("Full bidirectional connect + setup + activate",
          "[disrumpo][data-exchange][lifecycle]")
{
    HostApplication host;

    auto proc = std::make_unique<Disrumpo::Processor>();
    REQUIRE(proc->initialize(&host) == kResultOk);

    auto ctrl = std::make_unique<Disrumpo::Controller>();
    REQUIRE(ctrl->initialize(&host) == kResultOk);

    auto* procConn = static_cast<IConnectionPoint*>(
        static_cast<AudioEffect*>(proc.get()));
    auto* ctrlConn = static_cast<IConnectionPoint*>(
        static_cast<EditControllerEx1*>(ctrl.get()));

    INFO("proc->connect(ctrlConn)");
    REQUIRE(proc->connect(ctrlConn) == kResultOk);
    INFO("ctrl->connect(procConn)");
    REQUIRE(ctrl->connect(procConn) == kResultOk);

    ProcessSetup setup{};
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = kBlockSize;
    setup.symbolicSampleSize = kSample32;
    setup.processMode = kRealtime;

    INFO("setupProcessing");
    REQUIRE(proc->setupProcessing(setup) == kResultOk);
    INFO("setActive(true)");
    REQUIRE(proc->setActive(true) == kResultOk);

    INFO("setActive(false)");
    REQUIRE(proc->setActive(false) == kResultOk);

    INFO("proc->disconnect");
    REQUIRE(proc->disconnect(ctrlConn) == kResultOk);
    INFO("ctrl->disconnect");
    REQUIRE(ctrl->disconnect(procConn) == kResultOk);

    REQUIRE(ctrl->terminate() == kResultOk);
    REQUIRE(proc->terminate() == kResultOk);
}
