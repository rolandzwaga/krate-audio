// ==============================================================================
// Editor Lifecycle Harness (shared test helper)
// ==============================================================================
//
// Drives a plugin's VST3 editor through repeated open -> close cycles WITHOUT a
// platform window, so it runs headless and fully cross-platform in CTest.
//
// Why this catches real crashes:
//   VSTGUI::VST3Editor::open() builds the entire view tree from the .uidesc via
//   enableEditing() and fires the controller's verifyView()/didOpen() delegate
//   hooks BEFORE it attaches a platform window. close() then fires willClose().
//   The platform attach itself is a no-op here (CFrame::open(nullptr) returns
//   false harmlessly), but every VST3EditorDelegate teardown path still runs --
//   which is exactly where editor-lifecycle use-after-free / dangling-pointer /
//   double-free bugs live (e.g. a controller that removes a container and then
//   touches its now-freed child views in willClose).
//
// Run the owning test target under AddressSanitizer (ENABLE_ASAN) to turn those
// latent UAF bugs into hard test failures. Under a normal build the cycle still
// runs and guards against crashes/leaks that reproduce deterministically.
//
// Usage (one small test per plugin):
//
//   #include "test_helpers/editor_lifecycle_harness.h"
//   TEST_CASE("Gradus editor open/close is leak- and UAF-free") {
//       Gradus::Controller controller;
//       REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
//       Krate::TestSupport::exerciseEditorLifecycle(
//           controller, "editor", std::string(GRADUS_RESOURCES_DIR) + "/editor.uidesc");
//       controller.terminate();
//   }
//
// The uidesc path must be absolute. UIDescription::parse() falls back to a plain
// CFileStream open when there is no plugin bundle (the test-exe case), so an
// absolute filesystem path to the source resources/editor.uidesc resolves. Pass
// it via a per-plugin compile definition (e.g. <PLUGIN>_RESOURCES_DIR).

#pragma once

#include <catch2/catch_test_macros.hpp>

#include "pluginterfaces/base/fplatform.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/vstguiinit.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <cstdlib>
#include <string>

#if SMTG_OS_WINDOWS
#include <windows.h>
#elif SMTG_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace Krate::TestSupport {

// The test executable is not a plugin bundle, so nothing has initialized the
// VSTGUI platform factory (normally done from the plugin's module entry point).
// Without it, constructing a VST3Editor / building views dereferences a null
// platform factory and crashes. Initialize exactly once per process.
inline void ensureVstguiInitialized()
{
    static const bool initialized = [] {
#if SMTG_OS_WINDOWS
        VSTGUI::init(::GetModuleHandle(nullptr));
#elif SMTG_OS_MACOS
        VSTGUI::init(::CFBundleGetMainBundle());
#else
        VSTGUI::init(nullptr);
#endif
        std::atexit([] { VSTGUI::exit(); });
        return true;
    }();
    (void)initialized;
}

// Open and close the editor `cycles` times against `controller` as the delegate.
// `templateName` is the root view template in the .uidesc (e.g. "editor",
// "Editor", "EditorDefault"). `uidescAbsolutePath` is an absolute path to the
// plugin's editor.uidesc on disk.
inline void exerciseEditorLifecycle(Steinberg::Vst::EditController& controller,
                                    const char* templateName,
                                    const std::string& uidescAbsolutePath,
                                    int cycles = 3)
{
    // Native window type for the running platform. attached() gates on
    // isPlatformTypeSupported(type); the parent pointer stays null so no real
    // window is created (CFrame::open(nullptr) fails harmlessly).
#if SMTG_OS_WINDOWS
    const Steinberg::FIDString kPlatformType = Steinberg::kPlatformTypeHWND;
#elif SMTG_OS_MACOS
    const Steinberg::FIDString kPlatformType = Steinberg::kPlatformTypeNSView;
#else
    const Steinberg::FIDString kPlatformType = Steinberg::kPlatformTypeX11EmbedWindowID;
#endif

    ensureVstguiInitialized();

    for (int i = 0; i < cycles; ++i)
    {
        auto* editor =
            new VSTGUI::VST3Editor(&controller, templateName, uidescAbsolutePath.c_str());

        // Drive the host-facing IPlugView interface: attached() builds the view
        // tree (verifyView) and fires didOpen(); removed() fires willClose().
        Steinberg::IPlugView* view = editor;
        const Steinberg::tresult attached = view->attached(nullptr, kPlatformType);
        CHECK(attached == Steinberg::kResultTrue);

        // The .uidesc must actually have populated the frame, otherwise no views
        // were built and the teardown path under test never ran -- a green test
        // would be a lie.
        REQUIRE(editor->getFrame() != nullptr);
        REQUIRE(editor->getFrame()->getNbViews() > 0);

        view->removed();   // -> close() -> delegate->willClose()
        view->release();   // FUnknown-refcounted; created with refcount 1
    }
}

} // namespace Krate::TestSupport
