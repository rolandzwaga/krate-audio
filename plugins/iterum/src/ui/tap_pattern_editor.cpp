// =============================================================================
// TapPatternEditor Implementation
// =============================================================================
// Spec 046: Custom Tap Pattern Editor
// =============================================================================

#include "tap_pattern_editor.h"
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
    float height = static_cast<float>(viewRect.getHeight());

    context->setFrameColor(kGridColor);
    context->setLineWidth(1.0);

    // Vertical grid lines at 1/4 intervals
    for (int i = 1; i < 4; ++i) {
        float x = viewRect.left + width * (static_cast<float>(i) / 4.0f);
        context->drawLine(
            VSTGUI::CPoint(x, viewRect.top),
            VSTGUI::CPoint(x, viewRect.bottom));
    }

    // Horizontal grid line at 50% level
    float y = viewRect.top + height * 0.5f;
    context->drawLine(
        VSTGUI::CPoint(viewRect.left, y),
        VSTGUI::CPoint(viewRect.right, y));
}

void TapPatternEditor::drawTaps(VSTGUI::CDrawContext* context) {
    auto viewRect = getViewSize();
    float width = static_cast<float>(viewRect.getWidth());
    float height = static_cast<float>(viewRect.getHeight());

    // Set up font for tap numbers
    auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 8);
    context->setFont(font);

    for (size_t i = 0; i < activeTapCount_; ++i) {
        float timeRatio = tapTimeRatios_[i];
        float level = tapLevels_[i];

        // Calculate tap bar position
        float centerX = viewRect.left + timeRatio * width;
        float barTop = viewRect.top + (1.0f - level) * height;
        float barBottom = viewRect.bottom;

        // Tap bar rectangle
        float halfBarWidth = kTapBarWidth / 2.0f;
        VSTGUI::CRect tapRect(
            centerX - halfBarWidth,
            barTop,
            centerX + halfBarWidth,
            barBottom
        );

        // Choose color based on selection
        bool isSelected = (static_cast<int>(i) == selectedTap_);
        context->setFillColor(isSelected ? kTapSelectedColor : kTapColor);
        context->drawRect(tapRect, VSTGUI::kDrawFilled);

        // Draw tap handle at top of bar
        VSTGUI::CRect handleRect(
            centerX - halfBarWidth,
            barTop,
            centerX + halfBarWidth,
            barTop + 8.0f
        );
        context->setFillColor(isSelected ?
            VSTGUI::CColor(180, 220, 255, 255) :
            VSTGUI::CColor(120, 180, 220, 255));
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

    VSTGUI::CRect labelRect0(viewRect.left + 2, viewRect.bottom - 12, viewRect.left + 30, viewRect.bottom);
    context->drawString("0%", labelRect0, VSTGUI::kLeftText);
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
    float height = static_cast<float>(viewRect.getHeight());
    return levelFromYPosition(localY, height);
}

float TapPatternEditor::timeRatioToX(float ratio) const {
    auto viewRect = getViewSize();
    float width = static_cast<float>(viewRect.getWidth());
    return static_cast<float>(viewRect.left) + Iterum::timeRatioToPosition(ratio, width);
}

float TapPatternEditor::levelToY(float level) const {
    auto viewRect = getViewSize();
    float height = static_cast<float>(viewRect.getHeight());
    return static_cast<float>(viewRect.top) + Iterum::levelToYPosition(level, height);
}

// =============================================================================
// Hit Testing
// =============================================================================

int TapPatternEditor::hitTestTapAtPoint(float x, float y) const {
    auto viewRect = getViewSize();
    float localX = x - static_cast<float>(viewRect.left);
    float localY = y - static_cast<float>(viewRect.top);
    float width = static_cast<float>(viewRect.getWidth());
    float height = static_cast<float>(viewRect.getHeight());

    return hitTestTap(
        localX, localY,
        tapTimeRatios_.data(), tapLevels_.data(),
        activeTapCount_,
        width, height
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

    beginEdit();
    invalid();

    return VSTGUI::kMouseEventHandled;
}

VSTGUI::CMouseEventResult TapPatternEditor::onMouseMoved(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons)
{
    if (!isDragging_ || selectedTap_ < 0) {
        return VSTGUI::kMouseEventNotHandled;
    }

    float x = static_cast<float>(where.x);
    float y = static_cast<float>(where.y);

    // Calculate new values (clamped via logic functions - T018.1)
    float newTimeRatio = xToTimeRatio(x);
    float newLevel = yToLevel(y);

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
    selectedTap_ = -1;

    invalid();
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
    if (isDragging_ && selectedTap_ >= static_cast<int>(activeTapCount_)) {
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
