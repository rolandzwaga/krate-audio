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

#include <string>

namespace Iterum {
class Controller;
}

// =============================================================================
// OutlineButton: Rounded-rect outline button with hover highlight
// Matches the style used in Ruinae/Innexus, adapted for Iterum's light theme
// =============================================================================
class OutlineButton : public VSTGUI::CView {
public:
    OutlineButton(const VSTGUI::CRect& size, std::string title,
                  VSTGUI::CColor frameColor = VSTGUI::CColor(208, 208, 208))
        : CView(size)
        , title_(std::move(title))
        , frameColor_(frameColor)
    {}

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
        auto r = getViewSize();
        r.inset(0.5, 0.5);

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (path) {
            constexpr double kRadius = 3.0;
            path->addRoundRect(r, kRadius);

            if (hovered_) {
                context->setFillColor(VSTGUI::CColor(0, 0, 0, 20));
                context->drawGraphicsPath(path,
                    VSTGUI::CDrawContext::kPathFilled);
            }

            context->setFrameColor(frameColor_);
            context->setLineWidth(1.0);
            context->drawGraphicsPath(path,
                VSTGUI::CDrawContext::kPathStroked);
        }

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>(
            *VSTGUI::kNormalFontSmaller);
        context->setFont(font);
        context->setFontColor(VSTGUI::CColor(102, 102, 102));
        context->drawString(
            VSTGUI::UTF8String(title_), getViewSize(),
            VSTGUI::kCenterText);

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseEntered(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override {
        hovered_ = true;
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorHand);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override {
        hovered_ = false;
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorDefault);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& buttons) override {
        if (buttons.isLeftButton()) {
            onClick();
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }
        return VSTGUI::kMouseEventNotHandled;
    }

protected:
    virtual void onClick() = 0;

private:
    std::string title_;
    VSTGUI::CColor frameColor_;
    bool hovered_ = false;
};

// =============================================================================
// PresetBrowserButton: Button that opens the preset browser
// =============================================================================
class PresetBrowserButton : public OutlineButton {
public:
    PresetBrowserButton(const VSTGUI::CRect& size, Iterum::Controller* controller)
        : OutlineButton(size, "Presets")
        , controller_(controller) {}
protected:
    void onClick() override;
private:
    Iterum::Controller* controller_ = nullptr;
};

// =============================================================================
// SavePresetButton: Button that opens standalone save dialog (Spec 042)
// =============================================================================
class SavePresetButton : public OutlineButton {
public:
    SavePresetButton(const VSTGUI::CRect& size, Iterum::Controller* controller)
        : OutlineButton(size, "Save Preset")
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
