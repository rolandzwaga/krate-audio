#pragma once

// ==============================================================================
// MorphPad Custom View
// ==============================================================================
// FR-001: Custom VSTGUI control for 2D morph position with node visualization.
//
// Features:
// - Node rendering: 12px filled circles with category colors
// - Cursor rendering: 16px open circle, 2px white stroke
// - Connection lines: Gradient from white to node color, opacity by weight
// - Interaction: Click, drag, Shift+drag (fine), Alt+drag (node move), double-click (reset)
// - Mode visualization: 1D Linear, 2D Planar, 2D Radial (grid overlay)
// - Position label: "X: 0.00 Y: 0.00" at bottom-left
//
// Reference: specs/006-morph-ui/spec.md FR-001 through FR-012
// ==============================================================================

#include "dsp/distortion_types.h"

#include "base/source/fobject.h"
#include "pluginterfaces/base/ftypes.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/controls/ccontrol.h"

#include <array>
#include <atomic>
#include <map>

namespace Disrumpo {

// =============================================================================
// MorphPad Listener Interface
// =============================================================================

/// @brief Listener interface for MorphPad events.
class MorphPadListener {
public:
    virtual ~MorphPadListener() = default;

    /// Called when the morph cursor position changes
    /// @param morphX New X position [0, 1]
    /// @param morphY New Y position [0, 1]
    virtual void onMorphPositionChanged(float morphX, float morphY) = 0;

    /// Called when a morph node is repositioned
    /// @param nodeIndex Index of the node (0-3)
    /// @param posX New X position [0, 1]
    /// @param posY New Y position [0, 1]
    virtual void onNodePositionChanged(int nodeIndex, float posX, float posY) = 0;

    /// Called when a node is selected for editing
    /// @param nodeIndex Index of the selected node (0-3)
    virtual void onNodeSelected(int nodeIndex) = 0;
};

// =============================================================================
// MorphPad Custom Control
// =============================================================================

/// @brief Custom VSTGUI control for 2D morph position control with node visualization.
///
/// MorphPad provides a 2D XY pad for controlling morph position between up to 4
/// distortion nodes. Supports multiple visual modes (1D Linear, 2D Planar, 2D Radial)
/// and interaction patterns (click, drag, Shift+drag fine adjustment, Alt+drag node move).
///
/// This control inherits from CControl for parameter binding via control-tags.
/// The primary value tracks morph X position; morph Y is tracked separately.
///
/// Also inherits from FObject to use IDependent for watching the ActiveNodes parameter
/// and automatically updating the displayed node count.
class MorphPad : public VSTGUI::CControl, public Steinberg::FObject {
public:
    /// @brief Construct a MorphPad control.
    /// @param size The size and position rectangle.
    /// @param controller The edit controller for parameter access (optional, for ActiveNodes watching)
    /// @param activeNodesParamId Parameter ID for ActiveNodes (controls visible node count)
    MorphPad(const VSTGUI::CRect& size,
             Steinberg::Vst::EditControllerEx1* controller = nullptr,
             Steinberg::Vst::ParamID activeNodesParamId = 0);
    ~MorphPad() override;

    // Non-copyable due to std::atomic member
    MorphPad(const MorphPad&) = delete;
    MorphPad& operator=(const MorphPad&) = delete;

    // =========================================================================
    // Configuration API
    // =========================================================================

    /// @brief Set the number of active morph nodes.
    /// @param count Number of active nodes (2, 3, or 4)
    void setActiveNodeCount(int count);

    /// @brief Get the number of active morph nodes.
    int getActiveNodeCount() const { return activeNodeCount_; }

    /// @brief Set the morph mode for visualization.
    /// @param mode The morph mode (Linear1D, Planar2D, Radial2D)
    void setMorphMode(MorphMode mode);

    /// @brief Get the current morph mode.
    MorphMode getMorphMode() const { return morphMode_; }

    /// @brief Set the morph cursor position.
    /// @param x X position [0, 1]
    /// @param y Y position [0, 1]
    void setMorphPosition(float x, float y);

    /// @brief Get the morph X position.
    float getMorphX() const { return morphX_; }

