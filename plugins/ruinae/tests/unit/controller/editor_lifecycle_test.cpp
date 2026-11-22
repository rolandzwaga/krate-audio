// ==============================================================================
// Ruinae Editor Lifecycle Test
// ==============================================================================
// Exercises the full editor open -> close lifecycle headlessly via the shared
// harness, guarding against use-after-free / dangling-pointer crashes in the
// VST3EditorDelegate teardown path (verifyView/didOpen/willClose). Run under
// AddressSanitizer or valgrind to catch UAF regressions as hard failures.

#include <editor_lifecycle_harness.h>
#include <shared_editor_views_probe.h>
#include <registered_defaults_probe.h>
#include <terminate_with_editor_open_probe.h>

#include "view_lifecycle_rig.h"

#include "controller/controller.h"
#include "plugin_ids.h"
#include "ui/adsr_display.h"
#include "ui/step_pattern_editor.h"
#include "ui/xy_morph_pad.h"

#include "vst_event_list.h"
#include "vst_param_changes.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid: the temp preset directory names
#else
#include <unistd.h>  // getpid
#endif

TEST_CASE("Ruinae editor open/close cycle is UAF- and leak-free",
          "[ruinae][controller][ui][lifecycle]")
{
    Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    Krate::TestSupport::exerciseEditorLifecycle(
        controller, "editor",
        std::string(RUINAE_RESOURCES_DIR) + "/editor.uidesc",
        /*cycles=*/3);

    controller.terminate();
}

// Every Krate editor carries the shared views createSharedEditorView builds: one
// update banner, one Check for Updates button and one VersionLabel (VERSION_STR),
// and no hand-filled tag-9999 version label. tools/lint-plugin-editor-baseline.js
// requires this case in every plugin.
TEST_CASE("Ruinae editor carries the shared update and version views",
          "[ruinae][controller][ui][version]")
{
    ::Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    const auto count = Krate::TestSupport::openEditorAndCountSharedViews(
        controller, "editor", std::string(RUINAE_RESOURCES_DIR) + "/editor.uidesc");
    CHECK(count.banners == 1);
    CHECK(count.checkButtons == 1);
    CHECK(count.versionLabels == 1);
    CHECK(count.legacyTagged == 0);

    controller.terminate();
}

// A fresh instance starts every parameter at its registered default, so a
// host's "reset to default" returns to the value the plugin opened with.
TEST_CASE("Ruinae parameters start at their registered defaults",
          "[ruinae][controller][defaults]")
{
    ::Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    const auto mismatches = Krate::TestSupport::parametersStartingOffTheirDefault(controller);
    for (const auto& m : mismatches)
        UNSCOPED_INFO(m);
    CHECK(mismatches.empty());

    controller.terminate();
}

// A host may terminate the controller before it removes the editor view. The update banner polls the update
// checker every 500 ms until the editor closes, so the checker must outlive terminate().
// Teeth under AddressSanitizer (Windows): a poll of a freed checker is a heap-use-after-free.
TEST_CASE("Ruinae: terminating the controller before the editor closes leaves the update views safe",
          "[ruinae][controller][ui][lifecycle][editor-uaf]")
{
    ::Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    Krate::TestSupport::terminateControllerWithEditorOpen(
        controller, "editor", std::string(RUINAE_RESOURCES_DIR) + "/editor.uidesc");
}

namespace {

/// Output parameters as a host collects them: the last value per ID, handed to the controller with
/// setParamNormalized after the block (a host's output-parameter transfer).
class HostOutputParams final : public Steinberg::Vst::IParameterChanges {
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
    Steinberg::int32 PLUGIN_API getParameterCount() override { return static_cast<Steinberg::int32>(queues_.size()); }
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

    void transferTo(Steinberg::Vst::EditController& controller) {
        for (auto& q : queues_) controller.setParamNormalized(q.getParameterId(), q.value());
        queues_.clear();
    }

private:
    std::vector<Queue> queues_;
};

}  // namespace

