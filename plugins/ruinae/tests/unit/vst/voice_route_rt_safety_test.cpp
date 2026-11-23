// ==============================================================================
// Voice Route RT Safety Tests
// ==============================================================================
// Tests for the three race condition fixes in preset loading:
//   Issue 1: sendVoiceModRouteState() pre-allocated message
//   Issue 2: voiceRoutes_ published as one table (VoiceRouteTable, R-007)
//   Issue 3: (gone, R-005) the one-time pointer messages; display data now
//            travels as output parameters
//
// Tags: [voice-route][rt-safety]
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "drain_preset_transfer.h"
#include "vst_event_list.h"
#include "vst_param_changes.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// =============================================================================
// Helpers
// =============================================================================

namespace {

/// Expose protected/private members for testing
class TestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;

    // Access pre-allocated messages for verification
    Steinberg::Vst::IMessage* getVoiceRouteSyncMsg() const {
        return voiceRouteSyncMsg_.get();
    }
};

std::unique_ptr<TestableProcessor> makeTestableProcessor() {
    auto p = std::make_unique<TestableProcessor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);
    p->setActive(true);

    return p;
}

std::vector<char> captureState(Ruinae::Processor* proc) {
    Steinberg::MemoryStream stream;
    proc->getState(&stream);
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> data(static_cast<size_t>(size));
    Steinberg::int32 bytesRead = 0;
    stream.read(data.data(), static_cast<Steinberg::int32>(size), &bytesRead);
    return data;
}

void loadState(Ruinae::Processor* proc, const std::vector<char>& bytes) {
    Steinberg::MemoryStream stream;
    stream.write(const_cast<char*>(bytes.data()),
        static_cast<Steinberg::int32>(bytes.size()), nullptr);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    proc->setState(&stream);
}

/// Build a VoiceModRouteUpdate IMessage for a given slot
Steinberg::IPtr<Steinberg::Vst::IMessage> makeRouteUpdateMsg(
    int slot, int source, int dest, double amount, int curve = 0,
    double smoothMs = 0.0, int scale = 2, int bypass = 0, int active = 1) {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID("VoiceModRouteUpdate");
    auto* attrs = msg->getAttributes();
    attrs->setInt("slotIndex", static_cast<Steinberg::int64>(slot));
    attrs->setInt("source", static_cast<Steinberg::int64>(source));
    attrs->setInt("destination", static_cast<Steinberg::int64>(dest));
    attrs->setFloat("amount", amount);
    attrs->setInt("curve", static_cast<Steinberg::int64>(curve));
    attrs->setFloat("smoothMs", smoothMs);
    attrs->setInt("scale", static_cast<Steinberg::int64>(scale));
    attrs->setInt("bypass", static_cast<Steinberg::int64>(bypass));
    attrs->setInt("active", static_cast<Steinberg::int64>(active));
    return msg;
}

/// Build a VoiceModRouteRemove IMessage for a given slot
Steinberg::IPtr<Steinberg::Vst::IMessage> makeRouteRemoveMsg(int slot) {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID("VoiceModRouteRemove");
    auto* attrs = msg->getAttributes();
    attrs->setInt("slotIndex", static_cast<Steinberg::int64>(slot));
    return msg;
}

/// Decode voice routes from state bytes. Returns true if route at `slot` has
/// the expected source, destination, amount, and active flag.
struct RouteSnapshot {
    uint8_t source;
    uint8_t destination;
    float amount;
    uint8_t curve;
    float smoothMs;
    uint8_t scale;
    uint8_t bypass;
    uint8_t active;
};

