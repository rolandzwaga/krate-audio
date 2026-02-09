// =============================================================================
// TapPatternEditor Implementation
// =============================================================================
// Spec 046: Custom Tap Pattern Editor
// =============================================================================

#include "tap_pattern_editor.h"

#include <utility>
#include "vstgui/lib/cframe.h"

namespace Iterum {

TapPatternEditor::TapPatternEditor(const VSTGUI::CRect& size)
    : CControl(size, nullptr, -1)
{
    // Initialize with evenly spaced default pattern
    for (size_t i = 0; i < kMaxPatternTaps; ++i) {
        tapTimeRatios_[i] = calculateDefaultTapTime(i, kMaxPatternTaps);
        tapLevels_[i] = kDefaultTapLevel;
    }
}

// =============================================================================
// Drawing
// =============================================================================

void TapPatternEditor::draw(VSTGUI::CDrawContext* context) {
    drawBackground(context);
    drawGridLines(context);
    drawTaps(context);
    drawLabels(context);
    drawRuler(context);

    setDirty(false);
}

void TapPatternEditor::drawBackground(VSTGUI::CDrawContext* context) {
    auto viewRect = getViewSize();

    // Fill background
    context->setFillColor(kBackgroundColor);
    context->drawRect(viewRect, VSTGUI::kDrawFilled);

    // Draw border
    context->setFrameColor(kBorderColor);
    context->setLineWidth(1.0);
    context->drawRect(viewRect, VSTGUI::kDrawStroked);
}

void TapPatternEditor::drawGridLines(VSTGUI::CDrawContext* context) {
    auto viewRect = getViewSize();
    float width = static_cast<float>(viewRect.getWidth());
    // Tap area height excludes the ruler at bottom
    float tapAreaHeight = static_cast<float>(viewRect.getHeight()) - kRulerHeight;
    float tapAreaBottom = static_cast<float>(viewRect.bottom) - kRulerHeight;

    context->setFrameColor(kGridColor);
    context->setLineWidth(1.0);

    // Vertical grid lines at 1/4 intervals (stop at ruler)
    for (int i = 1; i < 4; ++i) {
        float x = static_cast<float>(viewRect.left) + width * (static_cast<float>(i) / 4.0f);
        context->drawLine(
            VSTGUI::CPoint(x, viewRect.top),
            VSTGUI::CPoint(x, tapAreaBottom));
    }

    // Horizontal grid line at 50% level (within tap area)
    float y = static_cast<float>(viewRect.top) + tapAreaHeight * 0.5f;
    context->drawLine(
        VSTGUI::CPoint(viewRect.left, y),
        VSTGUI::CPoint(viewRect.right, y));
}

void TapPatternEditor::drawTaps(VSTGUI::CDrawContext* context) {
    auto viewRect = getViewSize();
    float width = static_cast<float>(viewRect.getWidth());
    // Tap area height excludes the ruler at bottom
    float tapAreaHeight = static_cast<float>(viewRect.getHeight()) - kRulerHeight;

    // Set up font for tap numbers
    auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 8);
    context->setFont(font);

