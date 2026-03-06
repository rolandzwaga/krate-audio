// ==============================================================================
// MemorySlotStatusView Implementation
// ==============================================================================
// FR-029: 8 circles in a horizontal row; occupied = filled accent, empty = hollow dim
// ==============================================================================

#include "memory_slot_status_view.h"

#include "vstgui/lib/cdrawcontext.h"

namespace Innexus {

MemorySlotStatusView::MemorySlotStatusView(const VSTGUI::CRect& size)
    : CView(size)
{
}

void MemorySlotStatusView::updateData(const DisplayData& data)
{
    for (int i = 0; i < 8; ++i)
        slotOccupied_[i] = (data.slotOccupied[i] != 0);
    invalid();
}

void MemorySlotStatusView::draw(VSTGUI::CDrawContext* context)
{
    auto rect = getViewSize();

    // Clear background
    context->setFillColor(VSTGUI::CColor(26, 26, 46));  // bg-primary #1a1a2e
    context->drawRect(rect, VSTGUI::kDrawFilled);

    // Draw 8 circles in a horizontal row
    constexpr float kDiameter = 12.0f;
    constexpr float kSpacing = 3.0f;
    constexpr float kStrokeWidth = 1.5f;

    const VSTGUI::CColor accentColor(0, 188, 212);  // #00bcd4
    const VSTGUI::CColor dimColor(85, 85, 85);       // #555555

    // Center circles vertically within the view
    float totalWidth = 8.0f * kDiameter + 7.0f * kSpacing;
    float startX = static_cast<float>(rect.left) +
                   (static_cast<float>(rect.getWidth()) - totalWidth) * 0.5f;
    float centerY = static_cast<float>(rect.top) +
                    static_cast<float>(rect.getHeight()) * 0.5f;

    context->setLineWidth(kStrokeWidth);

    for (int i = 0; i < 8; ++i)
    {
        float cx = startX + static_cast<float>(i) * (kDiameter + kSpacing) + kDiameter * 0.5f;
        float cy = centerY;
        float radius = kDiameter * 0.5f;

        VSTGUI::CRect circleRect(
            cx - radius, cy - radius,
            cx + radius, cy + radius);

        if (slotOccupied_[i])
        {
            // Filled circle in accent color
            context->setFillColor(accentColor);
            context->drawEllipse(circleRect, VSTGUI::kDrawFilled);
        }
        else
        {
            // Hollow circle (stroke only) in dim color
            context->setFrameColor(dimColor);
            context->drawEllipse(circleRect, VSTGUI::kDrawStroked);
        }
    }

    setDirty(false);
}

} // namespace Innexus
