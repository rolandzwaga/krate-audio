// ==============================================================================
// X11 frame lifecycle tests (Linux, real X display)
// ==============================================================================
// GitHub #313, second report: in Carla the editor crashed on a later open, opened black until the window was moved,
// or drew without text. Two VSTGUI X11 defects, both worked around in KrateVST3Editor (x11_frame_lifecycle.h):
//   1. VSTGUI closes its X connection when the last editor closes without finishing cairo's device for it, so a
//      later connection at the same address inherits stale cairo state (upstream VSTGUI #376 / #377).
//   2. VSTGUI reads X events only when the host reports the socket readable, but its own round trips move pending
//      events into xcb's queue, where they wait for unrelated socket traffic (the window stayed unmapped).
// Each case opens a real KrateVST3Editor into an X window, with the host loop offered only on the IPlugFrame, as
// Carla does. They need an X server (CI runs them under xvfb-run); without DISPLAY they skip, or fail when
// KRATE_REQUIRE_X11 is set.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "test_helpers/editor_lifecycle_harness.h"
#include "ui/krate_vst3_editor.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/platform/linux/x11platform.h"
#include "vstgui/lib/platform/platform_x11.h"

#include <cairo/cairo-xcb.h>
#include <poll.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Steinberg::Linux::FileDescriptor;
using Steinberg::Linux::TimerInterval;

void requireDisplay() {
    const char* display = std::getenv("DISPLAY");
    if (display != nullptr && *display != '\0')
        return;
    if (std::getenv("KRATE_REQUIRE_X11") != nullptr)
        FAIL("KRATE_REQUIRE_X11 is set but there is no X display (run under xvfb-run)");
    SKIP("no X display");
}

// The host side, as Carla offers it: the run loop only on the editor's IPlugFrame.
struct FakeX11PlugFrame
    : Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::IPlugFrame, Steinberg::Linux::IRunLoop>> {
    std::vector<std::pair<Steinberg::Linux::IEventHandler*, FileDescriptor>> events;
    std::vector<std::pair<Steinberg::Linux::ITimerHandler*, TimerInterval>> timers;

    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView*, Steinberg::ViewRect*) override {
        return Steinberg::kResultTrue;
    }
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

    // A readiness-based host (JUCE in Carla, glib in Ardour) calls the event handlers only when the socket is
    // readable; the timers run regardless. Copies: a callback may (un)register.
    void fireReadyEvents() const {
        auto copy = events;
        for (const auto& [handler, fd] : copy) {
            pollfd p{.fd = fd, .events = POLLIN, .revents = 0};
            if (poll(&p, 1, 0) > 0)
                handler->onFDIsSet(fd);
        }
    }
    void fireTimers() const {
        auto copy = timers;
        for (const auto& [handler, interval] : copy)
            handler->onTimer();
    }
    void pump(std::chrono::milliseconds duration) const {
        const auto end = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < end) {
            fireReadyEvents();
            fireTimers();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
};

struct X11TestController : Steinberg::Vst::EditController {};

// A parent window on the test's own X connection, standing in for the host's editor window.
struct HostWindow {
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    xcb_window_t id = 0;

    HostWindow() {
        if (xcb_connection_has_error(connection) != 0)
            return;
        xcb_screen_t* const screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
        id = xcb_generate_id(connection);
        xcb_create_window(connection, XCB_COPY_FROM_PARENT, id, screen->root, 0, 0, 400, 300, 0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, nullptr);
        xcb_map_window(connection, id);
        xcb_aux_sync(connection);
    }
    ~HostWindow() {
        if (id != 0)
            xcb_destroy_window(connection, id);
        xcb_disconnect(connection);
    }
    HostWindow(const HostWindow&) = delete;
    HostWindow& operator=(const HostWindow&) = delete;
    HostWindow(HostWindow&&) = delete;
    HostWindow& operator=(HostWindow&&) = delete;
};

std::string writeUidesc() {
    const auto path = std::filesystem::temp_directory_path() / "krate_x11_frame_lifecycle.uidesc";
    std::ofstream(path) << R"(<?xml version="1.0" encoding="UTF-8"?>
<vstgui-ui-description version="1">
  <template class="CViewContainer" name="editor" origin="0, 0" size="200, 120"/>
</vstgui-ui-description>
)";
    return path.string();
}

// One editor opened into `parent` through the IPlugView calls a host makes.
struct OpenEditor {
    Krate::Plugins::KrateVST3Editor* editor;