    for (size_t i = 0; i < activeTapCount_; ++i) {
        float timeRatio = tapTimeRatios_[i];
        float level = tapLevels_[i];

        // Calculate tap bar position (within tap area, above ruler)
        float centerX = static_cast<float>(viewRect.left) + timeRatio * width;
        float barTop = static_cast<float>(viewRect.top) + (1.0f - level) * tapAreaHeight;
        float barBottom = static_cast<float>(viewRect.bottom) - kRulerHeight;

        // Tap bar rectangle
        float halfBarWidth = kTapBarWidth / 2.0f;
        VSTGUI::CRect tapRect(
            centerX - halfBarWidth,
            barTop,
            centerX + halfBarWidth,
            barBottom
        );

        // Choose color based on selection
        bool isSelected = (std::cmp_equal(i, selectedTap_));
        context->setFillColor(isSelected ? kTapSelectedColor : kTapColor);
        context->drawRect(tapRect, VSTGUI::kDrawFilled);

        // Draw tap handle at top of bar
        VSTGUI::CRect handleRect(
            centerX - halfBarWidth,
            barTop,
            centerX + halfBarWidth,
            barTop + 8.0f
        );
        // Handle color: white when hovered, lighter blue when selected, normal blue otherwise
        bool isHandleHovered = (std::cmp_equal(i, hoveredHandleTap_));
        VSTGUI::CColor handleColor;
        if (isHandleHovered) {
            handleColor = VSTGUI::CColor(255, 255, 255, 255);  // White when hovered
        } else if (isSelected) {
            handleColor = VSTGUI::CColor(180, 220, 255, 255);  // Lighter blue when selected
        } else {
            handleColor = VSTGUI::CColor(120, 180, 220, 255);  // Normal blue
        }
        context->setFillColor(handleColor);
        context->drawRect(handleRect, VSTGUI::kDrawFilled);

        // Draw tap number label at bottom of bar
        char tapNumStr[4];
        snprintf(tapNumStr, sizeof(tapNumStr), "%zu", i + 1);
        VSTGUI::CRect labelRect(
            centerX - halfBarWidth - 2,
            barBottom - 12,
            centerX + halfBarWidth + 2,
            barBottom - 1
        );
        context->setFontColor(VSTGUI::CColor(255, 255, 255, 200));
        context->drawString(tapNumStr, labelRect, VSTGUI::kCenterText);
    }
}

void TapPatternEditor::drawLabels(VSTGUI::CDrawContext* context) {
    auto viewRect = getViewSize();

    // Set up font
    auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 9);
    context->setFont(font);
    context->setFontColor(kTextColor);

    // Level labels on left side
    VSTGUI::CRect labelRect100(viewRect.left + 2, viewRect.top, viewRect.left + 30, viewRect.top + 12);
    context->drawString("100%", labelRect100, VSTGUI::kLeftText);

    // Adjust label position to account for ruler at bottom
    float rulerTop = static_cast<float>(viewRect.bottom) - kRulerHeight;
    VSTGUI::CRect labelRect0(viewRect.left + 2, rulerTop - 12, viewRect.left + 30, rulerTop);
    context->drawString("0%", labelRect0, VSTGUI::kLeftText);
}

void TapPatternEditor::drawRuler(VSTGUI::CDrawContext* context) {
    auto viewRect = getViewSize();
    float width = static_cast<float>(viewRect.getWidth());
    float rulerTop = static_cast<float>(viewRect.bottom) - kRulerHeight;
    float rulerBottom = static_cast<float>(viewRect.bottom);

    // Draw ruler background (slightly darker than main background)
    VSTGUI::CRect rulerRect(viewRect.left, rulerTop, viewRect.right, rulerBottom);
    context->setFillColor(VSTGUI::CColor(30, 30, 33, 255));
    context->drawRect(rulerRect, VSTGUI::kDrawFilled);

    // Draw horizontal baseline at top of ruler
    context->setFrameColor(kGridColor);
    context->setLineWidth(1.0);
    context->drawLine(
        VSTGUI::CPoint(viewRect.left, rulerTop),
        VSTGUI::CPoint(viewRect.right, rulerTop));

    // Get number of divisions based on snap setting
    int divisions = getSnapDivisions(snapDivision_);

    // If snap is off, just show major divisions (0, 0.25, 0.5, 0.75, 1.0)
    if (divisions == 0) {
        divisions = 4;  // Show quarter markers when snap is off
    }

    // Draw tick marks
    context->setFrameColor(kTextColor);

    for (int i = 0; i <= divisions; ++i) {
        float ratio = static_cast<float>(i) / static_cast<float>(divisions);
        float x = static_cast<float>(viewRect.left) + ratio * width;

        // Determine if this is a major tick (at 1/4 positions: 0, 0.25, 0.5, 0.75, 1.0)
        bool isMajorTick = false;
        if (divisions >= 4) {
            // Check if this tick falls on a quarter position
            float quarterCheck = ratio * 4.0f;
            isMajorTick = (std::abs(quarterCheck - std::round(quarterCheck)) < 0.001f);
        } else {
            // For triplets (12 divisions), all are minor except 0 and 1
            isMajorTick = (i == 0 || i == divisions);
        }

        float tickHeight = isMajorTick ? kRulerMajorTickHeight : kRulerMinorTickHeight;
        float tickTop = rulerTop + 2.0f;  // Small gap from baseline

        context->drawLine(
            VSTGUI::CPoint(x, tickTop),
            VSTGUI::CPoint(x, tickTop + tickHeight));
    }
}