// R-005 (and R-089 before it): the processor used to hand the controller the raw addresses of its display atomics
// (trance-gate step, transport, envelope outputs and stages, voice active, morph X/Y) and the editor's 30 fps timers
// read them. A host that releases the processor before the editor closes made those timers read freed memory. The
// display data now reaches the controller as output-parameter values; the editor keeps showing the last of them after
// the processor is gone. Teeth under AddressSanitizer: a poll of the destroyed processor is a heap-use-after-free.
TEST_CASE("Destroying the processor while the editor's poll timer ticks is safe", "[vst][lifecycle]")
{
    // The view-lifecycle rig builds the Tab_Sound template (ADSR displays, morph pad); didOpen started the poll timer.
    RuinaeViewLifecycleTest::ViewLifecycleRig rig;

    std::vector<Krate::Plugins::ADSRDisplay*> displays;
    for (const char* name : {"ampEnvDisplay_", "filterEnvDisplay_", "modEnvDisplay_"}) {
        INFO(name);
        auto* display = dynamic_cast<Krate::Plugins::ADSRDisplay*>(rig.pointer(name));
        REQUIRE(display != nullptr);
        REQUIRE(rig.isLive(display));
        REQUIRE_FALSE(display->isPlaybackDotVisible());
        displays.push_back(display);
    }
    auto* pad = dynamic_cast<Krate::Plugins::XYMorphPad*>(rig.pointer("xyMorphPad_"));
    REQUIRE(pad != nullptr);
    // The controller's own Mixer Position moves the pad until the processor publishes its modulated position.
    rig.ctl.setParamNormalized(Ruinae::kMixerPositionId, 0.2);
    REQUIRE(pad->getMorphX() == Catch::Approx(0.2f));

    // A processor connected to the controller as a host connects them, playing a note.
    Steinberg::Vst::HostApplication host;
    auto proc = std::make_unique<Ruinae::Processor>();
    REQUIRE(proc->initialize(&host) == Steinberg::kResultOk);
    REQUIRE(proc->connect(&rig.ctl) == Steinberg::kResultTrue);
    REQUIRE(rig.ctl.connect(proc.get()) == Steinberg::kResultTrue);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    proc->setupProcessing(setup);
    proc->setActive(true);

    {
        std::vector<float> outL(512, 0.0f);
        std::vector<float> outR(512, 0.0f);
        float* channels[2] = {outL.data(), outR.data()};
        Steinberg::Vst::AudioBusBuffers bus{};
        bus.numChannels = 2;
        bus.channelBuffers32 = channels;
        Krate::Test::EventList events;
        Krate::Test::ParameterChanges noParams;
        HostOutputParams out;
        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = 512;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.inputEvents = &events;
        data.inputParameterChanges = &noParams;
        data.outputParameterChanges = &out;
        events.addNoteOn(60, 0.8f);
        for (int block = 0; block < 4; ++block) {
            proc->process(data);
            events.clear();
            out.transferTo(rig.ctl);
        }
    }

    rig.ctl.pollProcessorDisplays();  // one timer tick, also where no run loop drives the timer (Linux)
    for (auto* d : displays) CHECK(d->isPlaybackDotVisible());
    CHECK(pad->getMorphX() == Catch::Approx(0.5f));  // the processor's published position, not the knob's 0.2

    // The host releases the processor first; the editor stays open and its timer keeps ticking.
    proc->setActive(false);
    rig.ctl.disconnect(proc.get());
    proc->disconnect(&rig.ctl);
    proc->terminate();
    proc.reset();

    Krate::TestSupport::dispatchThreadMessagesFor(100);  // the real 33 ms timer fires (Windows)
    rig.ctl.pollProcessorDisplays();
    for (auto* d : displays) {
        REQUIRE(rig.isLive(d));
        CHECK(d->isPlaybackDotVisible());  // the last published values, held by the controller
    }
    CHECK(pad->getMorphX() == Catch::Approx(0.5f));

    // The trance-gate step indicator (Tab_Seq) decodes the step parameter: (step + 1) / 32.
    rig.clickControl(Ruinae::kMainTabTag, RuinaeViewLifecycleTest::kTabSeq);
    auto* stepEditor = dynamic_cast<Krate::Plugins::StepPatternEditor*>(rig.pointer("stepPatternEditor_"));
    REQUIRE(stepEditor != nullptr);
    rig.ctl.setParamNormalized(Ruinae::kTranceGatePlaybackStepId, 6.0 / 32.0);
    rig.ctl.pollProcessorDisplays();
    CHECK(stepEditor->getPlaybackStep() == 5);
    rig.ctl.setParamNormalized(Ruinae::kTranceGatePlaybackStepId, 0.0);
    rig.ctl.pollProcessorDisplays();
    CHECK(stepEditor->getPlaybackStep() == -1);
}

