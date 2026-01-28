// ==============================================================================
// MorphPad Custom View Implementation
// ==============================================================================
// FR-001 through FR-012: 2D morph position control with node visualization.
// ==============================================================================

#include "morph_pad.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace Disrumpo {

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
// Constructor
// =============================================================================

MorphPad::MorphPad(const VSTGUI::CRect& size)
    : CControl(size)
{
    // Initialize default node positions (corners for 4-node mode)
    resetNodePositionsToDefault();
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
    invalid();
}

void MorphPad::setMorphMode(MorphMode mode) {
    morphMode_ = mode;
    invalid();
}

void MorphPad::setMorphPosition(float x, float y) {
    morphX_ = clampPosition(x);
    morphY_ = clampPosition(y);
    invalid();
}

void MorphPad::setNodePosition(int nodeIndex, float x, float y) {
    if (nodeIndex >= 0 && nodeIndex < kMaxNodes) {
        nodes_[nodeIndex].posX = clampPosition(x);
        nodes_[nodeIndex].posY = clampPosition(y);
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

    // Background fill
    context->setFillColor(VSTGUI::CColor{0x1E, 0x1E, 0x1E, 0xFF});  // Dark gray
    context->drawRect(rect, VSTGUI::kDrawFilled);

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
            float angle = static_cast<float>(i) * 3.14159265f / 4.0f;
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

        // Get node color based on its distortion family
        DistortionFamily family = getFamily(nodes_[i].type);
        VSTGUI::CColor nodeColor = getCategoryColor(family);

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
    // FR-002: 12px diameter filled circles with category-specific colors

    for (int i = 0; i < activeNodeCount_; ++i) {
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        positionToPixel(nodes_[i].posX, nodes_[i].posY, pixelX, pixelY);

        // Get color based on distortion family
        DistortionFamily family = getFamily(nodes_[i].type);
        VSTGUI::CColor fillColor = getCategoryColor(family);

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

void MorphPad::drawCursor(VSTGUI::CDrawContext* context) {
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

        // Check for Alt+drag node repositioning (FR-007)
        if (event.modifiers.has(VSTGUI::ModifierKey::Alt)) {
            int hitNode = hitTestNode(pixelX, pixelY);
            if (hitNode >= 0) {
                isDraggingNode_ = true;
                draggingNodeIndex_ = hitNode;
                isDragging_ = false;
                event.consumed = true;
                return;
            }
        }

        // Check for node selection (FR-025, FR-027)
        int hitNode = hitTestNode(pixelX, pixelY);
        if (hitNode >= 0) {
            setSelectedNode(hitNode);
            if (listener_) {
                listener_->onNodeSelected(hitNode);
            }
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
            valueChanged();
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
        isDragging_ = false;
        isDraggingNode_ = false;
        draggingNodeIndex_ = -1;
        isFineAdjustment_ = false;
        event.consumed = true;
    }
}

// =============================================================================
// Utility Functions
// =============================================================================

float MorphPad::clampPosition(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace Disrumpo
