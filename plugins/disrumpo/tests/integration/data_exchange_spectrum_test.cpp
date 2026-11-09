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

#include "controller/controller.h"
#include "controller/spectrum_block.h"
#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivstdataexchange.h"

#include "vst_param_changes.h"
#include "allocation_detector.h"
#include "display/shared_display_bridge.h"
#include "../test_support/disrumpo_rig.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
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

/// The SharedDisplayBridge instance ID a processor state stream ends with.
static uint64_t stateTrailerInstanceId(const std::vector<uint8_t>& stream)
{
    REQUIRE(stream.size() >= 12);
    uint64_t id = 0;
    std::memcpy(&id, stream.data() + stream.size() - 8, sizeof(id));
    return id;
}

/// Host context implementing IDataExchangeHandler (Tier 1, no timer or message
/// pump): one block of storage, every block sent to the controller is copied out.
class MockDataExchangeHost : public IDataExchangeHandler
{
public:
    MockDataExchangeHost() = default;
    virtual ~MockDataExchangeHost() = default;
    MockDataExchangeHost(const MockDataExchangeHost&) = delete;
    MockDataExchangeHost& operator=(const MockDataExchangeHost&) = delete;
    MockDataExchangeHost(MockDataExchangeHost&&) = delete;
    MockDataExchangeHost& operator=(MockDataExchangeHost&&) = delete;

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IDataExchangeHandler::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IDataExchangeHandler*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API openQueue(IAudioProcessor* /*processor*/, uint32 size, uint32 /*numBlocks*/,
                                 uint32 align, DataExchangeUserContextID /*userContextID*/,
                                 DataExchangeQueueID* outID) override
    {
        ++opened;
        blockSize = size;
        alignment = align;
        storage.assign(size / sizeof(float) + 1, 0.0f);
        *outID = 1;
        return kResultTrue;
    }
    tresult PLUGIN_API closeQueue(DataExchangeQueueID /*queueID*/) override { return kResultTrue; }
    tresult PLUGIN_API lockBlock(DataExchangeQueueID /*queueId*/, DataExchangeBlock* block) override
    {
        if (storage.empty()) return kResultFalse;
        block->data = storage.data();
        block->size = blockSize;
        block->blockID = 1;
        return kResultTrue;
    }
    tresult PLUGIN_API freeBlock(DataExchangeQueueID /*queueId*/, DataExchangeBlockID /*blockID*/,
                                 TBool sendToController) override
    {
        if (sendToController) {
            ++sent;
            lastSent = storage;
        }
        return kResultTrue;
    }

    [[nodiscard]] const Disrumpo::SpectrumBlock& lastBlock() const
    {
        return *reinterpret_cast<const Disrumpo::SpectrumBlock*>(lastSent.data());
    }

    int opened = 0;
    int sent = 0;
    uint32 blockSize = 0;
    uint32 alignment = 0;
    std::vector<float> storage;  // float storage: SpectrumBlock is all 4-byte fields
    std::vector<float> lastSent;
};