namespace {

[[nodiscard]] long ruinaeTestPid() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

/// A user and a factory preset directory under the system temp directory, unique to this process and this call,
/// removed in the destructor so a failing REQUIRE cannot leak them.
class TempPresetDirs {
public:
    TempPresetDirs() {
        static int counter = 0;
        const std::string tag = std::to_string(ruinaeTestPid()) + "_" + std::to_string(++counter);
        const std::filesystem::path root = std::filesystem::temp_directory_path();
        user_ = root / ("ruinae_terminate_probe_user_" + tag);
        factory_ = root / ("ruinae_terminate_probe_factory_" + tag);
        std::error_code ec;
        std::filesystem::remove_all(user_, ec);
        std::filesystem::remove_all(factory_, ec);
        std::filesystem::create_directories(user_, ec);
        std::filesystem::create_directories(factory_, ec);
    }
    ~TempPresetDirs() {
        std::error_code ec;
        std::filesystem::remove_all(user_, ec);
        std::filesystem::remove_all(factory_, ec);
    }
    TempPresetDirs(const TempPresetDirs&) = delete;
    TempPresetDirs& operator=(const TempPresetDirs&) = delete;
    TempPresetDirs(TempPresetDirs&&) = delete;
    TempPresetDirs& operator=(TempPresetDirs&&) = delete;

    [[nodiscard]] const std::filesystem::path& user() const noexcept { return user_; }
    [[nodiscard]] const std::filesystem::path& factory() const noexcept { return factory_; }

private:
    std::filesystem::path user_;
    std::filesystem::path factory_;
};

}  // namespace

// The synth and arp preset browsers the editor built in didOpen keep a raw PresetManager*, and their buttons still
// open them after terminate(): open() rescans the presets through that pointer, so the manager must outlive
// terminate(). Both browsers share the controller's one manager, which is rebuilt over empty temp user and factory
// directories, so neither scan creates or reads the real preset trees; the dirs are removed by their guard, declared
// before the controller so it outlives it. Teeth under AddressSanitizer: a scan through a freed manager is a
// heap-use-after-free in PresetManager::scanPresets.
TEST_CASE("Ruinae: opening the preset browsers after the controller terminates with the editor open is safe",
          "[ruinae][controller][ui][lifecycle][editor-uaf]")
{
    const TempPresetDirs dirs;
    REQUIRE(std::filesystem::is_directory(dirs.user()));
    REQUIRE(std::filesystem::is_directory(dirs.factory()));

    ::Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    controller.replacePresetManagerForTest(dirs.user(), dirs.factory());
    Krate::Plugins::PresetManager* manager = controller.getPresetManager();
    REQUIRE(manager != nullptr);
    REQUIRE(manager->getUserPresetDirectory() == dirs.user());
    REQUIRE(manager->getFactoryPresetDirectory() == dirs.factory());

    Krate::TestSupport::terminateControllerWithEditorOpen(
        controller, "editor", std::string(RUINAE_RESOURCES_DIR) + "/editor.uidesc", {}, [&controller] {
            controller.openPresetBrowser();     // the Presets button's action
            controller.openArpPresetBrowser();  // the arp section's Presets button
            CHECK(controller.getPresetManager() != nullptr);
        });
}
