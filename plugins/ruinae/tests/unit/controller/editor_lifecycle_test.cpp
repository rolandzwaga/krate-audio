// ==============================================================================
// Ruinae Editor Lifecycle Test
// ==============================================================================
// Exercises the full editor open -> close lifecycle headlessly via the shared
// harness, guarding against use-after-free / dangling-pointer crashes in the
// VST3EditorDelegate teardown path (verifyView/didOpen/willClose). Run under
// AddressSanitizer or valgrind to catch UAF regressions as hard failures.

#include <editor_lifecycle_harness.h>

#include "controller/controller.h"

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
