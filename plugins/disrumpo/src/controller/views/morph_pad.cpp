// ==============================================================================
// MorphPad Custom View Implementation
// ==============================================================================
// FR-001 through FR-012: 2D morph position control with node visualization.
// ==============================================================================

#include "morph_pad.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <iomanip>

namespace Disrumpo {

// =============================================================================
// Node Colors (US6)
// =============================================================================
// Fixed colors for nodes A, B, C, D - used by both MorphPad and DynamicNodeSelector
// for visual consistency. Colors are chosen to be distinct and vibrant on dark backgrounds.

static const std::array<VSTGUI::CColor, 4> kNodeColors = {{
    VSTGUI::CColor{0xFF, 0x6B, 0x6B, 0xFF},  // Node A - Coral/Salmon
    VSTGUI::CColor{0x4E, 0xCD, 0xC4, 0xFF},  // Node B - Teal
    VSTGUI::CColor{0x9B, 0x59, 0xB6, 0xFF},  // Node C - Purple/Violet
    VSTGUI::CColor{0xF1, 0xC4, 0x0F, 0xFF},  // Node D - Golden Yellow
}};

VSTGUI::CColor MorphPad::getNodeColor(int nodeIndex) {
    if (nodeIndex >= 0 && nodeIndex < 4) {
        return kNodeColors[nodeIndex];
    }
    return VSTGUI::CColor{0x80, 0x80, 0x80, 0xFF};  // Gray fallback
}

// =============================================================================
// Category Colors (FR-002)
// =============================================================================
// From custom-controls.md Section 2.3.1

static const std::map<DistortionFamily, VSTGUI::CColor> kCategoryColors = {
    {DistortionFamily::Saturation,   VSTGUI::CColor{0xFF, 0x6B, 0x35, 0xFF}},  // Orange
    {DistortionFamily::Wavefold,     VSTGUI::CColor{0x4E, 0xCD, 0xC4, 0xFF}},  // Teal
    {DistortionFamily::Digital,      VSTGUI::CColor{0x95, 0xE8, 0x6B, 0xFF}},  // Green
    {DistortionFamily::Rectify,      VSTGUI::CColor{0xC7, 0x92, 0xEA, 0xFF}},  // Purple
    {DistortionFamily::Dynamic,      VSTGUI::CColor{0xFF, 0xCB, 0x6B, 0xFF}},  // Yellow
    {DistortionFamily::Hybrid,       VSTGUI::CColor{0xFF, 0x53, 0x70, 0xFF}},  // Red
    {DistortionFamily::Experimental, VSTGUI::CColor{0x89, 0xDD, 0xFF, 0xFF}},  // Light Blue
};

VSTGUI::CColor MorphPad::getCategoryColor(DistortionFamily family) {
    auto it = kCategoryColors.find(family);
    if (it != kCategoryColors.end()) {
        return it->second;
    }
    return VSTGUI::CColor{0x80, 0x80, 0x80, 0xFF};  // Gray fallback
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

MorphPad::MorphPad(const VSTGUI::CRect& size,
                   Steinberg::Vst::EditControllerEx1* controller,
                   Steinberg::Vst::ParamID activeNodesParamId)
    : CControl(size)
    , controller_(controller)
{
    // Initialize default node positions (corners for 4-node mode)
    resetNodePositionsToDefault();

    // Set up IDependent watching for ActiveNodes parameter
    if (controller_ && activeNodesParamId != 0) {
        activeNodesParam_ = controller_->getParameterObject(activeNodesParamId);
        if (activeNodesParam_) {
            activeNodesParam_->addRef();
            activeNodesParam_->addDependent(this);

            // Initialize active node count from current parameter value
            activeNodeCount_ = getActiveNodeCountFromParam();
        }
    }

    // Calculate initial weights based on default cursor position (0.5, 0.5)
    recalculateWeights();
}

MorphPad::~MorphPad() {
    deactivate();

    if (activeNodesParam_) {
        activeNodesParam_->release();
        activeNodesParam_ = nullptr;
    }
}

// =============================================================================
// Lifecycle Management
// =============================================================================

void MorphPad::deactivate() {
    // Use exchange to ensure we only do this once (idempotent)
    if (isActive_.exchange(false, std::memory_order_acq_rel)) {
        if (activeNodesParam_) {
            activeNodesParam_->removeDependent(this);
        }
    }
}

// =============================================================================
// IDependent Implementation
// =============================================================================

void PLUGIN_API MorphPad::update(Steinberg::FUnknown* changedUnknown,
                                  Steinberg::int32 message) {
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

    // Update active node count from parameter
    int newCount = getActiveNodeCountFromParam();
    if (newCount != activeNodeCount_) {
        setActiveNodeCount(newCount);
    }
}

int MorphPad::getActiveNodeCountFromParam() const {
    if (!activeNodesParam_) {
        return 4;  // Default to 4 nodes
    }

    // ActiveNodes parameter: StringListParameter with 3 options
    // toPlain gives 0, 1, 2 -> map to 2, 3, 4 nodes
    double normalized = activeNodesParam_->getNormalized();
    int index = static_cast<int>(activeNodesParam_->toPlain(normalized));
    return index + 2;  // 0->2, 1->3, 2->4
}

void MorphPad::resetNodePositionsToDefault() {
    // Default corner positions for 2D Planar mode
    nodes_[0].posX = 0.0f;  nodes_[0].posY = 0.0f;  // Node A - bottom-left
    nodes_[1].posX = 1.0f;  nodes_[1].posY = 0.0f;  // Node B - bottom-right
    nodes_[2].posX = 0.0f;  nodes_[2].posY = 1.0f;  // Node C - top-left
    nodes_[3].posX = 1.0f;  nodes_[3].posY = 1.0f;  // Node D - top-right

    // Initialize with default types
    for (int i = 0; i < kMaxNodes; ++i) {
        nodes_[i].type = static_cast<DistortionType>(i % static_cast<int>(DistortionType::COUNT));
        nodes_[i].weight = 0.25f;  // Equal weights initially
    }
}

// =============================================================================
// Configuration API
// =============================================================================

void MorphPad::setActiveNodeCount(int count) {
    activeNodeCount_ = std::clamp(count, 2, kMaxNodes);
    recalculateWeights();
    invalid();
}

void MorphPad::setMorphMode(MorphMode mode) {
    morphMode_ = mode;
    invalid();
}

void MorphPad::setMorphPosition(float x, float y) {
    morphX_ = clampPosition(x);
    morphY_ = clampPosition(y);
    recalculateWeights();
    invalid();
}

void MorphPad::setNodePosition(int nodeIndex, float x, float y) {
    if (nodeIndex >= 0 && nodeIndex < kMaxNodes) {
        nodes_[nodeIndex].posX = clampPosition(x);
        nodes_[nodeIndex].posY = clampPosition(y);
        recalculateWeights();
        invalid();
    }
}

void MorphPad::getNodePosition(int nodeIndex, float& outX, float& outY) const {
    if (nodeIndex >= 0 && nodeIndex < kMaxNodes) {
        outX = nodes_[nodeIndex].posX;
        outY = nodes_[nodeIndex].posY;
    } else {
        outX = 0.0f;
        outY = 0.0f;
    }
}

void MorphPad::setNodeType(int nodeIndex, DistortionType type) {
    if (nodeIndex >= 0 && nodeIndex < kMaxNodes) {
        nodes_[nodeIndex].type = type;
        invalid();
    }
}

DistortionType MorphPad::getNodeType(int nodeIndex) const {
    if (nodeIndex >= 0 && nodeIndex < kMaxNodes) {
        return nodes_[nodeIndex].type;
    }
    return DistortionType::SoftClip;
}

void MorphPad::setNodeWeight(int nodeIndex, float weight) {
    if (nodeIndex >= 0 && nodeIndex < kMaxNodes) {
        nodes_[nodeIndex].weight = std::clamp(weight, 0.0f, 1.0f);
        invalid();
    }
}

float MorphPad::getNodeWeight(int nodeIndex) const {
    if (nodeIndex >= 0 && nodeIndex < kMaxNodes) {
        return nodes_[nodeIndex].weight;
    }
    return 0.0f;
}

void MorphPad::setSelectedNode(int nodeIndex) {
    if (nodeIndex >= -1 && nodeIndex < kMaxNodes) {
        selectedNode_ = nodeIndex;
        invalid();
    }
}

// =============================================================================
// Coordinate Conversion (T007)
// =============================================================================

void MorphPad::positionToPixel(float normX, float normY, float& outPixelX, float& outPixelY) const {
    const auto& rect = getViewSize();
    float innerWidth = static_cast<float>(rect.getWidth()) - 2.0f * kPadding;
    float innerHeight = static_cast<float>(rect.getHeight()) - 2.0f * kPadding;

    // Map [0,1] to pixel coordinates
    // Note: Y is inverted (0 at bottom, 1 at top in normalized, but Y increases downward in pixels)
    outPixelX = static_cast<float>(rect.left) + kPadding + normX * innerWidth;
    outPixelY = static_cast<float>(rect.bottom) - kPadding - normY * innerHeight;
}

void MorphPad::pixelToPosition(float pixelX, float pixelY, float& outNormX, float& outNormY) const {
    const auto& rect = getViewSize();
    float innerWidth = static_cast<float>(rect.getWidth()) - 2.0f * kPadding;
    float innerHeight = static_cast<float>(rect.getHeight()) - 2.0f * kPadding;

    // Avoid division by zero
    if (innerWidth <= 0.0f) innerWidth = 1.0f;
    if (innerHeight <= 0.0f) innerHeight = 1.0f;

    // Map pixel coordinates to [0,1]
    outNormX = (pixelX - static_cast<float>(rect.left) - kPadding) / innerWidth;
    outNormY = (static_cast<float>(rect.bottom) - kPadding - pixelY) / innerHeight;

    // Clamp to valid range
    outNormX = clampPosition(outNormX);
    outNormY = clampPosition(outNormY);
}

// =============================================================================
// Hit Testing
// =============================================================================

int MorphPad::hitTestNode(float pixelX, float pixelY) const {
    for (int i = 0; i < activeNodeCount_; ++i) {
        float nodePixelX = 0.0f;
        float nodePixelY = 0.0f;
        positionToPixel(nodes_[i].posX, nodes_[i].posY, nodePixelX, nodePixelY);

        float dx = pixelX - nodePixelX;
        float dy = pixelY - nodePixelY;
        float distSq = dx * dx + dy * dy;

        if (distSq <= kNodeHitRadius * kNodeHitRadius) {
            return i;
        }
    }
    return -1;
}

// =============================================================================
// CControl Overrides - Drawing
// =============================================================================

void MorphPad::draw(VSTGUI::CDrawContext* context) {
    drawBackground(context);
    drawModeOverlay(context);
    drawConnectionLines(context);
    drawNodes(context);
    drawCursor(context);
    drawPositionLabel(context);

    setDirty(false);
}

void MorphPad::drawBackground(VSTGUI::CDrawContext* context) {
    const auto& rect = getViewSize();

    // Draw multi-point gradient background using inverse distance weighted colors
    // Grid resolution - higher = smoother but slower
    constexpr int kGridResolution = 24;
    constexpr float kMinDistance = 0.01f;
    constexpr float kDistanceExponent = 2.0f;
    constexpr float kDarkenFactor = 0.35f;  // Darken colors to keep UI readable

    float cellWidth = static_cast<float>(rect.getWidth()) / kGridResolution;
    float cellHeight = static_cast<float>(rect.getHeight()) / kGridResolution;

    for (int gy = 0; gy < kGridResolution; ++gy) {
        for (int gx = 0; gx < kGridResolution; ++gx) {
            // Calculate center of this cell in normalized coordinates
            float cellCenterX = (static_cast<float>(gx) + 0.5f) / kGridResolution;
            float cellCenterY = 1.0f - (static_cast<float>(gy) + 0.5f) / kGridResolution;  // Y inverted

            // Calculate inverse distance weights to each active node
            float totalWeight = 0.0f;
            std::array<float, kMaxNodes> weights{};

            for (int i = 0; i < activeNodeCount_; ++i) {
                float dx = cellCenterX - nodes_[i].posX;
                float dy = cellCenterY - nodes_[i].posY;
                float distance = std::sqrt(dx * dx + dy * dy);

                if (distance < kMinDistance) {
                    weights[i] = 1000.0f;
                } else {
                    weights[i] = 1.0f / std::pow(distance, kDistanceExponent);
                }
                totalWeight += weights[i];
            }

            // Normalize weights and blend colors
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            if (totalWeight > 0.0f) {
                for (int i = 0; i < activeNodeCount_; ++i) {
                    float w = weights[i] / totalWeight;
                    VSTGUI::CColor nodeColor = getNodeColor(i);
                    r += w * static_cast<float>(nodeColor.red);
                    g += w * static_cast<float>(nodeColor.green);
                    b += w * static_cast<float>(nodeColor.blue);
                }
            }

            // Darken the color so UI elements remain visible
            VSTGUI::CColor cellColor{
                static_cast<uint8_t>(r * kDarkenFactor),
                static_cast<uint8_t>(g * kDarkenFactor),
                static_cast<uint8_t>(b * kDarkenFactor),
                0xFF
            };

            // Draw the cell
            VSTGUI::CRect cellRect(
                rect.left + gx * cellWidth,
                rect.top + gy * cellHeight,
                rect.left + (gx + 1) * cellWidth,
                rect.top + (gy + 1) * cellHeight
            );
            context->setFillColor(cellColor);
            context->drawRect(cellRect, VSTGUI::kDrawFilled);
        }
    }

    // Border
    context->setFrameColor(VSTGUI::CColor{0x40, 0x40, 0x40, 0xFF});  // Lighter gray
    context->setLineWidth(1.0);
    context->drawRect(rect, VSTGUI::kDrawStroked);
}

void MorphPad::drawModeOverlay(VSTGUI::CDrawContext* context) {
    if (morphMode_ == MorphMode::Radial2D) {
        // Draw radial grid overlay (FR-009)
        const auto& rect = getViewSize();
        float centerX = 0.0f;
        float centerY = 0.0f;
        positionToPixel(0.5f, 0.5f, centerX, centerY);

        float innerWidth = static_cast<float>(rect.getWidth()) - 2.0f * kPadding;
        float innerHeight = static_cast<float>(rect.getHeight()) - 2.0f * kPadding;
        float maxRadius = std::min(innerWidth, innerHeight) * 0.5f;

        context->setFrameColor(VSTGUI::CColor{0xFF, 0xFF, 0xFF, 0x40});  // White 25% opacity
        context->setLineWidth(1.0);

        // Draw concentric circles at r = 0.25, 0.5, 0.75, 1.0
        for (float r : {0.25f, 0.5f, 0.75f, 1.0f}) {
            float radius = r * maxRadius;
            VSTGUI::CRect circleRect(
                centerX - radius, centerY - radius,
                centerX + radius, centerY + radius
            );
            context->drawEllipse(circleRect, VSTGUI::kDrawStroked);
        }

        // Draw 8 radial lines at 45 degree intervals
        for (int i = 0; i < 8; ++i) {
            float angle = static_cast<float>(i) * std::numbers::pi_v<float> / 4.0f;
            float endX = centerX + maxRadius * std::cos(angle);
            float endY = centerY - maxRadius * std::sin(angle);  // Y inverted
            context->drawLine(VSTGUI::CPoint(centerX, centerY), VSTGUI::CPoint(endX, endY));
        }

        // Center point (4px filled circle)
        VSTGUI::CRect centerRect(centerX - 2.0, centerY - 2.0, centerX + 2.0, centerY + 2.0);
        context->setFillColor(VSTGUI::CColor{0xFF, 0xFF, 0xFF, 0x80});
        context->drawEllipse(centerRect, VSTGUI::kDrawFilled);
    }
    else if (morphMode_ == MorphMode::Linear1D) {
        // Draw horizontal center line for 1D mode
        float leftX = 0.0f;
        float rightX = 0.0f;
        float lineY = 0.0f;
        float tempY = 0.0f;
        positionToPixel(0.0f, 0.5f, leftX, lineY);
        positionToPixel(1.0f, 0.5f, rightX, tempY);

        context->setFrameColor(VSTGUI::CColor{0xFF, 0xFF, 0xFF, 0x40});  // White 25% opacity
        context->setLineWidth(1.0);
        context->drawLine(VSTGUI::CPoint(leftX, lineY), VSTGUI::CPoint(rightX, lineY));
    }
}

void MorphPad::drawConnectionLines(VSTGUI::CDrawContext* context) {
    // FR-008: Gradient lines from cursor (white) to nodes (category color)
    // Opacity proportional to node weight

    float cursorPixelX = 0.0f;
    float cursorPixelY = 0.0f;
    positionToPixel(morphX_, morphY_, cursorPixelX, cursorPixelY);

    for (int i = 0; i < activeNodeCount_; ++i) {
        float nodePixelX = 0.0f;
        float nodePixelY = 0.0f;
        positionToPixel(nodes_[i].posX, nodes_[i].posY, nodePixelX, nodePixelY);

        // Get fixed node color (A=coral, B=teal, C=purple, D=yellow)
        VSTGUI::CColor nodeColor = getNodeColor(i);

        // Set opacity based on weight
        uint8_t alpha = static_cast<uint8_t>(nodes_[i].weight * 255.0f);
        nodeColor.alpha = alpha;

        // Draw line (simple single color for now - gradient would require path)
        context->setFrameColor(nodeColor);
        context->setLineWidth(2.0);
        context->drawLine(
            VSTGUI::CPoint(cursorPixelX, cursorPixelY),
            VSTGUI::CPoint(nodePixelX, nodePixelY)
        );
    }
}

void MorphPad::drawNodes(VSTGUI::CDrawContext* context) {
    // US6: 12px diameter filled circles with fixed node colors (A/B/C/D)
    // Brightness scaled by weight (min 0.3 to max 1.0)

    constexpr float kMinBrightness = 0.3f;
    constexpr float kMaxBrightness = 1.0f;

    for (int i = 0; i < activeNodeCount_; ++i) {
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        positionToPixel(nodes_[i].posX, nodes_[i].posY, pixelX, pixelY);

        // Get fixed node color (A=coral, B=teal, C=purple, D=yellow)
        VSTGUI::CColor fillColor = getNodeColor(i);

        // Scale brightness based on weight
        float brightness = kMinBrightness + nodes_[i].weight * (kMaxBrightness - kMinBrightness);
        fillColor.red = static_cast<uint8_t>(fillColor.red * brightness);
        fillColor.green = static_cast<uint8_t>(fillColor.green * brightness);
        fillColor.blue = static_cast<uint8_t>(fillColor.blue * brightness);

        // Node circle
        float radius = kNodeDiameter * 0.5f;
        VSTGUI::CRect nodeRect(
            pixelX - radius, pixelY - radius,
            pixelX + radius, pixelY + radius
        );

        context->setFillColor(fillColor);
        context->drawEllipse(nodeRect, VSTGUI::kDrawFilled);

        // FR-027: Selected node has highlight ring
        if (i == selectedNode_) {
            context->setFrameColor(VSTGUI::CColor{0xFF, 0xFF, 0xFF, 0xFF});  // White
            context->setLineWidth(2.0);
            VSTGUI::CRect highlightRect(
                pixelX - radius - 3.0f, pixelY - radius - 3.0f,
                pixelX + radius + 3.0f, pixelY + radius + 3.0f
            );
            context->drawEllipse(highlightRect, VSTGUI::kDrawStroked);
        }

        // Draw node label (A, B, C, D)
        const char* labels[] = {"A", "B", "C", "D"};
        VSTGUI::CRect labelRect(pixelX - 10.0, pixelY - 6.0, pixelX + 10.0, pixelY + 6.0);
        context->setFontColor(VSTGUI::CColor{0xFF, 0xFF, 0xFF, 0xFF});  // White text
        context->drawString(labels[i], labelRect, VSTGUI::kCenterText);
    }
}

void MorphPad::drawCursor(VSTGUI::CDrawContext* context) const {
    // FR-003: 16px diameter open circle with 2px white stroke

    float pixelX = 0.0f;
    float pixelY = 0.0f;
    positionToPixel(morphX_, morphY_, pixelX, pixelY);

    float radius = kCursorDiameter * 0.5f;
    VSTGUI::CRect cursorRect(
        pixelX - radius, pixelY - radius,
        pixelX + radius, pixelY + radius
    );

    context->setFrameColor(VSTGUI::CColor{0xFF, 0xFF, 0xFF, 0xFF});  // White
    context->setLineWidth(kCursorStrokeWidth);
    context->drawEllipse(cursorRect, VSTGUI::kDrawStroked);

    // Small filled center point
    VSTGUI::CRect centerRect(pixelX - 2.0, pixelY - 2.0, pixelX + 2.0, pixelY + 2.0);
    context->setFillColor(VSTGUI::CColor{0xFF, 0xFF, 0xFF, 0xFF});
    context->drawEllipse(centerRect, VSTGUI::kDrawFilled);
}

void MorphPad::drawPositionLabel(VSTGUI::CDrawContext* context) {
    // FR-041: Position label "X: 0.00 Y: 0.00" at bottom-left corner

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "X: " << morphX_ << " Y: " << morphY_;

    const auto& rect = getViewSize();
    VSTGUI::CRect labelRect(
        rect.left + 4.0,
        rect.bottom - 16.0,
        rect.left + 100.0,
        rect.bottom - 2.0
    );

    context->setFontColor(VSTGUI::CColor{0xAA, 0xAA, 0xAA, 0xFF});  // Light gray
    context->drawString(oss.str().c_str(), labelRect, VSTGUI::kLeftText);
}

// =============================================================================
// CControl Overrides - Mouse Events
// =============================================================================

void MorphPad::onMouseDownEvent(VSTGUI::MouseDownEvent& event) {
    if (event.buttonState.isLeft()) {
        float pixelX = static_cast<float>(event.mousePosition.x);
        float pixelY = static_cast<float>(event.mousePosition.y);

        // Check for node click - selects and starts dragging (FR-007, FR-025, FR-027)
        int hitNode = hitTestNode(pixelX, pixelY);
        if (hitNode >= 0) {
            // Select the node
            setSelectedNode(hitNode);
            if (listener_) {
                listener_->onNodeSelected(hitNode);
            }
            // Start dragging it
            isDraggingNode_ = true;
            draggingNodeIndex_ = hitNode;
            isDragging_ = false;
            event.consumed = true;
            return;
        }

        // Check for double-click reset to center (FR-006 edge case)
        if (event.clickCount == 2) {
            setMorphPosition(0.5f, 0.5f);
            setValue(0.5f);  // Update CControl value for X
            if (listener_) {
                listener_->onMorphPositionChanged(0.5f, 0.5f);
            }
            beginEdit();
            valueChanged();
            endEdit();

            // Send Y parameter reset to host
            if (controller_ && morphYParamId_ != 0) {
                controller_->beginEdit(morphYParamId_);
                controller_->performEdit(morphYParamId_, 0.5);
                controller_->endEdit(morphYParamId_);
            }
            event.consumed = true;
            return;
        }

        // Start cursor drag (FR-004, FR-005)
        isDragging_ = true;
        isDraggingNode_ = false;
        isFineAdjustment_ = event.modifiers.has(VSTGUI::ModifierKey::Shift);

        // Store drag start state for fine adjustment
        dragStartX_ = pixelX;
        dragStartY_ = pixelY;
        dragStartMorphX_ = morphX_;
        dragStartMorphY_ = morphY_;

        // Move cursor to click position
        float newX = 0.0f;
        float newY = 0.0f;
        pixelToPosition(pixelX, pixelY, newX, newY);

        // Apply 1D mode constraint
        if (morphMode_ == MorphMode::Linear1D) {
            newY = 0.5f;  // Constrain to horizontal center line
        }

        setMorphPosition(newX, newY);
        setValue(newX);  // Update CControl value for X

        if (listener_) {
            listener_->onMorphPositionChanged(morphX_, morphY_);
        }

        beginEdit();
        valueChanged();

        // Send Y parameter change to host
        if (controller_ && morphYParamId_ != 0) {
            controller_->beginEdit(morphYParamId_);
            controller_->performEdit(morphYParamId_, static_cast<double>(morphY_));
        }

        event.consumed = true;
    }
}

void MorphPad::onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) {
    if (isDragging_) {
        float pixelX = static_cast<float>(event.mousePosition.x);
        float pixelY = static_cast<float>(event.mousePosition.y);

        // Check if Shift state changed during drag
        isFineAdjustment_ = event.modifiers.has(VSTGUI::ModifierKey::Shift);

        float newX = 0.0f;
        float newY = 0.0f;

        if (isFineAdjustment_) {
            // FR-006: Fine adjustment - 10x precision
            float deltaPixelX = pixelX - dragStartX_;
            float deltaPixelY = pixelY - dragStartY_;

            // Convert delta to normalized space and scale by fine adjustment factor
            const auto& rect = getViewSize();
            float innerWidth = static_cast<float>(rect.getWidth()) - 2.0f * kPadding;
            float innerHeight = static_cast<float>(rect.getHeight()) - 2.0f * kPadding;

            if (innerWidth > 0.0f && innerHeight > 0.0f) {
                float deltaNormX = (deltaPixelX / innerWidth) * kFineAdjustmentScale;
                float deltaNormY = (-deltaPixelY / innerHeight) * kFineAdjustmentScale;  // Y inverted

                newX = dragStartMorphX_ + deltaNormX;
                newY = dragStartMorphY_ + deltaNormY;
            }
        } else {
            // Normal drag - direct position mapping
            pixelToPosition(pixelX, pixelY, newX, newY);
        }

        // Apply 1D mode constraint (FR-009)
        if (morphMode_ == MorphMode::Linear1D) {
            newY = 0.5f;  // Constrain to horizontal center line
        }

        // Clamp and update
        setMorphPosition(newX, newY);
        setValue(morphX_);  // Update CControl value for X

        if (listener_) {
            listener_->onMorphPositionChanged(morphX_, morphY_);
        }

        valueChanged();

        // Send Y parameter change to host
        if (controller_ && morphYParamId_ != 0) {
            controller_->performEdit(morphYParamId_, static_cast<double>(morphY_));
        }

        event.consumed = true;
    }
    else if (isDraggingNode_) {
        // Alt+drag node repositioning (FR-007)
        float pixelX = static_cast<float>(event.mousePosition.x);
        float pixelY = static_cast<float>(event.mousePosition.y);

        float newX = 0.0f;
        float newY = 0.0f;
        pixelToPosition(pixelX, pixelY, newX, newY);

        setNodePosition(draggingNodeIndex_, newX, newY);

        if (listener_) {
            listener_->onNodePositionChanged(draggingNodeIndex_, newX, newY);
        }

        event.consumed = true;
    }
}

void MorphPad::onMouseUpEvent(VSTGUI::MouseUpEvent& event) {
    if (isDragging_ || isDraggingNode_) {
        endEdit();

        // End Y parameter editing (paired with beginEdit in onMouseDownEvent)
        if (isDragging_ && controller_ && morphYParamId_ != 0) {
            controller_->endEdit(morphYParamId_);
        }

        isDragging_ = false;
        isDraggingNode_ = false;
        draggingNodeIndex_ = -1;
        isFineAdjustment_ = false;
        event.consumed = true;
    }
}

void MorphPad::onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) {
    // FR-040: Scroll wheel interaction
    // Vertical scroll adjusts X, horizontal scroll adjusts Y

    constexpr float kScrollSensitivity = 0.05f;  // 5% per scroll unit
    float fineScale = event.modifiers.has(VSTGUI::ModifierKey::Shift) ? kFineAdjustmentScale : 1.0f;

    float deltaX = static_cast<float>(event.deltaY) * kScrollSensitivity * fineScale;  // Vertical wheel -> X
    float deltaY = static_cast<float>(event.deltaX) * kScrollSensitivity * fineScale;  // Horizontal wheel -> Y

    float newX = morphX_ + deltaX;
    float newY = morphY_ + deltaY;

    // Apply 1D mode constraint
    if (morphMode_ == MorphMode::Linear1D) {
        newY = 0.5f;
    }

    setMorphPosition(newX, newY);
    setValue(morphX_);

    if (listener_) {
        listener_->onMorphPositionChanged(morphX_, morphY_);
    }

    beginEdit();
    valueChanged();
    endEdit();

    // Send Y parameter change to host
    if (controller_ && morphYParamId_ != 0) {
        controller_->beginEdit(morphYParamId_);
        controller_->performEdit(morphYParamId_, static_cast<double>(morphY_));
        controller_->endEdit(morphYParamId_);
    }

    event.consumed = true;
}

