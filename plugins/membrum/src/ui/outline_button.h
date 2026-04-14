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

} // namespace Membrum::UI
