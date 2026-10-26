// ==============================================================================
// Membrum Editor Lifecycle Test
// ==============================================================================
// Exercises the full editor open -> close lifecycle headlessly via the shared
// harness, guarding against use-after-free / dangling-pointer crashes in the
// VST3EditorDelegate teardown path (verifyView/didOpen/willClose). Run under
// AddressSanitizer or valgrind to catch UAF regressions as hard failures.

#include <editor_lifecycle_harness.h>
#include <version_label_probe.h>

#include "controller/controller.h"

TEST_CASE("Membrum editor open/close cycle is UAF- and leak-free",
          "[membrum][controller][ui][lifecycle]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    Krate::TestSupport::exerciseEditorLifecycle(
        controller, "EditorDefault",
        std::string(MEMBRUM_RESOURCES_DIR) + "/editor.uidesc",
        /*cycles=*/3);

    controller.terminate();
}

// Every Krate editor shows its version through the shared VersionLabel view
// (createCustomView("VersionLabel") from VERSION_STR); no hand-filled tag-9999 label.
TEST_CASE("Membrum editor shows the version through the shared VersionLabel",
          "[membrum][controller][ui][version]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    const auto count = Krate::TestSupport::openEditorAndCountVersionLabels(
        controller, "EditorDefault", std::string(MEMBRUM_RESOURCES_DIR) + "/editor.uidesc");
    CHECK(count.shared == 1);
    CHECK(count.legacyTagged == 0);

    controller.terminate();
}