/// Extract voice route data from serialized state bytes.
/// Returns empty vector if the state is unparseable.
std::vector<RouteSnapshot> extractRoutes(const std::vector<char>& stateBytes) {
    Steinberg::MemoryStream memStream(
        const_cast<char*>(stateBytes.data()),
        static_cast<Steinberg::TSize>(stateBytes.size()));
    Steinberg::IBStreamer streamer(&memStream, kLittleEndian);

    // Skip version
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version)) return {};

    // Skip all atomic parameter packs (same order as getState)
    // We need to call the load functions to advance the stream cursor.
    // Create dummy param packs.
    Ruinae::GlobalParams gp;
    Ruinae::OscAParams oa;
    Ruinae::OscBParams ob;
    Ruinae::MixerParams mp;
    Ruinae::RuinaeFilterParams fp;
    Ruinae::RuinaeDistortionParams dp;
    Ruinae::RuinaeTranceGateParams tp;
    Ruinae::AmpEnvParams aep;
    Ruinae::FilterEnvParams fep;
    Ruinae::ModEnvParams mep;
    Ruinae::LFO1Params l1;
    Ruinae::LFO2Params l2;
    Ruinae::ChaosModParams cmp;
    Ruinae::ModMatrixParams mmp;
    Ruinae::GlobalFilterParams gfp;
    Ruinae::RuinaeDelayParams dlp;
    Ruinae::RuinaeReverbParams rvp;
    Ruinae::MonoModeParams mop;

    if (!loadGlobalParams(gp, streamer)) return {};
    if (!loadOscAParams(oa, streamer)) return {};
    if (!loadOscBParams(ob, streamer)) return {};
    if (!loadMixerParams(mp, streamer)) return {};
    if (!loadFilterParams(fp, streamer)) return {};
    if (!loadDistortionParams(dp, streamer)) return {};
    if (!loadTranceGateParams(tp, streamer)) return {};
    if (!loadAmpEnvParams(aep, streamer)) return {};
    if (!loadFilterEnvParams(fep, streamer)) return {};
    if (!loadModEnvParams(mep, streamer)) return {};
    if (!loadLFO1Params(l1, streamer)) return {};
    if (!loadLFO2Params(l2, streamer)) return {};
    if (!loadChaosModParams(cmp, streamer)) return {};
    if (!loadModMatrixParams(mmp, streamer)) return {};
    if (!loadGlobalFilterParams(gfp, streamer)) return {};
    if (!loadDelayParams(dlp, streamer)) return {};
    if (!loadReverbParams(rvp, streamer)) return {};
    { Steinberg::int32 reverbType = 0; streamer.readInt32(reverbType); } // 125-dual-reverb
    if (!loadMonoModeParams(mop, streamer)) return {};

    // Now read voice routes
    std::vector<RouteSnapshot> routes;
    for (int i = 0; i < Krate::Plugins::kMaxVoiceRoutes; ++i) {
        RouteSnapshot rs{};
        Steinberg::int8 i8 = 0;
        if (!streamer.readInt8(i8)) break;
        rs.source = static_cast<uint8_t>(i8);
        if (!streamer.readInt8(i8)) break;
        rs.destination = static_cast<uint8_t>(i8);
        if (!streamer.readFloat(rs.amount)) break;
        if (!streamer.readInt8(i8)) break;
        rs.curve = static_cast<uint8_t>(i8);
        if (!streamer.readFloat(rs.smoothMs)) break;
        if (!streamer.readInt8(i8)) break;
        rs.scale = static_cast<uint8_t>(i8);
        if (!streamer.readInt8(i8)) break;
        rs.bypass = static_cast<uint8_t>(i8);
        if (!streamer.readInt8(i8)) break;
        rs.active = static_cast<uint8_t>(i8);
        routes.push_back(rs);
    }
    return routes;
}

} // anonymous namespace

// =============================================================================
// Issue 2: Atomic voice route tests
// =============================================================================

TEST_CASE("Voice route update via notify round-trips through getState",
          "[voice-route][rt-safety]") {
    auto proc = makeTestableProcessor();

    // Send a VoiceModRouteUpdate for slot 3
    auto msg = makeRouteUpdateMsg(
        /*slot=*/3, /*source=*/2, /*dest=*/1, /*amount=*/0.75,
        /*curve=*/1, /*smoothMs=*/10.0, /*scale=*/3, /*bypass=*/0, /*active=*/1);
    proc->notify(msg);

    // Drain preset transfer (in case notify triggers any deferred work)
    drainPresetTransfer(proc.get());

    // Read state back
    auto stateBytes = captureState(proc.get());
    auto routes = extractRoutes(stateBytes);

    REQUIRE(routes.size() == Krate::Plugins::kMaxVoiceRoutes);

    // Slot 3 should have our values
    CHECK(routes[3].source == 2);
    CHECK(routes[3].destination == 1);
    CHECK(routes[3].amount == Catch::Approx(0.75f).margin(0.001f));
    CHECK(routes[3].curve == 1);
    CHECK(routes[3].smoothMs == Catch::Approx(10.0f).margin(0.01f));
    CHECK(routes[3].scale == 3);
    CHECK(routes[3].bypass == 0);
    CHECK(routes[3].active == 1);

    // Other slots should be default (inactive)
    CHECK(routes[0].active == 0);
    CHECK(routes[1].active == 0);
    CHECK(routes[2].active == 0);
    CHECK(routes[4].active == 0);

    proc->terminate();
}

