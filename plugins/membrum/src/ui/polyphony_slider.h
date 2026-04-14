#pragma once

// ==============================================================================
// PolyphonySliderView -- horizontal integer slider for kMaxPolyphonyId (4..16)
// ==============================================================================
// Replaces the stock CSlider (which requires a handle bitmap we don't ship).
// Draws a rounded bg rect, a filled fill-bar proportional to value, and a
// centered numeric readout "Max Polyphony: N". Click + drag changes the
// value; the framework's IControlListener (typically the VST3Editor) is
// notified via CControl's standard beginEdit/valueChanged/endEdit sequence,
// which is what drives parameter automation and host notification.
// ==============================================================================

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cstdio>

namespace Membrum::UI {

class PolyphonySliderView : public VSTGUI::CControl
{
public:
    PolyphonySliderView(const VSTGUI::CRect& size, int32_t tag) noexcept
        : CControl(size, nullptr, tag)
    {
        setMin(0.0f);
        setMax(1.0f);
        setWantsFocus(true);
    }
    PolyphonySliderView(const PolyphonySliderView&) = default;
    ~PolyphonySliderView() override = default;

    PolyphonySliderView& operator=(const PolyphonySliderView&) = delete;

    VSTGUI::CBaseObject* newCopy() const override
    {
        return new PolyphonySliderView(*this);
    }

    void draw(VSTGUI::CDrawContext* ctx) override
    {
        if (!ctx)
            return;

        const auto& rect = getViewSize();
        constexpr double kCornerRadius = 2.0;

        // Background.
        if (auto path = VSTGUI::owned(ctx->createGraphicsPath()))
        {
            path->addRoundRect(rect, kCornerRadius);
            ctx->setFillColor(kBgColor);
            ctx->drawGraphicsPath(path.get(), VSTGUI::CDrawContext::kPathFilled);
        }

        // Fill proportional to normalized value.
        const double n = std::clamp(
            static_cast<double>(getValueNormalized()), 0.0, 1.0);
        if (n > 0.0)
        {
            VSTGUI::CRect fill = rect;
            fill.right = fill.left + rect.getWidth() * n;
            if (auto path = VSTGUI::owned(ctx->createGraphicsPath()))
            {
                path->addRoundRect(fill, kCornerRadius);
                ctx->setFillColor(kFillColor);
                ctx->drawGraphicsPath(
                    path.get(), VSTGUI::CDrawContext::kPathFilled);
            }
        }

        // Frame.
        if (auto path = VSTGUI::owned(ctx->createGraphicsPath()))
        {
            path->addRoundRect(rect, kCornerRadius);
            ctx->setFrameColor(kFrameColor);
            ctx->setLineWidth(1.0);
            ctx->drawGraphicsPath(path.get(), VSTGUI::CDrawContext::kPathStroked);
        }

        // Numeric readout centred on the bar.
        const int voices = voiceCountFromNormalized(n);
        char buf[32] = {};
        std::snprintf(buf, sizeof(buf), "Max Polyphony: %d", voices);
        ctx->setFont(VSTGUI::kNormalFontSmall);
        ctx->setFontColor(kTextColor);
        ctx->drawString(buf, rect, VSTGUI::kCenterText, true);

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override
    {
        if (!buttons.isLeftButton())
            return VSTGUI::kMouseEventNotHandled;
        beginEdit();
        applyMouse(where);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override
    {
        if (!buttons.isLeftButton() || !isEditing())
            return VSTGUI::kMouseEventNotHandled;
        applyMouse(where);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override
    {
        if (!isEditing())
            return VSTGUI::kMouseEventNotHandled;
        applyMouse(where);
        endEdit();
        return VSTGUI::kMouseEventHandled;
    }

private:
    static constexpr VSTGUI::CColor kBgColor    = { 18,  18,  22, 255 };
    static constexpr VSTGUI::CColor kFillColor  = { 70, 110, 160, 200 };
    static constexpr VSTGUI::CColor kFrameColor = { 90, 110, 140, 255 };
    static constexpr VSTGUI::CColor kTextColor  = {220, 220, 225, 255 };

    static int voiceCountFromNormalized(double n) noexcept
    {
        // Parameter is RangeParameter [4, 16]; normalized -> plain voices.
        const int voices = static_cast<int>(std::lround(4.0 + n * 12.0));
        return std::clamp(voices, 4, 16);
    }

    void applyMouse(const VSTGUI::CPoint& where)
    {
        const auto& rect = getViewSize();
        const double w = rect.getWidth();
        if (w <= 0.0)
            return;
        const double x = std::clamp(where.x - rect.left, 0.0, w);
        const float n = static_cast<float>(x / w);
        // Quantise to integer voice steps so the fill-bar lines up with the
        // numeric readout and parameter persistence.
        const int voices = voiceCountFromNormalized(n);
        const float quant = static_cast<float>(voices - 4) / 12.0f;
        if (std::abs(quant - getValueNormalized()) > 1e-6f)
        {
            setValueNormalized(quant);
            valueChanged();
            invalid();
        }
    }
};

} // namespace Membrum::UI
