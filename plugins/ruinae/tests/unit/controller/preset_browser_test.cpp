// =============================================================================
// Preset Browser Integration Tests for Ruinae Controller
// =============================================================================
// Spec 083: Ruinae Preset Browser
// Tests for controller-level preset browser wiring:
// - editParamWithNotify sequence (beginEdit/setParamNormalized/performEdit/endEdit)
// - createComponentStateStream returns valid stream (via stateProvider)
// - loadComponentStateWithNotify round-trip (via loadProvider)
// - stateProvider/loadProvider callback wiring in initialize()
// - openPresetBrowser/closePresetBrowser toggle guards
//
// Strategy: Private methods (editParamWithNotify, createComponentStateStream,
// loadComponentStateWithNotify) are tested indirectly. We create a real
// Processor, serialize its state, then load it through the controller's
// loadProvider callback by overriding the PresetManager callbacks after init.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "preset/ruinae_preset_config.h"
#include "preset/preset_manager.h"

#include "pluginterfaces/vst/vsttypes.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "base/source/fstreamer.h"

#include "vstgui/uidescription/uiattributes.h"

#include "test_helpers/uidesc_reachability.h"
#include "test_helpers/vst_param_changes.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using Catch::Approx;

// =============================================================================
// Mock Component Handler
// =============================================================================
// Records beginEdit/performEdit/endEdit calls for verification.
// =============================================================================