TEST_CASE("Voice route remove via notify clears slot in getState",
          "[voice-route][rt-safety]") {
    auto proc = makeTestableProcessor();

    // Set a route on slot 5
    auto setMsg = makeRouteUpdateMsg(
        /*slot=*/5, /*source=*/1, /*dest=*/0, /*amount=*/-0.5,
        /*curve=*/2, /*smoothMs=*/5.0, /*scale=*/1, /*bypass=*/0, /*active=*/1);
    proc->notify(setMsg);
    drainPresetTransfer(proc.get());

    // Verify it's set
    {
        auto bytes = captureState(proc.get());
        auto routes = extractRoutes(bytes);
        REQUIRE(routes.size() == Krate::Plugins::kMaxVoiceRoutes);
        REQUIRE(routes[5].active == 1);
        REQUIRE(routes[5].source == 1);
    }

    // Remove slot 5
    auto removeMsg = makeRouteRemoveMsg(5);
    proc->notify(removeMsg);
    drainPresetTransfer(proc.get());

    // Verify it's cleared
    auto bytes = captureState(proc.get());
    auto routes = extractRoutes(bytes);
    REQUIRE(routes.size() == Krate::Plugins::kMaxVoiceRoutes);
    CHECK(routes[5].active == 0);
    CHECK(routes[5].source == 0);
    CHECK(routes[5].destination == 0);
    CHECK(routes[5].amount == Catch::Approx(0.0f));

    proc->terminate();
}

TEST_CASE("Voice route update after preset load preserves both",
          "[voice-route][rt-safety]") {
    // Create a processor with a route on slot 0
    auto srcProc = makeTestableProcessor();
    auto routeMsg = makeRouteUpdateMsg(
        /*slot=*/0, /*source=*/3, /*dest=*/2, /*amount=*/0.5,
        /*curve=*/0, /*smoothMs=*/0.0, /*scale=*/2, /*bypass=*/0, /*active=*/1);
    srcProc->notify(routeMsg);
    drainPresetTransfer(srcProc.get());

    // Capture preset A (which has slot 0 active)
    auto presetA = captureState(srcProc.get());

    // Load preset A into a fresh processor
    auto proc = makeTestableProcessor();
    loadState(proc.get(), presetA);
    drainPresetTransfer(proc.get());

    // Now modify slot 7 via notify (UI thread path)
    auto updateMsg = makeRouteUpdateMsg(
        /*slot=*/7, /*source=*/5, /*dest=*/4, /*amount=*/-0.3,
        /*curve=*/2, /*smoothMs=*/20.0, /*scale=*/0, /*bypass=*/0, /*active=*/1);
    proc->notify(updateMsg);
    drainPresetTransfer(proc.get());

    // Verify both slot 0 (from preset) and slot 7 (from notify) are present
    auto bytes = captureState(proc.get());
    auto routes = extractRoutes(bytes);
    REQUIRE(routes.size() == Krate::Plugins::kMaxVoiceRoutes);

    // Slot 0 from preset
    CHECK(routes[0].active == 1);
    CHECK(routes[0].source == 3);
    CHECK(routes[0].destination == 2);
    CHECK(routes[0].amount == Catch::Approx(0.5f).margin(0.001f));

    // Slot 7 from notify
    CHECK(routes[7].active == 1);
    CHECK(routes[7].source == 5);
    CHECK(routes[7].destination == 4);
    CHECK(routes[7].amount == Catch::Approx(-0.3f).margin(0.001f));

    srcProc->terminate();
    proc->terminate();
}

