// ==============================================================================
// Fix plan T2.31 (MB-065, MB-102, MB-100): the MetersBlock bridge
// ==============================================================================
// * The controller folds every MetersBlock delivered between two UI ticks
//   (max of the peaks and of each pad's glow bucket), so a drum transient in
//   any block reaches the kit meter and the pad glow, not only the last block.
// * The processor keeps the peak of blocks it could not send (DataExchange
//   queue full) and carries it in the next block that is sent.
// * KitMetersView holds a peak and lets it fall at 20 dB/s at the 30 Hz tick.
// * The processor reports its output-bus activation in MetersBlock, so the
//   controller's isBusActive() (Output Bus tooltip, FR-066) follows the host.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "processor/meters_block.h"
#include "processor/processor.h"
#include "ui/kit_meters_view.h"
#include "plugin_ids.h"

#include "vst_event_list.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

Membrum::MetersBlock makeMeters(float peakL, float peakR, int pad, std::uint8_t bucket)
{
    Membrum::MetersBlock mb;
    mb.peakL = peakL;
    mb.peakR = peakR;
    mb.padGlowBuckets[pad] = bucket;
    return mb;
}

void deliver(Membrum::Controller& ctl, std::vector<Membrum::MetersBlock>& meters)
{
    std::vector<DataExchangeBlock> blocks;
    for (auto& mb : meters)
    {
        DataExchangeBlock b{};
        b.data = &mb;
        b.size = static_cast<uint32>(sizeof(Membrum::MetersBlock));
        b.blockID = static_cast<DataExchangeBlockID>(blocks.size());
        blocks.push_back(b);
    }
    ctl.onDataExchangeBlocksReceived(Membrum::kMetersDataExchangeUserContextId,
                                     static_cast<uint32>(blocks.size()), blocks.data(),
                                     static_cast<TBool>(false));
}

std::uint8_t mirrorBucket(const Membrum::Controller& ctl, int pad)
{
    std::array<std::uint8_t, Membrum::kNumPads> buckets{};
    ctl.padGlowMirrorForTest().snapshot(buckets);
    return buckets[static_cast<std::size_t>(pad)];
}

/// Host context implementing IDataExchangeHandler: one block of storage; a
/// lock fails while `full` is set (the queue holds no free block), and every
/// block sent to the controller is copied out.
class MetersExchangeHost : public IDataExchangeHandler
{
public:
    MetersExchangeHost() = default;
    virtual ~MetersExchangeHost() = default;
    MetersExchangeHost(const MetersExchangeHost&) = delete;
    MetersExchangeHost& operator=(const MetersExchangeHost&) = delete;
    MetersExchangeHost(MetersExchangeHost&&) = delete;
    MetersExchangeHost& operator=(MetersExchangeHost&&) = delete;

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IDataExchangeHandler::iid)
            || FUnknownPrivate::iidEqual(iid, FUnknown::iid))
        {
            *obj = static_cast<IDataExchangeHandler*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API openQueue(IAudioProcessor* /*processor*/, uint32 size,
                                 uint32 /*numBlocks*/, uint32 /*align*/,
                                 DataExchangeUserContextID /*userContextID*/,
                                 DataExchangeQueueID* outID) override
    {
        blockSize = size;
        storage.assign(size / sizeof(float) + 1, 0.0f);
        *outID = 1;
        return kResultTrue;
    }
    tresult PLUGIN_API closeQueue(DataExchangeQueueID /*queueID*/) override
    {
        return kResultTrue;
    }
    tresult PLUGIN_API lockBlock(DataExchangeQueueID /*queueId*/,
                                 DataExchangeBlock* block) override
    {
        if (full || storage.empty())
            return kResultFalse;
        block->data = storage.data();
        block->size = blockSize;
        block->blockID = 1;
        return kResultTrue;
    }
    tresult PLUGIN_API freeBlock(DataExchangeQueueID /*queueId*/,
                                 DataExchangeBlockID /*blockID*/,
                                 TBool sendToController) override
    {
        if (sendToController != 0)
        {
            ++sent;
            std::memcpy(&lastSent, storage.data(), sizeof(Membrum::MetersBlock));
        }
        return kResultTrue;
    }

    bool full = false;
    int sent = 0;
    uint32 blockSize = 0;
    std::vector<float> storage;
    Membrum::MetersBlock lastSent{};
};

