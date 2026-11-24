#pragma once

// ==============================================================================
// HostRunLoopBridge - VSTGUI's Linux run loop for hosts that offer theirs only on IPlugFrame
// ==============================================================================
// The VST3 SDK gives VSTGUI the host's Linux::IRunLoop only through IPluginFactory3::setHostContext. Hosts that
// offer it solely on each editor's IPlugFrame (Carla 2.5) leave VSTGUI without one, and VSTGUI's own fallback
// dereferences the run loop before installing it, so opening the editor crashed the host (GitHub #313). When the
// fallback does install it, the module keeps a reference to that editor's IPlugFrame after the host destroyed it,
// and crashes on unload.
//
// The bridge is installed as VSTGUI's run loop for such hosts. It forwards to the host loop of one open editor
// (the active one); when that editor closes it moves every registration to the next open editor's loop, and when
// the last editor closes it unregisters everything, so it never holds a host loop past its editor's removed().
// Linux only: the SDK defines the Linux::IRunLoop / IEventHandler / ITimerHandler iids only there.
// ==============================================================================

#include "vstgui/lib/vstguibase.h"
#include "vstgui/lib/platform/platform_linux.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate::Plugins {

class HostRunLoopBridge final : public VSTGUI::IRunLoop, public VSTGUI::AtomicReferenceCounted {
public:
    HostRunLoopBridge();
    ~HostRunLoopBridge() override;

    /// An editor opened with this host loop. The first loop attached becomes the active one. Editors may share a
    /// loop (Carla gives every IPlugFrame the same one); each attach is counted.
    void addHostLoop(Steinberg::Linux::IRunLoop* loop);
    /// An editor is closing. Once no open editor uses its loop any more and that loop is the active one, every
    /// registration moves to the next open editor's loop, or is unregistered when none is left.
    void removeHostLoop(Steinberg::Linux::IRunLoop* loop);
    /// Open editors attached (each attach counts, shared loops included).
    [[nodiscard]] std::size_t hostLoopCount() const noexcept;

    // VSTGUI::IRunLoop. Registration fails while no editor's host loop is attached.
    bool registerEventHandler(int fd, VSTGUI::IEventHandler* handler) override;
    bool unregisterEventHandler(VSTGUI::IEventHandler* handler) override;
    bool registerTimer(uint64_t interval, VSTGUI::ITimerHandler* handler) override;
    bool unregisterTimer(VSTGUI::ITimerHandler* handler) override;

private:
    // The host calls these; they forward to the VSTGUI handler the bridge registered them for.
    struct EventAdapter : Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::Linux::IEventHandler>> {
        EventAdapter(VSTGUI::IEventHandler* h, int descriptor) : handler(h), fd(descriptor) {}
        void PLUGIN_API onFDIsSet(Steinberg::Linux::FileDescriptor) override { handler->onEvent(); }

        VSTGUI::IEventHandler* handler;
        int fd;
    };

    struct TimerAdapter : Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::Linux::ITimerHandler>> {
        TimerAdapter(VSTGUI::ITimerHandler* h, uint64_t ms) : handler(h), interval(ms) {}
        void PLUGIN_API onTimer() override { handler->onTimer(); }

        VSTGUI::ITimerHandler* handler;
        uint64_t interval;
    };

    [[nodiscard]] Steinberg::Linux::IRunLoop* activeLoop() const noexcept;
    void moveRegistrations(Steinberg::Linux::IRunLoop* from, Steinberg::Linux::IRunLoop* to);

    struct HostLoop {
        Steinberg::IPtr<Steinberg::Linux::IRunLoop> loop;
        std::size_t editors = 0;
    };
    std::vector<HostLoop> hostLoops_; // front() is the active loop
    std::vector<Steinberg::IPtr<EventAdapter>> events_;
    std::vector<Steinberg::IPtr<TimerAdapter>> timers_;
};

#if SMTG_OS_LINUX
/// Call from IPlugView::attached before VSTGUI opens its frame. When the host gave VSTGUI no run loop, installs the
/// module's bridge and attaches this editor's host loop to it; returns that loop, to pass to
/// detachEditorHostRunLoop when the editor is removed. Returns null when there is nothing to do (the host already
/// provided a run loop through setHostContext, or the frame offers none).
Steinberg::IPtr<Steinberg::Linux::IRunLoop> attachEditorHostRunLoop(Steinberg::IPlugFrame* frame);
/// Call from IPlugView::removed after VSTGUI closed its frame, with the loop attachEditorHostRunLoop returned.
void detachEditorHostRunLoop(Steinberg::Linux::IRunLoop* loop);
#endif

} // namespace Krate::Plugins