// =============================================================================
// R-109: the notify clamp uses the voice enums' bounds
// =============================================================================

TEST_CASE("VoiceModRouteUpdate clamps to the voice enums", "[vst][modmatrix]") {
    // A host context makes initialize() allocate the VoiceModRouteState message,
    // so the echo sendVoiceModRouteState() writes can be read back from it.
    Steinberg::Vst::HostApplication host;
    auto proc = std::make_unique<TestableProcessor>();
    REQUIRE(proc->initialize(&host) == Steinberg::kResultOk);

    proc->notify(makeRouteUpdateMsg(/*slot=*/4, /*source=*/9, /*dest=*/14,
                                    /*amount=*/0.5));

    // Echoed state (the message the controller receives)
    auto* syncMsg = proc->getVoiceRouteSyncMsg();
    REQUIRE(syncMsg != nullptr);
    const void* data = nullptr;
    Steinberg::uint32 size = 0;
    REQUIRE(syncMsg->getAttributes()->getBinary("routeData", data, size)
            == Steinberg::kResultOk);
    REQUIRE(size == 14u * Krate::Plugins::kMaxVoiceRoutes);
    const auto* echoed = static_cast<const uint8_t*>(data) + 4 * 14;
    CHECK(echoed[0] <= 7);  // source: VoiceModSource::NumSources - 1
    CHECK(echoed[1] <= 7);  // destination: kNumVoiceDestinations - 1
    CHECK(echoed[13] == 1); // active

    // Saved state carries the same clamped values
    const auto routes = extractRoutes(captureState(proc.get()));
    REQUIRE(routes.size() == Krate::Plugins::kMaxVoiceRoutes);
    CHECK(routes[4].source <= 7);
    CHECK(routes[4].destination <= 7);

    proc->terminate();
}

// =============================================================================
// R-007: a voice route is never applied torn
// =============================================================================
// notify() and setState() (message thread) write the routes while process()
// (audio thread) reads them. Field-by-field atomics let the reader see a new
// destination with the old amount; the reader must only ever see whole routes.

namespace {

bool sameRoute(const Krate::Plugins::VoiceModRoute& a, const Krate::Plugins::VoiceModRoute& b) {
    return a.source == b.source && a.destination == b.destination && a.amount == b.amount
        && a.curve == b.curve && a.smoothMs == b.smoothMs && a.scale == b.scale
        && a.bypass == b.bypass && a.active == b.active;
}

}  // namespace

TEST_CASE("Voice route table is never read torn", "[vst][modmatrix][threads]") {
    // Two routes that differ in every field
    const Krate::Plugins::VoiceModRoute routeA{.source = 1, .destination = 2, .amount = 0.5f,
                                               .curve = 1, .smoothMs = 10.0f, .scale = 1,
                                               .bypass = 0, .active = 1};
    const Krate::Plugins::VoiceModRoute routeB{.source = 5, .destination = 6, .amount = -0.9f,
                                               .curve = 3, .smoothMs = 50.0f, .scale = 4,
                                               .bypass = 1, .active = 0};

    Ruinae::VoiceRouteTable table;
    table.store(0, routeA);

    constexpr int kWrites = 200000;
    std::atomic<bool> start{false};
    std::atomic<bool> writerDone{false};

    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (int i = 0; i < kWrites; ++i) {
            table.store(0, (i & 1) != 0 ? routeA : routeB);
            std::this_thread::yield();
        }
        writerDone.store(true, std::memory_order_release);
    });

    // The audio thread's reader: a failed read keeps the previous snapshot
    Ruinae::VoiceRouteTable::Routes snapshot{};
    snapshot[0] = routeA;
    long long reads = 0;
    long long fresh = 0;
    long long torn = 0;
    bool sawA = false;
    bool sawB = false;
    start.store(true, std::memory_order_release);
    while (!writerDone.load(std::memory_order_acquire)) {
        if (table.tryLoadAll(snapshot)) ++fresh;
        ++reads;
        const auto& r = snapshot[0];
        if (sameRoute(r, routeA)) sawA = true;
        else if (sameRoute(r, routeB)) sawB = true;
        else ++torn;
    }
    writer.join();

    INFO("reads " << reads << ", fresh " << fresh << ", torn " << torn);
    CHECK(torn == 0);
    // Non-vacuity: the reader ran alongside the writer, got through, and saw both routes
    CHECK(fresh > 0);
    CHECK(sawA);
    CHECK(sawB);
}

