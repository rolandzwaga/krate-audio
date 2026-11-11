#pragma once

// ==============================================================================
// Terminate-with-editor-open probe
// ==============================================================================
// A host may terminate the edit controller before it removes the editor view.
// The editor's shared update views (UpdateBannerView, CheckForUpdatesButton in
// plugins/shared/src/ui/update_banner_view.h) keep a raw UpdateChecker* and the
// banner polls it every 500 ms until willClose, so the controller must keep its
// checker alive past terminate(): the editor holds a reference on the controller
// (EditorView's IPtr), so the controller's destructor runs after the views are gone.
//
// This opens the editor, terminates the controller, dispatches this thread's
// messages for longer than the banner's poll interval and then closes the editor.
// Teeth under AddressSanitizer on Windows, where VSTGUI timers are thread timers
// fired by the message dispatch: a poll of a freed checker is a heap-use-after-free
// in UpdateChecker::hasResult. macOS and Linux test harnesses run no loop that
// fires the timer, so there the case only exercises the close-after-terminate order.

#include "editor_lifecycle_harness.h"
#include "shared_editor_views_probe.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <chrono>
#include <functional>
#include <string>
#include <thread>

namespace Krate::TestSupport {

/// Dispatches this thread's messages for `ms` milliseconds (Windows); elsewhere it waits.
inline void dispatchThreadMessagesFor(int ms)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
#if SMTG_OS_WINDOWS
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

/// Opens the editor, terminates `controller`, lets the update banner's poll fire, closes the editor.
/// `beforeTerminate` runs with the editor open, `afterTerminate` right after terminate(): a case drives an
/// editor action there (open the preset browser, load a preset) that must stay safe after terminate().
inline void terminateControllerWithEditorOpen(Steinberg::Vst::EditController& controller,
                                              const char* templateName,
                                              const std::string& uidescAbsolutePath,
                                              const std::function<void()>& beforeTerminate = {},
                                              const std::function<void()>& afterTerminate = {})
{
    ensureVstguiInitialized();
    auto* editor = new VSTGUI::VST3Editor(&controller, templateName, uidescAbsolutePath.c_str());
    Steinberg::IPlugView* view = editor;
    REQUIRE(view->attached(nullptr, nativePlatformType()) == Steinberg::kResultTrue);
    REQUIRE(editor->getFrame() != nullptr);
    SharedEditorViewCount count;
    countSharedEditorViews(editor->getFrame(), count);
    REQUIRE(count.banners == 1);  // the view whose poll this exercises

    if (beforeTerminate)
        beforeTerminate();
    REQUIRE(controller.terminate() == Steinberg::kResultOk);
    if (afterTerminate)
        afterTerminate();
    dispatchThreadMessagesFor(700);  // the banner's 500 ms poll fires (Windows)

    view->removed();  // -> willClose
    view->release();
}

} // namespace Krate::TestSupport
