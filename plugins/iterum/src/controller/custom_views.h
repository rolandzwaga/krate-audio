#pragma once

// ==============================================================================
// Custom Views for Iterum Controller
// ==============================================================================
// VSTGUI custom view classes used by Controller::createCustomView().
// These are standalone view classes that receive a Controller pointer for callbacks.
// ==============================================================================

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "ui/outline_button.h"

#include <string>

namespace Iterum {
class Controller;
}

// Iterum uses the shared OutlineButton (CView + virtual onClick()) with a light
// theme: light-grey frame, dark hover fill, mid-grey label.
inline const Krate::Plugins::OutlineButtonColors kIterumLightButtonColors{
    .frame     = VSTGUI::CColor(208, 208, 208),
    .hoverFill = VSTGUI::CColor(0, 0, 0, 20),
    .font      = VSTGUI::CColor(102, 102, 102),
};

// =============================================================================
// PresetBrowserButton: Button that opens the preset browser
// =============================================================================
class PresetBrowserButton : public Krate::Plugins::OutlineButton {
public:
    PresetBrowserButton(const VSTGUI::CRect& size, Iterum::Controller* controller)
        : OutlineButton(size, "Presets", kIterumLightButtonColors)
        , controller_(controller) {}
protected:
    void onClick() override;
private:
    Iterum::Controller* controller_ = nullptr;
};

// =============================================================================
// SavePresetButton: Button that opens standalone save dialog (Spec 042)
// =============================================================================
class SavePresetButton : public Krate::Plugins::OutlineButton {
public:
    SavePresetButton(const VSTGUI::CRect& size, Iterum::Controller* controller)
        : OutlineButton(size, "Save Preset", kIterumLightButtonColors)
        , controller_(controller) {}
protected:
    void onClick() override;
private:
    Iterum::Controller* controller_ = nullptr;
};

// =============================================================================
// CopyPatternButton: Copy current pattern to Custom (Spec 046 - User Story 4)
// =============================================================================
class CopyPatternButton : public VSTGUI::CTextButton {
public:
    CopyPatternButton(const VSTGUI::CRect& size, Iterum::Controller* controller)
        : CTextButton(size, nullptr, -1, "Copy to custom pattern")
        , controller_(controller)
    {
        setFrameColor(VSTGUI::CColor(80, 80, 85));
        setTextColor(VSTGUI::CColor(255, 255, 255));
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;

private:
    Iterum::Controller* controller_ = nullptr;
};

// =============================================================================
// ResetPatternButton: Reset custom pattern to default linear spread (Spec 046)
// =============================================================================
class ResetPatternButton : public VSTGUI::CTextButton {
public:
    ResetPatternButton(const VSTGUI::CRect& size, Iterum::Controller* controller)
        : CTextButton(size, nullptr, -1, "Reset")
        , controller_(controller)
    {
        setFrameColor(VSTGUI::CColor(80, 80, 85));
        setTextColor(VSTGUI::CColor(255, 255, 255));
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;

private:
    Iterum::Controller* controller_ = nullptr;
};
