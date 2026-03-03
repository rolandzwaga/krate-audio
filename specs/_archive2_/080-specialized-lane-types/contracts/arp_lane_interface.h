// ==============================================================================
// IArpLane Interface Contract (080-specialized-lane-types)
// ==============================================================================
// Lightweight pure virtual interface for polymorphic lane management.
// All concrete lane classes (ArpLaneEditor, ArpModifierLane, ArpConditionLane)
// implement this interface. ArpLaneContainer holds std::vector<IArpLane*>.
//
// Location: plugins/shared/src/ui/arp_lane.h
// ==============================================================================

#pragma once

#include "vstgui/lib/cview.h"
#include <cstdint>
#include <functional>

namespace Krate::Plugins {

class IArpLane {
public:
    virtual ~IArpLane() = default;

    /// Get the underlying CView for this lane (for addView/removeView).
    virtual VSTGUI::CView* getView() = 0;

    /// Get the height of this lane when expanded (header + body).
    [[nodiscard]] virtual float getExpandedHeight() const = 0;

    /// Get the height of this lane when collapsed (header only = 16px).
    [[nodiscard]] virtual float getCollapsedHeight() const = 0;

    /// Whether this lane is currently collapsed.
    [[nodiscard]] virtual bool isCollapsed() const = 0;

    /// Set the collapsed state. Fires collapseCallback if state changes.
    virtual void setCollapsed(bool collapsed) = 0;

    /// Set the current playhead step (-1 = no playhead).
    virtual void setPlayheadStep(int32_t step) = 0;

    /// Set the active step count (2-32).
    virtual void setLength(int32_t length) = 0;

    /// Register a callback for collapse/expand state changes.
    /// The container uses this to trigger relayout.
    virtual void setCollapseCallback(std::function<void()> cb) = 0;
};

} // namespace Krate::Plugins
