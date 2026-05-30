#pragma once

// ==============================================================================
// Outline button family — shared across every Krate Audio plugin
// ==============================================================================
// Rounded-rect outline buttons with a centered label and a subtle hover fill.
// Two interaction models share one renderer (drawOutlineButton):
//
//   OutlineBrowserButton  : CControl — fires valueChanged() via an
//                           IControlListener/tag (used by modal dialogs that
//                           need Enter/Escape to fall through to the parent).
//   OutlineButton         : CView    — abstract, override onClick(); the common
//                           base for per-plugin Presets/Save/Expand buttons.
//   OutlineActionButton   : CView    — OutlineButton with a std::function
//                           callback (no subclassing needed).
//
// Colors are parameterized via OutlineButtonColors; the defaults are the Krate
// dark theme. Light themes (e.g. Iterum) pass an explicit color triple.
// ==============================================================================

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cstring.h"

#include <functional>
#include <string>
#include <utility>

namespace Krate::Plugins {

// Color triple for an outline button. Defaults are the Krate dark theme.
struct OutlineButtonColors {
    VSTGUI::CColor frame     {64, 64, 72};
    VSTGUI::CColor hoverFill {255, 255, 255, 20};
    VSTGUI::CColor font      {192, 192, 192};
};

// Single rendering routine for the whole family: rounded-rect outline, optional
// hover fill, centered small-font label. Both the CControl and CView variants
// call this so the drawing lives in exactly one place.
inline void drawOutlineButton(VSTGUI::CDrawContext* context,
                              const VSTGUI::CRect& viewSize,
                              const VSTGUI::UTF8String& title, bool hovered,
                              const OutlineButtonColors& colors) {
    context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
    auto r = viewSize;
    r.inset(0.5, 0.5);

    if (auto path = VSTGUI::owned(context->createGraphicsPath())) {
        constexpr double kRadius = 3.0;
        path->addRoundRect(r, kRadius);

        if (hovered) {
            context->setFillColor(colors.hoverFill);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
        }

        context->setFrameColor(colors.frame);
        context->setLineWidth(1.0);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>(*VSTGUI::kNormalFontSmaller);
    context->setFont(font);
    context->setFontColor(colors.font);
    context->drawString(title, viewSize, VSTGUI::kCenterText);
}

// ------------------------------------------------------------------------------
// OutlineBrowserButton — CControl variant (listener/tag based)
// ------------------------------------------------------------------------------
// Fires valueChanged() on click so it works with IControlListener. Does NOT
// consume Enter/Escape, so parent dialogs can handle them without a subclass.
class OutlineBrowserButton : public VSTGUI::CControl {
public:
    OutlineBrowserButton(const VSTGUI::CRect& size,
                         VSTGUI::IControlListener* listener,
                         int32_t tag,
                         std::string title,
                         VSTGUI::CColor frameColor = VSTGUI::CColor(64, 64, 72))
        : CControl(size, listener, tag)
        , title_(std::move(title))
        , frameColor_(frameColor)
    {}

    void draw(VSTGUI::CDrawContext* context) override {
        drawOutlineButton(context, getViewSize(), VSTGUI::UTF8String(title_),
                          hovered_, {.frame = frameColor_});
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
            beginEdit();
            setValueNormalized(1.0f);
            valueChanged();
            setValueNormalized(0.0f);
            endEdit();
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }
        return VSTGUI::kMouseEventNotHandled;
    }

    CLASS_METHODS(OutlineBrowserButton, CControl)

private:
    std::string title_;
    VSTGUI::CColor frameColor_;
    bool hovered_ = false;
};

// ------------------------------------------------------------------------------
// OutlineButton — CView variant (abstract; override onClick())
// ------------------------------------------------------------------------------
class OutlineButton : public VSTGUI::CView {
public:
    // Dark-theme convenience: only the frame color varies (hover/font keep the
    // dark defaults). Covers the common Presets/Save/Expand buttons.
    OutlineButton(const VSTGUI::CRect& size, std::string title,
                  VSTGUI::CColor frameColor = VSTGUI::CColor(64, 64, 72))
        : CView(size)
        , title_(std::move(title)) {
        colors_.frame = frameColor;
    }

    // Full control over all three colors (e.g. Iterum's light theme).
    OutlineButton(const VSTGUI::CRect& size, std::string title,
                  const OutlineButtonColors& colors)
        : CView(size)
        , title_(std::move(title))
        , colors_(colors) {}

    void setTitle(std::string title) {
        if (title != title_) {
            title_ = std::move(title);
            invalid();
        }
    }

    void draw(VSTGUI::CDrawContext* context) override {
        drawOutlineButton(context, getViewSize(), VSTGUI::UTF8String(title_),
                          hovered_, colors_);
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
    OutlineButtonColors colors_;
    bool hovered_ = false;
};

// ------------------------------------------------------------------------------
// OutlineActionButton — OutlineButton driven by a std::function callback
// ------------------------------------------------------------------------------
class OutlineActionButton : public OutlineButton {
public:
    OutlineActionButton(const VSTGUI::CRect& size, std::string title)
        : OutlineButton(size, std::move(title)) {}
    OutlineActionButton(const VSTGUI::CRect& size, std::string title,
                        std::function<void()> action)
        : OutlineButton(size, std::move(title)), action_(std::move(action)) {}
    OutlineActionButton(const VSTGUI::CRect& size, std::string title,
                        const OutlineButtonColors& colors,
                        std::function<void()> action)
        : OutlineButton(size, std::move(title), colors), action_(std::move(action)) {}

    void setAction(std::function<void()> action) { action_ = std::move(action); }

protected:
    void onClick() override { if (action_) action_(); }

private:
    std::function<void()> action_;
};

} // namespace Krate::Plugins