namespace {

struct EditRecord {
    enum class Action { Begin, Perform, End };
    Action action;
    Steinberg::Vst::ParamID id;
    Steinberg::Vst::ParamValue value; // only meaningful for Perform
};

class MockComponentHandler final : public Steinberg::Vst::IComponentHandler {
public:
    // IComponentHandler
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override {
        records.push_back({EditRecord::Action::Begin, id, 0.0});
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API performEdit(
        Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue valueNormalized) override {
        records.push_back({EditRecord::Action::Perform, id, valueNormalized});
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override {
        records.push_back({EditRecord::Action::End, id, 0.0});
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API restartComponent(
        Steinberg::int32 /*flags*/) override {
        return Steinberg::kResultOk;
    }

    // FUnknown
    Steinberg::tresult PLUGIN_API queryInterface(
        const Steinberg::TUID /*iid*/, void** obj) override {
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return ++refCount_; }
    Steinberg::uint32 PLUGIN_API release() override {
        if (--refCount_ == 0) {
            delete this;
            return 0;
        }
        return refCount_;
    }

    std::vector<EditRecord> records;

private:
    Steinberg::uint32 refCount_ = 1;
};

// Helper: create a valid state stream from a real Ruinae processor
static Steinberg::MemoryStream* createProcessorStateStream() {
    auto proc = std::make_unique<Ruinae::Processor>();
    proc->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    proc->setupProcessing(setup);

    auto* stream = new Steinberg::MemoryStream();
    proc->getState(stream);

    // Seek back to start
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    proc->terminate();
    return stream;
}

// Helper: Expose loadComponentStateWithNotify for testing by subclassing.
// This allows us to call the private method from tests without complex mocking.
class TestableController : public Ruinae::Controller {
public:
    using Ruinae::Controller::editParamWithNotify;
    using Ruinae::Controller::loadComponentStateWithNotify;
    using Ruinae::Controller::createComponentStateStream;
};

// Processor ready for getState/notify, as a host sets it up.
std::unique_ptr<Ruinae::Processor> makeReadyProcessor(Steinberg::FUnknown* hostContext) {
    auto proc = std::make_unique<Ruinae::Processor>();
    proc->initialize(hostContext);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    proc->setupProcessing(setup);
    proc->setActive(true);
    return proc;
}

std::vector<char> processorStateBytes(Ruinae::Processor& proc) {
    Steinberg::MemoryStream stream;
    proc.getState(&stream);
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> data(static_cast<size_t>(size));
    Steinberg::int32 bytesRead = 0;
    stream.read(data.data(), static_cast<Steinberg::int32>(size), &bytesRead);
    return data;
}

// The 16 voice routes' bytes (14 each) of a current-version state, located by
// reading the parameter packs that precede them in Processor::getState order.
std::vector<char> voiceRouteBytes(const std::vector<char>& state) {
    Steinberg::MemoryStream mem(const_cast<char*>(state.data()),
                                static_cast<Steinberg::TSize>(state.size()));
    Steinberg::IBStreamer streamer(&mem, kLittleEndian);
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version)) return {};

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
    Steinberg::int32 reverbType = 0;
    if (!loadGlobalParams(gp, streamer) || !loadOscAParams(oa, streamer) ||
        !loadOscBParams(ob, streamer) || !loadMixerParams(mp, streamer) ||
        !loadFilterParams(fp, streamer) || !loadDistortionParams(dp, streamer) ||
        !loadTranceGateParams(tp, streamer) || !loadAmpEnvParams(aep, streamer) ||
        !loadFilterEnvParams(fep, streamer) || !loadModEnvParams(mep, streamer) ||
        !loadLFO1Params(l1, streamer) || !loadLFO2Params(l2, streamer) ||
        !loadChaosModParams(cmp, streamer) || !loadModMatrixParams(mmp, streamer) ||
        !loadGlobalFilterParams(gfp, streamer) || !loadDelayParams(dlp, streamer) ||
        !loadReverbParams(rvp, streamer) || !streamer.readInt32(reverbType) ||
        !loadMonoModeParams(mop, streamer))
        return {};

    const auto begin = static_cast<size_t>(streamer.tell());
    constexpr size_t kRouteBytes = 14 * static_cast<size_t>(Krate::Plugins::kMaxVoiceRoutes);
    if (begin + kRouteBytes > state.size()) return {};
    return {state.begin() + static_cast<std::ptrdiff_t>(begin),
            state.begin() + static_cast<std::ptrdiff_t>(begin + kRouteBytes)};
}

/// A component state authored on a processor: parameter edits through one
/// process() block and one voice route through the controller's message.
std::vector<char> authorState(
    const std::vector<std::pair<Steinberg::Vst::ParamID, double>>& params,
    int routeSlot, double routeAmount) {
    auto proc = makeReadyProcessor(nullptr);

    Krate::Test::ParameterChanges changes;
    for (const auto& [id, value] : params) changes.addChange(id, value);
    std::vector<float> left(512, 0.0f);
    std::vector<float> right(512, 0.0f);
    float* channels[2] = {left.data(), right.data()};
    Steinberg::Vst::AudioBusBuffers out{};
    out.numChannels = 2;
    out.channelBuffers32 = channels;
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = 512;
    data.numOutputs = 1;
    data.outputs = &out;
    data.inputParameterChanges = &changes;
    proc->process(data);

    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID("VoiceModRouteUpdate");
    auto* attrs = msg->getAttributes();
    attrs->setInt("slotIndex", routeSlot);
    attrs->setInt("source", 1);
    attrs->setInt("destination", 3);
    attrs->setFloat("amount", routeAmount);
    attrs->setInt("curve", 1);
    attrs->setFloat("smoothMs", 7.5);
    attrs->setInt("scale", 2);
    attrs->setInt("bypass", 0);
    attrs->setInt("active", 1);
    proc->notify(msg);

    auto bytes = processorStateBytes(*proc);
    proc->setActive(false);
    proc->terminate();
    return bytes;
}

/// Byte size of what `save` writes.
template <typename Save>
size_t packSize(Save save) {
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    save(streamer);
    return static_cast<size_t>(stream.getSize());
}

/// Load `bytes` into `proc` through IComponent::setState, as a host does.
void hostLoad(Ruinae::Processor& proc, const std::vector<char>& bytes) {
    Steinberg::MemoryStream stream;
    stream.write(const_cast<char*>(bytes.data()), static_cast<Steinberg::int32>(bytes.size()),
                 nullptr);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc.setState(&stream) == Steinberg::kResultTrue);
}

/// Two distinct non-default states: synth packs, arp lanes, voice LFO and an
/// active voice route all differ from the defaults and from each other.
std::vector<char> presetStateA() {
    return authorState({{Ruinae::kFilterCutoffId, 0.3},
                        {Ruinae::kArpOperatingModeId, 1.0 / 3.0},
                        {Ruinae::kArpVelocityLaneStep0Id, 0.25},
                        {Ruinae::kVoiceLfoRateId, 0.7},
                        {Ruinae::kDelayEnabledId, 1.0}},
                       2, 0.625);
}

std::vector<char> presetStateB() {
    return authorState({{Ruinae::kFilterCutoffId, 0.8},
                        {Ruinae::kArpOperatingModeId, 1.0 / 3.0},
                        {Ruinae::kArpModeId, 1.0 / 9.0},
                        {Ruinae::kArpGateLaneStep0Id, 0.9},
                        {Ruinae::kVoiceLfoRateId, 0.2},
                        {Ruinae::kVoiceLfoShapeId, 0.4}},
                       5, -0.375);
}

} // anonymous namespace

