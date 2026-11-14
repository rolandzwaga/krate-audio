// ==============================================================================
// Iterum Editor Open/Close Lifecycle Test
// ==============================================================================
// Exercises the full editor open -> close lifecycle headlessly via the shared
// harness (IPlugView::attached/removed), building the real view tree and running
// the VST3EditorDelegate teardown path (verifyView/didOpen/willClose). Guards
// against use-after-free / dangling-pointer crashes on editor close. Run under
// AddressSanitizer or valgrind to catch UAF regressions as hard failures.
//
// Note: this complements editor_lifecycle_test.cpp (which is a focused
// VisibilityController dangling-pointer regression test, not a real open/close).

#include <editor_lifecycle_harness.h>
#include <shared_editor_views_probe.h>
#include <registered_defaults_probe.h>
#include <terminate_with_editor_open_probe.h>

#include "controller/controller.h"

TEST_CASE("Iterum editor open/close cycle is UAF- and leak-free",
          "[iterum][controller][ui][lifecycle]")
{
    Iterum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    Krate::TestSupport::exerciseEditorLifecycle(
        controller, "Editor",
        std::string(ITERUM_RESOURCES_DIR) + "/editor.uidesc",
        /*cycles=*/3);

    controller.terminate();
}

// Every Krate editor carries the shared views createSharedEditorView builds: one
// update banner, one Check for Updates button and one VersionLabel (VERSION_STR),
// and no hand-filled tag-9999 version label. tools/lint-plugin-editor-baseline.js
// requires this case in every plugin.
TEST_CASE("Iterum editor carries the shared update and version views",
          "[iterum][controller][ui][version]")
{
    ::Iterum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    const auto count = Krate::TestSupport::openEditorAndCountSharedViews(
        controller, "Editor", std::string(ITERUM_RESOURCES_DIR) + "/editor.uidesc");
    CHECK(count.banners == 1);
    CHECK(count.checkButtons == 1);
    CHECK(count.versionLabels == 1);
    CHECK(count.legacyTagged == 0);

    controller.terminate();
}

// A host may terminate the controller before it removes the editor view. The update banner polls the update
// checker every 500 ms until the editor closes, so the checker must outlive terminate().
// Teeth under AddressSanitizer (Windows): a poll of a freed checker is a heap-use-after-free.
TEST_CASE("Iterum: terminating the controller before the editor closes leaves the update views safe",
          "[iterum][controller][ui][lifecycle][editor-uaf]")
{
    ::Iterum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    Krate::TestSupport::terminateControllerWithEditorOpen(
        controller, "Editor", std::string(ITERUM_RESOURCES_DIR) + "/editor.uidesc");
}

// A fresh instance starts every parameter at its registered default, so a
// host's "reset to default" returns to the value the plugin opened with.
TEST_CASE("Iterum parameters start at their registered defaults",
          "[iterum][controller][defaults]")
{
    ::Iterum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    const auto mismatches = Krate::TestSupport::parametersStartingOffTheirDefault(controller);
    for (const auto& m : mismatches)
        UNSCOPED_INFO(m);
    CHECK(mismatches.empty());

    controller.terminate();
}
