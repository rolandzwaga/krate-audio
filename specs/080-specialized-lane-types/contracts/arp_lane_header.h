// ==============================================================================
// ArpLaneHeader Contract (080-specialized-lane-types)
// ==============================================================================
// Non-CView helper struct owned by composition in each lane class.
// Encapsulates collapse toggle triangle, accent-colored name label,
// and length dropdown rendering/interaction.
//
// Location: plugins/shared/src/ui/arp_lane_header.h
// ==============================================================================

#pragma once

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Krate::Plugins {

class ArpLaneHeader {
public:
    // Constants
    static constexpr float kHeight = 16.0f;
    static constexpr float kCollapseTriangleSize = 8.0f;
    static constexpr float kLengthDropdownX = 80.0f;
    static constexpr float kLengthDropdownWidth = 36.0f;

    // Configuration
    void setLaneName(const std::string& name);
    void setAccentColor(const VSTGUI::CColor& color);
    void setNumSteps(int steps);
    void setLengthParamId(uint32_t paramId);

    // Callbacks
    void setCollapseCallback(std::function<void()> cb);
    void setLengthParamCallback(std::function<void(uint32_t, float)> cb);

    // State
    void setCollapsed(bool collapsed);
    [[nodiscard]] bool isCollapsed() const;

    // Rendering: draws the header into the given rect
    void draw(VSTGUI::CDrawContext* context, const VSTGUI::CRect& headerRect);

    // Interaction: returns true if the click was handled (in header area)
    bool handleMouseDown(const VSTGUI::CPoint& where,
                         const VSTGUI::CRect& headerRect,
                         VSTGUI::CFrame* frame);

    [[nodiscard]] float getHeight() const { return kHeight; }
};

} // namespace Krate::Plugins