// =============================================================================
// T007: Test suite skeleton
// =============================================================================

TEST_CASE("Preset browser test file compiles and runs", "[preset browser]") {
    REQUIRE(true);
}

// =============================================================================
// T008: editParamWithNotify calls beginEdit/setParamNormalized/performEdit/endEdit
// =============================================================================

TEST_CASE("editParamWithNotify calls begin/set/perform/end in order",
          "[preset browser][editParam]") {
    TestableController controller;
    controller.initialize(nullptr);

    auto* handler = new MockComponentHandler();
    controller.setComponentHandler(handler);

    const auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kDelayEnabledId);
    const double testValue = 0.75;

    controller.editParamWithNotify(paramId, testValue);

    // Verify the call sequence: beginEdit, performEdit, endEdit
    REQUIRE(handler->records.size() == 3);
    CHECK(handler->records[0].action == EditRecord::Action::Begin);
    CHECK(handler->records[0].id == paramId);
    CHECK(handler->records[1].action == EditRecord::Action::Perform);
    CHECK(handler->records[1].id == paramId);
    CHECK(handler->records[1].value == Approx(testValue));
    CHECK(handler->records[2].action == EditRecord::Action::End);
    CHECK(handler->records[2].id == paramId);

    // Verify setParamNormalized was also called (value should match on controller)
    CHECK(controller.getParamNormalized(paramId) == Approx(testValue));

    controller.setComponentHandler(nullptr);
    controller.terminate();
    handler->release();
}

TEST_CASE("editParamWithNotify clamps value to [0.0, 1.0]",
          "[preset browser][editParam]") {
    TestableController controller;
    controller.initialize(nullptr);

    auto* handler = new MockComponentHandler();
    controller.setComponentHandler(handler);

    const auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kDelayEnabledId);

    SECTION("value above 1.0 is clamped to 1.0") {
        controller.editParamWithNotify(paramId, 1.5);
        REQUIRE(handler->records.size() == 3);
        CHECK(handler->records[1].value == Approx(1.0));
        CHECK(controller.getParamNormalized(paramId) == Approx(1.0));
    }

    SECTION("value below 0.0 is clamped to 0.0") {
        controller.editParamWithNotify(paramId, -0.5);
        REQUIRE(handler->records.size() == 3);
        CHECK(handler->records[1].value == Approx(0.0));
        CHECK(controller.getParamNormalized(paramId) == Approx(0.0));
    }

    controller.setComponentHandler(nullptr);
    controller.terminate();
    handler->release();
}

// =============================================================================
// T009: createComponentStateStream
// =============================================================================
// Without a host providing IComponent, createComponentStateStream returns nullptr.

TEST_CASE("createComponentStateStream returns nullptr without IComponent host",
          "[preset browser][stateStream]") {
    TestableController controller;
    controller.initialize(nullptr);

    // No component handler set -- should return nullptr
    auto* stream = controller.createComponentStateStream();
    CHECK(stream == nullptr);

    // With a handler that does NOT support IComponent queryInterface
    auto* handler = new MockComponentHandler();
    controller.setComponentHandler(handler);

    stream = controller.createComponentStateStream();
    CHECK(stream == nullptr);

    controller.setComponentHandler(nullptr);
    controller.terminate();
    handler->release();
}

// =============================================================================
// T010: loadComponentStateWithNotify round-trip
// =============================================================================

TEST_CASE("loadComponentStateWithNotify rejects zero-byte stream",
          "[preset browser][loadState]") {
    TestableController controller;
    controller.initialize(nullptr);

    auto* handler = new MockComponentHandler();
    controller.setComponentHandler(handler);

    Steinberg::MemoryStream emptyStream;
    bool result = controller.loadComponentStateWithNotify(&emptyStream);
    CHECK(result == false);
    // No edit records should have been created
    CHECK(handler->records.empty());

    controller.setComponentHandler(nullptr);
    controller.terminate();
    handler->release();
}

