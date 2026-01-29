// ==============================================================================
// NodeEditorBorder Custom View Implementation
// ==============================================================================

#include "node_editor_border.h"
#include "morph_pad.h"  // For getNodeColor()

#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/lib/cdrawcontext.h"

namespace Disrumpo {

// =============================================================================
// Constructor / Destructor
// =============================================================================

NodeEditorBorder::NodeEditorBorder(const VSTGUI::CRect& size,
                                   Steinberg::Vst::EditControllerEx1* controller,
                                   Steinberg::Vst::ParamID selectedNodeParamId)
    : CView(size)
    , controller_(controller)
{
    // Set up IDependent watching for SelectedNode parameter
    if (controller_ && selectedNodeParamId != 0) {
        selectedNodeParam_ = controller_->getParameterObject(selectedNodeParamId);
        if (selectedNodeParam_) {
            selectedNodeParam_->addRef();
            selectedNodeParam_->addDependent(this);

            // Initialize selected node from current parameter value
            selectedNode_ = getSelectedNodeFromParam();
        }
    }

    // Make sure the view is transparent so it only draws the border
    setTransparency(true);
}

NodeEditorBorder::~NodeEditorBorder() {
    deactivate();

    if (selectedNodeParam_) {
        selectedNodeParam_->release();
        selectedNodeParam_ = nullptr;
    }
}

// =============================================================================
// Lifecycle Management
// =============================================================================

void NodeEditorBorder::deactivate() {
    if (isActive_.exchange(false, std::memory_order_acq_rel)) {
        if (selectedNodeParam_) {
            selectedNodeParam_->removeDependent(this);
        }
    }
}

// =============================================================================
// IDependent Implementation
// =============================================================================

void PLUGIN_API NodeEditorBorder::update(Steinberg::FUnknown* changedUnknown,
                                          Steinberg::int32 message) {
    if (!isActive_.load(std::memory_order_acquire)) {
        return;
    }

    if (message != Steinberg::IDependent::kChanged) {
        return;
    }

    // Verify it's the SelectedNode parameter that changed
    auto* changedParam = Steinberg::FCast<Steinberg::Vst::Parameter>(changedUnknown);
    if (changedParam != selectedNodeParam_) {
        return;
    }

    // Update selected node and trigger redraw
    int newNode = getSelectedNodeFromParam();
    if (newNode != selectedNode_) {
        selectedNode_ = newNode;
        invalid();  // Trigger redraw with new color
    }
}

int NodeEditorBorder::getSelectedNodeFromParam() const {
    if (!selectedNodeParam_) {
        return 0;  // Default to node A
    }

    // SelectedNode parameter: StringListParameter with 4 options
    // toPlain gives 0, 1, 2, 3 -> node A, B, C, D
    double normalized = selectedNodeParam_->getNormalized();
    int index = static_cast<int>(selectedNodeParam_->toPlain(normalized));
    return std::clamp(index, 0, 3);
}

// =============================================================================
// CView Override
// =============================================================================

void NodeEditorBorder::draw(VSTGUI::CDrawContext* context) {
    const auto& rect = getViewSize();

    // Get the color for the selected node
    VSTGUI::CColor borderColor = MorphPad::getNodeColor(selectedNode_);

    // Draw the border
    context->setFrameColor(borderColor);
    context->setLineWidth(kBorderWidth);

    // Inset the rect slightly so the border is fully visible
    VSTGUI::CRect borderRect = rect;
    borderRect.inset(kBorderWidth / 2.0, kBorderWidth / 2.0);

    context->drawRect(borderRect, VSTGUI::kDrawStroked);

    // Draw node label in top-left corner
    static const char* nodeLabels[] = {"Node A", "Node B", "Node C", "Node D"};

    // Create a small label background
    VSTGUI::CRect labelRect(
        rect.left + kBorderWidth,
        rect.top + kBorderWidth,
        rect.left + 60,
        rect.top + 18
    );

    // Draw label background (slightly transparent node color)
    VSTGUI::CColor labelBgColor = borderColor;
    labelBgColor.alpha = 200;
    context->setFillColor(labelBgColor);
    context->drawRect(labelRect, VSTGUI::kDrawFilled);

    // Draw label text
    context->setFontColor(VSTGUI::CColor{0xFF, 0xFF, 0xFF, 0xFF});
    context->drawString(nodeLabels[selectedNode_], labelRect, VSTGUI::kCenterText);

    setDirty(false);
}

} // namespace Disrumpo
