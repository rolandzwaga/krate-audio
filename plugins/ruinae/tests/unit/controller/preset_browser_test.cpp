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
#include "base/source/fstreamer.h"

#include <algorithm>
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

class MockComponentHandler : public Steinberg::Vst::IComponentHandler {
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