// =============================================================================
// Issue 1 & 3: Pre-allocated message verification
// =============================================================================
// NOTE: allocateMessage() requires a host connection and returns nullptr in the
// test environment. We verify the message IDs round-trip correctly using
// HostMessage (the same class the SDK allocates in real hosts), confirming our
// initialize() code would set the correct IDs. The null guards in process() and
// sendVoiceModRouteState() are verified implicitly — the tests above exercise
// the full process() and notify() paths without crashing despite null messages.

TEST_CASE("Voice route sync message ID round-trips correctly",
          "[voice-route][rt-safety]") {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID("VoiceModRouteState");
    REQUIRE(std::strcmp(msg->getMessageID(), "VoiceModRouteState") == 0);

    // Verify binary attribute can hold route data (224 bytes)
    static constexpr size_t kRouteDataBytes = 14 * Krate::Plugins::kMaxVoiceRoutes;
    uint8_t buffer[kRouteDataBytes]{};
    auto* attrs = msg->getAttributes();
    REQUIRE(attrs != nullptr);
    REQUIRE(attrs->setBinary("routeData", buffer, kRouteDataBytes) == Steinberg::kResultOk);
    REQUIRE(attrs->setInt("routeCount", 0) == Steinberg::kResultOk);
}

TEST_CASE("Process succeeds without pre-allocated messages (null-safe)",
          "[voice-route][rt-safety]") {
    // In test environment, allocateMessage() returns nullptr.
    // Verify that process() and sendVoiceModRouteState() handle this gracefully.
    auto proc = makeTestableProcessor();

    // Verify the message is null (test environment has no host)
    CHECK(proc->getVoiceRouteSyncMsg() == nullptr);

    // Process should still work without crashing
    Steinberg::Vst::ProcessData data{};
    data.numSamples = 0;
    data.numInputs = 0;
    data.numOutputs = 0;
    REQUIRE(proc->process(data) == Steinberg::kResultTrue);

    proc->terminate();
}

// =============================================================================
// Route restore must not depend on the audio thread
// =============================================================================
// Route restore used to be deferred to applyPresetSnapshot(), which runs on the
// audio thread out of the RTTransfer drain, and the controller was then notified
// from process() via a needVoiceRouteSync_ flag. That send called
// IAttributeList::setBinary, which allocates on every call in the SDK's reference
// host -- a heap allocation on the audio thread once per preset load. Pre-warming
// the attribute in initialize() only created the map node; it did not make the
// call allocation-free.
//
// Voice routes are per-field atomics, so they are safe to store from the message
// thread, where the rest of the preset's parameters already land. Asserting that
// the routes are readable straight after setState() -- with no process() call in
// between -- pins the restore to the message thread and keeps the audio path out
// of it.

TEST_CASE("Preset routes are restored by setState without running the audio thread",
          "[voice-route][rt-safety]") {
    auto source = makeTestableProcessor();
    source->notify(makeRouteUpdateMsg(/*slot=*/2, /*source=*/3, /*dest=*/4,
                                      /*amount=*/0.75, /*curve=*/1,
                                      /*smoothMs=*/12.0, /*scale=*/2,
                                      /*bypass=*/0, /*active=*/1));
    const auto preset = captureState(source.get());

    auto target = makeTestableProcessor();
    loadState(target.get(), preset);

    // Deliberately no process() call here: the drain never runs.
    const auto routes = extractRoutes(captureState(target.get()));
    REQUIRE(routes.size() > 2);

    CHECK(routes[2].active == 1);
    CHECK(routes[2].source == 3);
    CHECK(routes[2].destination == 4);
    CHECK(routes[2].amount == Catch::Approx(0.75f));
}

