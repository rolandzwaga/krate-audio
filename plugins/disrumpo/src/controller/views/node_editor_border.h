#pragma once

// ==============================================================================
// NodeEditorBorder Custom View
// ==============================================================================
// Debug helper: Draws a colored border around the node editor that matches
// the currently selected node's color (A=coral, B=teal, C=purple, D=yellow).
// This helps visually confirm which node is being edited.
// ==============================================================================

#include "base/source/fobject.h"
#include "pluginterfaces/base/ftypes.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/cview.h"

#include <atomic>

namespace Disrumpo {

/// @brief Custom view that draws a colored border based on the selected node.
/// Watches the SelectedNode parameter and updates the border color accordingly.
class NodeEditorBorder : public VSTGUI::CView, public Steinberg::FObject {
public:
    /// @brief Construct a NodeEditorBorder.
    /// @param size The size and position rectangle.
    /// @param controller The edit controller for parameter access.
    /// @param selectedNodeParamId Parameter ID for SelectedNode.
    NodeEditorBorder(const VSTGUI::CRect& size,
                     Steinberg::Vst::EditControllerEx1* controller,
                     Steinberg::Vst::ParamID selectedNodeParamId);
    ~NodeEditorBorder() override;

    // Non-copyable
    NodeEditorBorder(const NodeEditorBorder&) = delete;
    NodeEditorBorder& operator=(const NodeEditorBorder&) = delete;

    // =========================================================================
    // CView Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override;

    // =========================================================================
    // Lifecycle Management
    // =========================================================================

    /// @brief Deactivate before destruction.
    void deactivate();

    /// @brief Enable high contrast mode (Spec 012 FR-025a)
    void setHighContrastMode(bool enabled) { highContrastEnabled_ = enabled; }

    // =========================================================================
    // IDependent Implementation
    // =========================================================================

    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown,
                           Steinberg::int32 message) override;

    // Required for FObject
    OBJ_METHODS(NodeEditorBorder, FObject)

    // Override newCopy to prevent copying
    VSTGUI::CBaseObject* newCopy() const override { return nullptr; }

private:
    /// @brief Get the selected node index from the parameter.
    int getSelectedNodeFromParam() const;

    Steinberg::Vst::EditControllerEx1* controller_ = nullptr;
    Steinberg::Vst::Parameter* selectedNodeParam_ = nullptr;
    std::atomic<bool> isActive_{true};
    int selectedNode_ = 0;

    static constexpr float kBorderWidth = 3.0f;

    // High contrast mode (Spec 012 FR-025a)
    bool highContrastEnabled_ = false;
};

} // namespace Disrumpo
