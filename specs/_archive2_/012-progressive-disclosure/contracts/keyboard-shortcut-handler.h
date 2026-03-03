// ==============================================================================
// KeyboardShortcutHandler Contract
// ==============================================================================
// Implements IKeyboardHook to provide global keyboard shortcuts for the plugin.
//
// FR-010: Tab cycles focus through band strips
// FR-011: Shift+Tab cycles focus in reverse
// FR-012: Space toggles bypass on focused band
// FR-013/FR-014: Arrow keys for fine adjustment (1/100th range)
// FR-015: Shift+Arrow for coarse adjustment (1/10th range)
// FR-016: Only active when editor has keyboard focus
// ==============================================================================

#pragma once

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/events.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <array>
#include <cstdint>

namespace Disrumpo {

class KeyboardShortcutHandler : public VSTGUI::IKeyboardHook {
public:
    /// @param controller The edit controller for parameter access
    /// @param frame The CFrame to manage focus on
    /// @param activeBandCount Pointer to current active band count (updated externally)
    KeyboardShortcutHandler(
        Steinberg::Vst::EditControllerEx1* controller,
        VSTGUI::CFrame* frame,
        int activeBandCount);

    ~KeyboardShortcutHandler() noexcept override = default;

    /// IKeyboardHook - intercept keyboard events before they reach views
    void onKeyboardEvent(VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* frame) override;

    /// Update the active band count (called when band count parameter changes)
    void setActiveBandCount(int count);

    /// Get the currently focused band index (-1 if no band focused)
    int getFocusedBandIndex() const;

private:
    bool handleTab(VSTGUI::KeyboardEvent& event);
    bool handleSpace(VSTGUI::KeyboardEvent& event);
    bool handleArrowKey(VSTGUI::KeyboardEvent& event);
    void cycleBandFocus(bool reverse);
    void toggleBandBypass(int bandIndex);
    void adjustFocusedParameter(float stepFraction);

    Steinberg::Vst::EditControllerEx1* controller_;
    VSTGUI::CFrame* frame_;
    int activeBandCount_;
    int focusedBandIndex_ = -1;

    // Band strip view references (populated on construction)
    static constexpr int kMaxBands = 8;
};

} // namespace Disrumpo