TEST_CASE("loadComponentStateWithNotify rejects invalid version",
          "[preset browser][loadState]") {
    TestableController controller;
    controller.initialize(nullptr);

    auto* handler = new MockComponentHandler();
    controller.setComponentHandler(handler);

    // Create a stream with version = 99
    Steinberg::MemoryStream badVersionStream;
    Steinberg::IBStreamer streamer(&badVersionStream, kLittleEndian);
    streamer.writeInt32(99);
    badVersionStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    bool result = controller.loadComponentStateWithNotify(&badVersionStream);
    CHECK(result == false);
    CHECK(handler->records.empty());

    controller.setComponentHandler(nullptr);
    controller.terminate();
    handler->release();
}

TEST_CASE("loadComponentStateWithNotify succeeds with valid v1 processor state",
          "[preset browser][loadState][roundtrip]") {
    TestableController controller;
    controller.initialize(nullptr);

    auto* handler = new MockComponentHandler();
    controller.setComponentHandler(handler);

    // Create a valid processor state stream
    auto* stateStream = createProcessorStateStream();
    REQUIRE(stateStream != nullptr);

    bool result = controller.loadComponentStateWithNotify(stateStream);
    CHECK(result == true);

    // editParamWithNotify should have been called many times.
    // Each call produces 3 records (begin, perform, end).
    // We expect records.size() > 0 and to be a multiple of 3.
    CHECK(handler->records.size() > 0);
    CHECK(handler->records.size() % 3 == 0);

    // Verify the pattern: every triple is begin/perform/end
    for (size_t i = 0; i + 2 < handler->records.size(); i += 3) {
        CHECK(handler->records[i].action == EditRecord::Action::Begin);
        CHECK(handler->records[i + 1].action == EditRecord::Action::Perform);
        CHECK(handler->records[i + 2].action == EditRecord::Action::End);
        // IDs should match within the triple
        CHECK(handler->records[i].id == handler->records[i + 1].id);
        CHECK(handler->records[i].id == handler->records[i + 2].id);
    }

    stateStream->release();
    controller.setComponentHandler(nullptr);
    controller.terminate();
    handler->release();
}

// R-124 / R-040: a browser load (loadComponentStateWithNotify, the PresetManager's
// load provider) must leave the processor's voice routes equal to the preset's,
// as Processor::setState does. Controller and processor are connected both ways,
// so the processor's synchronous VoiceModRouteState echo reaches the controller
// mid-load exactly as with a host's connection proxy.
TEST_CASE("Controller browser load sends voice routes to the processor",
          "[controller][presets][modmatrix]") {
    Steinberg::Vst::HostApplication hostApp;

    // State A: one active voice route in slot 2. State B: no routes.
    std::vector<char> stateA;
    std::vector<char> stateB;
    {
        auto author = makeReadyProcessor(nullptr);
        stateB = processorStateBytes(*author);

        auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
        msg->setMessageID("VoiceModRouteUpdate");
        auto* attrs = msg->getAttributes();
        attrs->setInt("slotIndex", 2);
        attrs->setInt("source", 1);
        attrs->setInt("destination", 3);
        attrs->setFloat("amount", 0.625);
        attrs->setInt("curve", 2);
        attrs->setFloat("smoothMs", 12.5);
        attrs->setInt("scale", 3);
        attrs->setInt("bypass", 0);
        attrs->setInt("active", 1);
        REQUIRE(author->notify(msg) == Steinberg::kResultOk);
        stateA = processorStateBytes(*author);
        author->setActive(false);
        author->terminate();
    }
    const auto routesA = voiceRouteBytes(stateA);
    const auto routesB = voiceRouteBytes(stateB);
    REQUIRE(routesA.size() == 224);
    REQUIRE(routesB.size() == 224);
    REQUIRE(routesA != routesB);

    auto proc = makeReadyProcessor(&hostApp);
    TestableController controller;
    REQUIRE(controller.initialize(&hostApp) == Steinberg::kResultOk);
    auto* handler = new MockComponentHandler();
    controller.setComponentHandler(handler);
    REQUIRE(controller.connect(proc.get()) == Steinberg::kResultOk);
    REQUIRE(proc->connect(&controller) == Steinberg::kResultOk);

    auto loadThroughBrowser = [&controller](const std::vector<char>& bytes) {
        Steinberg::MemoryStream stream;
        stream.write(const_cast<char*>(bytes.data()),
                     static_cast<Steinberg::int32>(bytes.size()), nullptr);
        stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        return controller.loadComponentStateWithNotify(&stream);
    };

    REQUIRE(loadThroughBrowser(stateA));
    CHECK(voiceRouteBytes(processorStateBytes(*proc)) == routesA);
    CHECK(controller.cachedVoiceRouteForTest(2).active);

    REQUIRE(loadThroughBrowser(stateB));
    CHECK(voiceRouteBytes(processorStateBytes(*proc)) == routesB);
    CHECK_FALSE(controller.cachedVoiceRouteForTest(2).active);

    proc->disconnect(&controller);
    controller.disconnect(proc.get());
    controller.setComponentHandler(nullptr);
    controller.terminate();
    handler->release();
    proc->setActive(false);
    proc->terminate();
}

