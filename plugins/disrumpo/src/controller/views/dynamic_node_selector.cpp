#include "dynamic_node_selector.h"
#include "morph_pad.h"

#include "base/source/fobject.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/events.h"

#include <utility>

namespace Disrumpo {

// ==============================================================================
// Constructor / Destructor
// ==============================================================================

DynamicNodeSelector::DynamicNodeSelector(
    const VSTGUI::CRect& size,
    Steinberg::Vst::EditControllerEx1* controller,
    Steinberg::Vst::ParamID activeNodesParamId,
    Steinberg::Vst::ParamID selectedNodeParamId)
    : CSegmentButton(size)
    , controller_(controller)
    , selectedNodeParamId_(selectedNodeParamId)
{
    // Get the ActiveNodes parameter and watch it
    if (controller_) {
        activeNodesParam_ = controller_->getParameterObject(activeNodesParamId);
        if (activeNodesParam_) {
            activeNodesParam_->addRef();
            activeNodesParam_->addDependent(this);
        }
    }

    // Build initial segments based on current ActiveNodes value
    int activeCount = getActiveNodeCount();
    rebuildSegments(activeCount);

    // Trigger initial update to sync state
    if (activeNodesParam_) {
        activeNodesParam_->deferUpdate();
    }
}

DynamicNodeSelector::~DynamicNodeSelector()
{
    deactivate();

    if (activeNodesParam_) {
        activeNodesParam_->release();
        activeNodesParam_ = nullptr;
    }
}

// ==============================================================================
// Lifecycle Management
// ==============================================================================

void DynamicNodeSelector::deactivate()
{
    // Use exchange to ensure we only do this once (idempotent)
    if (isActive_.exchange(false, std::memory_order_acq_rel)) {
        if (activeNodesParam_) {
            activeNodesParam_->removeDependent(this);
        }
    }
}

// ==============================================================================
// IDependent Implementation
// ==============================================================================

void PLUGIN_API DynamicNodeSelector::update(
    Steinberg::FUnknown* changedUnknown,
    Steinberg::int32 message)
{
    if (!isActive_.load(std::memory_order_acquire)) {
        return;
    }

    if (message != Steinberg::IDependent::kChanged) {
        return;
    }

    // Verify it's the ActiveNodes parameter that changed
    auto* changedParam = Steinberg::FCast<Steinberg::Vst::Parameter>(changedUnknown);
    if (changedParam != activeNodesParam_) {
        return;
    }

    // Get new active count and rebuild if changed
    int activeCount = getActiveNodeCount();
    if (activeCount != currentSegmentCount_) {
        rebuildSegments(activeCount);
        clampSelectedNode();
    }
}

// ==============================================================================
// CSegmentButton Overrides
// ==============================================================================

void DynamicNodeSelector::drawRect(VSTGUI::CDrawContext* context, const VSTGUI::CRect& /*updateRect*/)
{
    const auto& segments = getSegments();
    const size_t numSegments = segments.size();
    if (numSegments == 0) {
        setDirty(false);
        return;
    }

    const auto& viewSize = getViewSize();
    const uint32_t selectedIndex = getSelectedSegment();
    const VSTGUI::CCoord roundRadius = getRoundRadius();
    const VSTGUI::CCoord frameWidth = getFrameWidth();

    // Calculate segment width (CSegmentButton's internal rects may not be set)
    const VSTGUI::CCoord segmentWidth = viewSize.getWidth() / static_cast<VSTGUI::CCoord>(numSegments);

    // Draw each segment with its node color
    for (size_t i = 0; i < numSegments; ++i) {
        // Calculate segment rect manually
        VSTGUI::CRect segRect(
            viewSize.left + static_cast<VSTGUI::CCoord>(i) * segmentWidth,
            viewSize.top,
            viewSize.left + static_cast<VSTGUI::CCoord>(i + 1) * segmentWidth,
            viewSize.bottom
        );

        // Get node color for this segment (A=0, B=1, C=2, D=3)
        VSTGUI::CColor nodeColor = MorphPad::getNodeColor(static_cast<int>(i));

        // Dim unselected segments, brighten selected
        if (i != selectedIndex) {
            // Unselected: darken by reducing RGB values
            nodeColor.red = static_cast<uint8_t>(nodeColor.red * 0.4);
            nodeColor.green = static_cast<uint8_t>(nodeColor.green * 0.4);
            nodeColor.blue = static_cast<uint8_t>(nodeColor.blue * 0.4);
        }

        // Draw segment background
        context->setFillColor(nodeColor);
        context->drawRect(segRect, VSTGUI::kDrawFilled);

        // Draw segment label (A, B, C, D)
        static const char* nodeLabels[] = {"A", "B", "C", "D"};
        context->setFontColor(VSTGUI::CColor{0xFF, 0xFF, 0xFF, 0xFF});  // White text
        if (getFont()) {
            context->setFont(getFont());
        }
        context->drawString(nodeLabels[i], segRect, VSTGUI::kCenterText);
    }

    // Draw frame around entire control
    context->setFrameColor(getFrameColor());
    context->setLineWidth(frameWidth);

    auto framePath = VSTGUI::owned(context->createGraphicsPath());
    if (framePath) {
        framePath->addRoundRect(viewSize, roundRadius);
        context->drawGraphicsPath(framePath, VSTGUI::CDrawContext::kPathStroked);
    }

    // Draw segment separators
    for (size_t i = 1; i < numSegments; ++i) {
        VSTGUI::CCoord x = viewSize.left + static_cast<VSTGUI::CCoord>(i) * segmentWidth;
        context->drawLine(VSTGUI::CPoint(x, viewSize.top), VSTGUI::CPoint(x, viewSize.bottom));
    }

    setDirty(false);
}

void DynamicNodeSelector::onMouseDownEvent(VSTGUI::MouseDownEvent& event)
{
    uint32_t oldSelection = getSelectedSegment();

    // Call base class to handle the click
    CSegmentButton::onMouseDownEvent(event);

    uint32_t newSelection = getSelectedSegment();

    // Custom views don't have automatic VSTGUI ParameterChangeListener binding.
    // We must manually notify the controller when the selection changes.
    if (controller_ && newSelection != oldSelection && event.consumed) {
        // Convert selection index to normalized value (4 options: 0, 1, 2, 3 -> 0.0, 0.333, 0.667, 1.0)
        double normalizedValue = static_cast<double>(newSelection) / 3.0;

        controller_->beginEdit(selectedNodeParamId_);
        controller_->setParamNormalized(selectedNodeParamId_, normalizedValue);
        controller_->performEdit(selectedNodeParamId_, normalizedValue);
        controller_->endEdit(selectedNodeParamId_);
    }
}

// ==============================================================================
// Private Implementation
// ==============================================================================

int DynamicNodeSelector::getActiveNodeCount() const
{
    if (!activeNodesParam_) {
        return 4;  // Default to 4 nodes
    }

    // ActiveNodes parameter: 0 = 2 nodes, 0.5 = 3 nodes, 1.0 = 4 nodes
    // Convert normalized value to count (2, 3, or 4)
    double normalized = activeNodesParam_->getNormalized();

    // StringListParameter with 3 options: toPlain gives 0, 1, 2
    // Map to: 2, 3, 4 nodes
    int index = static_cast<int>(activeNodesParam_->toPlain(normalized));
    return index + 2;  // 0->2, 1->3, 2->4
}

void DynamicNodeSelector::rebuildSegments(int activeCount)
{
    // Clamp to valid range
    activeCount = std::clamp(activeCount, 2, 4);

    if (activeCount == currentSegmentCount_) {
        return;  // No change needed
    }

    // Store current selection before rebuilding
    uint32_t currentSelection = getSelectedSegment();

    // Clear existing segments
    removeAllSegments();

    // Add segments for active nodes
    static const char* nodeNames[] = {"A", "B", "C", "D"};

    for (int i = 0; i < activeCount; ++i) {
        Segment segment;
        segment.name = nodeNames[i];
        addSegment(std::move(segment));
    }

    currentSegmentCount_ = activeCount;

    // Restore selection (clamped to valid range)
    if (std::cmp_greater_equal(currentSelection, activeCount)) {
        currentSelection = static_cast<uint32_t>(activeCount - 1);
    }
    setSelectedSegment(currentSelection);

    // Trigger redraw
    invalid();
}

void DynamicNodeSelector::clampSelectedNode()
{
    if (!controller_) {
        return;
    }

    // Get current selected node from the parameter
    auto* selectedNodeParam = controller_->getParameterObject(selectedNodeParamId_);
    if (!selectedNodeParam) {
        return;
    }

    double normalized = selectedNodeParam->getNormalized();
    int selectedIndex = static_cast<int>(selectedNodeParam->toPlain(normalized));

    // Clamp to valid range
    int maxIndex = currentSegmentCount_ - 1;
    if (selectedIndex > maxIndex) {
        // Update the parameter to clamped value
        double newNormalized = static_cast<double>(maxIndex) / 3.0;  // 4 options, so divide by 3
        controller_->beginEdit(selectedNodeParamId_);
        controller_->setParamNormalized(selectedNodeParamId_, newNormalized);
        controller_->performEdit(selectedNodeParamId_, newNormalized);
        controller_->endEdit(selectedNodeParamId_);
    }
}

} // namespace Disrumpo
