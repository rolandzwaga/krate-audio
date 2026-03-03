// ==============================================================================
// ArpLaneContainer - Contract / API Surface
// ==============================================================================
// This file documents the public API contract for ArpLaneContainer.
// NOT a compilable header -- a design contract for implementation guidance.
// ==============================================================================

#pragma once

namespace Krate::Plugins {

/// ArpLaneContainer: A vertically-scrolling container that stacks
/// ArpLaneEditor instances.
///
/// Manages dynamic height, scroll, left-alignment, and mouse routing.
/// Children are added programmatically (NOT from XML).
///
/// Registered as "ArpLaneContainer" ViewCreator.
/// Location: plugins/shared/src/ui/arp_lane_container.h
///
class ArpLaneContainer : public CViewContainer {
public:
    // -- Construction --
    explicit ArpLaneContainer(const CRect& size);
    ArpLaneContainer(const ArpLaneContainer& other);

    // -- Viewport --
    void setViewportHeight(float height);
    float getViewportHeight() const;

    // -- Lane Management --
    void addLane(ArpLaneEditor* lane);
    void removeLane(ArpLaneEditor* lane);
    size_t getLaneCount() const;
    ArpLaneEditor* getLane(size_t index) const;

    // -- Layout --
    void recalculateLayout();
    float getTotalContentHeight() const;

    // -- Scroll --
    float getScrollOffset() const;
    void setScrollOffset(float offset);
    float getMaxScrollOffset() const;

    // -- CViewContainer Overrides --
    void drawBackgroundRect(CDrawContext* context, const CRect& rect) override;
    bool onWheel(const CPoint& where, const CMouseWheelAxis& axis,
                 const float& distance, const CButtonState& buttons) override;

    CLASS_METHODS(ArpLaneContainer, CViewContainer)
};

// ViewCreator: "ArpLaneContainer"
// Attributes:
//   viewport-height  (float)

} // namespace Krate::Plugins