    OpenEditor(X11TestController& controller, FakeX11PlugFrame& plugFrame, xcb_window_t parent,
               const std::string& uidesc)
        : editor(new Krate::Plugins::KrateVST3Editor(&controller, "editor", uidesc.c_str())) {
        Steinberg::IPlugView* view = editor;
        view->setFrame(&plugFrame);
        // IPlugView takes an X11EmbedWindowID parent as the window id in a void*.
        void* const parentHandle =
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(parent)); // NOLINT(performance-no-int-to-ptr)
        REQUIRE(view->attached(parentHandle, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue);
        REQUIRE(editor->getFrame() != nullptr);
        REQUIRE(editor->getFrame()->getPlatformFrame() != nullptr);
    }
    ~OpenEditor() { close(); }
    OpenEditor(const OpenEditor&) = delete;
    OpenEditor& operator=(const OpenEditor&) = delete;
    OpenEditor(OpenEditor&&) = delete;
    OpenEditor& operator=(OpenEditor&&) = delete;

    void close() {
        if (editor == nullptr)
            return;
        Steinberg::IPlugView* view = editor;
        view->removed();
        view->setFrame(nullptr);
        view->release();
        editor = nullptr;
    }
    [[nodiscard]] xcb_window_t window() const {
        const auto* x11 = dynamic_cast<const VSTGUI::X11::IX11Frame*>(editor->getFrame()->getPlatformFrame());
        REQUIRE(x11 != nullptr);
        return x11->getX11WindowID();
    }
};

[[nodiscard]] cairo_device_t* referenceCairoDevice(xcb_connection_t* connection) {
    xcb_screen_t* const screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    xcb_visualtype_t* const visual = xcb_aux_find_visual_by_id(screen, screen->root_visual);
    cairo_surface_t* const probe = cairo_xcb_surface_create(connection, screen->root, visual, 1, 1);
    cairo_device_t* const device = cairo_device_reference(cairo_surface_get_device(probe));
    cairo_surface_destroy(probe);
    return device;
}

} // namespace

TEST_CASE("Closing the last X11 editor finishes cairo's device for VSTGUI's X connection",
          "[x11][editor][linux]") {
    requireDisplay();
    Krate::TestSupport::ensureVstguiInitialized();
    const std::string uidesc = writeUidesc();
    HostWindow host;
    REQUIRE(host.id != 0);
    X11TestController controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    FakeX11PlugFrame plugFrame;

    cairo_device_t* device = nullptr;
    {
        OpenEditor open(controller, plugFrame, host.id, uidesc);
        plugFrame.pump(std::chrono::milliseconds(100));
        device = referenceCairoDevice(VSTGUI::X11::RunLoop::instance().getXcbConnection());
        REQUIRE(cairo_device_status(device) == CAIRO_STATUS_SUCCESS);
    }

    // Unfinished, cairo would keep this state keyed by a closed connection's address and hand it to the next one.
    // A finished device reports itself only when acquired (its status stays SUCCESS until then).
    const cairo_status_t acquired = cairo_device_acquire(device);
    CHECK(acquired == CAIRO_STATUS_DEVICE_FINISHED);
    if (acquired == CAIRO_STATUS_SUCCESS)
        cairo_device_release(device);
    cairo_device_destroy(device);

    // The next editor gets fresh state and opens cleanly, repeatedly.
    for (int i = 0; i < 5; ++i) {
        OpenEditor reopen(controller, plugFrame, host.id, uidesc);
        plugFrame.pump(std::chrono::milliseconds(20));
    }
    controller.terminate();
}

TEST_CASE("X events already in xcb's queue reach VSTGUI without new socket data", "[x11][editor][linux]") {
    requireDisplay();
    Krate::TestSupport::ensureVstguiInitialized();
    const std::string uidesc = writeUidesc();
    HostWindow host;
    REQUIRE(host.id != 0);
    X11TestController controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    FakeX11PlugFrame plugFrame;
    {
        OpenEditor open(controller, plugFrame, host.id, uidesc);
        plugFrame.pump(std::chrono::milliseconds(200));

        xcb_connection_t* const connection = VSTGUI::X11::RunLoop::instance().getXcbConnection();
        pollfd socket{.fd = xcb_get_file_descriptor(connection), .events = POLLIN, .revents = 0};

        // A PropertyNotify for VSTGUI's window, then a round trip on VSTGUI's own connection, as its sync calls do:
        // the event is now in xcb's queue and the socket has nothing left to read.
        const char value = 'x';
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, open.window(), XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                            1, &value);
        xcb_aux_sync(connection);
        REQUIRE(poll(&socket, 1, 0) == 0);

        // Only the host's timers run; a readiness-based host would otherwise never call VSTGUI's handler again.
        plugFrame.fireTimers();
        CHECK(poll(&socket, 1, 1000) == 1);
    }
    controller.terminate();
}