// =============================================================================
// R-005: processor -> controller display data travels as values
// =============================================================================
// The processor used to send the controller the raw addresses of its member
// atomics (trance-gate step, transport flag, envelope outputs and stages, voice
// active flag, morph X/Y) in three messages. The controller's 30 fps timer read
// them, so a host that frees the processor before the editor closes, or runs the
// two components in separate address spaces, made the timer read freed memory.

namespace {

/// Records every message the processor sends and every pointer attribute in it.
class PointerRecordingPeer final : public Steinberg::Vst::IConnectionPoint {
public:
    std::vector<std::string> messageIds;
    int pointerAttributes = 0;

    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint*) override {
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint*) override {
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override {
        if (message == nullptr) return Steinberg::kInvalidArgument;
        messageIds.emplace_back(message->getMessageID());
        if (auto* attrs = message->getAttributes()) {
            for (const char* key : {"stepPtr", "playingPtr", "ampOutputPtr", "ampStagePtr",
                                    "filterOutputPtr", "filterStagePtr", "modOutputPtr",
                                    "modStagePtr", "voiceActivePtr", "morphXPtr", "morphYPtr"}) {
                Steinberg::int64 value = 0;
                if (attrs->getInt(key, value) == Steinberg::kResultOk) ++pointerAttributes;
            }
        }
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    [[nodiscard]] bool received(const char* id) const {
        return std::find(messageIds.begin(), messageIds.end(), id) != messageIds.end();
    }
};

/// Output parameter capture: the last value the processor wrote per ID.
class LastValueOutputChanges final : public Steinberg::Vst::IParameterChanges {
public:
    class Queue final : public Steinberg::Vst::IParamValueQueue {
    public:
        explicit Queue(Steinberg::Vst::ParamID id) : id_(id) {}
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
            return Steinberg::kNoInterface;
        }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id_; }
        Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }
        Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32, Steinberg::int32& offset,
                                               Steinberg::Vst::ParamValue& value) override {
            offset = 0;
            value = value_;
            return Steinberg::kResultTrue;
        }
        Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32, Steinberg::Vst::ParamValue value,
                                               Steinberg::int32& index) override {
            value_ = value;
            index = 0;
            return Steinberg::kResultTrue;
        }
        [[nodiscard]] double value() const { return value_; }

    private:
        Steinberg::Vst::ParamID id_;
        double value_ = 0.0;
    };

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(queues_.size());
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size())) return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id,
                                                                  Steinberg::int32& index) override {
        for (size_t i = 0; i < queues_.size(); ++i) {
            if (queues_[i].getParameterId() == id) {
                index = static_cast<Steinberg::int32>(i);
                return &queues_[i];
            }
        }
        index = static_cast<Steinberg::int32>(queues_.size());
        return &queues_.emplace_back(id);
    }

    /// The last value written for `id`, or -1 if the processor never wrote it.
    [[nodiscard]] double last(Steinberg::Vst::ParamID id) {
        for (auto& q : queues_)
            if (q.getParameterId() == id) return q.value();
        return -1.0;
    }

private:
    std::vector<Queue> queues_;
};

/// One rendered block with a note held and the transport playing.
void renderBlocks(Ruinae::Processor& proc, int blocks, bool noteOn,
                  Steinberg::Vst::IParameterChanges* out = nullptr) {
    constexpr Steinberg::int32 kBlock = 512;
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    float* channels[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = channels;

    Steinberg::Vst::ProcessContext context{};
    context.state = Steinberg::Vst::ProcessContext::kPlaying | Steinberg::Vst::ProcessContext::kTempoValid;
    context.tempo = 120.0;
    context.sampleRate = 44100.0;

    Krate::Test::EventList events;
    Krate::Test::ParameterChanges noParams;
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = kBlock;
    data.numOutputs = 1;
    data.outputs = &bus;
    data.inputEvents = &events;
    data.inputParameterChanges = &noParams;
    data.processContext = &context;
    data.outputParameterChanges = out;

    for (int b = 0; b < blocks; ++b) {
        events.clear();
        if (noteOn && b == 0) events.addNoteOn(60, 0.8f);
        proc.process(data);
    }
}

}  // namespace

