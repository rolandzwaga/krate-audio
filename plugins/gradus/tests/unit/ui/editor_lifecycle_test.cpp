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