/// One stereo process() call on `proc` with the given buffers and parameter changes.
static void processStereo(Disrumpo::Processor& proc, std::vector<float>& inL, std::vector<float>& inR,
                          std::vector<float>& outL, std::vector<float>& outR,
                          IParameterChanges* changes = nullptr)
{
    float* inCh[2] = {inL.data(), inR.data()};
    float* outCh[2] = {outL.data(), outR.data()};
    AudioBusBuffers inputBus{};
    inputBus.numChannels = 2;
    inputBus.channelBuffers32 = inCh;
    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outCh;
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = static_cast<int32>(inL.size());
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inputBus;
    data.outputs = &outputBus;
    data.inputParameterChanges = changes;
    proc.process(data);
}

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
    float* inChannels[2]{};
    float* outChannels[2]{};
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
        proc->setActive(static_cast<TBool>(true));
    }

    ~PipelineFixture()
    {
        proc->setActive(static_cast<TBool>(false));

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

// D-128 (T5.07): the delivery to the controller needs the SDK's DataExchange
// fallback timer, which fires in a bare harness only on Windows (pumpMessages
// dispatches its messages). macOS and Linux test harnesses have no run loop, so
// there the case checks only that sendSpectrumBlock wrote the Tier-3 shared FIFOs,
// which need no timer.
TEST_CASE("DataExchange delivers audio samples to controller local FIFOs",
          "[disrumpo][data-exchange][integration][D-128]")
{
    constexpr int kBlocks = 10;
    PipelineFixture fix;

    // Verify no data before processing
    REQUIRE_FALSE(fix.ctrl->isSpectrumDataAvailable());
    REQUIRE(fix.ctrl->getLocalInputFIFO().totalWritten() == 0);
    REQUIRE(fix.ctrl->getLocalOutputFIFO().totalWritten() == 0);

    // Process several blocks of sine wave audio and pump messages
    fix.processAndDeliver(kBlocks, 1000.0f);

    // Tier 3: every block reaches the shared FIFOs, on every platform
    const auto id = stateTrailerInstanceId(Disrumpo::TestSupport::getStateBytes(*fix.proc));
    const auto* shared =
        static_cast<const Disrumpo::SharedDisplay*>(Krate::Plugins::SharedDisplayBridge::instance().lookupInstance(id));
    REQUIRE(shared != nullptr);
    const size_t tier3Input = shared->inputFIFO->totalWritten();
    const size_t tier3Output = shared->outputFIFO->totalWritten();
    std::printf("D-128 after %d blocks: Tier-3 FIFOs %zu / %zu samples, controller FIFOs %zu / %zu\n", kBlocks,
                tier3Input, tier3Output, fix.ctrl->getLocalInputFIFO().totalWritten(),
                fix.ctrl->getLocalOutputFIFO().totalWritten());
    CHECK(tier3Input == static_cast<size_t>(kBlocks * kBlockSize));
    CHECK(tier3Output == static_cast<size_t>(kBlocks * kBlockSize));

#ifdef _WIN32
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
#endif
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
    std::vector<float> inL(512, 0.5f);
    std::vector<float> inR(512, 0.5f);
    std::vector<float> outL(512, 0.0f);
    std::vector<float> outR(512, 0.0f);
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

// D-172: a host that connects the components after activating the processor
// (or reconnects while active) still gets an open DataExchange queue.
TEST_CASE("Connect after activation opens the DataExchange queue",
          "[disrumpo][data-exchange][lifecycle][D-172]")
{
    constexpr int32 kBlock = 2048;  // one block is past the ~30 ms send throttle
    MockDataExchangeHost host;
    auto proc = std::make_unique<Disrumpo::Processor>();
    REQUIRE(proc->initialize(&host) == kResultOk);
    auto ctrl = std::make_unique<Disrumpo::Controller>();
    REQUIRE(ctrl->initialize(nullptr) == kResultOk);

    ProcessSetup setup{};
    setup.sampleRate = 48000.0;
    setup.maxSamplesPerBlock = kBlock;
    setup.symbolicSampleSize = kSample32;
    setup.processMode = kRealtime;
    REQUIRE(proc->setupProcessing(setup) == kResultOk);
    REQUIRE(proc->setActive(true) == kResultOk);

    auto* ctrlConn = static_cast<IConnectionPoint*>(static_cast<EditControllerEx1*>(ctrl.get()));
    REQUIRE(proc->connect(ctrlConn) == kResultOk);

    std::vector<float> inL(kBlock, 0.25f);
    std::vector<float> inR(kBlock, 0.25f);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    processStereo(*proc, inL, inR, outL, outR);

    std::printf("T2.19 D-172 connect after activation: queues opened %d, blocks sent %d (want 1, >= 1)\n",
                host.opened, host.sent);
    CHECK(host.opened == 1);
    CHECK(host.sent >= 1);

    REQUIRE(proc->setActive(false) == kResultOk);
    REQUIRE(proc->disconnect(ctrlConn) == kResultOk);
    REQUIRE(ctrl->terminate() == kResultOk);
    REQUIRE(proc->terminate() == kResultOk);
}

// D-045: the mod offsets now ride in the SpectrumBlock. The block size stays a
// multiple of the 32-byte alignment (macOS aligned_alloc), the spectrum samples
// are the same mono mixdowns as before, and the controller FIFOs receive them unchanged.
TEST_CASE("SpectrumBlock carries mod offsets and the unchanged spectrum samples",
          "[disrumpo][data-exchange][D-045]")
{
    constexpr int32 kBlock = 2048;
    constexpr double kRate = 48000.0;
    MockDataExchangeHost host;
    auto proc = std::make_unique<Disrumpo::Processor>();
    REQUIRE(proc->initialize(&host) == kResultOk);
    auto ctrl = std::make_unique<Disrumpo::Controller>();
    REQUIRE(ctrl->initialize(nullptr) == kResultOk);
    auto* ctrlConn = static_cast<IConnectionPoint*>(static_cast<EditControllerEx1*>(ctrl.get()));
    REQUIRE(proc->connect(ctrlConn) == kResultOk);

    ProcessSetup setup{};
    setup.sampleRate = kRate;
    setup.maxSamplesPerBlock = kBlock;
    setup.symbolicSampleSize = kSample32;
    setup.processMode = kRealtime;
    REQUIRE(proc->setupProcessing(setup) == kResultOk);
    REQUIRE(proc->setActive(true) == kResultOk);

    std::printf("T2.19 D-045 block: sizeof(SpectrumBlock) %zu, queue block size %u, alignment %u, "
                "size %% alignment = %u\n",
                sizeof(Disrumpo::SpectrumBlock), host.blockSize, host.alignment,
                host.alignment != 0 ? host.blockSize % host.alignment : 0u);
    REQUIRE(host.opened == 1);
    CHECK(host.alignment == 32);
    CHECK(host.blockSize % host.alignment == 0);
    CHECK(host.blockSize >= sizeof(Disrumpo::SpectrumBlock));

    // Macro 1 at 0.8 routed to Global Mix at amount +1: one known non-zero offset
    Krate::Test::ParameterChanges changes;
    changes.addChange(Disrumpo::makeRoutingParamId(0, 0), 5.0 / static_cast<double>(Disrumpo::kUIModSourceCount - 1));
    changes.addChange(Disrumpo::makeRoutingParamId(0, 1),
                      static_cast<double>(Disrumpo::ModDest::kGlobalMix) /
                          static_cast<double>(Disrumpo::ModDest::kTotalDestinations - 1));
    changes.addChange(Disrumpo::makeRoutingParamId(0, 2), 1.0);
    changes.addChange(Disrumpo::makeModParamId(Disrumpo::ModParamType::kMacro1Value), 0.8);

    std::vector<float> inL(kBlock);
    std::vector<float> inR(kBlock);
    generateSine(inL.data(), kBlock, 1000.0f, kRate);
    generateSine(inR.data(), kBlock, 700.0f, kRate);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    processStereo(*proc, inL, inR, outL, outR, &changes);
    REQUIRE(host.sent == 1);

    const auto& blk = host.lastBlock();
    size_t inMismatch = 0;
    size_t outMismatch = 0;
    for (int32 i = 0; i < kBlock; ++i) {
        const auto k = static_cast<size_t>(i);
        if (blk.inputSamples[k] != (inL[k] + inR[k]) * 0.5f) ++inMismatch;
        if (blk.outputSamples[k] != (outL[k] + outR[k]) * 0.5f) ++outMismatch;
    }
    size_t strayOffsets = 0;
    for (uint32_t d = 0; d < Disrumpo::kSpectrumBlockModOffsets; ++d) {
        if (d != Disrumpo::ModDest::kGlobalMix && blk.modOffsets[d] != 0.0f) ++strayOffsets;
    }
    const float mixOffset = blk.modOffsets[Disrumpo::ModDest::kGlobalMix];
    std::printf("T2.19 D-045 block content: numSamples %u, rate %.0f, input mismatches %zu, output mismatches %zu, "
                "Global Mix offset %.6f (want > 0), stray offsets %zu\n",
                blk.numSamples, static_cast<double>(blk.sampleRate), inMismatch, outMismatch,
                static_cast<double>(mixOffset), strayOffsets);
    CHECK(blk.numSamples == static_cast<uint32_t>(kBlock));
    CHECK(blk.sampleRate == static_cast<float>(kRate));
    CHECK(inMismatch == 0);
    CHECK(outMismatch == 0);
    CHECK(mixOffset > 0.1f);
    CHECK(strayOffsets == 0);
    // The level-meter window rides after the mod offsets (it covers this whole block)
    STATIC_REQUIRE(offsetof(Disrumpo::SpectrumBlock, meters) >=
                   offsetof(Disrumpo::SpectrumBlock, modOffsets) + sizeof(float) * Disrumpo::kSpectrumBlockModOffsets);
    std::printf("Meters: window samples %u (want %d), input L peak %.6f\n", blk.meters.numSamples, kBlock,
                static_cast<double>(blk.meters.inPeak[0]));
    CHECK(blk.meters.numSamples == static_cast<uint32_t>(kBlock));
    CHECK(blk.meters.inPeak[0] > 0.99f);

    // The controller's FIFOs receive exactly the block's samples
    DataExchangeBlock deb{.data = host.lastSent.data(), .size = host.blockSize, .blockID = 1};
    ctrl->onDataExchangeBlocksReceived(0, 1, &deb, static_cast<TBool>(false));
    std::vector<float> readIn(static_cast<size_t>(kBlock));
    std::vector<float> readOut(static_cast<size_t>(kBlock));
    REQUIRE(ctrl->getLocalInputFIFO().readLatest(readIn.data(), readIn.size()) == readIn.size());
    REQUIRE(ctrl->getLocalOutputFIFO().readLatest(readOut.data(), readOut.size()) == readOut.size());
    CHECK(std::memcmp(readIn.data(), blk.inputSamples, readIn.size() * sizeof(float)) == 0);
    CHECK(std::memcmp(readOut.data(), blk.outputSamples, readOut.size() * sizeof(float)) == 0);

    REQUIRE(proc->setActive(false) == kResultOk);
    REQUIRE(proc->disconnect(ctrlConn) == kResultOk);
    REQUIRE(ctrl->terminate() == kResultOk);
    REQUIRE(proc->terminate() == kResultOk);
}

// ==============================================================================
// D-133: the spectrum feed is contiguous audio at every host block size
// ==============================================================================

namespace {

/// Layout of Processor::SharedDisplay, the pointer the SharedDisplayBridge holds
/// (the controller casts it the same way).
struct SharedDisplayLayout {
    Krate::DSP::SpectrumFIFO<8192>* inputFIFO;
    Krate::DSP::SpectrumFIFO<8192>* outputFIFO;
    std::atomic<float>* sampleRate;
};

/// Largest sample-to-sample step and largest sine-recurrence residual
/// |x[n+1] - 2cos(w) x[n] + x[n-1]| of a window that should be one unit sine.
struct Continuity {
    double maxStep = 0.0;
    double maxResidual = 0.0;
};

Continuity measureSineContinuity(const std::vector<float>& x, double w) {
    Continuity c;
    const double c2 = 2.0 * std::cos(w);
    for (size_t i = 1; i < x.size(); ++i)
        c.maxStep = std::max(c.maxStep, std::fabs(static_cast<double>(x[i]) - static_cast<double>(x[i - 1])));
    for (size_t i = 1; i + 1 < x.size(); ++i) {
        const double r = static_cast<double>(x[i + 1]) - (c2 * static_cast<double>(x[i])) + static_cast<double>(x[i - 1]);
        c.maxResidual = std::max(c.maxResidual, std::fabs(r));
    }
    return c;
}

}  // namespace

TEST_CASE("Spectrum FIFO receives a contiguous 1 kHz sine at 64-sample blocks",
          "[disrumpo][data-exchange][D-133]")
{
    constexpr double kRate = 48000.0;
    constexpr double kFreq = 1000.0;
    constexpr double kW = 6.283185307179586 * kFreq / kRate;
    constexpr double kMaxStepBar = kW * 1.1;  // a unit 1 kHz sine never steps further
    constexpr double kResidualBar = 1e-3;     // float rounding of a clean sine is ~1e-6
    constexpr size_t kWindow = 2048;          // SpectrumAnalyzer fftSize

    for (const int32 blockSize : {64, 512, 2048}) {
        MockDataExchangeHost host;
        auto proc = std::make_unique<Disrumpo::Processor>();
        REQUIRE(proc->initialize(&host) == kResultOk);
        auto ctrl = std::make_unique<Disrumpo::Controller>();
        REQUIRE(ctrl->initialize(nullptr) == kResultOk);
        auto* ctrlConn = static_cast<IConnectionPoint*>(static_cast<EditControllerEx1*>(ctrl.get()));
        REQUIRE(proc->connect(ctrlConn) == kResultOk);
        ProcessSetup setup{};
        setup.sampleRate = kRate;
        setup.maxSamplesPerBlock = blockSize;
        setup.symbolicSampleSize = kSample32;
        setup.processMode = kRealtime;
        REQUIRE(proc->setupProcessing(setup) == kResultOk);
        REQUIRE(proc->setActive(true) == kResultOk);

        const auto bs = static_cast<size_t>(blockSize);
        std::vector<float> inL(bs);
        std::vector<float> inR(bs);
        std::vector<float> outL(bs);
        std::vector<float> outR(bs);
        const int numBlocks = static_cast<int>(96000 / blockSize);  // 2 s
        std::vector<float> inHistory;
        std::vector<float> outHistory;
        inHistory.reserve(static_cast<size_t>(numBlocks + 64) * bs);
        outHistory.reserve(static_cast<size_t>(numBlocks + 64) * bs);
        size_t lastSendEnd = 0;  // history length when the last block was sent

        auto runBlock = [&](int b) {
            generateSine(inL.data(), blockSize, static_cast<float>(kFreq), kRate, b * blockSize);
            std::copy(inL.begin(), inL.end(), inR.begin());
            const int sentBefore = host.sent;
            processStereo(*proc, inL, inR, outL, outR);
            for (size_t i = 0; i < bs; ++i) {
                inHistory.push_back((inL[i] + inR[i]) * 0.5f);
                outHistory.push_back((outL[i] + outR[i]) * 0.5f);
            }
            if (host.sent != sentBefore) {
                lastSendEnd = inHistory.size();
                DataExchangeBlock deb{.data = host.lastSent.data(), .size = host.blockSize, .blockID = 1};
                ctrl->onDataExchangeBlocksReceived(0, 1, &deb, static_cast<TBool>(false));
            }
        };
        for (int b = 0; b < numBlocks; ++b) runBlock(b);

        // Display path (DataExchange -> controller FIFO) and Tier-3 path (shared FIFO)
        std::vector<float> local(kWindow);
        REQUIRE(ctrl->getLocalInputFIFO().readLatest(local.data(), kWindow) == kWindow);
        const auto id = stateTrailerInstanceId(Disrumpo::TestSupport::getStateBytes(*proc));
        const auto* shared = static_cast<const SharedDisplayLayout*>(
            Krate::Plugins::SharedDisplayBridge::instance().lookupInstance(id));
        REQUIRE(shared != nullptr);
        std::vector<float> tier3(kWindow);
        REQUIRE(shared->inputFIFO->readLatest(tier3.data(), kWindow) == kWindow);
        std::vector<float> tier3Out(kWindow);
        REQUIRE(shared->outputFIFO->readLatest(tier3Out.data(), kWindow) == kWindow);

        const auto localC = measureSineContinuity(local, kW);
        const auto tier3C = measureSineContinuity(tier3, kW);

        // The last block sent holds the audio just before it was sent, sample for sample
        const auto& blk = host.lastBlock();
        const size_t n = blk.numSamples;
        REQUIRE(n <= lastSendEnd);
        size_t blockInMismatch = 0;
        size_t blockOutMismatch = 0;
        for (size_t i = 0; i < n; ++i) {
            if (blk.inputSamples[i] != inHistory[lastSendEnd - n + i]) ++blockInMismatch;
            if (blk.outputSamples[i] != outHistory[lastSendEnd - n + i]) ++blockOutMismatch;
        }
        size_t tier3OutMismatch = 0;
        for (size_t i = 0; i < kWindow; ++i)
            if (tier3Out[i] != outHistory[outHistory.size() - kWindow + i]) ++tier3OutMismatch;

        std::printf("D-133 block %4d: sends %3d, last block %4zu samples; controller FIFO max step %.4f residual %.2e;"
                    " Tier-3 FIFO max step %.4f residual %.2e (bars %.4f / %.0e); last block in/out mismatches"
                    " %zu/%zu; Tier-3 output mismatches %zu\n",
                    blockSize, host.sent, n, localC.maxStep, localC.maxResidual, tier3C.maxStep,
                    tier3C.maxResidual, kMaxStepBar, kResidualBar, blockInMismatch, blockOutMismatch,
                    tier3OutMismatch);
        INFO("block size " << blockSize);
        CHECK(localC.maxStep < kMaxStepBar);
        CHECK(localC.maxResidual < kResidualBar);
        CHECK(tier3C.maxStep < kMaxStepBar);
        CHECK(tier3C.maxResidual < kResidualBar);
        CHECK(n == kWindow);
        CHECK(blockInMismatch == 0);
        CHECK(blockOutMismatch == 0);
        CHECK(tier3OutMismatch == 0);

        // The feed allocates nothing on the audio thread once running
        size_t allocations = 0;
        {
            TestHelpers::AllocationScope scope;
            for (int b = numBlocks; b < numBlocks + 64; ++b) runBlock(b);
            allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        std::printf("D-133 block %4d: allocations over 64 more blocks %zu\n", blockSize, allocations);
        CHECK(allocations == 0);

        REQUIRE(proc->setActive(false) == kResultOk);
        REQUIRE(proc->disconnect(ctrlConn) == kResultOk);
        REQUIRE(ctrl->terminate() == kResultOk);
        REQUIRE(proc->terminate() == kResultOk);
    }
}

// ==============================================================================
// Level meters: the window the processor accumulates between two sends
// ==============================================================================

namespace {

double linDb(double lin) { return 20.0 * std::log10(std::max(lin, 1e-12)); }
double meanSqDb(double meanSq) { return 10.0 * std::log10(std::max(meanSq, 1e-24)); }

/// Processor on a Tier-1 mock host at 48 kHz, 256-sample blocks, one band with every
/// node at drive 0 and Oversample Max 1x: the band and the output are the input.
struct MeterHarness {
    static constexpr double kRate = 48000.0;
    static constexpr int32 kBlock = 256;

    MockDataExchangeHost host;
    std::unique_ptr<Disrumpo::Processor> proc = std::make_unique<Disrumpo::Processor>();
    std::unique_ptr<Disrumpo::Controller> ctrl = std::make_unique<Disrumpo::Controller>();
    IConnectionPoint* ctrlConn = nullptr;
    std::vector<float> inL = std::vector<float>(kBlock);
    std::vector<float> inR = std::vector<float>(kBlock);
    std::vector<float> outL = std::vector<float>(kBlock);
    std::vector<float> outR = std::vector<float>(kBlock);

    explicit MeterHarness(double outputGainNorm = 0.5) {
        REQUIRE(proc->initialize(&host) == kResultOk);
        REQUIRE(ctrl->initialize(nullptr) == kResultOk);
        ctrlConn = static_cast<IConnectionPoint*>(static_cast<EditControllerEx1*>(ctrl.get()));
        REQUIRE(proc->connect(ctrlConn) == kResultOk);
        ProcessSetup setup{};
        setup.sampleRate = kRate;
        setup.maxSamplesPerBlock = kBlock;
        setup.symbolicSampleSize = kSample32;
        setup.processMode = kRealtime;
        REQUIRE(proc->setupProcessing(setup) == kResultOk);
        REQUIRE(proc->setActive(true) == kResultOk);
        Krate::Test::ParameterChanges pc;
        pc.addChange(Disrumpo::kBandCountId, 0.0);
        pc.addChange(Disrumpo::kOversampleMaxId, 0.0);
        pc.addChange(Disrumpo::kOutputGainId, outputGainNorm);
        for (uint8_t n = 0; n < 4; ++n)
            pc.addChange(Disrumpo::makeNodeParamId(0, n, Disrumpo::NodeParamType::kNodeDrive), 0.0);
        std::vector<float> none;
        processStereo(*proc, none, none, none, none, &pc);
        // Reactivate: every smoother starts at its target
        REQUIRE(proc->setActive(false) == kResultOk);
        REQUIRE(proc->setActive(true) == kResultOk);
    }
    ~MeterHarness() {
        proc->setActive(static_cast<TBool>(false));
        proc->disconnect(ctrlConn);
        ctrl->terminate();
        proc->terminate();
    }
    MeterHarness(const MeterHarness&) = delete;
    MeterHarness& operator=(const MeterHarness&) = delete;
    MeterHarness(MeterHarness&&) = delete;
    MeterHarness& operator=(MeterHarness&&) = delete;

    /// One block; true when it sent a SpectrumBlock
    bool run() {
        const int before = host.sent;
        processStereo(*proc, inL, inR, outL, outR);
        return host.sent != before;
    }
    [[nodiscard]] const Disrumpo::MeterWindow& meters() const { return host.lastBlock().meters; }
};

}  // namespace

TEST_CASE("Level meters read a -6 dBFS sine at the input, the output and band 0",
          "[disrumpo][data-exchange][meters]")
{
    MeterHarness h;
    // 0.5 amplitude = -6.02 dBFS peak, -9.03 dBFS RMS; 1 kHz at 48 kHz has a sample on every crest
    int block = 0;
    auto runSine = [&] {
        generateSine(h.inL.data(), MeterHarness::kBlock, 1000.0f, MeterHarness::kRate, block * MeterHarness::kBlock);
        for (auto& s : h.inL) s *= 0.5f;
        h.inR = h.inL;
        ++block;
        return h.run();
    };
    int sends = 0;
    while (sends < 2) sends += runSine() ? 1 : 0;  // the second window: no start-up in it
    const auto& m = h.meters();
    std::printf("Meters -6 dBFS sine: window %u samples; in peak %.3f/%.3f dB, in RMS %.3f/%.3f dB; out peak "
                "%.3f/%.3f dB, out RMS %.3f/%.3f dB; band peaks %.3f %.4f %.4f %.4f dB; clip 0x%x\n",
                m.numSamples, linDb(m.inPeak[0]), linDb(m.inPeak[1]), meanSqDb(m.inMeanSq[0]),
                meanSqDb(m.inMeanSq[1]), linDb(m.outPeak[0]), linDb(m.outPeak[1]), meanSqDb(m.outMeanSq[0]),
                meanSqDb(m.outMeanSq[1]), linDb(m.bandPeak[0]), static_cast<double>(m.bandPeak[1]),
                static_cast<double>(m.bandPeak[2]), static_cast<double>(m.bandPeak[3]), m.clipFlags);
    CHECK(m.numSamples == 1536u);  // six blocks: the first past the 30 ms (1440-sample) throttle
    for (int c = 0; c < 2; ++c) {
        INFO("channel " << c);
        CHECK(std::fabs(linDb(m.inPeak[c]) - -6.02) < 0.05);
        CHECK(std::fabs(meanSqDb(m.inMeanSq[c]) - -9.03) < 0.05);
        CHECK(std::fabs(linDb(m.outPeak[c]) - -6.02) < 0.05);
        CHECK(std::fabs(meanSqDb(m.outMeanSq[c]) - -9.03) < 0.05);
    }
    CHECK(std::fabs(linDb(m.bandPeak[0]) - linDb(m.inPeak[0])) < 0.05);
    CHECK(m.bandPeak[1] == 0.0f);  // inactive bands report nothing
    CHECK(m.bandPeak[2] == 0.0f);
    CHECK(m.bandPeak[3] == 0.0f);
    CHECK(m.clipFlags == 0u);

    // Metering allocates nothing on the audio thread
    size_t allocations = 0;
    {
        TestHelpers::AllocationScope scope;
        for (int i = 0; i < 64; ++i) (void)runSine();
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    std::printf("Meters: allocations over 64 metered blocks %zu\n", allocations);
    CHECK(allocations == 0);
}

TEST_CASE("Level meters accumulate every block between two sends",
          "[disrumpo][data-exchange][meters]")
{
    MeterHarness h;
    // A different level in every block; L and R differ
    double peak[2] = {0.0, 0.0};
    double sumSq[2] = {0.0, 0.0};
    uint32_t count = 0;
    int checkedWindows = 0;
    for (int block = 0; block < 60; ++block) {
        const auto ampL = static_cast<float>(0.05 + 0.9 * std::fabs(std::sin(0.7 * block)));
        const float ampR = ampL * 0.5f;
        generateSine(h.inL.data(), MeterHarness::kBlock, 997.0f, MeterHarness::kRate, block * MeterHarness::kBlock);
        for (int32 i = 0; i < MeterHarness::kBlock; ++i) {
            const auto k = static_cast<size_t>(i);
            h.inR[k] = h.inL[k] * ampR;
            h.inL[k] *= ampL;
            for (int c = 0; c < 2; ++c) {
                const double x = (c == 0) ? h.inL[k] : h.inR[k];
                peak[c] = std::max(peak[c], std::fabs(x));
                sumSq[c] += x * x;
            }
        }
        count += static_cast<uint32_t>(MeterHarness::kBlock);
        if (!h.run()) continue;
        const auto& m = h.meters();
        INFO("window " << checkedWindows << " ending at block " << block << ": " << count << " samples");
        CHECK(m.numSamples == count);
        for (int c = 0; c < 2; ++c) {
            CHECK(std::fabs(linDb(m.inPeak[c]) - linDb(peak[c])) < 0.01);
            CHECK(std::fabs(meanSqDb(m.inMeanSq[c]) - meanSqDb(sumSq[c] / count)) < 0.01);
        }
        if (checkedWindows == 0)
            std::printf("Meters accumulation: first window %u samples (%u blocks), in L peak %.4f (want %.4f), "
                        "in L RMS %.4f dB (want %.4f dB)\n",
                        m.numSamples, m.numSamples / MeterHarness::kBlock, static_cast<double>(m.inPeak[0]),
                        peak[0], meanSqDb(m.inMeanSq[0]), meanSqDb(sumSq[0] / count));
        ++checkedWindows;
        peak[0] = peak[1] = sumSq[0] = sumSq[1] = 0.0;
        count = 0;
    }
    std::printf("Meters accumulation: %d windows checked\n", checkedWindows);
    CHECK(checkedWindows >= 9);
}

TEST_CASE("A single sample above 0 dBFS sets the clip bit of its channel only",
          "[disrumpo][data-exchange][meters]")
{
    // Unity output: the sample reaches input L, band 0 and output L. Output Gain -6 dB:
    // it no longer clips at the output.
    for (const double outputGainNorm : {0.5, (-6.0 + 24.0) / 48.0}) {
        MeterHarness h(outputGainNorm);
        std::fill(h.inL.begin(), h.inL.end(), 0.0f);
        std::fill(h.inR.begin(), h.inR.end(), 0.0f);
        while (!h.run()) {}
        const uint32_t silentFlags = h.meters().clipFlags;
        const float silentPeak = h.meters().inPeak[0];
        h.inL[100] = 1.2f;
        bool sent = h.run();
        h.inL[100] = 0.0f;
        while (!sent) sent = h.run();
        const uint32_t clipFlags = h.meters().clipFlags;
        while (!h.run()) {}
        const uint32_t nextFlags = h.meters().clipFlags;
        const uint32_t want = (outputGainNorm == 0.5)
            ? (Disrumpo::kMeterClipInL | Disrumpo::kMeterClipOutL | Disrumpo::kMeterClipBand0)
            : (Disrumpo::kMeterClipInL | Disrumpo::kMeterClipBand0);
        std::printf("Meters clip (output gain norm %.3f): silent window flags 0x%x peak %.3g; window with the 1.2 "
                    "sample 0x%x (want 0x%x); next window 0x%x\n",
                    outputGainNorm, silentFlags, static_cast<double>(silentPeak), clipFlags, want, nextFlags);
        CHECK(silentFlags == 0u);
        CHECK(silentPeak == 0.0f);
        CHECK(clipFlags == want);
        CHECK(nextFlags == 0u);
    }
}

TEST_CASE("Silence reads zero on every meter", "[disrumpo][data-exchange][meters]")
{
    MeterHarness h;
    std::fill(h.inL.begin(), h.inL.end(), 0.0f);
    std::fill(h.inR.begin(), h.inR.end(), 0.0f);
    int sends = 0;
    while (sends < 3) sends += h.run() ? 1 : 0;
    const auto& m = h.meters();
    float largest = 0.0f;
    for (int c = 0; c < 2; ++c)
        largest = std::max({largest, m.inPeak[c], m.inMeanSq[c], m.outPeak[c], m.outMeanSq[c]});
    for (const float b : m.bandPeak) largest = std::max(largest, b);
    std::printf("Meters silence: largest value %.3g, clip 0x%x, window %u samples\n", static_cast<double>(largest),
                m.clipFlags, m.numSamples);
    CHECK(largest == 0.0f);
    CHECK(m.clipFlags == 0u);
    CHECK(m.numSamples > 0u);
}

TEST_CASE("Tier 3 SharedMeters delivers the DataExchange meter windows",
          "[disrumpo][data-exchange][meters][tier3]")
{
    MeterHarness h;
    const auto id = stateTrailerInstanceId(Disrumpo::TestSupport::getStateBytes(*h.proc));
    auto* shared = static_cast<Disrumpo::SharedDisplay*>(Krate::Plugins::SharedDisplayBridge::instance().lookupInstance(id));
    REQUIRE(shared != nullptr);
    REQUIRE(shared->meters != nullptr);

    int block = 0;
    bool clipNext = false;
    auto runBlock = [&] {
        generateSine(h.inL.data(), MeterHarness::kBlock, 440.0f, MeterHarness::kRate, block * MeterHarness::kBlock);
        const auto amp = static_cast<float>(0.2 + 0.1 * (block % 5));
        for (auto& s : h.inL) s *= amp;
        h.inR = h.inL;
        if (clipNext) h.inL[7] = 1.5f;
        clipNext = false;
        ++block;
        return h.run();
    };

    // Read after every send: Tier 3 holds exactly the window DataExchange carried
    Disrumpo::MeterWindow tier3;
    int same = 0;
    int sends = 0;
    while (sends < 5) {
        if (!runBlock()) continue;
        ++sends;
        REQUIRE(shared->meters->consume(tier3));
        if (tier3 == h.meters()) ++same;
        CHECK_FALSE(shared->meters->consume(tier3));  // nothing new until the next send
    }
    std::printf("Meters Tier 3: %d of %d windows identical to the DataExchange block\n", same, sends);
    CHECK(same == sends);

    // Two sends without a read: the read gets both windows merged (the clip in the first kept)
    Disrumpo::MeterWindow first;
    Disrumpo::MeterWindow second;
    clipNext = true;
    while (!runBlock()) {}
    first = h.meters();
    while (!runBlock()) {}
    second = h.meters();
    REQUIRE(shared->meters->consume(tier3));
    Disrumpo::MeterWindow merged = first;
    Disrumpo::mergeMeterWindow(merged, second);
    std::printf("Meters Tier 3 merge: read %u samples (want %u + %u), in L peak %.4f (want max %.4f, %.4f), "
                "clip 0x%x (want 0x%x)\n",
                tier3.numSamples, first.numSamples, second.numSamples, static_cast<double>(tier3.inPeak[0]),
                static_cast<double>(first.inPeak[0]), static_cast<double>(second.inPeak[0]), tier3.clipFlags,
                first.clipFlags | second.clipFlags);
    CHECK((first.clipFlags & Disrumpo::kMeterClipInL) != 0u);
    CHECK((second.clipFlags & Disrumpo::kMeterClipInL) == 0u);
    CHECK(tier3.numSamples == first.numSamples + second.numSamples);
    CHECK(tier3.inPeak[0] == std::max(first.inPeak[0], second.inPeak[0]));
    CHECK(tier3.clipFlags == (first.clipFlags | second.clipFlags));
    CHECK(std::fabs(meanSqDb(tier3.inMeanSq[0]) - meanSqDb(merged.inMeanSq[0])) < 1e-3);
}

