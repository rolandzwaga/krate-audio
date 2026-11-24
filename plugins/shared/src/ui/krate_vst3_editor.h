#pragma once

// ==============================================================================
// KrateVST3Editor - the VST3Editor every Krate plugin's createView returns
// ==============================================================================
// On Linux it gives VSTGUI the editor's host run loop (HostRunLoopBridge) when the host offered it only on the
// IPlugFrame. Without it, opening the editor in Carla crashed the host (GitHub #313). Identical to VST3Editor on
// every other platform and in every host that provides its run loop through setHostContext.
// ==============================================================================

#include "vstgui/plugin-bindings/vst3editor.h"

#if SMTG_OS_LINUX
#include "ui/host_run_loop_bridge.h"
#endif

namespace Krate::Plugins {

class KrateVST3Editor : public VSTGUI::VST3Editor {
public:
    using VSTGUI::VST3Editor::VST3Editor;

#if SMTG_OS_LINUX
    ~KrateVST3Editor() override { detachHostRunLoop(); }

protected: // as in VSTGUIEditor; hosts call these through IPlugView
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override {
        hostRunLoop_ = attachEditorHostRunLoop(plugFrame);
        const auto result = VST3Editor::attached(parent, type);
        if (result != Steinberg::kResultTrue)
            detachHostRunLoop();
        return result;
    }

    Steinberg::tresult PLUGIN_API removed() override {
        const auto result = VST3Editor::removed(); // closes the frame while the host loop is still attached
        detachHostRunLoop();
        return result;
    }

private:
    void detachHostRunLoop() {
        if (hostRunLoop_) {
            detachEditorHostRunLoop(hostRunLoop_);
            hostRunLoop_ = nullptr;
        }
    }

    Steinberg::IPtr<Steinberg::Linux::IRunLoop> hostRunLoop_;
#endif
};

} // namespace Krate::Plugins