// =============================================================================
// Utility Functions
// =============================================================================

void MorphPad::recalculateWeights() {
    // Calculate inverse distance weights from cursor to each active node
    // Uses inverse square distance for sharper falloff near nodes

    constexpr float kMinDistance = 0.001f;  // Avoid division by zero
    constexpr float kDistanceExponent = 2.0f;  // Squared distance for sharper falloff

    float totalWeight = 0.0f;
    std::array<float, kMaxNodes> rawWeights{};

    for (int i = 0; i < activeNodeCount_; ++i) {
        float dx = morphX_ - nodes_[i].posX;
        float dy = morphY_ - nodes_[i].posY;
        float distance = std::sqrt(dx * dx + dy * dy);

        // Inverse distance weighting (with minimum to avoid infinity)
        if (distance < kMinDistance) {
            // Cursor is essentially on this node - give it maximum weight
            rawWeights[i] = 1000.0f;  // Very high weight
        } else {
            rawWeights[i] = 1.0f / std::pow(distance, kDistanceExponent);
        }
        totalWeight += rawWeights[i];
    }

    // Normalize weights to sum to 1.0
    if (totalWeight > 0.0f) {
        for (int i = 0; i < activeNodeCount_; ++i) {
            nodes_[i].weight = rawWeights[i] / totalWeight;
        }
    }

    // Set weights for inactive nodes to 0
    for (int i = activeNodeCount_; i < kMaxNodes; ++i) {
        nodes_[i].weight = 0.0f;
    }
}

float MorphPad::clampPosition(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace Disrumpo
