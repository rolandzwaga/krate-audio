#pragma once

// ==============================================================================
// ADSRExpandedOverlayView - Full-width ADSR envelope editor overlay for Ruinae
// ==============================================================================
// Modal overlay that displays a large ADSRDisplay spanning the full plugin width.
// Supports configurable title and colors for Amp/Filter/Mod envelopes.
// Opened via expand buttons in each compact ADSR section, closed via close
// button or clicking the backdrop.
//
// Based on Innexus version, extended with:
// - setTitle() for per-envelope labeling
// - setColors() for per-envelope color theming
// ==============================================================================

#include "ui/adsr_display.h"
#include "ui/outline_button.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/ctextlabel.h"

namespace Ruinae {

class ADSRExpandedOverlayView : public VSTGUI::CViewContainer,
                                 public VSTGUI::IControlListener {
public:
    using CloseCallback = std::function<void()>;

    ADSRExpandedOverlayView(const VSTGUI::CRect& frameSize)
        : CViewContainer(frameSize)
    {
        setTransparency(true);
        setVisible(false);

        createChildViews(frameSize);
    }

    void open()
    {
        setVisible(true);
        invalid();
    }

    void close()
    {
        setVisible(false);
        if (closeCallback_)
            closeCallback_();
    }

    bool isOpen() const { return isVisible(); }

    Krate::Plugins::ADSRDisplay* getDisplay() { return adsrDisplay_; }

    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

    void setTitle(const char* title)
    {
        if (titleLabel_)
            titleLabel_->setText(title);
    }

    void setColors(VSTGUI::CColor stroke, VSTGUI::CColor fill,
                   VSTGUI::CColor controlPoint)
    {
        if (adsrDisplay_) {
            adsrDisplay_->setStrokeColor(stroke);
            adsrDisplay_->setFillColor(fill);
            adsrDisplay_->setControlPointColor(controlPoint);
        }
    }

    // IControlListener — handle close button
    void valueChanged(VSTGUI::CControl* control) override
    {
        if (control->getTag() == kCloseTag)
            close();
    }

    // Draw semi-transparent backdrop
    void drawBackgroundRect(VSTGUI::CDrawContext* context,
                            const VSTGUI::CRect& /*rect*/) override
    {
        auto r = getViewSize();
        context->setFillColor(VSTGUI::CColor(0, 0, 0, 180));
        context->drawRect(r, VSTGUI::kDrawFilled);

        // Draw panel background
        context->setFillColor(VSTGUI::CColor(26, 26, 46, 255)); // bg-primary
        context->drawRect(panelRect_, VSTGUI::kDrawFilled);

        // Panel border
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (path) {
            path->addRoundRect(panelRect_, 4.0);
            context->setFrameColor(VSTGUI::CColor(85, 85, 102)); // text-dim
            context->setLineWidth(1.0);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
        }
    }

    // Click on backdrop (outside panel) closes overlay
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        // First let child views handle
        auto result = CViewContainer::onMouseDown(where, buttons);
        if (result != VSTGUI::kMouseEventNotHandled)
            return result;

        // Click outside panel closes
        if (!panelRect_.pointInside(where)) {
            close();
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }

        return VSTGUI::kMouseEventHandled;
    }

private:
    static constexpr int32_t kCloseTag = 9999;
    static constexpr float kPanelMarginX = 10.0f;
    static constexpr float kPanelMarginTop = 160.0f;
    static constexpr float kPanelHeight = 330.0f;
    static constexpr float kDisplayMargin = 15.0f;
    static constexpr float kTitleBarHeight = 30.0f;

    void createChildViews(const VSTGUI::CRect& frameSize)
    {
        float panelW = frameSize.getWidth() - 2 * kPanelMarginX;
        float panelX = kPanelMarginX;
        float panelY = kPanelMarginTop;
        panelRect_ = VSTGUI::CRect(panelX, panelY,
                                    panelX + panelW, panelY + kPanelHeight);

        // Title label
        VSTGUI::CRect titleRect(panelX + kDisplayMargin, panelY + 6,
                                 panelX + 200, panelY + 24);
        titleLabel_ = new VSTGUI::CTextLabel(titleRect);
        titleLabel_->setTransparency(true);
        titleLabel_->setText("ADSR Envelope");
        titleLabel_->setFontColor(VSTGUI::CColor(224, 224, 240)); // text-primary
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12, VSTGUI::kBoldFace);
        titleLabel_->setFont(font);
        titleLabel_->setHoriAlign(VSTGUI::kLeftText);
        addView(titleLabel_);

        // Close button (top-right of panel)
        VSTGUI::CRect closeRect(panelX + panelW - 55, panelY + 5,
                                 panelX + panelW - 10, panelY + 25);
        auto* closeBtn = new Krate::Plugins::OutlineBrowserButton(
            closeRect, this, kCloseTag, "Close",
            VSTGUI::CColor(85, 85, 102));
        addView(closeBtn);

        // Large ADSRDisplay
        float displayX = panelX + kDisplayMargin;
        float displayY = panelY + kTitleBarHeight + 5;
        float displayW = panelW - 2 * kDisplayMargin;
        float displayH = kPanelHeight - kTitleBarHeight - kDisplayMargin - 5;

        VSTGUI::CRect displayRect(displayX, displayY,
                                   displayX + displayW, displayY + displayH);
        adsrDisplay_ = new Krate::Plugins::ADSRDisplay(displayRect, nullptr, -1);
        addView(adsrDisplay_);
    }

    Krate::Plugins::ADSRDisplay* adsrDisplay_ = nullptr;
    VSTGUI::CTextLabel* titleLabel_ = nullptr;
    VSTGUI::CRect panelRect_;
    CloseCallback closeCallback_;
};

} // namespace Ruinae
