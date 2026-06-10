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