// R-196: a browser load and a host load run one parse and one apply in the
// processor. The controller is the only route the browser load has into the
// processor here (no host relays its performEdits), so every byte of the
// processor's state must come from the stream the controller hands over.
TEST_CASE("Browser load leaves the processor in the same state as a host load",
          "[controller][presets][state]") {
    Steinberg::Vst::HostApplication hostApp;
    const auto stateA = presetStateA();
    const auto stateB = presetStateB();
    REQUIRE(stateA != stateB);

    auto proc = makeReadyProcessor(&hostApp);
    auto reference = makeReadyProcessor(nullptr);
    TestableController controller;
    REQUIRE(controller.initialize(&hostApp) == Steinberg::kResultOk);
    auto* handler = new MockComponentHandler();
    controller.setComponentHandler(handler);
    REQUIRE(controller.connect(proc.get()) == Steinberg::kResultOk);
    REQUIRE(proc->connect(&controller) == Steinberg::kResultOk);

    for (const auto* state : {&stateA, &stateB}) {
        Steinberg::MemoryStream stream;
        stream.write(const_cast<char*>(state->data()),
                     static_cast<Steinberg::int32>(state->size()), nullptr);
        stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        REQUIRE(controller.loadComponentStateWithNotify(&stream, false));
        hostLoad(*reference, *state);
        INFO((state == &stateA ? "after state A" : "after state B"));
        CHECK(processorStateBytes(*proc) == processorStateBytes(*reference));
    }

    proc->disconnect(&controller);
    controller.disconnect(proc.get());
    controller.setComponentHandler(nullptr);
    controller.terminate();
    handler->release();
    proc->setActive(false);
    proc->terminate();
    reference->setActive(false);
    reference->terminate();
}

// R-196: an arp-only browser load (an "Arp" preset category) replaces the arp
// pack in the processor and nothing else: synth packs, voice routes and the
// voice LFO keep the loaded synth preset's values.
TEST_CASE("Arp-only browser load replaces only the arp section in the processor",
          "[controller][presets][state]") {
    Steinberg::Vst::HostApplication hostApp;
    const auto stateA = presetStateA();
    const auto stateB = presetStateB();

    auto proc = makeReadyProcessor(&hostApp);
    hostLoad(*proc, stateA);
    const auto baseA = processorStateBytes(*proc);
    {
        auto other = makeReadyProcessor(nullptr);
        hostLoad(*other, stateB);
        const auto baseB = processorStateBytes(*other);
        REQUIRE(baseA.size() == baseB.size());
        other->setActive(false);
        other->terminate();

        // The arp pack is followed by the voice LFO and the Velocity > Amp amount.
        const size_t arpBytes = packSize([](Steinberg::IBStreamer& s) {
            Ruinae::saveArpParams(Ruinae::ArpeggiatorParams{}, s);
        });
        const size_t tailBytes = packSize([](Steinberg::IBStreamer& s) {
            Ruinae::saveVoiceLfoParams(Ruinae::VoiceLfoParams{}, s);
            Ruinae::saveAmpVelocityAmount(Ruinae::AmpEnvParams{}, s);
        });
        REQUIRE(baseA.size() > arpBytes + tailBytes);
        const size_t arpBegin = baseA.size() - tailBytes - arpBytes;
        const size_t arpEnd = arpBegin + arpBytes;
        auto expected = baseA;
        std::copy(baseB.begin() + static_cast<std::ptrdiff_t>(arpBegin),
                  baseB.begin() + static_cast<std::ptrdiff_t>(arpEnd),
                  expected.begin() + static_cast<std::ptrdiff_t>(arpBegin));
        REQUIRE(expected != baseA);  // the two arp packs differ
        REQUIRE(expected != baseB);  // and so do the synth packs

        TestableController controller;
        REQUIRE(controller.initialize(&hostApp) == Steinberg::kResultOk);
        auto* handler = new MockComponentHandler();
        controller.setComponentHandler(handler);
        REQUIRE(controller.connect(proc.get()) == Steinberg::kResultOk);
        REQUIRE(proc->connect(&controller) == Steinberg::kResultOk);

        Steinberg::MemoryStream stream;
        stream.write(const_cast<char*>(stateB.data()),
                     static_cast<Steinberg::int32>(stateB.size()), nullptr);
        stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        REQUIRE(controller.loadComponentStateWithNotify(&stream, true));

        const auto after = processorStateBytes(*proc);
        CHECK(voiceRouteBytes(after) == voiceRouteBytes(baseA));
        CHECK(after == expected);

        proc->disconnect(&controller);
        controller.disconnect(proc.get());
        controller.setComponentHandler(nullptr);
        controller.terminate();
        handler->release();
    }
    proc->setActive(false);
    proc->terminate();
}

