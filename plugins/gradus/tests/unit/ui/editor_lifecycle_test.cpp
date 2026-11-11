// ==============================================================================
// Gradus Editor Lifecycle Test
// ==============================================================================
// Regression guard for the editor-close use-after-free fixed in willClose():
// the speed-curve container was removed (destroying its child controls) before
// those child pointers were dynamic_cast'd to unregister listeners, crashing in
// __dynamic_cast on freed memory when the host closed the editor window.
//
// Exercises the full open -> close lifecycle headlessly via the shared harness.
// Run gradus_tests under AddressSanitizer (ENABLE_ASAN) to catch UAF regressions
// here as hard failures.

#include <editor_lifecycle_harness.h>
#include <shared_editor_views_probe.h>
#include <registered_defaults_probe.h>
#include <terminate_with_editor_open_probe.h>

#include "controller/controller.h"

#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>  // _getpid: the temp preset directory names
#else
#include <unistd.h>  // getpid
#endif

TEST_CASE("Gradus editor open/close cycle is UAF- and leak-free",
          "[gradus][controller][ui][lifecycle]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    Krate::TestSupport::exerciseEditorLifecycle(
        controller, "editor",
        std::string(GRADUS_RESOURCES_DIR) + "/editor.uidesc",
        /*cycles=*/3);

    controller.terminate();
}

// Every Krate editor carries the shared views createSharedEditorView builds: one
// update banner, one Check for Updates button and one VersionLabel (VERSION_STR),
// and no hand-filled tag-9999 version label. tools/lint-plugin-editor-baseline.js
// requires this case in every plugin.
TEST_CASE("Gradus editor carries the shared update and version views",
          "[gradus][controller][ui][version]")
{
    ::Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    const auto count = Krate::TestSupport::openEditorAndCountSharedViews(
        controller, "editor", std::string(GRADUS_RESOURCES_DIR) + "/editor.uidesc");
    CHECK(count.banners == 1);
    CHECK(count.checkButtons == 1);
    CHECK(count.versionLabels == 1);
    CHECK(count.legacyTagged == 0);

    controller.terminate();
}

// A fresh instance starts every parameter at its registered default, so a
// host's "reset to default" returns to the value the plugin opened with.
TEST_CASE("Gradus parameters start at their registered defaults",
          "[gradus][controller][defaults]")
{
    ::Gradus::Controller controller;
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
TEST_CASE("Gradus: terminating the controller before the editor closes leaves the update views safe",
          "[gradus][controller][ui][lifecycle][editor-uaf]")
{
    ::Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    Krate::TestSupport::terminateControllerWithEditorOpen(
        controller, "editor", std::string(GRADUS_RESOURCES_DIR) + "/editor.uidesc");
}

namespace {

[[nodiscard]] long gradusTestPid() {
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
        const std::string tag = std::to_string(gradusTestPid()) + "_" + std::to_string(++counter);
        const std::filesystem::path root = std::filesystem::temp_directory_path();
        user_ = root / ("gradus_terminate_probe_user_" + tag);
        factory_ = root / ("gradus_terminate_probe_factory_" + tag);
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
TEST_CASE("Gradus: opening the preset browser after the controller terminates with the editor open is safe",
          "[gradus][controller][ui][lifecycle][editor-uaf]")
{
    const TempPresetDirs dirs;
    REQUIRE(std::filesystem::is_directory(dirs.user()));
    REQUIRE(std::filesystem::is_directory(dirs.factory()));

    ::Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    controller.replacePresetManagerForTest(dirs.user(), dirs.factory());
    Krate::Plugins::PresetManager* manager = controller.getPresetManager();
    REQUIRE(manager != nullptr);
    REQUIRE(manager->getUserPresetDirectory() == dirs.user());
    REQUIRE(manager->getFactoryPresetDirectory() == dirs.factory());

    Krate::TestSupport::terminateControllerWithEditorOpen(
        controller, "editor", std::string(GRADUS_RESOURCES_DIR) + "/editor.uidesc", {}, [&controller] {
            controller.openPresetBrowser();  // the Presets button's action
            CHECK(controller.getPresetManager() != nullptr);
        });
}