// =============================================================================
// Coordinate Conversion
// =============================================================================

float TapPatternEditor::xToTimeRatio(float x) const {
    auto viewRect = getViewSize();
    float localX = x - static_cast<float>(viewRect.left);
    float width = static_cast<float>(viewRect.getWidth());
    return positionToTimeRatio(localX, width);
}

float TapPatternEditor::yToLevel(float y) const {
    auto viewRect = getViewSize();
    float localY = y - static_cast<float>(viewRect.top);
    // Use tap area height (excluding ruler) for level calculation
    float tapAreaHeight = static_cast<float>(viewRect.getHeight()) - kRulerHeight;
    return levelFromYPosition(localY, tapAreaHeight);
}

float TapPatternEditor::timeRatioToX(float ratio) const {
    auto viewRect = getViewSize();
    float width = static_cast<float>(viewRect.getWidth());
    return static_cast<float>(viewRect.left) + Iterum::timeRatioToPosition(ratio, width);
}

float TapPatternEditor::levelToY(float level) const {
    auto viewRect = getViewSize();
    // Use tap area height (excluding ruler) for level calculation
    float tapAreaHeight = static_cast<float>(viewRect.getHeight()) - kRulerHeight;
    return static_cast<float>(viewRect.top) + Iterum::levelToYPosition(level, tapAreaHeight);
}

// =============================================================================
// Hit Testing
// =============================================================================

int TapPatternEditor::hitTestTapAtPoint(float x, float y) const {
    auto viewRect = getViewSize();
    float localX = x - static_cast<float>(viewRect.left);
    float localY = y - static_cast<float>(viewRect.top);
    float width = static_cast<float>(viewRect.getWidth());
    // Use tap area height (excluding ruler) for hit testing
    float tapAreaHeight = static_cast<float>(viewRect.getHeight()) - kRulerHeight;

    return hitTestTap(
        localX, localY,
        tapTimeRatios_.data(), tapLevels_.data(),
        activeTapCount_,
        width, tapAreaHeight
    );
}

// =============================================================================
// Mouse Handling
// =============================================================================

