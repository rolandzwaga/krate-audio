#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cfont.h"

#include <functional>
#include <string>
#include <utility>

namespace Membrum::UI {

class OutlineButton : public VSTGUI::CView {
public:
    OutlineButton(const VSTGUI::CRect& size, std::string title)
        : CView(size), title_(std::move(title)) {}

    void setTitle(std::string title) {
        if (title != title_) {
            title_ = std::move(title);
            invalid();
        }
    }

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
        auto r = getViewSize();
        r.inset(0.5, 0.5);

        if (auto path = VSTGUI::owned(context->createGraphicsPath())) {
            constexpr double kRadius = 3.0;
            path->addRoundRect(r, kRadius);

            if (hovered_) {
                context->setFillColor(VSTGUI::CColor(255, 255, 255, 20));
                context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
            }

            context->setFrameColor(VSTGUI::CColor(64, 64, 72));
            context->setLineWidth(1.0);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
        }

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>(*VSTGUI::kNormalFontSmaller);
        context->setFont(font);
        context->setFontColor(VSTGUI::CColor(192, 192, 192));
        context->drawString(VSTGUI::UTF8String(title_), getViewSize(), VSTGUI::kCenterText);

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseEntered(VSTGUI::CPoint&, const VSTGUI::CButtonState&) override {
        hovered_ = true;
        if (auto* frame = getFrame()) frame->setCursor(VSTGUI::kCursorHand);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(VSTGUI::CPoint&, const VSTGUI::CButtonState&) override {
        hovered_ = false;
        if (auto* frame = getFrame()) frame->setCursor(VSTGUI::kCursorDefault);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint&, const VSTGUI::CButtonState& buttons) override {
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
    bool hovered_ = false;
};

class OutlineActionButton : public OutlineButton {
public:
    OutlineActionButton(const VSTGUI::CRect& size, std::string title)
        : OutlineButton(size, std::move(title)) {}
    OutlineActionButton(const VSTGUI::CRect& size, std::string title, std::function<void()> action)
        : OutlineButton(size, std::move(title)), action_(std::move(action)) {}
    void setAction(std::function<void()> action) { action_ = std::move(action); }
protected:
    void onClick() override { if (action_) action_(); }
private:
    std::function<void()> action_;
};

// Small icon-only action button that draws the IEC-style "expand" glyph
// (two diagonal arrows radiating to opposite corners). Click fires an
// action callback; no CControl tag / VST parameter binding involved. Used
// for one-shot UI actions like opening an expanded overlay editor.
class IconExpandActionButton : public VSTGUI::CView {
public:
    explicit IconExpandActionButton(const VSTGUI::CRect& size,
                                    std::function<void()> action = {})
        : CView(size), action_(std::move(action)) {}

    void setAction(std::function<void()> action) { action_ = std::move(action); }
    void setIconColor(VSTGUI::CColor color) { iconColor_ = color; invalid(); }
    void setHoverColor(VSTGUI::CColor color) { hoverColor_ = color; }

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
        const auto rect = getViewSize();

        const VSTGUI::CColor color = hovered_ ? hoverColor_ : iconColor_;

        // Two diagonal arrows to opposite corners; matches Krate::Plugins::
        // ToggleButton's IconStyle::kExpand so the glyph reads the same
        // everywhere in the plugin family.
        const double viewW = rect.getWidth();
        const double viewH = rect.getHeight();
        const double dim   = std::min(viewW, viewH) * 0.85;
        const double half  = dim * 0.5;
        const double cx    = rect.left + viewW * 0.5;
        const double cy    = rect.top  + viewH * 0.5;
        const double innerGap = dim * 0.18;
        const double wingLen  = dim * 0.30;

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) { setDirty(false); return; }

        {
            VSTGUI::CPoint inner(cx - innerGap, cy - innerGap);
            VSTGUI::CPoint outer(cx - half,     cy - half);
            path->beginSubpath(inner);
            path->addLine(outer);
            path->beginSubpath(outer);
            path->addLine(VSTGUI::CPoint(outer.x + wingLen, outer.y));
            path->beginSubpath(outer);
            path->addLine(VSTGUI::CPoint(outer.x, outer.y + wingLen));
        }
        {
            VSTGUI::CPoint inner(cx + innerGap, cy + innerGap);
            VSTGUI::CPoint outer(cx + half,     cy + half);
            path->beginSubpath(inner);
            path->addLine(outer);
            path->beginSubpath(outer);
            path->addLine(VSTGUI::CPoint(outer.x - wingLen, outer.y));
            path->beginSubpath(outer);
            path->addLine(VSTGUI::CPoint(outer.x, outer.y - wingLen));
        }

        context->setFrameColor(color);
        context->setLineWidth(1.5);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseEntered(VSTGUI::CPoint&,
                                             const VSTGUI::CButtonState&) override {
        hovered_ = true;
        if (auto* frame = getFrame()) frame->setCursor(VSTGUI::kCursorHand);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }
    VSTGUI::CMouseEventResult onMouseExited(VSTGUI::CPoint&,
                                            const VSTGUI::CButtonState&) override {
        hovered_ = false;
        if (auto* frame = getFrame()) frame->setCursor(VSTGUI::kCursorDefault);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint&,
                                          const VSTGUI::CButtonState& buttons) override {
        if (buttons.isLeftButton()) {
            if (action_) action_();
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }
        return VSTGUI::kMouseEventNotHandled;
    }

private:
    std::function<void()> action_;
    bool hovered_ = false;
    VSTGUI::CColor iconColor_  = VSTGUI::CColor(170, 170, 170, 255);
    VSTGUI::CColor hoverColor_ = VSTGUI::CColor(0,   188, 212, 255); // accent
};

} // namespace Membrum::UI