    /// @brief Get the morph Y position.
    float getMorphY() const { return morphY_; }

    /// @brief Set a node's position in morph space.
    /// @param nodeIndex Node index (0-3)
    /// @param x X position [0, 1]
    /// @param y Y position [0, 1]
    void setNodePosition(int nodeIndex, float x, float y);

    /// @brief Get a node's position.
    /// @param nodeIndex Node index (0-3)
    /// @param outX Output X position
    /// @param outY Output Y position
    void getNodePosition(int nodeIndex, float& outX, float& outY) const;

    /// @brief Set a node's distortion type (for color rendering).
    /// @param nodeIndex Node index (0-3)
    /// @param type The distortion type
    void setNodeType(int nodeIndex, DistortionType type);

    /// @brief Get a node's distortion type.
    DistortionType getNodeType(int nodeIndex) const;

    /// @brief Set a node's weight (for connection line opacity).
    /// @param nodeIndex Node index (0-3)
    /// @param weight Weight value [0, 1]
    void setNodeWeight(int nodeIndex, float weight);

    /// @brief Get a node's weight.
    float getNodeWeight(int nodeIndex) const;

    /// @brief Set the selected node index for editing.
    /// @param nodeIndex Node index (0-3), or -1 for no selection
    void setSelectedNode(int nodeIndex);

    /// @brief Get the selected node index.
    int getSelectedNode() const { return selectedNode_; }

    /// @brief Set the listener for events.
    void setMorphPadListener(MorphPadListener* listener) { listener_ = listener; }

    /// @brief Enable high contrast mode (Spec 012 FR-025a)
    /// Increases node border widths, uses high contrast accent for cursor.
    void setHighContrastMode(bool enabled,
                             const VSTGUI::CColor& borderColor = VSTGUI::CColor(255, 255, 255),
                             const VSTGUI::CColor& accentColor = VSTGUI::CColor(0x3A, 0x96, 0xDD));

    /// @brief Set the parameter ID for MorphY (secondary parameter).
    /// MorphX is transmitted via CControl tag; MorphY needs explicit edit calls.
    void setMorphYParamId(Steinberg::Vst::ParamID id) { morphYParamId_ = id; }

    // =========================================================================
    // Coordinate Conversion
    // =========================================================================

    /// @brief Convert normalized position [0,1] to pixel coordinates.
    /// @param normX Normalized X position
    /// @param normY Normalized Y position
    /// @param outPixelX Output pixel X coordinate
    /// @param outPixelY Output pixel Y coordinate
    void positionToPixel(float normX, float normY, float& outPixelX, float& outPixelY) const;

    /// @brief Convert pixel coordinates to normalized position [0,1].
    /// @param pixelX Pixel X coordinate
    /// @param pixelY Pixel Y coordinate
    /// @param outNormX Output normalized X position
    /// @param outNormY Output normalized Y position
    void pixelToPosition(float pixelX, float pixelY, float& outNormX, float& outNormY) const;

    // =========================================================================
    // Hit Testing
    // =========================================================================

    /// @brief Test if a pixel position hits a node circle.
    /// @param pixelX Pixel X coordinate
    /// @param pixelY Pixel Y coordinate
    /// @return Node index if hit (0-3), -1 if no hit
    int hitTestNode(float pixelX, float pixelY) const;

    // =========================================================================
    // CControl Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override;

    // VSTGUI 4.11+ Event API
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override;

    // =========================================================================
    // Lifecycle Management (IDependent)
    // =========================================================================

    /// @brief Deactivate the controller before destruction.
    /// Must be called in willClose() before destroying the control.
    void deactivate();

    // =========================================================================
    // IDependent Implementation (from FObject)
    // =========================================================================

    /// @brief Called when a watched parameter changes.
    /// Automatically invoked on UI thread via deferred updates.
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown,
                           Steinberg::int32 message) override;

    // Required for FObject
    OBJ_METHODS(MorphPad, FObject)

    // Override newCopy to prevent copying (due to std::atomic member)
    // Custom views created via createCustomView() don't need copying support
    VSTGUI::CBaseObject* newCopy() const override { return nullptr; }