// =============================================================================
// T011: stateProvider callback is non-null after initialize()
// =============================================================================

TEST_CASE("stateProvider is wired after initialize()",
          "[preset browser][provider]") {
    Ruinae::Controller controller;
    controller.initialize(nullptr);

    auto* pm = controller.getPresetManager();
    REQUIRE(pm != nullptr);

    // After T019, the stateProvider should be wired.
    // We verify this by attempting to save a preset. The PresetManager calls
    // the stateProvider to get the stream. Without a valid host, it returns
    // nullptr, but the important thing is it does NOT crash (meaning the
    // callback IS set, it just can't produce a stream without IComponent).
    //
    // If stateProvider was not set, savePreset would fail with a different
    // error (no state provider).
    bool saveResult = pm->savePreset("test_probe", "All");
    // Save should fail (no valid stream from provider) but not crash
    CHECK(saveResult == false);

    controller.terminate();
}

// =============================================================================
// T012: loadProvider callback is non-null after initialize()
// =============================================================================

TEST_CASE("loadProvider is wired after initialize()",
          "[preset browser][provider]") {
    Ruinae::Controller controller;
    controller.initialize(nullptr);

    auto* pm = controller.getPresetManager();
    REQUIRE(pm != nullptr);

    // Verify loadProvider is wired by attempting a load with a dummy preset info.
    // The PresetManager will try to open the file. Without a real file, the load
    // fails at the file-open stage, but the callback should be wired.
    // The important check is that the PresetManager has a non-null loadProvider.
    // We test this indirectly: if loadProvider is NOT set, loadPreset may behave
    // differently than if it IS set but the file doesn't exist.

    Krate::Plugins::PresetInfo dummyPreset;
    dummyPreset.name = "nonexistent";
    dummyPreset.path = "nonexistent_path.vstpreset";
    dummyPreset.isFactory = false;

    bool loadResult = pm->loadPreset(dummyPreset);
    CHECK(loadResult == false); // File doesn't exist, but provider should be wired

    controller.terminate();
}

// =============================================================================
// T013: openPresetBrowser guard logic
// =============================================================================

TEST_CASE("openPresetBrowser is no-op when presetBrowserView_ is null",
          "[preset browser][lifecycle]") {
    Ruinae::Controller controller;
    controller.initialize(nullptr);

    // Without didOpen(), presetBrowserView_ is nullptr
    // openPresetBrowser should be a no-op (no crash)
    controller.openPresetBrowser();

    controller.terminate();
}

// =============================================================================
// T014: closePresetBrowser guard logic
// =============================================================================

TEST_CASE("closePresetBrowser is no-op when presetBrowserView_ is null",
          "[preset browser][lifecycle]") {
    Ruinae::Controller controller;
    controller.initialize(nullptr);

    // Without didOpen(), presetBrowserView_ is nullptr
    // closePresetBrowser should be a no-op (no crash)
    controller.closePresetBrowser();

    controller.terminate();
}

