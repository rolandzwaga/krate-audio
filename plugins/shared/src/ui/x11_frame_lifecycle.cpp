#include "ui/x11_frame_lifecycle.h"

#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/platform/linux/x11platform.h"

#include <cairo/cairo-xcb.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>

#include <cstddef>

namespace Krate::Plugins {

namespace {

// The module's open X11 frames. Editors are opened and closed on the host's UI thread only.
struct X11Frames {
    std::size_t open = 0;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> eventWake; // runs while any frame is open
};

X11Frames& x11Frames() {
    static X11Frames frames;
    return frames;
}

constexpr uint32_t kEventWakeMs = 16;

[[nodiscard]] xcb_connection_t* liveConnection() {
    xcb_connection_t* const connection = VSTGUI::X11::RunLoop::instance().getXcbConnection();
    return connection != nullptr && xcb_connection_has_error(connection) == 0 ? connection : nullptr;
}

/// A request whose reply lands on the socket, so the host sees the descriptor readable and calls VSTGUI's handler,
/// which then also dispatches the events xcb had already queued.
void wakeEventHandler() {
    if (xcb_connection_t* const connection = liveConnection()) {
        xcb_discard_reply(connection, xcb_get_input_focus(connection).sequence);
        xcb_flush(connection);
    }
}

/// cairo looks its device up by connection; a probe surface on the root window returns the one VSTGUI drew with.
void finishCairoDevice() {
    xcb_connection_t* const connection = liveConnection();
    if (connection == nullptr)
        return;
    xcb_screen_t* const screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    if (screen == nullptr)
        return;
    xcb_visualtype_t* const visual = xcb_aux_find_visual_by_id(screen, screen->root_visual);
    if (visual == nullptr)
        return;
    cairo_surface_t* const probe = cairo_xcb_surface_create(connection, screen->root, visual, 1, 1);
    if (cairo_device_t* const device = cairo_surface_get_device(probe); device != nullptr)
        cairo_device_finish(device);
    cairo_surface_destroy(probe);
}

} // namespace

void noteX11FrameOpened() {
    auto& frames = x11Frames();
    ++frames.open;
    if (frames.open == 1)
        frames.eventWake = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [](VSTGUI::CVSTGUITimer*) { wakeEventHandler(); }, kEventWakeMs);
}

bool beginX11FrameClose() {
    auto& frames = x11Frames();
    if (frames.open == 0)
        return false;
    --frames.open;
    if (frames.open != 0)
        return false;
    if (frames.eventWake) {
        frames.eventWake->stop();
        frames.eventWake = nullptr;
    }
    VSTGUI::X11::RunLoop::init(); // the closing frame still holds the connection, so this only adds a use
    return true;
}

void endX11FrameClose(bool lastFrame) {
    if (!lastFrame)
        return;
    finishCairoDevice();
    VSTGUI::X11::RunLoop::exit(); // the last use: VSTGUI closes the connection
}

} // namespace Krate::Plugins