/// The processor's connection peer: accepts and drops every message.
class NullPeer : public IConnectionPoint
{
public:
    NullPeer() = default;
    virtual ~NullPeer() = default;
    NullPeer(const NullPeer&) = delete;
    NullPeer& operator=(const NullPeer&) = delete;
    NullPeer(NullPeer&&) = delete;
    NullPeer& operator=(NullPeer&&) = delete;

    tresult PLUGIN_API queryInterface(const TUID /*iid*/, void** obj) override
    {
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API connect(IConnectionPoint* /*other*/) override { return kResultOk; }
    tresult PLUGIN_API disconnect(IConnectionPoint* /*other*/) override { return kResultOk; }
    tresult PLUGIN_API notify(IMessage* /*message*/) override { return kResultOk; }
};

constexpr int32 kMbBlock = 256;
constexpr double kMbRate = 48000.0;

/// A Processor connected to a MetersExchangeHost, active and processing.
struct ConnectedProcessor
{
    MetersExchangeHost host;
    NullPeer peer;
    std::unique_ptr<Membrum::Processor> proc = std::make_unique<Membrum::Processor>();
    std::vector<float> outL = std::vector<float>(kMbBlock, 0.0f);
    std::vector<float> outR = std::vector<float>(kMbBlock, 0.0f);

    ConnectedProcessor()
    {
        REQUIRE(proc->initialize(&host) == kResultOk);
        REQUIRE(proc->connect(&peer) == kResultTrue);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = kMbBlock;
        setup.sampleRate = kMbRate;
        REQUIRE(proc->setupProcessing(setup) == kResultOk);
        REQUIRE(proc->setActive(true) == kResultOk);
        REQUIRE(host.blockSize >= sizeof(Membrum::MetersBlock));
    }
    ~ConnectedProcessor()
    {
        proc->setActive(false);
        proc->disconnect(&peer);
        proc->terminate();
    }
    ConnectedProcessor(const ConnectedProcessor&) = delete;
    ConnectedProcessor& operator=(const ConnectedProcessor&) = delete;
    ConnectedProcessor(ConnectedProcessor&&) = delete;
    ConnectedProcessor& operator=(ConnectedProcessor&&) = delete;

    /// One block; returns the main-bus peak of channel L the host received.
    float render(Krate::Test::EventList* events)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        std::array<float*, 2> chans{outL.data(), outR.data()};
        AudioBusBuffers bus{};
        bus.numChannels = 2;
        bus.channelBuffers32 = chans.data();
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kMbBlock;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.inputEvents = events;
        REQUIRE(proc->process(data) == kResultOk);
        float peak = 0.0f;
        for (const float s : outL)
            peak = std::max(peak, std::abs(s));
        return peak;
    }
};

} // namespace

TEST_CASE("Meters: the peak of any block delivered between UI ticks reaches the meter",
          "[controller][meters]")
{
    Membrum::Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == kResultOk);

    std::vector<Membrum::MetersBlock> first{makeMeters(0.9f, 0.9f, 3, 20),
                                            makeMeters(0.1f, 0.1f, 3, 0),
                                            makeMeters(0.1f, 0.1f, 3, 0)};
    deliver(ctl, first);
    std::vector<Membrum::MetersBlock> second{makeMeters(0.05f, 0.05f, 3, 0)};
    deliver(ctl, second);

    ctl.consumeMetersForUiTick();

    INFO("pushed peakL " << ctl.lastPushedMetersForTest().peakL << " peakR "
                         << ctl.lastPushedMetersForTest().peakR << ", pad 3 glow bucket "
                         << static_cast<int>(mirrorBucket(ctl, 3)));
    CHECK(ctl.lastPushedMetersForTest().peakL >= 0.9f);
    CHECK(ctl.lastPushedMetersForTest().peakR >= 0.9f);
    CHECK(mirrorBucket(ctl, 3) == 20);

    // The next tick starts from the blocks delivered after this one.
    std::vector<Membrum::MetersBlock> third{makeMeters(0.2f, 0.2f, 3, 4)};
    deliver(ctl, third);
    ctl.consumeMetersForUiTick();
    CHECK(ctl.lastPushedMetersForTest().peakL == 0.2f);
    CHECK(mirrorBucket(ctl, 3) == 4);

    ctl.terminate();
}

