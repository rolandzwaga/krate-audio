// ==============================================================================
// ArpModifierLane Contract (080-specialized-lane-types)
// ==============================================================================
// Custom CView for 4-row toggle dot grid (Rest/Tie/Slide/Accent).
// Implements IArpLane for container integration.
//
// Location: plugins/shared/src/ui/arp_modifier_lane.h
// ==============================================================================

#pragma once

#include "arp_lane.h"          // IArpLane
#include "arp_lane_header.h"   // ArpLaneHeader
#include "color_utils.h"

#include "vstgui/lib/controls/ccontrol.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace Krate::Plugins {

class ArpModifierLane : public VSTGUI::CControl, public IArpLane {
public:
    // Constants
    static constexpr int kMaxSteps = 32;
    static constexpr int kMinSteps = 2;
    static constexpr int kRowCount = 4;
    static constexpr float kLeftMargin = 40.0f;
    static constexpr float kDotRadius = 4.0f;
    static constexpr float kBodyHeight = 44.0f;
    static constexpr float kRowHeight = 11.0f;  // kBodyHeight / kRowCount

    // Construction
    ArpModifierLane(const VSTGUI::CRect& size,
                    VSTGUI::IControlListener* listener,
                    int32_t tag);

    // Step Flag API
    void setStepFlags(int index, uint8_t flags);
    [[nodiscard]] uint8_t getStepFlags(int index) const;
    void setNumSteps(int count);
    [[nodiscard]] int getNumSteps() const;

    // Configuration
    void setAccentColor(const VSTGUI::CColor& color);
    void setLaneName(const std::string& name);
    void setStepFlagBaseParamId(uint32_t baseId);
    void setLengthParamId(uint32_t paramId);
    void setPlayheadParamId(uint32_t paramId);

    // Parameter Callbacks
    using ParameterCallback = std::function<void(uint32_t paramId, float normalizedValue)>;
    using EditCallback = std::function<void(uint32_t paramId)>;
    void setParameterCallback(ParameterCallback cb);
    void setBeginEditCallback(EditCallback cb);
    void setEndEditCallback(EditCallback cb);

    // IArpLane interface
    VSTGUI::CView* getView() override;
    [[nodiscard]] float getExpandedHeight() const override;
    [[nodiscard]] float getCollapsedHeight() const override;
    [[nodiscard]] bool isCollapsed() const override;
    void setCollapsed(bool collapsed) override;
    void setPlayheadStep(int32_t step) override;
    void setLength(int32_t length) override;
    void setCollapseCallback(std::function<void()> cb) override;

    // CControl overrides
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                          const VSTGUI::CButtonState& buttons) override;

    CLASS_METHODS(ArpModifierLane, CControl)

private:
    ArpLaneHeader header_;
    std::array<uint8_t, kMaxSteps> stepFlags_{};
    int numSteps_ = 16;
    int playheadStep_ = -1;
    VSTGUI::CColor accentColor_{192, 112, 124, 255};
    uint32_t stepFlagBaseParamId_ = 0;
    float expandedHeight_ = kBodyHeight + 16.0f;  // 44.0f + ArpLaneHeader::kHeight = 60.0f
    ParameterCallback paramCallback_;
    EditCallback beginEditCallback_;
    EditCallback endEditCallback_;
};

// ViewCreator Registration
struct ArpModifierLaneCreator : VSTGUI::ViewCreatorAdapter { /* ... */ };
inline ArpModifierLaneCreator gArpModifierLaneCreator;

} // namespace Krate::Plugins
