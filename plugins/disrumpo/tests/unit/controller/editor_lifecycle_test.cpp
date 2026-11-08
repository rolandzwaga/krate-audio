// ==============================================================================
// Disrumpo Editor Lifecycle Test
// ==============================================================================
// Exercises the full editor open -> close lifecycle headlessly via the shared
// harness, guarding against use-after-free / dangling-pointer crashes in the
// VST3EditorDelegate teardown path (verifyView/didOpen/willClose). Run under
// AddressSanitizer or valgrind to catch UAF regressions as hard failures.

#include <editor_lifecycle_harness.h>
#include <shared_editor_views_probe.h>

#include "controller/controller.h"

#include "base/source/updatehandler.h"

#include <cstdio>

TEST_CASE("Disrumpo editor open/close cycle is UAF- and leak-free",
          "[disrumpo][controller][ui][lifecycle]")
{
    Disrumpo::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    // Every addDependent made while the editor is open (NodeSelectionController,
    // MorphPad, DynamicNodeSelector, ...) must be undone when it closes.
    const size_t dependentsBefore = Steinberg::UpdateHandler::instance()->countDependencies();

    Krate::TestSupport::exerciseEditorLifecycle(
        controller, "editor",
        std::string(DISRUMPO_RESOURCES_DIR) + "/editor.uidesc",
        /*cycles=*/3);

    const size_t dependentsAfter = Steinberg::UpdateHandler::instance()->countDependencies();
    std::printf("Disrumpo editor lifecycle: dependents before %zu, after 3 open/close cycles %zu\n",
                dependentsBefore, dependentsAfter);
    CHECK(dependentsAfter == dependentsBefore);

    controller.terminate();
}

// Every Krate editor carries the shared views createSharedEditorView builds: one
// update banner, one Check for Updates button and one VersionLabel (VERSION_STR),
// and no hand-filled tag-9999 version label. tools/lint-plugin-editor-baseline.js
// requires this case in every plugin.
TEST_CASE("Disrumpo editor carries the shared update and version views",
          "[disrumpo][controller][ui][version]")
{
    ::Disrumpo::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    const auto count = Krate::TestSupport::openEditorAndCountSharedViews(
        controller, "editor", std::string(DISRUMPO_RESOURCES_DIR) + "/editor.uidesc");
    CHECK(count.banners == 1);
    CHECK(count.checkButtons == 1);
    CHECK(count.versionLabels == 1);
    CHECK(count.legacyTagged == 0);

    controller.terminate();
}