VSTGUI::CMouseEventResult TapPatternEditor::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons)
{
    // Ignore right-click (T018.5)
    if (buttons.isRightButton()) {
        return VSTGUI::kMouseEventNotHandled;
    }

    float x = static_cast<float>(where.x);
    float y = static_cast<float>(where.y);

    int tapIndex = hitTestTapAtPoint(x, y);
    if (tapIndex < 0) {
        return VSTGUI::kMouseEventNotHandled;
    }

    // Double-click resets tap to default (T018.3)
    if (buttons.isDoubleClick()) {
        float defaultTime = calculateDefaultTapTime(
            static_cast<size_t>(tapIndex), activeTapCount_);
        float defaultLevel = kDefaultTapLevel;

        beginEdit();
        setTapTimeRatio(static_cast<size_t>(tapIndex), defaultTime);
        setTapLevel(static_cast<size_t>(tapIndex), defaultLevel);
        notifyTimeRatioChanged(static_cast<size_t>(tapIndex), defaultTime);
        notifyLevelChanged(static_cast<size_t>(tapIndex), defaultLevel);
        endEdit();

        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    // Start drag
    selectedTap_ = tapIndex;
    isDragging_ = true;

    // Store pre-drag values for Escape cancellation (T018.4)
    preDragTimeRatio_ = tapTimeRatios_[static_cast<size_t>(tapIndex)];
    preDragLevel_ = tapLevels_[static_cast<size_t>(tapIndex)];
    dragStartX_ = x;
    dragStartY_ = y;

    // Check if click started on handle (enables Y-axis control)
    // Handle is at top of bar, height = kTapHandleHeight
    auto viewRect = getViewSize();
    float tapAreaHeight = static_cast<float>(viewRect.getHeight()) - kRulerHeight;
    float barTop = static_cast<float>(viewRect.top) + (1.0f - preDragLevel_) * tapAreaHeight;
    float handleBottom = barTop + kTapHandleHeight;
    dragStartedOnHandle_ = (y >= barTop && y <= handleBottom);

    beginEdit();
    invalid();

    return VSTGUI::kMouseEventHandled;
}

VSTGUI::CMouseEventResult TapPatternEditor::onMouseMoved(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons)
{
    float x = static_cast<float>(where.x);
    float y = static_cast<float>(where.y);

    if (!isDragging_ || selectedTap_ < 0) {
        // Not dragging - update cursor based on hover position
        updateCursorForPosition(x, y);
        return VSTGUI::kMouseEventNotHandled;
    }

    // Calculate new values (clamped via logic functions - T018.1)
    float newTimeRatio = xToTimeRatio(x);

    // Level only changes if drag started on handle (top of bar)
    // Otherwise, only horizontal (time) movement is allowed
    float newLevel = dragStartedOnHandle_ ? yToLevel(y) : preDragLevel_;

    // Apply axis constraint if Shift is held (T018.2)
    if (buttons.isShiftSet()) {
        float deltaX = x - dragStartX_;
        float deltaY = y - dragStartY_;
        ConstraintAxis axis = determineConstraintAxis(deltaX, deltaY);

        auto [constrainedTime, constrainedLevel] = applyAxisConstraint(
            newTimeRatio, newLevel,
            preDragTimeRatio_, preDragLevel_,
            axis
        );
        newTimeRatio = constrainedTime;
        newLevel = constrainedLevel;
    }

    // Apply grid snapping to time ratio (Phase 5 - T057)
    newTimeRatio = snapToGrid(newTimeRatio, snapDivision_);

    // Update tap values
    size_t tapIdx = static_cast<size_t>(selectedTap_);
    tapTimeRatios_[tapIdx] = newTimeRatio;
    tapLevels_[tapIdx] = newLevel;

    // Notify parameter changes
    notifyTimeRatioChanged(tapIdx, newTimeRatio);
    notifyLevelChanged(tapIdx, newLevel);

    invalid();
    return VSTGUI::kMouseEventHandled;
}

VSTGUI::CMouseEventResult TapPatternEditor::onMouseUp(
    VSTGUI::CPoint& /*where*/,
    const VSTGUI::CButtonState& /*buttons*/)
{
    if (!isDragging_) {
        return VSTGUI::kMouseEventNotHandled;
    }

    endEdit();

    isDragging_ = false;
    dragStartedOnHandle_ = false;
    selectedTap_ = -1;

    invalid();
    return VSTGUI::kMouseEventHandled;
}

VSTGUI::CMouseEventResult TapPatternEditor::onMouseCancel() {
    if (isDragging_) {
        cancelDrag();
    }
    return VSTGUI::kMouseEventHandled;
}

int32_t TapPatternEditor::onKeyDown(VstKeyCode& keyCode) {
    // Escape cancels drag and restores pre-drag values (T018.4)
    if (isDragging_ && keyCode.virt == VKEY_ESCAPE) {
        cancelDrag();
        return 1;  // Key handled
    }
    return -1;  // Key not handled
}

void TapPatternEditor::cancelDrag() {
    if (!isDragging_ || selectedTap_ < 0) return;

    size_t tapIdx = static_cast<size_t>(selectedTap_);

    // Restore pre-drag values
    tapTimeRatios_[tapIdx] = preDragTimeRatio_;
    tapLevels_[tapIdx] = preDragLevel_;

    // Notify parameters of restoration
    notifyTimeRatioChanged(tapIdx, preDragTimeRatio_);
    notifyLevelChanged(tapIdx, preDragLevel_);

    endEdit();

    isDragging_ = false;
    dragStartedOnHandle_ = false;
    selectedTap_ = -1;

    invalid();
}

VSTGUI::CMouseEventResult TapPatternEditor::onMouseExited(
    VSTGUI::CPoint& /*where*/,
    const VSTGUI::CButtonState& /*buttons*/)
{
    // Reset cursor to default when leaving the control
    if (auto *frame = getFrame()) {
        frame->setCursor(VSTGUI::kCursorDefault);
    }
    // Clear hover state
    if (hoveredHandleTap_ != -1) {
        hoveredHandleTap_ = -1;
        invalid();
    }
    return VSTGUI::kMouseEventHandled;
}

// =============================================================================
// Cursor Management
// =============================================================================

bool TapPatternEditor::isPointOverTapHandle(float x, float y) const {
    return hitTestTapHandleAtPoint(x, y) >= 0;
}

int TapPatternEditor::hitTestTapHandleAtPoint(float x, float y) const {
    auto viewRect = getViewSize();
    float width = static_cast<float>(viewRect.getWidth());
    float tapAreaHeight = static_cast<float>(viewRect.getHeight()) - kRulerHeight;

    // Check each active tap's handle region (reverse order for front-to-back)
    for (int i = static_cast<int>(activeTapCount_) - 1; i >= 0; --i) {
        float tapCenterX = static_cast<float>(viewRect.left) +
                           tapTimeRatios_[static_cast<size_t>(i)] * width;
        float barTop = static_cast<float>(viewRect.top) +
                       (1.0f - tapLevels_[static_cast<size_t>(i)]) * tapAreaHeight;

        // Handle region: tap bar width, from barTop to barTop + kTapHandleHeight
        float halfWidth = kTapHandleWidth / 2.0f;
        if (x >= tapCenterX - halfWidth && x <= tapCenterX + halfWidth &&
            y >= barTop && y <= barTop + kTapHandleHeight) {
            return i;
        }
    }
    return -1;
}

void TapPatternEditor::updateCursorForPosition(float x, float y) {
    auto *frame = getFrame();
    if (!frame) return;

    int handleTap = hitTestTapHandleAtPoint(x, y);

    // Update hover state and trigger redraw if changed
    if (handleTap != hoveredHandleTap_) {
        hoveredHandleTap_ = handleTap;
        invalid();
    }

    if (handleTap >= 0) {
        // Show vertical resize cursor when over a tap handle (level adjustment)
        frame->setCursor(VSTGUI::kCursorVSize);
    } else if (hitTestTapAtPoint(x, y) >= 0) {
        // Show horizontal resize cursor when over tap bar body (time adjustment)
        frame->setCursor(VSTGUI::kCursorHSize);
    } else {
        // Default cursor elsewhere
        frame->setCursor(VSTGUI::kCursorDefault);
    }
}

// =============================================================================
// Tap Data Accessors
// =============================================================================

void TapPatternEditor::setTapTimeRatio(size_t tapIndex, float ratio) {
    if (tapIndex < kMaxPatternTaps) {
        tapTimeRatios_[tapIndex] = clampRatio(ratio);
        invalid();
    }
}

void TapPatternEditor::setTapLevel(size_t tapIndex, float level) {
    if (tapIndex < kMaxPatternTaps) {
        tapLevels_[tapIndex] = clampRatio(level);
        invalid();
    }
}

float TapPatternEditor::getTapTimeRatio(size_t tapIndex) const {
    if (tapIndex < kMaxPatternTaps) {
        return tapTimeRatios_[tapIndex];
    }
    return 0.0f;
}

float TapPatternEditor::getTapLevel(size_t tapIndex) const {
    if (tapIndex < kMaxPatternTaps) {
        return tapLevels_[tapIndex];
    }
    return 0.0f;
}

void TapPatternEditor::setActiveTapCount(size_t count) {
    size_t oldCount = activeTapCount_;
    size_t newCount = std::min(count, kMaxPatternTaps);
    activeTapCount_ = newCount;

    // Cancel drag if selected tap is now out of range (T018.6)
    if (isDragging_ && std::cmp_greater_equal(selectedTap_, activeTapCount_)) {
        cancelDrag();
    }

    // Initialize new taps with snapped positions when count increases
    if (newCount > oldCount) {
        for (size_t i = oldCount; i < newCount; ++i) {
            // Calculate default linear spread position for new tap
            float defaultPosition = static_cast<float>(i + 1) / static_cast<float>(newCount + 1);
            // Apply current snap setting
            float snappedPosition = snapToGrid(defaultPosition, snapDivision_);

            tapTimeRatios_[i] = snappedPosition;
            tapLevels_[i] = kDefaultTapLevel;

            // Notify parameters of the new tap values
            notifyTimeRatioChanged(i, snappedPosition);
            notifyLevelChanged(i, kDefaultTapLevel);
        }
    }

    invalid();
}

// =============================================================================
// Parameter Notification
// =============================================================================

void TapPatternEditor::notifyTimeRatioChanged(size_t tapIndex, float ratio) {
    if (paramCallback_ && tapIndex < kMaxPatternTaps) {
        Steinberg::Vst::ParamID paramId =
            kMultiTapCustomTime0Id + static_cast<Steinberg::Vst::ParamID>(tapIndex);
        paramCallback_(paramId, ratio);
    }
}

void TapPatternEditor::notifyLevelChanged(size_t tapIndex, float level) {
    if (paramCallback_ && tapIndex < kMaxPatternTaps) {
        Steinberg::Vst::ParamID paramId =
            kMultiTapCustomLevel0Id + static_cast<Steinberg::Vst::ParamID>(tapIndex);
        paramCallback_(paramId, level);
    }
}

// =============================================================================
// Pattern Change Handler (T031.7)
// =============================================================================

void TapPatternEditor::onPatternChanged(int patternIndex) {
    // Cancel active drag if pattern changes away from Custom
    if (isDragging_ && patternIndex != kCustomPatternIndex) {
        cancelDrag();
    }
}

// =============================================================================
// Reset to Default (T076)
// =============================================================================

void TapPatternEditor::resetToDefault() {
    // Set all taps to evenly-spaced linear spread with full levels
    for (size_t i = 0; i < kMaxPatternTaps; ++i) {
        // Linear spread: tap i at position i / (tapCount - 1) for tapCount > 1
        // For single tap, position at 0.5
        float ratio = (activeTapCount_ > 1)
            ? static_cast<float>(i) / static_cast<float>(activeTapCount_ - 1)
            : 0.5f;

        if (i < activeTapCount_) {
            tapTimeRatios_[i] = ratio;
            tapLevels_[i] = 1.0f;

            // Notify host of changes
            notifyTimeRatioChanged(i, ratio);
            notifyLevelChanged(i, 1.0f);
        } else {
            // Inactive taps reset to 0
            tapTimeRatios_[i] = 0.0f;
            tapLevels_[i] = 0.0f;
        }
    }

    // Trigger redraw
    invalid();
}

} // namespace Iterum
