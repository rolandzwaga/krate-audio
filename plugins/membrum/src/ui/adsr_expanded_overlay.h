#pragma once

// ==============================================================================
// ADSRExpandedOverlayView - Full-width ADSR envelope editor overlay for Membrum
// ==============================================================================
// Modal overlay that hosts a large shared-UI ADSRDisplay spanning the plugin
// width. Opened via the IconExpandActionButton in the inline Tone Shaper
// section, closed via the Close button or by clicking the backdrop outside
// the panel. The large display inside the overlay is wired to the same
// filter-envelope parameters as the inline one (see controller verifyView).
//
// Modeled on the Ruinae / Innexus pattern but kept Membrum-local so we can
// use Membrum's OutlineActionButton for the close control.
// ==============================================================================

#include "ui/adsr_display.h"
#include "outline_button.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/ctextlabel.h"

#include <functional>

namespace Membrum::UI {

class ADSRExpandedOverlayView : public VSTGUI::CViewContainer
{
public:
    using CloseCallback = std::function<void()>;

    explicit ADSRExpandedOverlayView(const VSTGUI::CRect& frameSize)
        : CViewContainer(frameSize)
    {
        setTransparency(true);
        setVisible(false);
        createChildViews(frameSize);
    }

    void open()  { setVisible(true);  invalid(); }
    void close() { setVisible(false); if (closeCallback_) closeCallback_(); }
    [[nodiscard]] bool isOpen() const { return isVisible(); }

    Krate::Plugins::ADSRDisplay* getDisplay() { return adsrDisplay_; }

    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

    void setTitle(const char* title)
    {
        if (titleLabel_) titleLabel_->setText(title);
    }

    void setColors(VSTGUI::CColor stroke,
                   VSTGUI::CColor fill,
                   VSTGUI::CColor controlPoint)
    {
        if (adsrDisplay_)
        {
            adsrDisplay_->setStrokeColor(stroke);
            adsrDisplay_->setFillColor(fill);
            adsrDisplay_->setControlPointColor(controlPoint);
        }
    }

    // Dimmed backdrop + rounded panel frame.
    void drawBackgroundRect(VSTGUI::CDrawContext* context,
                            const VSTGUI::CRect& /*rect*/) override
    {
        auto r = getViewSize();
        context->setFillColor(VSTGUI::CColor(0, 0, 0, 180));
        context->drawRect(r, VSTGUI::kDrawFilled);

        context->setFillColor(VSTGUI::CColor(26, 26, 46, 255)); // bg-primary
        context->drawRect(panelRect_, VSTGUI::kDrawFilled);

        if (auto path = VSTGUI::owned(context->createGraphicsPath()))
        {
            path->addRoundRect(panelRect_, 4.0);
            context->setFrameColor(VSTGUI::CColor(85, 85, 102));
            context->setLineWidth(1.0);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
        }
    }

    // Click outside the panel closes the overlay.
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        auto result = CViewContainer::onMouseDown(where, buttons);
        if (result != VSTGUI::kMouseEventNotHandled)
            return result;

        if (!panelRect_.pointInside(where))
        {
            close();
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }
        return VSTGUI::kMouseEventHandled;
    }

private:
    static constexpr float kPanelMarginX   = 10.0f;
    static constexpr float kPanelMarginTop = 160.0f;
    static constexpr float kPanelHeight    = 330.0f;
    static constexpr float kDisplayMargin  = 15.0f;
    static constexpr float kTitleBarHeight = 30.0f;

    void createChildViews(const VSTGUI::CRect& frameSize)
    {
        const float panelW = frameSize.getWidth() - 2 * kPanelMarginX;
        const float panelX = kPanelMarginX;
        const float panelY = kPanelMarginTop;
        panelRect_ = VSTGUI::CRect(panelX, panelY,
                                    panelX + panelW, panelY + kPanelHeight);

        VSTGUI::CRect titleRect(panelX + kDisplayMargin, panelY + 6,
                                 panelX + 240, panelY + 24);
        titleLabel_ = new VSTGUI::CTextLabel(titleRect);
        titleLabel_->setTransparency(true);
        titleLabel_->setText("Filter Envelope");
        titleLabel_->setFontColor(VSTGUI::CColor(224, 224, 240));
        titleLabel_->setFont(
            VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12, VSTGUI::kBoldFace));
        titleLabel_->setHoriAlign(VSTGUI::kLeftText);
        addView(titleLabel_);

        VSTGUI::CRect closeRect(panelX + panelW - 65, panelY + 5,
                                 panelX + panelW - 10, panelY + 25);
        auto* closeBtn = new OutlineActionButton(closeRect, "Close",
            [this]() { close(); });
        addView(closeBtn);

        const float displayX = panelX + kDisplayMargin;
        const float displayY = panelY + kTitleBarHeight + 5;
        const float displayW = panelW - 2 * kDisplayMargin;
        const float displayH = kPanelHeight - kTitleBarHeight - kDisplayMargin - 5;

        VSTGUI::CRect displayRect(displayX, displayY,
                                   displayX + displayW, displayY + displayH);
        adsrDisplay_ = new Krate::Plugins::ADSRDisplay(displayRect, nullptr, -1);
        addView(adsrDisplay_);
    }

    Krate::Plugins::ADSRDisplay* adsrDisplay_ = nullptr;
    VSTGUI::CTextLabel*          titleLabel_  = nullptr;
    VSTGUI::CRect                panelRect_;
    CloseCallback                closeCallback_;
};

} // namespace Membrum::UI