    // =========================================================================
    // Node Colors (US6)
    // =========================================================================

    /// @brief Get the fixed color for a node position (A, B, C, D).
    /// Used by both MorphPad and DynamicNodeSelector for visual consistency.
    /// @param nodeIndex Node index (0=A, 1=B, 2=C, 3=D)
    /// @return VSTGUI color
    static VSTGUI::CColor getNodeColor(int nodeIndex);

    // =========================================================================
    // Category Colors (FR-002)
    // =========================================================================

    /// @brief Get the color for a distortion family.
    /// @param family The distortion family
    /// @return VSTGUI color
    static VSTGUI::CColor getCategoryColor(DistortionFamily family);

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Draw the background and border.
    void drawBackground(VSTGUI::CDrawContext* context);

    /// @brief Draw the connection lines from cursor to nodes.
    void drawConnectionLines(VSTGUI::CDrawContext* context);

    /// @brief Draw all active morph nodes.
    void drawNodes(VSTGUI::CDrawContext* context);

    /// @brief Draw the morph cursor.
    void drawCursor(VSTGUI::CDrawContext* context) const;

    /// @brief Draw the position label.
    void drawPositionLabel(VSTGUI::CDrawContext* context);

    /// @brief Draw mode-specific overlays (e.g., radial grid).
    void drawModeOverlay(VSTGUI::CDrawContext* context);

    /// @brief Recalculate node weights based on inverse distance from cursor.
    /// Weights are normalized to sum to 1.0 for active nodes.
    void recalculateWeights();

    /// @brief Clamp position to valid [0,1] range.
    static float clampPosition(float value);

    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxNodes = 4;
    static constexpr float kNodeDiameter = 12.0f;
    static constexpr float kCursorDiameter = 16.0f;
    static constexpr float kCursorStrokeWidth = 2.0f;
    static constexpr float kNodeHitRadius = 8.0f;  // Slightly larger than visual for easier clicking
    static constexpr float kFineAdjustmentScale = 0.1f;  // 10x precision with Shift

    // Padding from edges
    static constexpr float kPadding = 8.0f;

    // =========================================================================
    // State
    // =========================================================================

    // Morph position [0, 1]
    float morphX_ = 0.5f;
    float morphY_ = 0.5f;

    // Node positions [0, 1] - default to corners
    struct NodeState {
        float posX = 0.0f;
        float posY = 0.0f;
        DistortionType type = DistortionType::SoftClip;
        float weight = 0.0f;
    };
    std::array<NodeState, kMaxNodes> nodes_;

    // Mode and configuration
    MorphMode morphMode_ = MorphMode::Planar2D;
    int activeNodeCount_ = 4;
    int selectedNode_ = -1;  // -1 = no selection

    // Drag state
    bool isDragging_ = false;
    bool isDraggingNode_ = false;
    int draggingNodeIndex_ = -1;
    bool isFineAdjustment_ = false;
    float dragStartX_ = 0.0f;
    float dragStartY_ = 0.0f;
    float dragStartMorphX_ = 0.0f;
    float dragStartMorphY_ = 0.0f;

    // Listener
    MorphPadListener* listener_ = nullptr;

    // IDependent support for ActiveNodes parameter watching
    Steinberg::Vst::EditControllerEx1* controller_ = nullptr;
    Steinberg::Vst::Parameter* activeNodesParam_ = nullptr;
    std::atomic<bool> isActive_{true};

    // Secondary parameter ID for MorphY (MorphX uses CControl tag)
    Steinberg::Vst::ParamID morphYParamId_ = 0;

    /// @brief Get the current active node count from the parameter.
    int getActiveNodeCountFromParam() const;

    // Default node positions (corners for 4-node mode)
    void resetNodePositionsToDefault();

    // High contrast mode (Spec 012 FR-025a)
    bool highContrastEnabled_ = false;
    VSTGUI::CColor hcBorderColor_{255, 255, 255};
    VSTGUI::CColor hcAccentColor_{0x3A, 0x96, 0xDD};
};

} // namespace Disrumpo
