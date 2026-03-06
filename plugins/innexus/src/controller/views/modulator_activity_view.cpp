// ==============================================================================
// ModulatorActivityView Implementation
// ==============================================================================
// FR-038, FR-040: Animated modulation indicator
// When active: filled circle in cyan with alpha oscillating based on phase
// When inactive: hollow circle in gray
// ==============================================================================

#include "modulator_activity_view.h"

#include "vstgui/lib/cdrawcontext.h"

#include <cmath>
#include <numbers>

namespace Innexus {

ModulatorActivityView::ModulatorActivityView(const VSTGUI::CRect& size)
    : CView(size)
{
}

void ModulatorActivityView::updateData(float phase, bool active)
{
    phase_ = phase;
    active_ = active;
    invalid();
}

void ModulatorActivityView::draw(VSTGUI::CDrawContext* context)
{
    auto rect = getViewSize();

    // Background
    context->setFillColor(VSTGUI::CColor(13, 13, 26, 0)); // transparent
    context->drawRect(rect, VSTGUI::kDrawFilled);

    // Calculate circle bounds (centered, using smaller dimension)
    auto diameter = std::min(rect.getWidth(), rect.getHeight()) - 4.0;
    auto cx = rect.left + rect.getWidth() * 0.5;
    auto cy = rect.top + rect.getHeight() * 0.5;
    auto r = diameter * 0.5;

    VSTGUI::CRect circleRect(cx - r, cy - r, cx + r, cy + r);

    if (active_)
    {
        // Pulsing filled circle in cyan
        // Alpha oscillates: 0.4 + 0.6 * sin(phase * 2pi)
        auto alpha = 0.4f + 0.6f * std::sin(phase_ * 2.0f *
                     std::numbers::pi_v<float>);
        auto alphaInt = static_cast<uint8_t>(
            std::clamp(alpha * 255.0f, 0.0f, 255.0f));

        context->setFillColor(VSTGUI::CColor(0x00, 0xBC, 0xD4, alphaInt));
        context->drawEllipse(circleRect, VSTGUI::kDrawFilled);
    }
    else
    {
        // Hollow circle in gray
        context->setFrameColor(VSTGUI::CColor(0x55, 0x55, 0x55));
        context->setLineWidth(1.0);
        context->drawEllipse(circleRect, VSTGUI::kDrawStroked);
    }

    setDirty(false);
}

} // namespace Innexus