TEST_CASE("Meters: the processor keeps the peak of blocks it could not send",
          "[processor][meters]")
{
    ConnectedProcessor cp;

    // Prime one sent block so the queue has carried data before.
    cp.render(nullptr);
    REQUIRE(cp.host.sent == 1);

    // The queue is full: the hit block and 0.5 s after it are never sent.
    cp.host.full = true;
    Krate::Test::EventList hit;
    hit.addNoteOn(36, 1.0f, 0);
    const float hitPeak = cp.render(&hit);
    float droppedPeak = hitPeak;
    for (int b = 0; b < static_cast<int>(kMbRate * 0.5) / kMbBlock; ++b)
        droppedPeak = std::max(droppedPeak, cp.render(nullptr));
    REQUIRE(cp.host.sent == 1);
    REQUIRE(hitPeak > 0.05f);

    // The queue has room again: this quieter block is sent.
    cp.host.full = false;
    const float quietPeak = cp.render(nullptr);
    REQUIRE(cp.host.sent == 2);

    INFO("hit block peak " << hitPeak << ", max dropped peak " << droppedPeak
                           << ", sent block's own peak " << quietPeak
                           << ", sent peakL " << cp.host.lastSent.peakL);
    REQUIRE(quietPeak < hitPeak);
    CHECK(cp.host.lastSent.peakL >= hitPeak);
    CHECK(cp.host.lastSent.peakL >= droppedPeak);

    // Once sent, the accumulator restarts: the next block carries its own peak.
    const float nextPeak = cp.render(nullptr);
    REQUIRE(cp.host.sent == 3);
    CHECK(cp.host.lastSent.peakL == nextPeak);
}

TEST_CASE("Meters: KitMetersView holds a peak and falls at 20 dB per second",
          "[ui][meters]")
{
    Membrum::UI::KitMetersView view(VSTGUI::CRect(0, 0, 100, 20));
    view.setPeaks(0.95f, 0.95f);
    view.setPeaks(0.0f, 0.0f);
    const double expected = 0.95 * std::pow(10.0, -20.0 / 20.0 / 30.0);
    INFO("after one tick: L " << view.peakLForTest() << " R " << view.peakRForTest()
                              << " expected " << expected);
    CHECK(std::abs(static_cast<double>(view.peakLForTest()) - expected) <= 1.0e-4);
    CHECK(std::abs(static_cast<double>(view.peakRForTest()) - expected) <= 1.0e-4);

    for (int i = 0; i < 30; ++i)
        view.setPeaks(0.0f, 0.0f);
    INFO("after 31 ticks: L " << view.peakLForTest());
    CHECK(view.peakLForTest() < 0.1f);
    CHECK(view.peakRForTest() < 0.1f);

    // A new, higher peak replaces the falling one at once.
    view.setPeaks(0.5f, 0.6f);
    CHECK(view.peakLForTest() == 0.5f);
    CHECK(view.peakRForTest() == 0.6f);
}

TEST_CASE("Meters: bus activation reaches the controller through MetersBlock",
          "[controller][meters][output_routing]")
{
    Membrum::Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == kResultOk);

    auto mb = makeMeters(0.0f, 0.0f, 0, 0);
    mb.busActiveMask = static_cast<std::uint16_t>((1u << 0) | (1u << 2));
    std::vector<Membrum::MetersBlock> meters{mb};
    deliver(ctl, meters);

    CHECK(ctl.isBusActive(0));
    CHECK(ctl.isBusActive(2));
    CHECK_FALSE(ctl.isBusActive(3));

    // The processor deactivates bus 2 again.
    meters[0].busActiveMask = 1;
    deliver(ctl, meters);
    CHECK(ctl.isBusActive(0));
    CHECK_FALSE(ctl.isBusActive(2));

    ctl.terminate();
}

TEST_CASE("Meters: the processor reports its active output buses in MetersBlock",
          "[processor][meters][output_routing]")
{
    ConnectedProcessor cp;
    REQUIRE(cp.proc->activateBus(kAudio, kOutput, 2, true) == kResultTrue);
    cp.render(nullptr);
    REQUIRE(cp.host.sent >= 1);
    INFO("busActiveMask " << cp.host.lastSent.busActiveMask);
    CHECK(cp.host.lastSent.busActiveMask == ((1u << 0) | (1u << 2)));

    // End to end: the controller follows the processor's activateBus.
    Membrum::Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == kResultOk);
    std::vector<Membrum::MetersBlock> meters{cp.host.lastSent};
    deliver(ctl, meters);
    CHECK(ctl.isBusActive(2));
    CHECK_FALSE(ctl.isBusActive(3));
    ctl.terminate();
}
