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
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "ui/outline_button.h"

#include <string>

namespace Iterum {
class Controller;
}

// Iterum uses the shared OutlineButton (CView + virtual onClick()) with the Krate
// dark-theme defaults (OutlineButtonColors{}).

// The MultiTap Copy / Reset pattern buttons are CTextButtons. CTextButton's own
// defaults (a 220-to-180 grey gradient, white highlighted text) are unreadable on
// the dark panel, so both set the whole colour set to the editor's tokens
// (specs/iterum-ui-redesign/design-system.md 5.2, 5.6). Each gradient is handed to
// the button through owned(): setGradient keeps its own SharedPointer, so a bare
// CGradient::create would leak one reference per panel instantiation.
inline void styleIterumPatternButton(VSTGUI::CTextButton& button) {
    const VSTGUI::CColor sliderFrame(58, 58, 70);
    const VSTGUI::CColor sliderTrack(39, 39, 48);
    const VSTGUI::CColor accentDim(46, 90, 136);
    button.setFrameColor(sliderFrame);
    button.setFrameColorHighlighted(accentDim);
    button.setTextColor(VSTGUI::CColor(230, 230, 236));
    button.setTextColorHighlighted(VSTGUI::CColor(255, 255, 255));
    button.setGradient(VSTGUI::owned(VSTGUI::CGradient::create(0, 1, sliderTrack, sliderTrack)));
    button.setGradientHighlighted(VSTGUI::owned(VSTGUI::CGradient::create(0, 1, accentDim, accentDim)));
    button.setRoundRadius(3);
}

// =============================================================================
// PresetBrowserButton: Button that opens the preset browser
// =============================================================================
class PresetBrowserButton : public Krate::Plugins::OutlineButton {
public:
    PresetBrowserButton(const VSTGUI::CRect& size, Iterum::Controller* controller)
        : OutlineButton(size, "Presets", Krate::Plugins::OutlineButtonColors{})
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
        : OutlineButton(size, "Save Preset", Krate::Plugins::OutlineButtonColors{})
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
        styleIterumPatternButton(*this);
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
        styleIterumPatternButton(*this);
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;

private:
    Iterum::Controller* controller_ = nullptr;
};