TEST_CASE("No message carries a raw pointer", "[vst][lifecycle]") {
    Steinberg::Vst::HostApplication host;  // allocateMessage() works, as in a host
    Ruinae::Processor proc;
    PointerRecordingPeer peer;
    REQUIRE(proc.initialize(&host) == Steinberg::kResultOk);
    REQUIRE(proc.connect(&peer) == Steinberg::kResultTrue);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    proc.setupProcessing(setup);

    // Activate, play, deactivate and reactivate: every point a handshake was sent from.
    proc.setActive(true);
    const size_t beforeProcess = peer.messageIds.size();
    renderBlocks(proc, 20, /*noteOn=*/true);
    // Audio-thread hygiene: process() makes no synchronous sendMessage() call.
    CHECK(peer.messageIds.size() == beforeProcess);
    proc.setActive(false);
    proc.setActive(true);
    renderBlocks(proc, 4, /*noteOn=*/false);

    // A state load sends the voice-route sync: proof that the peer receives what
    // the processor sends, so the checks below are not vacuous.
    loadState(&proc, captureState(&proc));
    REQUIRE(peer.received("VoiceModRouteState"));

    std::string ids;
    for (const auto& id : peer.messageIds) ids += id + " ";
    INFO("messages received: " << ids);
    CHECK_FALSE(peer.received("TranceGatePlayback"));
    CHECK_FALSE(peer.received("EnvelopeDisplayState"));
    CHECK_FALSE(peer.received("MorphPadModulation"));
    CHECK(peer.pointerAttributes == 0);

    proc.setActive(false);
    proc.disconnect(&peer);
    proc.terminate();
}

TEST_CASE("Display data reaches the controller as output parameters", "[vst][lifecycle]") {
    Ruinae::Processor proc;
    REQUIRE(proc.initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    proc.setupProcessing(setup);
    proc.setActive(true);

    // Note held, transport playing: ~46 ms in, the amp envelope is past its onset.
    LastValueOutputChanges out;
    renderBlocks(proc, 4, /*noteOn=*/true, &out);

    INFO("voice active " << out.last(Ruinae::kEnvDisplayVoiceActiveId)
         << ", amp out " << out.last(Ruinae::kAmpEnvDisplayOutputId)
         << ", amp stage " << out.last(Ruinae::kAmpEnvDisplayStageId)
         << ", transport " << out.last(Ruinae::kTransportPlayingId)
         << ", gate step " << out.last(Ruinae::kTranceGatePlaybackStepId)
         << ", morph " << out.last(Ruinae::kMorphPadModXId) << "/" << out.last(Ruinae::kMorphPadModYId));
    CHECK(out.last(Ruinae::kEnvDisplayVoiceActiveId) == 1.0);
    CHECK(out.last(Ruinae::kAmpEnvDisplayOutputId) > 0.0);
    CHECK(out.last(Ruinae::kAmpEnvDisplayOutputId) <= 1.0);
    for (const auto stageId : {Ruinae::kAmpEnvDisplayStageId, Ruinae::kFilterEnvDisplayStageId,
                               Ruinae::kModEnvDisplayStageId}) {
        const double stage = out.last(stageId) * Ruinae::kEnvDisplayStageScale;
        CHECK(stage >= 1.0);  // Attack..Release while the voice sounds
        CHECK(stage <= 4.0);
    }
    CHECK(out.last(Ruinae::kFilterEnvDisplayOutputId) >= 0.0);
    CHECK(out.last(Ruinae::kModEnvDisplayOutputId) >= 0.0);
    CHECK(out.last(Ruinae::kTransportPlayingId) == 1.0);
    CHECK(out.last(Ruinae::kTranceGatePlaybackStepId) >= 0.0);
    CHECK(out.last(Ruinae::kTranceGatePlaybackStepId) <= 1.0);
    // Default mixer position 0.5 and tilt 0 dB, no modulation
    CHECK(out.last(Ruinae::kMorphPadModXId) == Catch::Approx(0.5).margin(1e-6));
    CHECK(out.last(Ruinae::kMorphPadModYId) == Catch::Approx(0.5).margin(1e-6));

    proc.setActive(false);
    proc.terminate();
}
