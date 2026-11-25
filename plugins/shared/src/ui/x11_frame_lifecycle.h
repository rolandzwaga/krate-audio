#pragma once

// ==============================================================================
// X11 frame lifecycle - cairo state for VSTGUI's X connection is finished before the connection closes
// ==============================================================================
// VSTGUI opens one X connection per module and closes it (xcb_disconnect) when the last X11 frame closes, but it
// never calls cairo_device_finish for that connection (upstream VSTGUI #376 / #377, open). cairo keeps its state for
// the closed connection in a process-wide list keyed by the xcb_connection_t address. The next editor's connection
// often gets the same address, cairo then hands it the stale state, and reopening the editor aborts in
// cairo_xcb_surface_create ("_get_screen_index: Assertion !reached"), or draws it black or without text
// (GitHub #313: Carla; the same in REAPER per VSTGUI #377).
//
// KrateVST3Editor wraps each X11 frame's close: when it is the module's last one, VSTGUI's connection is held open
// across VST3Editor::removed(), cairo's device for it is finished, and only then does VSTGUI close the connection.
// Linux only.
// ==============================================================================

namespace Krate::Plugins {

/// VSTGUI opened an X11 frame for an editor.
void noteX11FrameOpened();

/// Call before VST3Editor::removed() closes an X11 frame that noteX11FrameOpened() counted. Returns true when it is
/// the module's last open X11 frame; VSTGUI's X connection then stays open until endX11FrameClose(true).
[[nodiscard]] bool beginX11FrameClose();

/// Call after VST3Editor::removed(), with beginX11FrameClose()'s result. When it was the last frame, finishes cairo's
/// device for VSTGUI's X connection and lets VSTGUI close the connection.
void endX11FrameClose(bool lastFrame);

} // namespace Krate::Plugins