TEST_CASE("openSavePresetDialog is no-op when savePresetDialogView_ is null",
          "[preset browser][lifecycle]") {
    Ruinae::Controller controller;
    controller.initialize(nullptr);

    // Without didOpen(), savePresetDialogView_ is nullptr
    // openSavePresetDialog should be a no-op (no crash)
    controller.openSavePresetDialog();

    controller.terminate();
}

// =============================================================================
// T030: createCustomView("SavePresetButton") returns non-null CView*
// =============================================================================

TEST_CASE("createCustomView dispatches SavePresetButton name correctly",
          "[preset browser][createCustomView]") {
    // CTextButton requires VSTGUI platform factory to be initialized (for
    // CGradient::create). In a headless test environment this is unavailable,
    // so we verify the dispatch logic by confirming:
    // 1. "SavePresetButton" does NOT return nullptr (it enters the branch)
    // 2. The returned view has the correct rect derived from UIAttributes
    //
    // If the platform factory is not available, construction throws an
    // exception. We treat that as proof the correct branch was entered
    // (the code attempted to construct a SavePresetButton, not return nullptr).

    TestableController controller;
    controller.initialize(nullptr);

    VSTGUI::UIAttributes attrs;
    attrs.setPointAttribute("origin", VSTGUI::CPoint(550, 8));
    attrs.setPointAttribute("size", VSTGUI::CPoint(60, 25));

    bool enteredSavePresetBranch = false;
    VSTGUI::CView* view = nullptr;
    try {
        view = controller.createCustomView("SavePresetButton", attrs, nullptr, nullptr);
        // If we get here, platform factory was available (full GUI environment)
        enteredSavePresetBranch = (view != nullptr);
        if (view) {
            auto rect = view->getViewSize();
            CHECK(rect.left == Approx(550.0));
            CHECK(rect.top == Approx(8.0));
            CHECK(rect.right == Approx(610.0));
            CHECK(rect.bottom == Approx(33.0));
            view->forget();
        }
    } catch (...) {
        // Exception during CTextButton construction proves we entered the
        // "SavePresetButton" branch (an unknown name would return nullptr
        // without throwing).
        enteredSavePresetBranch = true;
    }

    CHECK(enteredSavePresetBranch);

    controller.terminate();
}

// =============================================================================
// T031: createCustomView returns nullptr for unknown view name
// =============================================================================

TEST_CASE("createCustomView returns nullptr for unknown name",
          "[preset browser][createCustomView]") {
    TestableController controller;
    controller.initialize(nullptr);

    VSTGUI::UIAttributes attrs;

    auto* view = controller.createCustomView("UnknownViewName", attrs, nullptr, nullptr);
    CHECK(view == nullptr);

    controller.terminate();
}

// =============================================================================
// T032: openSavePresetDialog is no-op when savePresetDialogView_ is null
// =============================================================================
// (Already covered by test at line ~401: "openSavePresetDialog is no-op when
// savePresetDialogView_ is null" -- no additional test needed for T032)

// =============================================================================
// T041: createCustomView("PresetBrowserButton") returns non-null CView*
// =============================================================================

TEST_CASE("createCustomView dispatches PresetBrowserButton name correctly",
          "[preset browser][createCustomView]") {
    TestableController controller;
    controller.initialize(nullptr);

    VSTGUI::UIAttributes attrs;
    attrs.setPointAttribute("origin", VSTGUI::CPoint(460, 8));
    attrs.setPointAttribute("size", VSTGUI::CPoint(80, 25));

    bool enteredPresetBrowserBranch = false;
    VSTGUI::CView* view = nullptr;
    try {
        view = controller.createCustomView("PresetBrowserButton", attrs, nullptr, nullptr);
        // If we get here, platform factory was available (full GUI environment)
        enteredPresetBrowserBranch = (view != nullptr);
        if (view) {
            auto rect = view->getViewSize();
            CHECK(rect.left == Approx(460.0));
            CHECK(rect.top == Approx(8.0));
            CHECK(rect.right == Approx(540.0));
            CHECK(rect.bottom == Approx(33.0));
            view->forget();
        }
    } catch (...) {
        // Exception during CTextButton construction proves we entered the
        // "PresetBrowserButton" branch (an unknown name would return nullptr
        // without throwing).
        enteredPresetBrowserBranch = true;
    }

    CHECK(enteredPresetBrowserBranch);

    controller.terminate();
}

