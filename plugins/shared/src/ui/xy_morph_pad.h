#pragma once

// ==============================================================================
// XYMorphPad - 2D Morph Position and Spectral Tilt Control
// ==============================================================================
// A shared VSTGUI CControl for controlling two parameters simultaneously via
// a 2D XY pad. Renders a bilinear color gradient background, an interactive
// cursor with crosshair lines, optional modulation visualization, and labels.
//
// X parameter: Controlled via standard CControl tag binding (morph position).
// Y parameter: Controlled via direct performEdit() on the edit controller
//              (spectral tilt), using the dual-parameter pattern from Disrumpo.
//
// Features:
// - Bilinear gradient background with configurable corner colors
// - 16px open cursor circle with 4px center dot
// - Click, drag, Shift+drag (fine 0.1x), double-click (reset to center)
// - Scroll wheel adjustment (horizontal=X, vertical=Y)
// - Escape to cancel drag and restore pre-drag values
// - Modulation range visualization (translucent region)
// - Corner labels (A/B, Dark/Bright) and position label
// - Crosshair alignment lines at cursor position
//
// Registered as "XYMorphPad" via VSTGUI ViewCreator system.
// ==============================================================================

#include "color_utils.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/events.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace Krate::Plugins {

// ==============================================================================
// XYMorphPad Control
// ==============================================================================

class XYMorphPad : public VSTGUI::CControl {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kCursorDiameter = 16.0f;
    static constexpr float kCursorStrokeWidth = 2.0f;
    static constexpr float kCenterDotDiameter = 4.0f;
    static constexpr float kPadding = 8.0f;
    static constexpr float kFineAdjustmentScale = 0.1f;
    static constexpr float kScrollSensitivity = 0.05f;
    static constexpr float kMinDimension = 80.0f;
    static constexpr float kLabelHideThreshold = 100.0f;
    static constexpr int kDefaultGridSize = 24;

    // =========================================================================
    // Construction
    // =========================================================================

    XYMorphPad(const VSTGUI::CRect& size,
               VSTGUI::IControlListener* listener,
               int32_t tag)
        : CControl(size, listener, tag) {}

    XYMorphPad(const XYMorphPad& other)
        : CControl(other)
        , morphX_(other.morphX_)
        , morphY_(other.morphY_)
        , modRangeX_(other.modRangeX_)
        , modRangeY_(other.modRangeY_)
        , gridSize_(other.gridSize_)
        , colorBottomLeft_(other.colorBottomLeft_)
        , colorBottomRight_(other.colorBottomRight_)
        , colorTopLeft_(other.colorTopLeft_)
        , colorTopRight_(other.colorTopRight_)
        , cursorColor_(other.cursorColor_)
        , labelColor_(other.labelColor_)
        , secondaryParamId_(other.secondaryParamId_)
        , crosshairOpacity_(other.crosshairOpacity_) {}

    CLASS_METHODS(XYMorphPad, CControl)

    // =========================================================================
    // Configuration API
    // =========================================================================

    /// @brief Set the edit controller for Y-axis parameter updates.
    void setController(Steinberg::Vst::EditControllerEx1* controller) {
        controller_ = controller;
    }

    /// @brief Set the parameter ID for the Y axis (secondary parameter).
    void setSecondaryParamId(Steinberg::Vst::ParamID id) {
        secondaryParamId_ = id;
    }

    /// @brief Get the secondary parameter ID.
    [[nodiscard]] Steinberg::Vst::ParamID getSecondaryParamId() const {
        return secondaryParamId_;
    }

    /// @brief Set the morph position (both X and Y).
    /// For programmatic updates (from controller). Updates internal state and
    /// triggers redraw without sending parameter changes to the host.
    void setMorphPosition(float x, float y) {
        morphX_ = std::clamp(x, 0.0f, 1.0f);
        morphY_ = std::clamp(y, 0.0f, 1.0f);
        invalid();
    }

