// ==============================================================================
// ArpLaneEditor - Contract / API Surface
// ==============================================================================
// This file documents the public API contract for ArpLaneEditor.
// NOT a compilable header -- a design contract for implementation guidance.
// ==============================================================================

#pragma once

namespace Krate::Plugins {

enum class ArpLaneType { kVelocity, kGate, kPitch, kRatchet };

/// ArpLaneEditor: A StepPatternEditor subclass for arpeggiator lane editing.
///
/// Adds: collapsible header, lane type/color, display range labels,
///       per-lane playhead, miniature bar preview when collapsed.
///
/// Registered as "ArpLaneEditor" ViewCreator.
/// Location: plugins/shared/src/ui/arp_lane_editor.h
///
class ArpLaneEditor : public StepPatternEditor {
public:
    // -- Constants --
    static constexpr float kHeaderHeight = 16.0f;
    static constexpr float kMiniPreviewHeight = 12.0f;

    // -- Construction --
    ArpLaneEditor(const CRect& size, IControlListener* listener, int32_t tag);
    ArpLaneEditor(const ArpLaneEditor& other);

    // -- Lane Configuration --
    void setLaneType(ArpLaneType type);
    ArpLaneType getLaneType() const;

    void setLaneName(const std::string& name);
    const std::string& getLaneName() const;

    void setAccentColor(const CColor& color);
    CColor getAccentColor() const;

    void setDisplayRange(float min, float max,
                         const std::string& topLabel,
                         const std::string& bottomLabel);

    // -- Parameter Binding --
    void setLengthParamId(uint32_t paramId);
    void setLengthParamCallback(std::function<void(uint32_t, float)> cb);  // Called when user selects length from dropdown
    void setPlayheadParamId(uint32_t paramId);

    // -- Collapse/Expand --
    void setCollapsed(bool collapsed);
    bool isCollapsed() const;
    void setCollapseCallback(std::function<void()> cb);

    // -- Height Queries --
    float getExpandedHeight() const;   // kHeaderHeight + bodyHeight
    float getCollapsedHeight() const;  // kHeaderHeight

    // -- CControl Overrides --
    void draw(CDrawContext* context) override;
    CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override;

    CLASS_METHODS(ArpLaneEditor, StepPatternEditor)
};

// ViewCreator: "ArpLaneEditor"
// Attributes:
//   lane-type        (list: velocity/gate/pitch/ratchet)
//   accent-color     (color)
//   lane-name        (string)
//   step-level-base-param-id  (integer as string)
//   length-param-id  (integer as string)
//   playhead-param-id (integer as string)

} // namespace Krate::Plugins
