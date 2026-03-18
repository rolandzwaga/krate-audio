#pragma once

// ==============================================================================
// Custom Buttons: Outline-style buttons for preset browser and save
// ==============================================================================

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cstring.h"

#include <string>

namespace Disrumpo {

class Controller;  // Forward declaration

// ==============================================================================
// OutlineButton: Rounded-rect outline button with hover highlight (dark theme)
// ==============================================================================

class OutlineButton : public VSTGUI::CView {
public:
    OutlineButton(const VSTGUI::CRect& size, std::string title,
                  VSTGUI::CColor frameColor = VSTGUI::CColor(64, 64, 72))
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
                context->setFillColor(VSTGUI::CColor(255, 255, 255, 20));
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
        context->setFontColor(VSTGUI::CColor(192, 192, 192));
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

// ==============================================================================
// PresetBrowserButton: Opens the preset browser modal (Spec 010)
// ==============================================================================

class PresetBrowserButton : public OutlineButton {
public:
    PresetBrowserButton(const VSTGUI::CRect& size, Controller* controller)
        : OutlineButton(size, "Presets")
        , controller_(controller) {}
protected:
    void onClick() override;
private:
    Controller* controller_ = nullptr;
};

// ==============================================================================
// SavePresetButton: Opens the save preset dialog (Spec 010)
// ==============================================================================

class SavePresetButton : public OutlineButton {
public:
    SavePresetButton(const VSTGUI::CRect& size, Controller* controller)
        : OutlineButton(size, "Save")
        , controller_(controller) {}
protected:
    void onClick() override;
private:
    Controller* controller_ = nullptr;
};

} // namespace Disrumpo