    /// @brief Get the morph X position.
    [[nodiscard]] float getMorphX() const { return morphX_; }

    /// @brief Get the morph Y position.
    [[nodiscard]] float getMorphY() const { return morphY_; }

    /// @brief Set the modulation range visualization extents.
    void setModulationRange(float xRange, float yRange) {
        modRangeX_ = xRange;
        modRangeY_ = yRange;
        setDirty();
    }

    /// @brief Get the X modulation range.
    [[nodiscard]] float getModulationRangeX() const { return modRangeX_; }

    /// @brief Get the Y modulation range.
    [[nodiscard]] float getModulationRangeY() const { return modRangeY_; }

    // --- Gradient corner colors ---

    void setColorBottomLeft(VSTGUI::CColor color) { colorBottomLeft_ = color; }
    [[nodiscard]] VSTGUI::CColor getColorBottomLeft() const {
        return colorBottomLeft_;
    }

    void setColorBottomRight(VSTGUI::CColor color) { colorBottomRight_ = color; }
    [[nodiscard]] VSTGUI::CColor getColorBottomRight() const {
        return colorBottomRight_;
    }

    void setColorTopLeft(VSTGUI::CColor color) { colorTopLeft_ = color; }
    [[nodiscard]] VSTGUI::CColor getColorTopLeft() const {
        return colorTopLeft_;
    }

    void setColorTopRight(VSTGUI::CColor color) { colorTopRight_ = color; }
    [[nodiscard]] VSTGUI::CColor getColorTopRight() const {
        return colorTopRight_;
    }

    // --- Cursor and label colors ---

