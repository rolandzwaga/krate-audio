// ==============================================================================
// Innexus Editor Lifecycle Test
// ==============================================================================
// Exercises the full editor open -> close lifecycle headlessly via the shared
// harness, guarding against use-after-free / dangling-pointer crashes in the
// VST3EditorDelegate teardown path (verifyView/didOpen/willClose). Run under
// AddressSanitizer or valgrind to catch UAF regressions as hard failures.

#include <editor_lifecycle_harness.h>
#include <shared_editor_views_probe.h>
#include <terminate_with_editor_open_probe.h>

#include "controller/controller.h"
#include "plugin_ids.h"
// The modulator panel's Rate knob and Sync toggle are shared controls: their ViewCreators register when the
// header is included (the plugin does that in entry.cpp, which innexus_tests does not compile).
#include "ui/arc_knob.h"
#include "ui/toggle_button.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>  // _getpid: the temp preset directory names
#else
#include <unistd.h>  // getpid
#endif

TEST_CASE("Innexus editor open/close cycle is UAF- and leak-free",
          "[innexus][controller][ui][lifecycle]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    Krate::TestSupport::exerciseEditorLifecycle(
        controller, "Editor",
        std::string(INNEXUS_RESOURCES_DIR) + "/editor.uidesc",
        /*cycles=*/3);

    controller.terminate();
}

// Every Krate editor carries the shared views createSharedEditorView builds: one
// update banner, one Check for Updates button and one VersionLabel (VERSION_STR),
// and no hand-filled tag-9999 version label. tools/lint-plugin-editor-baseline.js
// requires this case in every plugin.
TEST_CASE("Innexus editor carries the shared update and version views",
          "[innexus][controller][ui][version]")
{
    ::Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    const auto count = Krate::TestSupport::openEditorAndCountSharedViews(
        controller, "Editor", std::string(INNEXUS_RESOURCES_DIR) + "/editor.uidesc");
    CHECK(count.banners == 1);
    CHECK(count.checkButtons == 1);
    CHECK(count.versionLabels == 1);
    CHECK(count.legacyTagged == 0);

    controller.terminate();
}

// A host may terminate the controller before it removes the editor view. The update banner polls the update
// checker every 500 ms until the editor closes, so the checker must outlive terminate().
// Teeth under AddressSanitizer (Windows): a poll of a freed checker is a heap-use-after-free.
TEST_CASE("Innexus: terminating the controller before the editor closes leaves the update views safe",
          "[innexus][controller][ui][lifecycle][editor-uaf]")
{
    ::Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    Krate::TestSupport::terminateControllerWithEditorOpen(
        controller, "Editor", std::string(INNEXUS_RESOURCES_DIR) + "/editor.uidesc");
}

namespace {

[[nodiscard]] long innexusTestPid() {
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
        const std::string tag = std::to_string(innexusTestPid()) + "_" + std::to_string(++counter);
        const std::filesystem::path root = std::filesystem::temp_directory_path();
        user_ = root / ("innexus_terminate_probe_user_" + tag);
        factory_ = root / ("innexus_terminate_probe_factory_" + tag);
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

// The preset browser the editor built in didOpen keeps a raw PresetManager*, and its Presets button still opens it
// after terminate(): open() rescans the presets through that pointer, so the manager must outlive terminate().
// The controller's manager is rebuilt over empty temp user and factory directories, so the scan never creates or
// reads the real preset trees; the dirs are removed by their guard, declared before the controller so it outlives it.
// Teeth under AddressSanitizer: a scan through a freed manager is a heap-use-after-free in PresetManager::scanPresets.
TEST_CASE("Innexus: opening the preset browser after the controller terminates with the editor open is safe",
          "[innexus][controller][ui][lifecycle][editor-uaf]")
{
    const TempPresetDirs dirs;
    REQUIRE(std::filesystem::is_directory(dirs.user()));
    REQUIRE(std::filesystem::is_directory(dirs.factory()));

    ::Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    controller.replacePresetManagerForTest(dirs.user(), dirs.factory());
    Krate::Plugins::PresetManager* manager = controller.getPresetManager();
    REQUIRE(manager != nullptr);
    REQUIRE(manager->getUserPresetDirectory() == dirs.user());
    REQUIRE(manager->getFactoryPresetDirectory() == dirs.factory());

    Krate::TestSupport::terminateControllerWithEditorOpen(
        controller, "Editor", std::string(INNEXUS_RESOURCES_DIR) + "/editor.uidesc", {}, [&controller] {
            controller.openPresetBrowser();  // the Presets button's action
            CHECK(controller.getPresetManager() != nullptr);
        });
}

namespace {

VSTGUI::CControl* findControlWithTag(VSTGUI::CViewContainer* container, std::int32_t tag)
{
    VSTGUI::CControl* found = nullptr;
    container->forEachChild([&](VSTGUI::CView* child) {
        if (found != nullptr)
            return;
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(child); control != nullptr && control->getTag() == tag)
            found = control;
        else if (auto* sub = child->asViewContainer())
            found = findControlWithTag(sub, tag);
    });
    return found;
}

} // namespace

// Each modulator panel carries a ModulatorSubController that listens to the panel's Sync toggle. When the editor's
// view tree is not attached to an open frame (a headless editor, CFrame::open(nullptr)), VST3Editor::onViewRemoved
// never runs, so VSTGUI frees the panel's children before its sub-controller (CViewContainer::beforeDelete, then
// CView::beforeDelete): the sub-controller must not touch the toggle when it is destroyed. Each editor open builds
// both panels; each close frees them this way.
// Teeth under AddressSanitizer: a touch of the freed toggle is a heap-use-after-free.
TEST_CASE("Innexus modulator panels with a Sync sub-controller close without touching a freed control",
          "[innexus][controller][ui][lifecycle][editor-uaf]")
{
    ::Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    Krate::TestSupport::ensureVstguiInitialized();
    const std::string uidescPath = std::string(INNEXUS_RESOURCES_DIR) + "/editor.uidesc";
    for (int cycle = 0; cycle < 2; ++cycle) {
        auto* editor = new VSTGUI::VST3Editor(&controller, "Editor", uidescPath.c_str());
        Steinberg::IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue);
        REQUIRE(editor->getFrame() != nullptr);
        REQUIRE_FALSE(editor->getFrame()->isAttached());
        // Both panels are built and their sub-controllers listen: Sync off retags the Rate knob to the free rate
        for (const std::int32_t sync : {static_cast<std::int32_t>(Innexus::kMod1RateSyncId),
                                        static_cast<std::int32_t>(Innexus::kMod2RateSyncId)}) {
            const std::int32_t offset = sync - static_cast<std::int32_t>(Innexus::kMod1RateSyncId);
            auto* toggle = findControlWithTag(editor->getFrame(), sync);
            REQUIRE(toggle != nullptr);
            REQUIRE(findControlWithTag(editor->getFrame(),
                                       static_cast<std::int32_t>(Innexus::kMod1NoteValueId) + offset) != nullptr);
            toggle->setValueNormalized(0.0f);
            toggle->valueChanged();
            CHECK(findControlWithTag(editor->getFrame(), static_cast<std::int32_t>(Innexus::kMod1RateId) + offset) !=
                  nullptr);
            toggle->setValueNormalized(1.0f);
            toggle->valueChanged();
        }
        view->removed();  // the editor frees each panel, then its sub-controller
        view->release();
    }
    controller.terminate();
}