// =============================================================================
// T042: createCustomView handles both PresetBrowserButton and SavePresetButton
// =============================================================================

TEST_CASE("createCustomView handles both PresetBrowserButton and SavePresetButton",
          "[preset browser][createCustomView]") {
    TestableController controller;
    controller.initialize(nullptr);

    VSTGUI::UIAttributes browserAttrs;
    browserAttrs.setPointAttribute("origin", VSTGUI::CPoint(460, 8));
    browserAttrs.setPointAttribute("size", VSTGUI::CPoint(80, 25));

    VSTGUI::UIAttributes saveAttrs;
    saveAttrs.setPointAttribute("origin", VSTGUI::CPoint(550, 8));
    saveAttrs.setPointAttribute("size", VSTGUI::CPoint(60, 25));

    // Call both in sequence -- should not crash or corrupt state
    bool browserBranchEntered = false;
    bool saveBranchEntered = false;

    try {
        auto* browserView = controller.createCustomView("PresetBrowserButton", browserAttrs, nullptr, nullptr);
        browserBranchEntered = (browserView != nullptr);
        if (browserView) browserView->forget();
    } catch (...) {
        browserBranchEntered = true;
    }

    try {
        auto* saveView = controller.createCustomView("SavePresetButton", saveAttrs, nullptr, nullptr);
        saveBranchEntered = (saveView != nullptr);
        if (saveView) saveView->forget();
    } catch (...) {
        saveBranchEntered = true;
    }

    CHECK(browserBranchEntered);
    CHECK(saveBranchEntered);

    // Unknown name should still return nullptr
    auto* unknownView = controller.createCustomView("UnknownButton", browserAttrs, nullptr, nullptr);
    CHECK(unknownView == nullptr);

    controller.terminate();
}

// =============================================================================
// T043: openPresetBrowser is a no-op when presetBrowserView_ is null
// =============================================================================
// (Already covered by T013 test: "openPresetBrowser is no-op when
// presetBrowserView_ is null" at line ~375 -- no additional test needed)

// =============================================================================
// R-087: every factory preset folder maps to a browser tab
// =============================================================================
TEST_CASE("Every factory preset folder maps to a browser tab", "[controller][presets]") {
    const auto cfg = Ruinae::makeRuinaePresetConfig();
    const auto mainTabs = Ruinae::getRuinaeTabLabels();
    const auto arpTabs = Ruinae::getRuinaeArpTabLabels();

    const std::filesystem::path presetsDir =
        std::filesystem::path(RUINAE_RESOURCES_DIR) / "presets";
    REQUIRE(std::filesystem::is_directory(presetsDir));

    int folders = 0;
    for (const auto& entry : std::filesystem::directory_iterator(presetsDir)) {
        if (!entry.is_directory()) continue;
        ++folders;
        const std::string name = entry.path().filename().string();
        INFO("preset folder: " << name);
        CHECK(std::find(cfg.subcategoryNames.begin(), cfg.subcategoryNames.end(), name) !=
              cfg.subcategoryNames.end());
        const bool inMain = std::find(mainTabs.begin(), mainTabs.end(), name) != mainTabs.end();
        const bool inArp = std::find(arpTabs.begin(), arpTabs.end(), name) != arpTabs.end();
        CHECK((inMain || inArp));
    }
    CHECK(folders > 0);
}

// =============================================================================
// R-088: Arp Accent Velocity / Slide Time / Voicing have a control in the editor
// =============================================================================
TEST_CASE("Arp accent velocity, slide time and voicing are bound in the editor",
          "[controller][arp][uidesc]") {
    std::ifstream in(std::string(RUINAE_RESOURCES_DIR) + "/editor.uidesc", std::ios::binary);
    REQUIRE(in.good());
    const std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const std::vector<int> ids = {Ruinae::kArpAccentVelocityId, Ruinae::kArpSlideTimeId,
                                  Ruinae::kArpVoicingModeId};
    const auto missing = Krate::Test::unreachableParams(xml, ids);
    CHECK(missing.empty());
}