    void setCursorColor(VSTGUI::CColor color) { cursorColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getCursorColor() const { return cursorColor_; }

    void setLabelColor(VSTGUI::CColor color) { labelColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getLabelColor() const { return labelColor_; }

    // --- Crosshair opacity ---

    void setCrosshairOpacity(float opacity) {
        crosshairOpacity_ = std::clamp(opacity, 0.0f, 1.0f);
    }
    [[nodiscard]] float getCrosshairOpacity() const { return crosshairOpacity_; }

    // --- Grid resolution ---

    void setGridSize(int size) {
        gridSize_ = std::clamp(size, 4, 64);
    }
    [[nodiscard]] int getGridSize() const { return gridSize_; }

    // =========================================================================
    // Coordinate Conversion (FR-033, FR-034)
    // =========================================================================

    /// @brief Convert normalized [0,1] position to pixel coordinates.
    /// Y is inverted: normY=0 maps to bottom, normY=1 maps to top.
    void positionToPixel(float normX, float normY,
                         float& outPixelX, float& outPixelY) const {
        VSTGUI::CRect vs = getViewSize();
        float innerWidth = static_cast<float>(vs.getWidth()) - 2.0f * kPadding;
        float innerHeight = static_cast<float>(vs.getHeight()) - 2.0f * kPadding;

        outPixelX = static_cast<float>(vs.left) + kPadding + normX * innerWidth;
        // Y-inverted: normY=1 is top (low pixel), normY=0 is bottom (high pixel)
        outPixelY = static_cast<float>(vs.top) + kPadding +
                    (1.0f - normY) * innerHeight;
    }

    /// @brief Convert pixel coordinates to normalized [0,1] position (clamped).
    /// Y is inverted: bottom pixel maps to normY=0, top pixel maps to normY=1.
    void pixelToPosition(float pixelX, float pixelY,
                         float& outNormX, float& outNormY) const {
        VSTGUI::CRect vs = getViewSize();
        float innerWidth = static_cast<float>(vs.getWidth()) - 2.0f * kPadding;
        float innerHeight = static_cast<float>(vs.getHeight()) - 2.0f * kPadding;

        float rawX = (pixelX - static_cast<float>(vs.left) - kPadding) / innerWidth;
        float rawY = (pixelY - static_cast<float>(vs.top) - kPadding) / innerHeight;

        outNormX = std::clamp(rawX, 0.0f, 1.0f);
        // Y-inverted: low pixel = high normY
        outNormY = std::clamp(1.0f - rawY, 0.0f, 1.0f);
    }

    // =========================================================================
    // CControl Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        const auto& rect = getViewSize();
        if (rect.getWidth() < kMinDimension || rect.getHeight() < kMinDimension) {
            // FR-035: Below minimum size, draw simple background only
            context->setFillColor(colorBottomLeft_);
            context->drawRect(rect, VSTGUI::kDrawFilled);
            setDirty(false);
            return;
        }

        drawGradientBackground(context);
        drawCrosshairs(context);
        drawModulationRegion(context);
        drawCursor(context);
        drawLabels(context);
        setDirty(false);
    }

    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override {
        if (!event.buttonState.isLeft())
            return;

        float pixelX = static_cast<float>(event.mousePosition.x);
        float pixelY = static_cast<float>(event.mousePosition.y);

        // Double-click resets to center (0.5, 0.5) (FR-018)
        if (event.clickCount == 2) {
            beginEdit();
            if (controller_ && secondaryParamId_ != 0) {
                controller_->beginEdit(secondaryParamId_);
            }

            morphX_ = 0.5f;
            morphY_ = 0.5f;
            setValue(0.5f);
            valueChanged();

            if (controller_ && secondaryParamId_ != 0) {
                controller_->performEdit(secondaryParamId_, 0.5);
                controller_->endEdit(secondaryParamId_);
            }

            endEdit();
            invalid();
            event.consumed = true;
            return;
        }

        // Store pre-drag values for Escape cancellation (FR-019)
        preDragMorphX_ = morphX_;
        preDragMorphY_ = morphY_;

        // Store drag start state for fine adjustment (FR-017)
        dragStartPixelX_ = pixelX;
        dragStartPixelY_ = pixelY;
        dragStartMorphX_ = morphX_;
        dragStartMorphY_ = morphY_;

        isDragging_ = true;
        isFineAdjustment_ = event.modifiers.has(VSTGUI::ModifierKey::Shift);

        // Convert click pixel to normalized position
        float newX = 0.0f;
        float newY = 0.0f;
        pixelToPosition(pixelX, pixelY, newX, newY);

        // Update morph position
        morphX_ = newX;
        morphY_ = newY;

        // Begin editing for X (CControl tag) and Y (secondary param)
        beginEdit();
        setValue(morphX_);
        valueChanged();

        if (controller_ && secondaryParamId_ != 0) {
            controller_->beginEdit(secondaryParamId_);
            controller_->performEdit(secondaryParamId_,
                                     static_cast<double>(morphY_));
        }

        invalid();
        event.consumed = true;
    }

    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override {
        if (!isDragging_)
            return;

        float pixelX = static_cast<float>(event.mousePosition.x);
        float pixelY = static_cast<float>(event.mousePosition.y);

        // Detect Shift state change to prevent cursor jump (FR-017)
        bool shiftHeld = event.modifiers.has(VSTGUI::ModifierKey::Shift);
        if (shiftHeld != isFineAdjustment_) {
            // Shift state changed mid-drag - re-anchor drag start
            dragStartPixelX_ = pixelX;
            dragStartPixelY_ = pixelY;
            dragStartMorphX_ = morphX_;
            dragStartMorphY_ = morphY_;
            isFineAdjustment_ = shiftHeld;
        }

        float newX = 0.0f;
        float newY = 0.0f;

        if (isFineAdjustment_) {
            // Fine adjustment mode - 0.1x scale relative to drag start
            float deltaPixelX = pixelX - dragStartPixelX_;
            float deltaPixelY = pixelY - dragStartPixelY_;

            VSTGUI::CRect vs = getViewSize();
            float innerWidth =
                static_cast<float>(vs.getWidth()) - 2.0f * kPadding;
            float innerHeight =
                static_cast<float>(vs.getHeight()) - 2.0f * kPadding;

            if (innerWidth > 0.0f && innerHeight > 0.0f) {
                float deltaNormX =
                    (deltaPixelX / innerWidth) * kFineAdjustmentScale;
                // Y inverted: moving mouse down = negative deltaPixelY
                float deltaNormY =
                    (-deltaPixelY / innerHeight) * kFineAdjustmentScale;

                newX = std::clamp(dragStartMorphX_ + deltaNormX, 0.0f, 1.0f);
                newY = std::clamp(dragStartMorphY_ + deltaNormY, 0.0f, 1.0f);
            } else {
                newX = morphX_;
                newY = morphY_;
            }
        } else {
            // Normal drag - absolute position mapping
            pixelToPosition(pixelX, pixelY, newX, newY);
        }

        // Update morph position
        morphX_ = newX;
        morphY_ = newY;

        // Send X via CControl
        setValue(morphX_);
        valueChanged();

        // Send Y via secondary parameter
        if (controller_ && secondaryParamId_ != 0) {
            controller_->performEdit(secondaryParamId_,
                                     static_cast<double>(morphY_));
        }

        invalid();
        event.consumed = true;
    }

    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override {
        if (!isDragging_)
            return;

        // End editing for X (CControl tag)
        endEdit();

        // End editing for Y (secondary param)
        if (controller_ && secondaryParamId_ != 0) {
            controller_->endEdit(secondaryParamId_);
        }

        isDragging_ = false;
        isFineAdjustment_ = false;
        event.consumed = true;
    }

    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override {
        float fineScale = event.modifiers.has(VSTGUI::ModifierKey::Shift)
            ? kFineAdjustmentScale : 1.0f;

        // Vertical scroll -> Y (tilt), Horizontal scroll -> X (morph)
        float deltaY = static_cast<float>(event.deltaY) * kScrollSensitivity
                        * fineScale;
        float deltaX = static_cast<float>(event.deltaX) * kScrollSensitivity
                        * fineScale;

        if (std::abs(deltaX) < 0.0001f && std::abs(deltaY) < 0.0001f)
            return;

        float newX = std::clamp(morphX_ + deltaX, 0.0f, 1.0f);
        float newY = std::clamp(morphY_ + deltaY, 0.0f, 1.0f);

        morphX_ = newX;
        morphY_ = newY;

        beginEdit();
        setValue(morphX_);
        valueChanged();
        endEdit();

        if (controller_ && secondaryParamId_ != 0) {
            controller_->beginEdit(secondaryParamId_);
            controller_->performEdit(secondaryParamId_,
                                     static_cast<double>(morphY_));
            controller_->endEdit(secondaryParamId_);
        }

        invalid();
        event.consumed = true;
    }
    void onKeyboardEvent(VSTGUI::KeyboardEvent& event) override {
        if (event.type != VSTGUI::EventType::KeyDown)
            return;

        if (isDragging_ && event.virt == VSTGUI::VirtualKey::Escape) {
            // Restore pre-drag values
            morphX_ = preDragMorphX_;
            morphY_ = preDragMorphY_;
            setValue(morphX_);
            valueChanged();

            if (controller_ && secondaryParamId_ != 0) {
                controller_->performEdit(secondaryParamId_,
                    static_cast<double>(morphY_));
                controller_->endEdit(secondaryParamId_);
            }

            endEdit();
            isDragging_ = false;
            isFineAdjustment_ = false;
            invalid();
            event.consumed = true;
        }
    }

private:
    // =========================================================================
    // Internal Drawing Methods
    // =========================================================================

    void drawGradientBackground(VSTGUI::CDrawContext* context) {
        VSTGUI::CRect vs = getViewSize();
        float padWidth = static_cast<float>(vs.getWidth());
        float padHeight = static_cast<float>(vs.getHeight());

        float cellWidth = padWidth / static_cast<float>(gridSize_);
        float cellHeight = padHeight / static_cast<float>(gridSize_);

        for (int row = 0; row < gridSize_; ++row) {
            for (int col = 0; col < gridSize_; ++col) {
                // Calculate normalized center position of this cell
                float tx = (static_cast<float>(col) + 0.5f) /
                           static_cast<float>(gridSize_);
                // Y-axis: ty=0 at bottom, ty=1 at top
                // Row 0 is top of screen (pixel), so invert for color calc
                float ty = 1.0f - (static_cast<float>(row) + 0.5f) /
                                   static_cast<float>(gridSize_);

                VSTGUI::CColor cellColor = bilinearColor(
                    colorBottomLeft_, colorBottomRight_,
                    colorTopLeft_, colorTopRight_, tx, ty);

                float x = static_cast<float>(vs.left) +
                          static_cast<float>(col) * cellWidth;
                float y = static_cast<float>(vs.top) +
                          static_cast<float>(row) * cellHeight;

                VSTGUI::CRect cellRect(
                    static_cast<double>(x),
                    static_cast<double>(y),
                    static_cast<double>(x + cellWidth),
                    static_cast<double>(y + cellHeight));

                context->setFillColor(cellColor);
                context->drawRect(cellRect, VSTGUI::kDrawFilled);
            }
        }

        // Draw thin border around the pad
        context->setFrameColor(VSTGUI::CColor{128, 128, 128, 255});
        context->setLineWidth(1.0);
        context->drawRect(vs, VSTGUI::kDrawStroked);
    }

    void drawCrosshairs(VSTGUI::CDrawContext* context) {
        if (crosshairOpacity_ < 0.001f)
            return;

        float cursorPixelX = 0.0f;
        float cursorPixelY = 0.0f;
        positionToPixel(morphX_, morphY_, cursorPixelX, cursorPixelY);

        const auto& rect = getViewSize();
        auto alpha = static_cast<uint8_t>(crosshairOpacity_ * 255.0f);
        VSTGUI::CColor crosshairColor{255, 255, 255, alpha};

        context->setFrameColor(crosshairColor);
        context->setLineWidth(1.0);
        context->setLineStyle(VSTGUI::kLineSolid);

        // Vertical crosshair line at cursor X
        context->drawLine(
            VSTGUI::CPoint(cursorPixelX, rect.top),
            VSTGUI::CPoint(cursorPixelX, rect.bottom));

        // Horizontal crosshair line at cursor Y
        context->drawLine(
            VSTGUI::CPoint(rect.left, cursorPixelY),
            VSTGUI::CPoint(rect.right, cursorPixelY));
    }
    void drawModulationRegion(VSTGUI::CDrawContext* context) {
        if (std::abs(modRangeX_) < 0.001f && std::abs(modRangeY_) < 0.001f)
            return;

        // Get cursor pixel position
        float cursorPixelX = 0.0f;
        float cursorPixelY = 0.0f;
        positionToPixel(morphX_, morphY_, cursorPixelX, cursorPixelY);

        // Calculate modulation extents in pixels
        const auto& rect = getViewSize();
        float innerWidth =
            static_cast<float>(rect.getWidth()) - 2.0f * kPadding;
        float innerHeight =
            static_cast<float>(rect.getHeight()) - 2.0f * kPadding;

        // Translucent fill color (cyan-ish, matching ArcKnob mod color)
        VSTGUI::CColor modFillColor{100, 200, 255, 50};

        if (std::abs(modRangeX_) >= 0.001f &&
            std::abs(modRangeY_) < 0.001f) {
            // X-only: horizontal stripe
            float leftX = cursorPixelX -
                           std::abs(modRangeX_) * innerWidth;
            float rightX = cursorPixelX +
                            std::abs(modRangeX_) * innerWidth;
            float stripeTop = static_cast<float>(rect.top);
            float stripeBottom = static_cast<float>(rect.bottom);
            VSTGUI::CRect modRect(leftX, stripeTop, rightX, stripeBottom);
            context->setFillColor(modFillColor);
            context->drawRect(modRect, VSTGUI::kDrawFilled);
        } else if (std::abs(modRangeX_) < 0.001f &&
                   std::abs(modRangeY_) >= 0.001f) {
            // Y-only: vertical stripe
            float topY = cursorPixelY -
                          std::abs(modRangeY_) * innerHeight;
            float bottomY = cursorPixelY +
                             std::abs(modRangeY_) * innerHeight;
            float stripeLeft = static_cast<float>(rect.left);
            float stripeRight = static_cast<float>(rect.right);
            VSTGUI::CRect modRect(stripeLeft, topY, stripeRight, bottomY);
            context->setFillColor(modFillColor);
            context->drawRect(modRect, VSTGUI::kDrawFilled);
        } else {
            // Both X and Y: 2D rectangular region
            float leftX = cursorPixelX -
                           std::abs(modRangeX_) * innerWidth;
            float rightX = cursorPixelX +
                            std::abs(modRangeX_) * innerWidth;
            float topY = cursorPixelY -
                          std::abs(modRangeY_) * innerHeight;
            float bottomY = cursorPixelY +
                             std::abs(modRangeY_) * innerHeight;
            VSTGUI::CRect modRect(leftX, topY, rightX, bottomY);
            context->setFillColor(modFillColor);
            context->drawRect(modRect, VSTGUI::kDrawFilled);
        }
    }

    void drawCursor(VSTGUI::CDrawContext* context) {
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        positionToPixel(morphX_, morphY_, pixelX, pixelY);

        // Draw 16px open circle with 2px stroke
        float radius = kCursorDiameter * 0.5f;
        VSTGUI::CRect cursorRect(
            static_cast<double>(pixelX - radius),
            static_cast<double>(pixelY - radius),
            static_cast<double>(pixelX + radius),
            static_cast<double>(pixelY + radius));

        context->setFrameColor(cursorColor_);
        context->setLineWidth(kCursorStrokeWidth);
        context->drawEllipse(cursorRect, VSTGUI::kDrawStroked);

        // Draw 4px filled center dot
        float dotRadius = kCenterDotDiameter * 0.5f;
        VSTGUI::CRect dotRect(
            static_cast<double>(pixelX - dotRadius),
            static_cast<double>(pixelY - dotRadius),
            static_cast<double>(pixelX + dotRadius),
            static_cast<double>(pixelY + dotRadius));

        context->setFillColor(cursorColor_);
        context->drawEllipse(dotRect, VSTGUI::kDrawFilled);
    }

    void drawLabels(VSTGUI::CDrawContext* context) {
        const auto& rect = getViewSize();

        // FR-014a: Hide all labels if either dimension is below 100px
        if (rect.getWidth() < kLabelHideThreshold ||
            rect.getHeight() < kLabelHideThreshold)
            return;

        context->setFontColor(labelColor_);
        context->setFont(VSTGUI::kNormalFontSmall);

        // FR-012: "A" at bottom-left, "B" at bottom-right
        VSTGUI::CRect labelA(
            rect.left + 4.0, rect.bottom - 16.0,
            rect.left + 20.0, rect.bottom - 2.0);
        context->drawString("A", labelA, VSTGUI::kLeftText);

        VSTGUI::CRect labelB(
            rect.right - 20.0, rect.bottom - 16.0,
            rect.right - 4.0, rect.bottom - 2.0);
        context->drawString("B", labelB, VSTGUI::kRightText);

        // FR-013: "Dark" at bottom-center, "Bright" at top-center
        double centerX = rect.left + rect.getWidth() / 2.0;

        VSTGUI::CRect labelDark(
            centerX - 20.0, rect.bottom - 16.0,
            centerX + 20.0, rect.bottom - 2.0);
        context->drawString("Dark", labelDark, VSTGUI::kCenterText);

        VSTGUI::CRect labelBright(
            centerX - 25.0, rect.top + 2.0,
            centerX + 25.0, rect.top + 16.0);
        context->drawString("Bright", labelBright, VSTGUI::kCenterText);

        // FR-014: Position label "Mix: 0.XX  Tilt: +Y.YdB"
        // Denormalize tilt: tilt_dB = -12 + morphY_ * 24
        float tiltDb = -12.0f + morphY_ * 24.0f;

        std::ostringstream oss;
        oss << std::fixed;
        oss << "Mix: " << std::setprecision(2) << morphX_ << "  Tilt: ";
        if (tiltDb >= 0.0f)
            oss << "+";
        oss << std::setprecision(1) << tiltDb << "dB";

        // Position label to the right of "A", avoiding overlap
        VSTGUI::CRect posLabel(
            rect.left + 24.0, rect.bottom - 16.0,
            rect.left + 200.0, rect.bottom - 2.0);
        context->drawString(oss.str().c_str(), posLabel, VSTGUI::kLeftText);
    }

    // =========================================================================
    // State
    // =========================================================================

    // Morph position [0, 1]
    float morphX_ = 0.5f;
    float morphY_ = 0.5f;

    // Modulation range (bipolar)
    float modRangeX_ = 0.0f;
    float modRangeY_ = 0.0f;

    // Grid resolution for gradient rendering
    int gridSize_ = kDefaultGridSize;

    // Drag state
    bool isDragging_ = false;
    bool isFineAdjustment_ = false;
    float preDragMorphX_ = 0.0f;
    float preDragMorphY_ = 0.0f;
    float dragStartPixelX_ = 0.0f;
    float dragStartPixelY_ = 0.0f;
    float dragStartMorphX_ = 0.0f;
    float dragStartMorphY_ = 0.0f;

    // Controller for Y-axis parameter updates
    Steinberg::Vst::EditControllerEx1* controller_ = nullptr;
    Steinberg::Vst::ParamID secondaryParamId_ = 0;

    // Gradient corner colors
    VSTGUI::CColor colorBottomLeft_{48, 84, 120, 255};
    VSTGUI::CColor colorBottomRight_{132, 102, 36, 255};
    VSTGUI::CColor colorTopLeft_{80, 140, 200, 255};
    VSTGUI::CColor colorTopRight_{220, 170, 60, 255};

    // Cursor and label colors
    VSTGUI::CColor cursorColor_{255, 255, 255, 255};
    VSTGUI::CColor labelColor_{170, 170, 170, 255};

    // Crosshair opacity
    float crosshairOpacity_ = 0.12f;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================
// Registers "XYMorphPad" with the VSTGUI UIViewFactory.
// getBaseViewName() -> "CControl" ensures all CControl attributes
// (control-tag, default-value, min-value, max-value, etc.) are applied.

struct XYMorphPadCreator : VSTGUI::ViewCreatorAdapter {
    XYMorphPadCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return "XYMorphPad"; }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "XY Morph Pad";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new XYMorphPad(VSTGUI::CRect(0, 0, 200, 150), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* pad = dynamic_cast<XYMorphPad*>(view);
        if (!pad)
            return false;

        // Color attributes
        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("color-bottom-left"),
                color, description))
            pad->setColorBottomLeft(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("color-bottom-right"),
                color, description))
            pad->setColorBottomRight(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("color-top-left"),
                color, description))
            pad->setColorTopLeft(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("color-top-right"),
                color, description))
            pad->setColorTopRight(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("cursor-color"),
                color, description))
            pad->setCursorColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("label-color"),
                color, description))
            pad->setLabelColor(color);

        // Float attributes
        double d = 0.0;
        if (attributes.getDoubleAttribute("crosshair-opacity", d))
            pad->setCrosshairOpacity(static_cast<float>(d));

        // Integer attributes
        int32_t intVal = 0;
        if (attributes.getIntegerAttribute("grid-size", intVal))
            pad->setGridSize(static_cast<int>(intVal));

        // Secondary tag attribute (kTagType)
        const auto* secondaryTagAttr =
            attributes.getAttributeValue("secondary-tag");
        if (secondaryTagAttr && !secondaryTagAttr->empty()) {
            int32_t tag = description->getTagForName(secondaryTagAttr->c_str());
            if (tag != -1) {
                pad->setSecondaryParamId(
                    static_cast<Steinberg::Vst::ParamID>(tag));
            } else {
                // Try parsing as a numeric ID
                char* endPtr = nullptr;
                auto numTag = static_cast<int32_t>(
                    strtol(secondaryTagAttr->c_str(), &endPtr, 10));
                if (endPtr != secondaryTagAttr->c_str()) {
                    pad->setSecondaryParamId(
                        static_cast<Steinberg::Vst::ParamID>(numTag));
                }
            }
        }

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("color-bottom-left");
        attributeNames.emplace_back("color-bottom-right");
        attributeNames.emplace_back("color-top-left");
        attributeNames.emplace_back("color-top-right");
        attributeNames.emplace_back("cursor-color");
        attributeNames.emplace_back("label-color");
        attributeNames.emplace_back("crosshair-opacity");
        attributeNames.emplace_back("grid-size");
        attributeNames.emplace_back("secondary-tag");
        return true;
    }

    AttrType getAttributeType(
        const std::string& attributeName) const override {
        if (attributeName == "color-bottom-left") return kColorType;
        if (attributeName == "color-bottom-right") return kColorType;
        if (attributeName == "color-top-left") return kColorType;
        if (attributeName == "color-top-right") return kColorType;
        if (attributeName == "cursor-color") return kColorType;
        if (attributeName == "label-color") return kColorType;
        if (attributeName == "crosshair-opacity") return kFloatType;
        if (attributeName == "grid-size") return kIntegerType;
        if (attributeName == "secondary-tag") return kTagType;
        return kUnknownType;
    }

    bool getAttributeValue(
        VSTGUI::CView* view,
        const std::string& attributeName,
        std::string& stringValue,
        const VSTGUI::IUIDescription* desc) const override {
        auto* pad = dynamic_cast<XYMorphPad*>(view);
        if (!pad)
            return false;

        if (attributeName == "color-bottom-left") {
            VSTGUI::UIViewCreator::colorToString(
                pad->getColorBottomLeft(), stringValue, desc);
            return true;
        }
        if (attributeName == "color-bottom-right") {
            VSTGUI::UIViewCreator::colorToString(
                pad->getColorBottomRight(), stringValue, desc);
            return true;
        }
        if (attributeName == "color-top-left") {
            VSTGUI::UIViewCreator::colorToString(
                pad->getColorTopLeft(), stringValue, desc);
            return true;
        }
        if (attributeName == "color-top-right") {
            VSTGUI::UIViewCreator::colorToString(
                pad->getColorTopRight(), stringValue, desc);
            return true;
        }
        if (attributeName == "cursor-color") {
            VSTGUI::UIViewCreator::colorToString(
                pad->getCursorColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "label-color") {
            VSTGUI::UIViewCreator::colorToString(
                pad->getLabelColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "crosshair-opacity") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                static_cast<double>(pad->getCrosshairOpacity()));
            return true;
        }
        if (attributeName == "grid-size") {
            stringValue = std::to_string(pad->getGridSize());
            return true;
        }
        if (attributeName == "secondary-tag") {
            auto paramId = pad->getSecondaryParamId();
            if (paramId != 0) {
                VSTGUI::UTF8StringPtr tagName =
                    desc->lookupControlTagName(
                        static_cast<int32_t>(paramId));
                if (tagName) {
                    stringValue = tagName;
                    return true;
                }
            }
            return false;
        }
        return false;
    }
};

// Inline variable (C++17) - safe for inclusion from multiple translation units.
// Include this header from each plugin's entry.cpp to register the view type.
inline XYMorphPadCreator gXYMorphPadCreator;

} // namespace Krate::Plugins
