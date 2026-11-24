// ==============================================================================
// HostRunLoopBridge Unit Tests
// ==============================================================================
// Some Linux hosts (Carla 2.5) hand the plug-in its run loop only on each editor's IPlugFrame, never through
// IPluginFactory3::setHostContext. VSTGUI then had no run loop when the first X11 frame opened and crashed the
// host (GitHub #313). The bridge is VSTGUI's run loop for such hosts: it forwards to an open editor's host loop,
// moves every registration to the next open editor when that one closes, and lets go of the last one on close.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "ui/host_run_loop_bridge.h"

#include "pluginterfaces/base/funknownimpl.h"

#include <algorithm>
#include <utility>
#include <vector>

using namespace Krate::Plugins;
using Steinberg::Linux::FileDescriptor;
using Steinberg::Linux::TimerInterval;

namespace {

// A host's Linux::IRunLoop that records what is registered on it and can fire it, as the host's event loop would.
struct FakeHostLoop : Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::Linux::IRunLoop>> {
    std::vector<std::pair<Steinberg::Linux::IEventHandler*, FileDescriptor>> events;
    std::vector<std::pair<Steinberg::Linux::ITimerHandler*, TimerInterval>> timers;

    Steinberg::tresult PLUGIN_API registerEventHandler(Steinberg::Linux::IEventHandler* handler,
                                                       FileDescriptor fd) override {
        events.emplace_back(handler, fd);
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API unregisterEventHandler(Steinberg::Linux::IEventHandler* handler) override {
        return std::erase_if(events, [handler](const auto& e) { return e.first == handler; }) != 0
                   ? Steinberg::kResultTrue
                   : Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API registerTimer(Steinberg::Linux::ITimerHandler* handler,
                                                TimerInterval milliseconds) override {
        timers.emplace_back(handler, milliseconds);
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API unregisterTimer(Steinberg::Linux::ITimerHandler* handler) override {
        return std::erase_if(timers, [handler](const auto& t) { return t.first == handler; }) != 0
                   ? Steinberg::kResultTrue
                   : Steinberg::kResultFalse;
    }

    void fireAll() const {
        for (const auto& [handler, fd] : events)
            handler->onFDIsSet(fd);
        for (const auto& [handler, interval] : timers)
            handler->onTimer();
    }
};

struct CountingEventHandler final : VSTGUI::IEventHandler {
    CountingEventHandler() = default;
    CountingEventHandler(const CountingEventHandler&) = delete;
    CountingEventHandler& operator=(const CountingEventHandler&) = delete;
    CountingEventHandler(CountingEventHandler&&) = delete;
    CountingEventHandler& operator=(CountingEventHandler&&) = delete;
    virtual ~CountingEventHandler() = default;
    int calls = 0;
    void onEvent() override { ++calls; }
};

struct CountingTimerHandler final : VSTGUI::ITimerHandler {
    CountingTimerHandler() = default;
    CountingTimerHandler(const CountingTimerHandler&) = delete;
    CountingTimerHandler& operator=(const CountingTimerHandler&) = delete;
    CountingTimerHandler(CountingTimerHandler&&) = delete;
    CountingTimerHandler& operator=(CountingTimerHandler&&) = delete;
    virtual ~CountingTimerHandler() = default;
    int calls = 0;
    void onTimer() override { ++calls; }
};

auto makeHostLoop() { return Steinberg::owned(new FakeHostLoop()); }

} // namespace

TEST_CASE("HostRunLoopBridge refuses registrations while no editor's host loop is attached", "[host_run_loop]") {
    auto bridge = VSTGUI::makeOwned<HostRunLoopBridge>();
    CountingEventHandler event;
    CountingTimerHandler timer;

    CHECK_FALSE(bridge->registerEventHandler(7, &event));
    CHECK_FALSE(bridge->registerTimer(16, &timer));

    auto loop = makeHostLoop();
    bridge->addHostLoop(loop);
    CHECK(loop->events.empty());
    CHECK(loop->timers.empty());
    bridge->removeHostLoop(loop);
}

TEST_CASE("HostRunLoopBridge forwards registrations to the attached host loop and dispatches its callbacks",
          "[host_run_loop]") {
    auto bridge = VSTGUI::makeOwned<HostRunLoopBridge>();
    auto loop = makeHostLoop();
    bridge->addHostLoop(loop);

    CountingEventHandler event;
    CountingTimerHandler timer;
    REQUIRE(bridge->registerEventHandler(7, &event));
    REQUIRE(bridge->registerTimer(16, &timer));

    REQUIRE(loop->events.size() == 1);
    CHECK(loop->events.at(0).second == 7);
    REQUIRE(loop->timers.size() == 1);
    CHECK(loop->timers.at(0).second == 16);

    loop->fireAll();
    CHECK(event.calls == 1);
    CHECK(timer.calls == 1);

    CHECK(bridge->unregisterEventHandler(&event));
    CHECK(bridge->unregisterTimer(&timer));
    CHECK(loop->events.empty());
    CHECK(loop->timers.empty());
    bridge->removeHostLoop(loop);
}

TEST_CASE("HostRunLoopBridge moves registrations to the next open editor when the active editor closes",
          "[host_run_loop]") {
    auto bridge = VSTGUI::makeOwned<HostRunLoopBridge>();
    auto first = makeHostLoop();
    auto second = makeHostLoop();
    bridge->addHostLoop(first);
    bridge->addHostLoop(second);

    CountingEventHandler event;
    CountingTimerHandler timer;
    REQUIRE(bridge->registerEventHandler(7, &event));
    REQUIRE(bridge->registerTimer(16, &timer));
    CHECK(second->events.empty());
    CHECK(second->timers.empty());

    bridge->removeHostLoop(first); // the first editor closes; its host loop is about to go away

    CHECK(first->events.empty());
    CHECK(first->timers.empty());
    REQUIRE(second->events.size() == 1);
    CHECK(second->events.at(0).second == 7);
    REQUIRE(second->timers.size() == 1);
    CHECK(second->timers.at(0).second == 16);

    second->fireAll();
    CHECK(event.calls == 1);
    CHECK(timer.calls == 1);

    // Unregistering after the move reaches the loop that now holds the registration.
    CHECK(bridge->unregisterTimer(&timer));
    CHECK(second->timers.empty());
    CHECK_FALSE(bridge->unregisterTimer(&timer));
    CHECK(bridge->unregisterEventHandler(&event));
    CHECK(second->events.empty());
    bridge->removeHostLoop(second);
}

TEST_CASE("HostRunLoopBridge keeps a host loop that two editors share until both have closed", "[host_run_loop]") {
    // Carla hands every editor's IPlugFrame the same IRunLoop object.
    auto bridge = VSTGUI::makeOwned<HostRunLoopBridge>();
    auto shared = makeHostLoop();
    bridge->addHostLoop(shared);
    bridge->addHostLoop(shared);

    CountingEventHandler event;
    CountingTimerHandler timer;
    REQUIRE(bridge->registerEventHandler(7, &event));
    REQUIRE(bridge->registerTimer(16, &timer));

    bridge->removeHostLoop(shared); // the first editor closes

    CHECK(bridge->hostLoopCount() == 1);
    REQUIRE(shared->events.size() == 1);
    REQUIRE(shared->timers.size() == 1);
    shared->fireAll();
    CHECK(event.calls == 1);
    CHECK(timer.calls == 1);

    bridge->removeHostLoop(shared); // the second editor closes

    CHECK(bridge->hostLoopCount() == 0);
    CHECK(shared->events.empty());
    CHECK(shared->timers.empty());
}

TEST_CASE("HostRunLoopBridge leaves registrations alone when an inactive editor closes", "[host_run_loop]") {
    auto bridge = VSTGUI::makeOwned<HostRunLoopBridge>();
    auto first = makeHostLoop();
    auto second = makeHostLoop();
    bridge->addHostLoop(first);
    bridge->addHostLoop(second);

    CountingTimerHandler timer;
    REQUIRE(bridge->registerTimer(16, &timer));

    bridge->removeHostLoop(second);

    CHECK(first->timers.size() == 1);
    CHECK(second->timers.empty());
    CHECK(bridge->hostLoopCount() == 1);
    CHECK(bridge->unregisterTimer(&timer));
    bridge->removeHostLoop(first);
}

TEST_CASE("HostRunLoopBridge lets go of everything when the last editor closes", "[host_run_loop]") {
    auto bridge = VSTGUI::makeOwned<HostRunLoopBridge>();
    auto loop = makeHostLoop();
    bridge->addHostLoop(loop);

    CountingEventHandler event;
    CountingTimerHandler timer;
    REQUIRE(bridge->registerEventHandler(7, &event));
    REQUIRE(bridge->registerTimer(16, &timer));

    bridge->removeHostLoop(loop);

    CHECK(bridge->hostLoopCount() == 0);
    CHECK(loop->events.empty());
    CHECK(loop->timers.empty());
    CHECK_FALSE(bridge->registerTimer(16, &timer));

    // A later editor starts clean: nothing left over is registered on its loop.
    auto later = makeHostLoop();
    bridge->addHostLoop(later);
    CHECK(later->events.empty());
    CHECK(later->timers.empty());
    bridge->removeHostLoop(later);
}
