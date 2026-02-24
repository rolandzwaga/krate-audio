// ==============================================================================
// ArpConditionLane Contract (080-specialized-lane-types)
// ==============================================================================
// Custom CView for per-step condition enum selection with icon display
// and COptionMenu popup. Implements IArpLane for container integration.
//
// Location: plugins/shared/src/ui/arp_condition_lane.h
// ==============================================================================

#pragma once

#include "arp_lane.h"          // IArpLane
#include "arp_lane_header.h"   // ArpLaneHeader
#include "color_utils.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace Krate::Plugins {

class ArpConditionLane : public VSTGUI::CControl, public IArpLane {
public:
    // Constants
    static constexpr int kMaxSteps = 32;
    static constexpr int kMinSteps = 2;
    static constexpr int kConditionCount = 18;
    static constexpr float kBodyHeight = 28.0f;

    // Construction
    ArpConditionLane(const VSTGUI::CRect& size,
                     VSTGUI::IControlListener* listener,
                     int32_t tag);

    // Step Condition API
    void setStepCondition(int index, uint8_t conditionIndex);
    [[nodiscard]] uint8_t getStepCondition(int index) const;
    void setNumSteps(int count);
    [[nodiscard]] int getNumSteps() const;

    // Configuration
    void setAccentColor(const VSTGUI::CColor& color);
    void setLaneName(const std::string& name);
    void setStepConditionBaseParamId(uint32_t baseId);
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
    VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where,
                                           const VSTGUI::CButtonState& buttons) override;

    CLASS_METHODS(ArpConditionLane, CControl)

    // Static lookup tables
    static constexpr const char* kConditionAbbrev[18] = {
        "Alw", "10%", "25%", "50%", "75%", "90%",
        "Ev2", "2:2", "Ev3", "2:3", "3:3",
        "Ev4", "2:4", "3:4", "4:4",
        "1st", "Fill", "!F"
    };

    // Used for COptionMenu popup entries
    static constexpr const char* kConditionFullNames[18] = {
        "Always", "10%", "25%", "50%", "75%", "90%",
        "Every 2", "2nd of 2", "Every 3", "2nd of 3", "3rd of 3",
        "Every 4", "2nd of 4", "3rd of 4", "4th of 4",
        "First", "Fill", "Not Fill"
    };

    // Used for setTooltipText() on hover (longer descriptive strings)
    static constexpr const char* kConditionTooltips[18] = {
        "Always -- Step fires unconditionally",
        "10% -- ~10% probability of firing",
        "25% -- ~25% probability of firing",
        "50% -- ~50% probability of firing",
        "75% -- ~75% probability of firing",
        "90% -- ~90% probability of firing",
        "Every 2 -- Fires on 1st of every 2 loops",
        "2nd of 2 -- Fires on 2nd of every 2 loops",
        "Every 3 -- Fires on 1st of every 3 loops",
        "2nd of 3 -- Fires on 2nd of every 3 loops",
        "3rd of 3 -- Fires on 3rd of every 3 loops",
        "Every 4 -- Fires on 1st of every 4 loops",
        "2nd of 4 -- Fires on 2nd of every 4 loops",
        "3rd of 4 -- Fires on 3rd of every 4 loops",
        "4th of 4 -- Fires on 4th of every 4 loops",
        "First -- Fires only on first loop",
        "Fill -- Fires only when fill mode is active",
        "Not Fill -- Fires only when fill mode is NOT active"
    };

private:
    ArpLaneHeader header_;
    std::array<uint8_t, kMaxSteps> stepConditions_{};
    int numSteps_ = 8;
    int playheadStep_ = -1;
    VSTGUI::CColor accentColor_{124, 144, 176, 255};
    uint32_t stepConditionBaseParamId_ = 0;
    float expandedHeight_ = kBodyHeight + 16.0f;  // 28.0f + ArpLaneHeader::kHeight = 44.0f
    ParameterCallback paramCallback_;
    EditCallback beginEditCallback_;
    EditCallback endEditCallback_;
};

// ViewCreator Registration
struct ArpConditionLaneCreator : VSTGUI::ViewCreatorAdapter { /* ... */ };
inline ArpConditionLaneCreator gArpConditionLaneCreator;

} // namespace Krate::Plugins
