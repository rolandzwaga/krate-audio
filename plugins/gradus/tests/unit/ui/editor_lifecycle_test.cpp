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

#include "controller/controller.h"

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
