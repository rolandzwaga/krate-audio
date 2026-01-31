#pragma once

// ==============================================================================
// DynamicNodeSelector Custom View
// ==============================================================================
// A CSegmentButton that dynamically shows/hides segments based on ActiveNodes.
//
// When ActiveNodes = 2: Shows segments A, B
// When ActiveNodes = 3: Shows segments A, B, C
// When ActiveNodes = 4: Shows segments A, B, C, D
//
// Uses IDependent pattern to watch the ActiveNodes parameter and rebuild
// segments automatically when it changes. Also handles value clamping when
// active nodes decrease (e.g., if D was selected and we go to 3 nodes, select C).
//
// Reference: CSegmentButton source in extern/vst3sdk/vstgui4/vstgui/lib/controls/csegmentbutton.h
// ==============================================================================

#include "pluginterfaces/base/ftypes.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/controls/csegmentbutton.h"

#include <atomic>

namespace Disrumpo {

/// @brief CSegmentButton that dynamically adjusts segments based on ActiveNodes parameter.
///
/// This control extends CSegmentButton to watch a "controlling" parameter (ActiveNodes)
/// and rebuild its segments when that parameter changes. It maintains proper value mapping
/// with the "value" parameter (SelectedNode).
///
/// Key behaviors:
/// - When ActiveNodes changes, segments are rebuilt (A,B / A,B,C / A,B,C,D)
/// - When segments decrease, selection is clamped to valid range
/// - Uses IDependent for thread-safe parameter watching
class DynamicNodeSelector : public VSTGUI::CSegmentButton, public Steinberg::FObject {
public:
    /// @brief Construct a DynamicNodeSelector.
    /// @param size The control's rectangle
    /// @param controller The edit controller for parameter access
    /// @param activeNodesParamId Parameter ID for ActiveNodes (controls segment count)
    /// @param selectedNodeParamId Parameter ID for SelectedNode (the control's value)
    DynamicNodeSelector(
        const VSTGUI::CRect& size,
        Steinberg::Vst::EditControllerEx1* controller,
        Steinberg::Vst::ParamID activeNodesParamId,
        Steinberg::Vst::ParamID selectedNodeParamId);

    ~DynamicNodeSelector() override;

    // =========================================================================
    // Lifecycle Management
    // =========================================================================

    /// @brief Deactivate the controller before destruction.
    /// Must be called in willClose() before destroying the control.
    void deactivate();

    // =========================================================================
    // CSegmentButton Overrides
    // =========================================================================

    /// @brief Custom drawRect to render each segment with its node color.
    /// Note: VSTGUI CViewContainer calls drawRect() directly, NOT draw()!
    void drawRect(VSTGUI::CDrawContext* context, const VSTGUI::CRect& updateRect) override;

    /// @brief Override to add debug logging and ensure click handling works.
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;

    /// @brief Enable high contrast mode (Spec 012 FR-025a)
    /// Increases segment borders and uses solid fills.
    void setHighContrastMode(bool enabled) { highContrastEnabled_ = enabled; }

    // =========================================================================
    // IDependent Implementation (from FObject)
    // =========================================================================

    /// @brief Called when a watched parameter changes.
    /// Automatically invoked on UI thread via deferred updates.
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown,
                           Steinberg::int32 message) override;

    // Required for FObject
    OBJ_METHODS(DynamicNodeSelector, FObject)

private:
    /// @brief Rebuild segments based on active node count.
    /// @param activeCount Number of active nodes (2, 3, or 4)
    void rebuildSegments(int activeCount);

    /// @brief Get the current active node count from the parameter.
    int getActiveNodeCount() const;

    /// @brief Clamp the selected node to valid range after segment count decreases.
    void clampSelectedNode();

    Steinberg::Vst::EditControllerEx1* controller_ = nullptr;
    Steinberg::Vst::Parameter* activeNodesParam_ = nullptr;
    Steinberg::Vst::ParamID selectedNodeParamId_ = 0;

    std::atomic<bool> isActive_{true};
    int currentSegmentCount_ = 0;  // Track to avoid unnecessary rebuilds

    // High contrast mode (Spec 012 FR-025a)
    bool highContrastEnabled_ = false;
};

} // namespace Disrumpo
