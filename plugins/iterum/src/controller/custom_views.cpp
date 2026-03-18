// ==============================================================================
// Custom Views Implementation
// ==============================================================================

#include "custom_views.h"
#include "controller.h"

void PresetBrowserButton::onClick() {
    if (controller_) controller_->openPresetBrowser();
}

void SavePresetButton::onClick() {
    if (controller_) controller_->openSavePresetDialog();
}

VSTGUI::CMouseEventResult CopyPatternButton::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons)
{
    if (buttons.isLeftButton() && controller_) {
        controller_->copyCurrentPatternToCustom();
        return VSTGUI::kMouseEventHandled;
    }
    return CTextButton::onMouseDown(where, buttons);
}

VSTGUI::CMouseEventResult ResetPatternButton::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons)
{
    if (buttons.isLeftButton() && controller_) {
        controller_->resetPatternToDefault();
        return VSTGUI::kMouseEventHandled;
    }
    return CTextButton::onMouseDown(where, buttons);
}
