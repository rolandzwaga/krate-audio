// ==============================================================================
// EvolutionPositionView Implementation
// ==============================================================================
// FR-036: Horizontal track with playhead line
// FR-050: Ghost marker tracks manual morph position
// ==============================================================================

#include "evolution_position_view.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>
#include <cmath>

namespace Innexus {

EvolutionPositionView::EvolutionPositionView(const VSTGUI::CRect& size)
    : CView(size)
{
}

void EvolutionPositionView::updateData(const DisplayData& data, bool evolutionActive)
{
    position_ = data.evolutionPosition;
    manualPosition_ = data.manualMorphPosition;
    showGhost_ = evolutionActive;
    invalid();
}

void EvolutionPositionView::draw(VSTGUI::CDrawContext* context)
{
    auto rect = getViewSize();
    constexpr float kPadding = 4.0f;
    constexpr float kTrackHeight = 4.0f;
    constexpr float kPlayheadWidth = 2.0f;

    // Background
    context->setFillColor(VSTGUI::CColor(13, 13, 26));
    context->drawRect(rect, VSTGUI::kDrawFilled);

    // Track: dark gray rounded rectangle centered vertically
    auto trackY = rect.top + (rect.getHeight() - kTrackHeight) * 0.5;
    VSTGUI::CRect trackRect(
        rect.left + kPadding, trackY,
        rect.right - kPadding, trackY + kTrackHeight);
    context->setFillColor(VSTGUI::CColor(0x33, 0x33, 0x33));
    context->drawRect(trackRect, VSTGUI::kDrawFilled);

    auto usableWidth = rect.getWidth() - 2.0 * kPadding;

    // Ghost marker (30% opacity cyan) when evolution is active
    if (showGhost_)
    {
        auto ghostX = rect.left + kPadding +
                       static_cast<double>(manualPosition_) * usableWidth;
        VSTGUI::CRect ghostLine(
            ghostX - kPlayheadWidth * 0.5, rect.top + 1,
            ghostX + kPlayheadWidth * 0.5, rect.bottom - 1);
        context->setFillColor(VSTGUI::CColor(0x00, 0xBC, 0xD4, 76)); // ~30% of 255
        context->drawRect(ghostLine, VSTGUI::kDrawFilled);
    }

    // Playhead: cyan vertical line at position
    {
        auto playheadX = rect.left + kPadding +
                          static_cast<double>(position_) * usableWidth;
        VSTGUI::CRect playheadLine(
            playheadX - kPlayheadWidth * 0.5, rect.top + 1,
            playheadX + kPlayheadWidth * 0.5, rect.bottom - 1);
        context->setFillColor(VSTGUI::CColor(0x00, 0xBC, 0xD4)); // cyan #00bcd4
        context->drawRect(playheadLine, VSTGUI::kDrawFilled);
    }

    setDirty(false);
}

} // namespace Innexus
