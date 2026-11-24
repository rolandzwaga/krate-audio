#include "ui/host_run_loop_bridge.h"

#include "pluginterfaces/base/funknownimpl.h"

#if SMTG_OS_LINUX
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/platformfactory.h"
#endif

#include <algorithm>

namespace Krate::Plugins {

// The host calls these; they forward to the VSTGUI handler the bridge registered them for.
struct HostRunLoopBridge::EventAdapter : Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::Linux::IEventHandler>> {
    EventAdapter(VSTGUI::IEventHandler* h, int descriptor) : handler(h), fd(descriptor) {}
    void PLUGIN_API onFDIsSet(Steinberg::Linux::FileDescriptor) override { handler->onEvent(); }

    VSTGUI::IEventHandler* handler;
    int fd;
};

struct HostRunLoopBridge::TimerAdapter : Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::Linux::ITimerHandler>> {
    TimerAdapter(VSTGUI::ITimerHandler* h, uint64_t ms) : handler(h), interval(ms) {}
    void PLUGIN_API onTimer() override { handler->onTimer(); }

    VSTGUI::ITimerHandler* handler;
    uint64_t interval;
};

HostRunLoopBridge::HostRunLoopBridge() = default;
HostRunLoopBridge::~HostRunLoopBridge() = default;

Steinberg::Linux::IRunLoop* HostRunLoopBridge::activeLoop() const noexcept {
    return hostLoops_.empty() ? nullptr : hostLoops_.front().loop.get();
}

std::size_t HostRunLoopBridge::hostLoopCount() const noexcept {
    std::size_t editors = 0;
    for (const auto& l : hostLoops_)
        editors += l.editors;
    return editors;
}

void HostRunLoopBridge::addHostLoop(Steinberg::Linux::IRunLoop* loop) {
    if (loop == nullptr)
        return;
    const auto it = std::ranges::find_if(hostLoops_, [loop](const auto& l) { return l.loop.get() == loop; });
    if (it != hostLoops_.end()) {
        ++it->editors;
        return;
    }
    hostLoops_.push_back({.loop = Steinberg::IPtr<Steinberg::Linux::IRunLoop>(loop), .editors = 1});
}

void HostRunLoopBridge::removeHostLoop(Steinberg::Linux::IRunLoop* loop) {
    const auto it = std::ranges::find_if(hostLoops_, [loop](const auto& l) { return l.loop.get() == loop; });
    if (it == hostLoops_.end())
        return;
    if (--it->editors != 0)
        return; // another open editor still uses this loop

    if (it != hostLoops_.begin()) {
        hostLoops_.erase(it);
        return;
    }

    // The active loop's last editor is closing: keep the loop referenced until every registration has left it.
    const Steinberg::IPtr<Steinberg::Linux::IRunLoop> closing = it->loop;
    hostLoops_.erase(it);
    moveRegistrations(closing, activeLoop());
}

void HostRunLoopBridge::moveRegistrations(Steinberg::Linux::IRunLoop* from, Steinberg::Linux::IRunLoop* to) {
    for (const auto& e : events_)
        from->unregisterEventHandler(e);
    for (const auto& t : timers_)
        from->unregisterTimer(t);

    if (to == nullptr) {
        events_.clear();
        timers_.clear();
        return;
    }

    // A registration the new loop refuses can never fire again; drop it.
    std::erase_if(events_, [to](const auto& e) { return to->registerEventHandler(e, e->fd) != Steinberg::kResultTrue; });
    std::erase_if(timers_, [to](const auto& t) { return to->registerTimer(t, t->interval) != Steinberg::kResultTrue; });
}

bool HostRunLoopBridge::registerEventHandler(int fd, VSTGUI::IEventHandler* handler) {
    auto* loop = activeLoop();
    if (loop == nullptr || handler == nullptr)
        return false;
    auto adapter = Steinberg::owned(new EventAdapter(handler, fd));
    if (loop->registerEventHandler(adapter, fd) != Steinberg::kResultTrue)
        return false;
    events_.push_back(adapter);
    return true;
}

bool HostRunLoopBridge::unregisterEventHandler(VSTGUI::IEventHandler* handler) {
    const auto it = std::ranges::find_if(events_, [handler](const auto& e) { return e->handler == handler; });
    if (it == events_.end())
        return false;
    if (auto* loop = activeLoop())
        loop->unregisterEventHandler(*it);
    events_.erase(it);
    return true;
}

bool HostRunLoopBridge::registerTimer(uint64_t interval, VSTGUI::ITimerHandler* handler) {
    auto* loop = activeLoop();
    if (loop == nullptr || handler == nullptr)
        return false;
    auto adapter = Steinberg::owned(new TimerAdapter(handler, interval));
    if (loop->registerTimer(adapter, interval) != Steinberg::kResultTrue)
        return false;
    timers_.push_back(adapter);
    return true;
}

bool HostRunLoopBridge::unregisterTimer(VSTGUI::ITimerHandler* handler) {
    const auto it = std::ranges::find_if(timers_, [handler](const auto& t) { return t->handler == handler; });
    if (it == timers_.end())
        return false;
    if (auto* loop = activeLoop())
        loop->unregisterTimer(*it);
    timers_.erase(it);
    return true;
}

#if SMTG_OS_LINUX
namespace {

HostRunLoopBridge& moduleBridge() {
    static const auto bridge = VSTGUI::makeOwned<HostRunLoopBridge>();
    return *bridge;
}

} // namespace

Steinberg::IPtr<Steinberg::Linux::IRunLoop> attachEditorHostRunLoop(Steinberg::IPlugFrame* frame) {
    const auto* factory = VSTGUI::getPlatformFactory().asLinuxFactory();
    if (factory == nullptr || frame == nullptr)
        return nullptr;

    Steinberg::FUnknownPtr<Steinberg::Linux::IRunLoop> loop(frame);
    if (!loop)
        return nullptr;

    auto& bridge = moduleBridge();
    VSTGUI::IRunLoop* const asRunLoop = &bridge;
    if (factory->getRunLoop() == nullptr)
        factory->setRunLoop(VSTGUI::SharedPointer<VSTGUI::IRunLoop>(asRunLoop));
    else if (factory->getRunLoop().get() != asRunLoop)
        return nullptr; // the host provided a run loop through setHostContext; VSTGUI uses that one

    bridge.addHostLoop(loop);
    return {loop.getInterface()};
}

void detachEditorHostRunLoop(Steinberg::Linux::IRunLoop* loop) {
    auto& bridge = moduleBridge();
    bridge.removeHostLoop(loop);
    if (bridge.hostLoopCount() != 0)
        return;
    const VSTGUI::IRunLoop* const asRunLoop = &bridge;
    if (const auto* factory = VSTGUI::getPlatformFactory().asLinuxFactory();
        factory != nullptr && factory->getRunLoop().get() == asRunLoop)
        factory->setRunLoop(nullptr);
}
#endif

} // namespace Krate::Plugins
