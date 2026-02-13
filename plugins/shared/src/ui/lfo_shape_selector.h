#pragma once

// ==============================================================================
// LfoShapeSelector - Compact Visual LFO Waveform Shape Chooser
// ==============================================================================
// A shared VSTGUI CControl for selecting LFO waveform shapes via a compact
// icon-only control with a popup 3x2 tile grid. The collapsed state shows a
// waveform icon + dropdown arrow. Clicking opens a popup overlay with 6
// programmatically-drawn waveform icons.
//
// Features:
// - 6 LFO waveform shapes with programmatic icons (no bitmaps)
// - Configurable highlight color (default: modulation green #5AC882)
// - Popup tile grid with smart 4-corner positioning
// - Scroll wheel cycling (without opening popup)
// - Keyboard navigation (arrow keys, Enter/Space, Escape)
// - Host automation support (valueChanged() updates display)
// - Multi-instance exclusivity (only one popup open at a time)
// - NaN/inf defensive value handling
//
// Usage in editor.uidesc XML:
//   <view class="LfoShapeSelector"
//         origin="10, 50"
//         size="36, 28"
//         control-tag="LFO1Shape"
//         lfo-color="#5AC882"
//         min-value="0"
//         max-value="1" />
//
// Registered as "LfoShapeSelector" via VSTGUI ViewCreator system.
// ==============================================================================

#include "color_utils.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/events.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <krate/dsp/primitives/lfo.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Krate::Plugins {

// ==============================================================================
// Value Conversion Functions (testable without VSTGUI)
// ==============================================================================

inline constexpr int kNumLfoShapes = 6;

/// Convert normalized parameter value (0.0-1.0) to integer LFO shape index (0-5).
/// Handles NaN, infinity, and out-of-range values defensively.
[[nodiscard]] inline int lfoShapeIndexFromNormalized(float value) {
    if (std::isnan(value) || std::isinf(value))
        value = 0.0f;
    value = std::clamp(value, 0.0f, 1.0f);
    return static_cast<int>(std::round(value * static_cast<float>(kNumLfoShapes - 1)));
}

/// Convert integer LFO shape index (0-5) to normalized parameter value.
/// Out-of-range indices are clamped to [0, 5].
[[nodiscard]] inline float normalizedFromLfoShapeIndex(int index) {
    return static_cast<float>(std::clamp(index, 0, kNumLfoShapes - 1))
           / static_cast<float>(kNumLfoShapes - 1);
}

// ==============================================================================
// Display Name Tables
// ==============================================================================

/// Full display names for tooltips.
constexpr std::array<const char*, 6> kLfoShapeDisplayNames = {
    "Sine",
    "Triangle",
    "Sawtooth",
    "Square",
    "Sample & Hold",
    "Smooth Random"
};

/// Abbreviated labels for popup cells (space-constrained).
constexpr std::array<const char*, 6> kLfoShapePopupLabels = {
    "Sine",
    "Tri",
    "Saw",
    "Sq",
    "S&H",
    "SmRnd"
};

/// Get the full display name for a shape index. Clamps out-of-range.
[[nodiscard]] inline const char* lfoShapeDisplayName(int index) {
    return kLfoShapeDisplayNames[static_cast<size_t>(std::clamp(index, 0, kNumLfoShapes - 1))];
}

/// Get the abbreviated popup label for a shape index. Clamps out-of-range.
[[nodiscard]] inline const char* lfoShapePopupLabel(int index) {
    return kLfoShapePopupLabels[static_cast<size_t>(std::clamp(index, 0, kNumLfoShapes - 1))];
}

// ==============================================================================
// Waveform Icon Path Data (testable without VSTGUI)
// ==============================================================================

namespace LfoWaveformIcons {

/// A normalized 2D point (x, y in [0, 1]).
struct NormalizedPoint {
    float x;
    float y;
};

/// A waveform icon path as a sequence of normalized points.
struct IconPath {
    std::array<NormalizedPoint, 16> points{};  ///< Max 16 points per icon
    int count = 0;                              ///< Actual number of points used
};

/// Get the normalized point data for a given LFO waveform icon.
/// Returns points in [0,1] x [0,1] coordinate space.
[[nodiscard]] inline IconPath getIconPath(Krate::DSP::Waveform shape) {
    IconPath path;

    switch (shape) {
    case Krate::DSP::Waveform::Sine:
        // Smooth sine wave: one full cycle
        path.points = {{
            {0.0f, 0.5f}, {0.08f, 0.3f}, {0.17f, 0.15f}, {0.25f, 0.1f},
            {0.33f, 0.15f}, {0.42f, 0.3f}, {0.5f, 0.5f},
            {0.58f, 0.7f}, {0.67f, 0.85f}, {0.75f, 0.9f},
            {0.83f, 0.85f}, {0.92f, 0.7f}, {1.0f, 0.5f}
        }};
        path.count = 13;
        break;

    case Krate::DSP::Waveform::Triangle:
        // Triangle: linear up, linear down
        path.points = {{
            {0.0f, 0.5f}, {0.25f, 0.1f}, {0.75f, 0.9f}, {1.0f, 0.5f}
        }};
        path.count = 4;
        break;

    case Krate::DSP::Waveform::Sawtooth:
        // Sawtooth: ramp up, instant drop
        path.points = {{
            {0.0f, 0.9f}, {0.47f, 0.1f}, {0.47f, 0.9f},
            {0.97f, 0.1f}, {0.97f, 0.9f}
        }};
        path.count = 5;
        break;

    case Krate::DSP::Waveform::Square:
        // Square: flat high, drop, flat low, rise
        path.points = {{
            {0.0f, 0.15f}, {0.45f, 0.15f}, {0.45f, 0.85f},
            {0.95f, 0.85f}, {0.95f, 0.15f}, {1.0f, 0.15f}
        }};
        path.count = 6;
        break;

    case Krate::DSP::Waveform::SampleHold:
        // Stepped random: horizontal segments at different heights
        path.points = {{
            {0.0f, 0.3f}, {0.18f, 0.3f}, {0.18f, 0.75f},
            {0.36f, 0.75f}, {0.36f, 0.2f}, {0.54f, 0.2f},
            {0.54f, 0.6f}, {0.72f, 0.6f}, {0.72f, 0.4f},
            {0.9f, 0.4f}, {0.9f, 0.8f}, {1.0f, 0.8f}
        }};
        path.count = 12;
        break;

    case Krate::DSP::Waveform::SmoothRandom:
        // Smooth random: irregular smooth curves
        path.points = {{
            {0.0f, 0.5f}, {0.12f, 0.25f}, {0.28f, 0.7f},
            {0.42f, 0.2f}, {0.58f, 0.65f}, {0.72f, 0.35f},
            {0.88f, 0.75f}, {1.0f, 0.45f}
        }};
        path.count = 8;
        break;

    default:
        // Fallback: horizontal line
        path.points = {{{0.0f, 0.5f}, {1.0f, 0.5f}}};
        path.count = 2;
        break;
    }

    return path;
}

/// Draw a waveform icon into the given rectangle.
/// Uses CGraphicsPath for cross-platform vector drawing.
/// 1.5px anti-aliased stroke, no fill.
inline void drawIcon(VSTGUI::CDrawContext* context,
                     const VSTGUI::CRect& targetRect,
                     Krate::DSP::Waveform shape,
                     const VSTGUI::CColor& strokeColor) {
    auto iconPath = getIconPath(shape);
    if (iconPath.count < 2) return;

    auto* gPath = context->createGraphicsPath();
    if (!gPath) return;

    auto w = targetRect.getWidth();
    auto h = targetRect.getHeight();

    gPath->beginSubpath(VSTGUI::CPoint(
        targetRect.left + iconPath.points[0].x * w,
        targetRect.top + iconPath.points[0].y * h));

    for (int i = 1; i < iconPath.count; ++i) {
        gPath->addLine(VSTGUI::CPoint(
            targetRect.left + iconPath.points[i].x * w,
            targetRect.top + iconPath.points[i].y * h));
    }

    context->setFrameColor(strokeColor);
    context->setLineWidth(1.5);
    context->setLineStyle(VSTGUI::CLineStyle(VSTGUI::CLineStyle::kLineCapRound,
                                              VSTGUI::CLineStyle::kLineJoinRound));
    context->drawGraphicsPath(gPath, VSTGUI::CDrawContext::kPathStroked);
    gPath->forget();
}

} // namespace LfoWaveformIcons

// ==============================================================================
// Grid Hit Testing (testable without VSTGUI)
// ==============================================================================

/// Hit-test the popup grid cells. Returns cell index (0-5) or -1 if not in a cell.
/// localX/localY are relative to the popup view's top-left corner.
[[nodiscard]] inline int hitTestLfoPopupCell(double localX, double localY) {
    constexpr double kPadding = 6.0;
    constexpr double kCellW = 48.0;
    constexpr double kCellH = 40.0;
    constexpr double kGap = 2.0;
    constexpr int kCols = 3;
    constexpr int kRows = 2;

    double gridX = localX - kPadding;
    double gridY = localY - kPadding;

    if (gridX < 0.0 || gridY < 0.0) return -1;

    int col = static_cast<int>(gridX / (kCellW + kGap));
    int row = static_cast<int>(gridY / (kCellH + kGap));

    if (col < 0 || col >= kCols || row < 0 || row >= kRows) return -1;

    // Check we are inside the cell, not in the gap
    double cellLocalX = gridX - col * (kCellW + kGap);
    double cellLocalY = gridY - row * (kCellH + kGap);
    if (cellLocalX > kCellW || cellLocalY > kCellH) return -1;

    return row * kCols + col;
}

// ==============================================================================
// LfoShapeSelector Control
// ==============================================================================

class LfoShapeSelector : public VSTGUI::CControl,
                         public VSTGUI::IMouseObserver,
                         public VSTGUI::IKeyboardHook {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    // Collapsed state layout
    static constexpr VSTGUI::CCoord kCollapsedPadX = 4.0;
    static constexpr VSTGUI::CCoord kIconW = 22.0;
    static constexpr VSTGUI::CCoord kIconH = 16.0;
    static constexpr VSTGUI::CCoord kArrowW = 6.0;
    static constexpr VSTGUI::CCoord kArrowH = 4.0;
    static constexpr VSTGUI::CCoord kArrowGap = 3.0;
    static constexpr VSTGUI::CCoord kBorderRadius = 3.0;

    // Popup grid layout
    static constexpr VSTGUI::CCoord kPopupPadding = 6.0;
    static constexpr VSTGUI::CCoord kCellW = 48.0;
    static constexpr VSTGUI::CCoord kCellH = 40.0;
    static constexpr VSTGUI::CCoord kCellGap = 2.0;
    static constexpr VSTGUI::CCoord kCellIconH = 26.0;
    static constexpr int kGridCols = 3;
    static constexpr int kGridRows = 2;
    static constexpr VSTGUI::CCoord kPopupW =
        kPopupPadding * 2 + kGridCols * kCellW + (kGridCols - 1) * kCellGap;
    static constexpr VSTGUI::CCoord kPopupH =
        kPopupPadding * 2 + kGridRows * kCellH + (kGridRows - 1) * kCellGap;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    LfoShapeSelector(const VSTGUI::CRect& size,
                     VSTGUI::IControlListener* listener = nullptr,
                     int32_t tag = -1)
        : CControl(size, listener, tag) {
        setMin(0.0f);
        setMax(1.0f);
        setWantsFocus(true);
    }

    LfoShapeSelector(const LfoShapeSelector& other)
        : CControl(other)
        , highlightColor_(other.highlightColor_)
        , popupOpen_(false)
        , popupView_(nullptr)
        , hoveredCell_(-1)
        , focusedCell_(-1)
        , isHovered_(false) {}

    ~LfoShapeSelector() override {
        if (popupOpen_)
            closePopup();
        if (sOpenInstance_ == this)
            sOpenInstance_ = nullptr;
    }

    CLASS_METHODS(LfoShapeSelector, CControl)

    // =========================================================================
    // Color Configuration
    // =========================================================================

    /// Set the highlight color (used for selected state, icon tint).
    void setHighlightColor(const VSTGUI::CColor& color) {
        highlightColor_ = color;
        invalid();
    }

    [[nodiscard]] VSTGUI::CColor getHighlightColor() const { return highlightColor_; }

    /// Set highlight color from a hex string (e.g., "#5AC882").
    void setHighlightColorFromString(const std::string& hexStr) {
        if (hexStr.size() >= 7 && hexStr[0] == '#') {
            auto r = static_cast<uint8_t>(std::stoul(hexStr.substr(1, 2), nullptr, 16));
            auto g = static_cast<uint8_t>(std::stoul(hexStr.substr(3, 2), nullptr, 16));
            auto b = static_cast<uint8_t>(std::stoul(hexStr.substr(5, 2), nullptr, 16));
            highlightColor_ = VSTGUI::CColor(r, g, b, 255);
            invalid();
        }
    }

    [[nodiscard]] std::string getHighlightColorString() const {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                 highlightColor_.red, highlightColor_.green, highlightColor_.blue);
        return buf;
    }

    // =========================================================================
    // State Query
    // =========================================================================

    /// Get the current LFO shape index (0-5).
    [[nodiscard]] int getCurrentIndex() const {
        return lfoShapeIndexFromNormalized(getValueNormalized());
    }

    /// Get the current waveform enum value.
    [[nodiscard]] Krate::DSP::Waveform getCurrentShape() const {
        return static_cast<Krate::DSP::Waveform>(getCurrentIndex());
    }

    /// Whether the popup is currently open.
    [[nodiscard]] bool isPopupOpen() const { return popupOpen_; }

    // =========================================================================
    // CControl Overrides: Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        drawCollapsedState(context);
        setDirty(false);
    }

    // =========================================================================
    // CView Overrides: Mouse Events (on collapsed control)
    // =========================================================================

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        (void)where;
        if (!(buttons & VSTGUI::kLButton))
            return VSTGUI::kMouseEventNotHandled;

        if (popupOpen_) {
            closePopup();
        } else {
            openPopup();
        }
        return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
    }

    void onMouseEnterEvent(VSTGUI::MouseEnterEvent& event) override {
        isHovered_ = true;
        invalid();
        event.consumed = true;
    }

    void onMouseExitEvent(VSTGUI::MouseExitEvent& event) override {
        isHovered_ = false;
        invalid();
        event.consumed = true;
    }

    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override {
        event.consumed = true;
    }

    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override {
        if (event.deltaY == 0.0) {
            event.consumed = true;
            return;
        }

        int currentIdx = getCurrentIndex();
        int delta = (event.deltaY > 0.0) ? 1 : -1;
        int newIdx = (currentIdx + delta + kNumLfoShapes) % kNumLfoShapes;

        selectShape(newIdx);

        if (popupOpen_) {
            focusedCell_ = newIdx;
            if (popupView_)
                popupView_->invalid();
        }

        event.consumed = true;
    }

    // =========================================================================
    // CView Overrides: Keyboard Events (on collapsed control when focused)
    // =========================================================================

    void onKeyboardEvent(VSTGUI::KeyboardEvent& event) override {
        if (event.type != VSTGUI::EventType::KeyDown)
            return;

        if (!popupOpen_ &&
            (event.virt == VSTGUI::VirtualKey::Return ||
             event.virt == VSTGUI::VirtualKey::Space)) {
            openPopup();
            event.consumed = true;
        }
    }

    // =========================================================================
    // CView Overrides: Focus
    // =========================================================================

    bool getFocusPath(VSTGUI::CGraphicsPath& outPath) override {
        auto r = getViewSize();
        r.inset(1.0, 1.0);
        outPath.addRoundRect(r, kBorderRadius);
        return true;
    }

    // =========================================================================
    // CControl Overrides: Value Changed (host automation)
    // =========================================================================

    void valueChanged() override {
        CControl::valueChanged();
        invalid();
    }

    // =========================================================================
    // IMouseObserver Overrides (modal popup dismissal)
    // =========================================================================

    void onMouseEvent(VSTGUI::MouseEvent& event, VSTGUI::CFrame* frame) override {
        (void)frame;
        if (!popupOpen_) return;

        if (event.type == VSTGUI::EventType::MouseMove) {
            handlePopupMouseMove(event);
            return;
        }

        if (event.type == VSTGUI::EventType::MouseDown) {
            if (popupView_) {
                auto popupRect = popupView_->getViewSize();
                if (popupRect.pointInside(event.mousePosition)) {
                    double localX = event.mousePosition.x - popupRect.left;
                    double localY = event.mousePosition.y - popupRect.top;
                    int cell = hitTestLfoPopupCell(localX, localY);

                    if (cell >= 0) {
                        selectShape(cell);
                        closePopup();
                        event.consumed = true;
                        return;
                    }
                }
            }

            closePopup();
            event.consumed = true;
        }
    }

    void onMouseEntered(VSTGUI::CView* /*view*/, VSTGUI::CFrame* /*frame*/) override {}
    void onMouseExited(VSTGUI::CView* /*view*/, VSTGUI::CFrame* /*frame*/) override {}

    // =========================================================================
    // IKeyboardHook Overrides (modal keyboard interception)
    // =========================================================================

    void onKeyboardEvent(VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* /*frame*/) override {
        if (!popupOpen_) return;
        if (event.type != VSTGUI::EventType::KeyDown) return;

        if (event.virt == VSTGUI::VirtualKey::Escape) {
            closePopup();
            event.consumed = true;
            return;
        }

        if (event.virt == VSTGUI::VirtualKey::Return ||
            event.virt == VSTGUI::VirtualKey::Space) {
            if (focusedCell_ >= 0 && focusedCell_ < kNumLfoShapes) {
                selectShape(focusedCell_);
            }
            closePopup();
            event.consumed = true;
            return;
        }

        if (event.virt == VSTGUI::VirtualKey::Left ||
            event.virt == VSTGUI::VirtualKey::Right ||
            event.virt == VSTGUI::VirtualKey::Up ||
            event.virt == VSTGUI::VirtualKey::Down) {
            navigateFocus(event.virt);
            event.consumed = true;
        }
    }

private:
    // =========================================================================
    // Drawing: Collapsed State (icon + dropdown arrow)
    // =========================================================================

    void drawCollapsedState(VSTGUI::CDrawContext* context) const {
        auto r = getViewSize();

        // Background
        VSTGUI::CColor bgColor(38, 38, 42, 255);
        auto* bgPath = context->createGraphicsPath();
        if (bgPath) {
            bgPath->addRoundRect(r, kBorderRadius);
            context->setFillColor(bgColor);
            context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathFilled);

            // Border
            VSTGUI::CColor borderColor = isHovered_
                ? VSTGUI::CColor(90, 90, 95, 255)
                : VSTGUI::CColor(60, 60, 65, 255);
            context->setFrameColor(borderColor);
            context->setLineWidth(1.0);
            context->setLineStyle(VSTGUI::kLineSolid);
            context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathStroked);
            bgPath->forget();
        }

        auto shape = getCurrentShape();

        // Waveform icon (centered vertically, left-aligned)
        VSTGUI::CCoord iconY = r.top + (r.getHeight() - kIconH) / 2.0;
        VSTGUI::CRect iconRect(
            r.left + kCollapsedPadX, iconY,
            r.left + kCollapsedPadX + kIconW, iconY + kIconH);
        LfoWaveformIcons::drawIcon(context, iconRect, shape, highlightColor_);

        // Dropdown arrow (right-aligned)
        drawDropdownArrow(context, r);
    }

    void drawDropdownArrow(VSTGUI::CDrawContext* context,
                           const VSTGUI::CRect& controlRect) const {
        VSTGUI::CCoord arrowX = controlRect.right - kCollapsedPadX - kArrowW;
        VSTGUI::CCoord arrowY = controlRect.top +
                                (controlRect.getHeight() - kArrowH) / 2.0;

        auto* arrowPath = context->createGraphicsPath();
        if (!arrowPath) return;

        arrowPath->beginSubpath(VSTGUI::CPoint(arrowX, arrowY));
        arrowPath->addLine(VSTGUI::CPoint(arrowX + kArrowW / 2.0,
                                           arrowY + kArrowH));
        arrowPath->addLine(VSTGUI::CPoint(arrowX + kArrowW, arrowY));

        context->setFrameColor(VSTGUI::CColor(160, 160, 165, 255));
        context->setLineWidth(1.5);
        context->setLineStyle(VSTGUI::CLineStyle(VSTGUI::CLineStyle::kLineCapRound,
                                                  VSTGUI::CLineStyle::kLineJoinRound));
        context->drawGraphicsPath(arrowPath, VSTGUI::CDrawContext::kPathStroked);
        arrowPath->forget();
    }

    // =========================================================================
    // Popup: Open / Close
    // =========================================================================

    void openPopup() {
        if (popupOpen_) return;

        // Close any other open instance
        if (sOpenInstance_ && sOpenInstance_ != this)
            sOpenInstance_->closePopup();

        auto* frame = getFrame();
        if (!frame) return;

        VSTGUI::CRect popupRect = computePopupRect();

        popupView_ = new PopupView(popupRect, *this);
        frame->addView(popupView_);

        frame->registerMouseObserver(this);
        frame->registerKeyboardHook(this);

        popupOpen_ = true;
        sOpenInstance_ = this;
        focusedCell_ = getCurrentIndex();

        invalid();
    }

    void closePopup() {
        if (!popupOpen_) return;

        auto* frame = getFrame();
        if (frame) {
            frame->unregisterKeyboardHook(this);
            frame->unregisterMouseObserver(this);
            if (popupView_) {
                frame->removeView(popupView_, true);
                popupView_ = nullptr;
            }
        }

        popupOpen_ = false;
        if (sOpenInstance_ == this)
            sOpenInstance_ = nullptr;

        hoveredCell_ = -1;
        focusedCell_ = -1;
        invalid();
    }

    // =========================================================================
    // Popup: Positioning (4-corner fallback)
    // =========================================================================

    [[nodiscard]] VSTGUI::CRect computePopupRect() const {
        VSTGUI::CPoint frameOrigin(0, 0);
        localToFrame(frameOrigin);
        auto vs = getViewSize();
        VSTGUI::CRect controlRect(
            frameOrigin.x, frameOrigin.y,
            frameOrigin.x + vs.getWidth(),
            frameOrigin.y + vs.getHeight());

        auto* frame = getFrame();
        VSTGUI::CRect frameRect;
        if (frame) {
            frameRect = frame->getViewSize();
        } else {
            frameRect = VSTGUI::CRect(0, 0, 1920, 1080);
        }

        VSTGUI::CRect candidates[4] = {
            // Below-left
            VSTGUI::CRect(controlRect.left, controlRect.bottom,
                          controlRect.left + kPopupW, controlRect.bottom + kPopupH),
            // Below-right
            VSTGUI::CRect(controlRect.right - kPopupW, controlRect.bottom,
                          controlRect.right, controlRect.bottom + kPopupH),
            // Above-left
            VSTGUI::CRect(controlRect.left, controlRect.top - kPopupH,
                          controlRect.left + kPopupW, controlRect.top),
            // Above-right
            VSTGUI::CRect(controlRect.right - kPopupW, controlRect.top - kPopupH,
                          controlRect.right, controlRect.top),
        };

        for (const auto& rect : candidates) {
            if (frameRect.left <= rect.left &&
                frameRect.top <= rect.top &&
                frameRect.right >= rect.right &&
                frameRect.bottom >= rect.bottom) {
                return rect;
            }
        }
        return candidates[0];
    }

    // =========================================================================
    // Popup: Drawing
    // =========================================================================

    class PopupView : public VSTGUI::CViewContainer {
    public:
        PopupView(const VSTGUI::CRect& size, LfoShapeSelector& owner)
            : CViewContainer(size), owner_(owner) {
            setBackgroundColor(VSTGUI::CColor(0, 0, 0, 0));
        }

        void drawRect(VSTGUI::CDrawContext* context,
                      const VSTGUI::CRect& /*updateRect*/) override {
            auto r = getViewSize();

            // Shadow
            VSTGUI::CRect shadowRect = r;
            shadowRect.offset(2.0, 2.0);
            context->setFillColor(VSTGUI::CColor(0, 0, 0, 80));
            context->drawRect(shadowRect, VSTGUI::kDrawFilled);

            // Background
            auto* bgPath = context->createGraphicsPath();
            if (bgPath) {
                bgPath->addRoundRect(r, 4.0);
                context->setFillColor(VSTGUI::CColor(30, 30, 35, 255));
                context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathFilled);

                context->setFrameColor(VSTGUI::CColor(70, 70, 75, 255));
                context->setLineWidth(1.0);
                context->setLineStyle(VSTGUI::kLineSolid);
                context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathStroked);
                bgPath->forget();
            }

            // Draw grid cells
            int selectedIdx = owner_.getCurrentIndex();
            auto highlightColor = owner_.getHighlightColor();

            for (int row = 0; row < kGridRows; ++row) {
                for (int col = 0; col < kGridCols; ++col) {
                    int cellIdx = row * kGridCols + col;
                    drawPopupCell(context, r, cellIdx, col, row,
                                  selectedIdx, highlightColor);
                }
            }
        }

    private:
        void drawPopupCell(VSTGUI::CDrawContext* context,
                           const VSTGUI::CRect& popupRect,
                           int cellIdx, int col, int row,
                           int selectedIdx,
                           const VSTGUI::CColor& highlightColor) const {
            auto cellRect = getCellRect(popupRect, col, row);
            auto shape = static_cast<Krate::DSP::Waveform>(cellIdx);
            bool isSelected = (cellIdx == selectedIdx);
            bool isHovered = (cellIdx == owner_.hoveredCell_);
            bool isFocused = (cellIdx == owner_.focusedCell_);

            // Cell background
            if (isSelected) {
                VSTGUI::CColor selBg(highlightColor.red, highlightColor.green,
                                     highlightColor.blue, 25);
                context->setFillColor(selBg);
                context->drawRect(cellRect, VSTGUI::kDrawFilled);
            }

            if (isHovered && !isSelected) {
                context->setFillColor(VSTGUI::CColor(255, 255, 255, 15));
                context->drawRect(cellRect, VSTGUI::kDrawFilled);
            }

            // Cell border
            if (isSelected) {
                context->setFrameColor(highlightColor);
            } else {
                context->setFrameColor(VSTGUI::CColor(60, 60, 65, 255));
            }
            context->setLineWidth(1.0);
            context->setLineStyle(VSTGUI::kLineSolid);
            context->drawRect(cellRect, VSTGUI::kDrawStroked);

            // Focus indicator (dotted border)
            if (isFocused && !isSelected) {
                VSTGUI::CRect focusRect = cellRect;
                focusRect.inset(-1.0, -1.0);
                const VSTGUI::CLineStyle::CoordVector dashes = {2.0, 2.0};
                VSTGUI::CLineStyle dottedStyle(
                    VSTGUI::CLineStyle::kLineCapButt,
                    VSTGUI::CLineStyle::kLineJoinMiter,
                    0.0, dashes);
                context->setFrameColor(VSTGUI::CColor(200, 200, 205, 200));
                context->setLineWidth(1.0);
                context->setLineStyle(dottedStyle);
                context->drawRect(focusRect, VSTGUI::kDrawStroked);
            }

            // Icon rect (full cell width, kCellIconH tall)
            VSTGUI::CRect iconRect(cellRect.left + 2.0, cellRect.top + 2.0,
                                   cellRect.right - 2.0,
                                   cellRect.top + kCellIconH);
            VSTGUI::CColor iconColor = isSelected
                ? highlightColor
                : VSTGUI::CColor(140, 140, 150, 255);
            LfoWaveformIcons::drawIcon(context, iconRect, shape, iconColor);

            // Label (9px font, centered below icon)
            VSTGUI::CRect labelRect(cellRect.left, cellRect.top + kCellIconH,
                                    cellRect.right, cellRect.bottom);
            auto labelFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("", 9);
            context->setFont(labelFont);
            VSTGUI::CColor labelColor = isSelected
                ? highlightColor
                : VSTGUI::CColor(140, 140, 150, 255);
            context->setFontColor(labelColor);
            context->drawString(lfoShapePopupLabel(cellIdx), labelRect,
                                VSTGUI::kCenterText, true);
        }

        [[nodiscard]] static VSTGUI::CRect getCellRect(
            const VSTGUI::CRect& popupRect, int col, int row) {
            VSTGUI::CCoord x = popupRect.left + kPopupPadding +
                               col * (kCellW + kCellGap);
            VSTGUI::CCoord y = popupRect.top + kPopupPadding +
                               row * (kCellH + kCellGap);
            return VSTGUI::CRect(x, y, x + kCellW, y + kCellH);
        }

        LfoShapeSelector& owner_;
    };

    // =========================================================================
    // Popup: Mouse Move Handling (hover + tooltips)
    // =========================================================================

    void handlePopupMouseMove(VSTGUI::MouseEvent& event) {
        if (!popupView_) return;

        auto popupRect = popupView_->getViewSize();
        if (!popupRect.pointInside(event.mousePosition)) {
            if (hoveredCell_ != -1) {
                hoveredCell_ = -1;
                popupView_->invalid();
            }
            return;
        }

        double localX = event.mousePosition.x - popupRect.left;
        double localY = event.mousePosition.y - popupRect.top;
        int cell = hitTestLfoPopupCell(localX, localY);

        if (cell != hoveredCell_) {
            hoveredCell_ = cell;
            if (cell >= 0) {
                popupView_->setTooltipText(lfoShapeDisplayName(cell));
            } else {
                popupView_->setTooltipText(nullptr);
            }
            popupView_->invalid();
        }
    }

    // =========================================================================
    // Selection
    // =========================================================================

    void selectShape(int index) {
        float newValue = normalizedFromLfoShapeIndex(index);
        beginEdit();
        setValueNormalized(newValue);
        valueChanged();
        endEdit();
        invalid();
    }

    // =========================================================================
    // Keyboard Navigation
    // =========================================================================

    void navigateFocus(VSTGUI::VirtualKey direction) {
        if (focusedCell_ < 0) focusedCell_ = 0;

        int col = focusedCell_ % kGridCols;
        int row = focusedCell_ / kGridCols;

        switch (direction) {
        case VSTGUI::VirtualKey::Left:
            col = (col - 1 + kGridCols) % kGridCols;
            if (col == kGridCols - 1 && row > 0) row--;
            else if (col == kGridCols - 1 && row == 0) row = kGridRows - 1;
            break;
        case VSTGUI::VirtualKey::Right:
            col = (col + 1) % kGridCols;
            if (col == 0 && row < kGridRows - 1) row++;
            else if (col == 0 && row == kGridRows - 1) row = 0;
            break;
        case VSTGUI::VirtualKey::Up:
            row = (row - 1 + kGridRows) % kGridRows;
            break;
        case VSTGUI::VirtualKey::Down:
            row = (row + 1) % kGridRows;
            break;
        default:
            break;
        }

        focusedCell_ = row * kGridCols + col;
        if (popupView_)
            popupView_->invalid();
    }

    // =========================================================================
    // State
    // =========================================================================

    VSTGUI::CColor highlightColor_{90, 200, 130, 255};  ///< Default: modulation green #5AC882
    bool popupOpen_ = false;
    PopupView* popupView_ = nullptr;
    int hoveredCell_ = -1;
    int focusedCell_ = -1;
    bool isHovered_ = false;

    static inline LfoShapeSelector* sOpenInstance_ = nullptr;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct LfoShapeSelectorCreator : VSTGUI::ViewCreatorAdapter {
    LfoShapeSelectorCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "LfoShapeSelector";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "LFO Shape Selector";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new LfoShapeSelector(VSTGUI::CRect(0, 0, 36, 28),
                                     nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* /*description*/) const override {
        auto* sel = dynamic_cast<LfoShapeSelector*>(view);
        if (!sel) return false;

        if (auto colorStr = attributes.getAttributeValue("lfo-color"))
            sel->setHighlightColorFromString(*colorStr);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("lfo-color");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "lfo-color") return kStringType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* /*desc*/) const override {
        auto* sel = dynamic_cast<LfoShapeSelector*>(view);
        if (!sel) return false;
        if (attributeName == "lfo-color") {
            stringValue = sel->getHighlightColorString();
            return true;
        }
        return false;
    }
};

/// Inline variable (C++17) -- safe for inclusion from multiple translation units.
inline LfoShapeSelectorCreator gLfoShapeSelectorCreator;

} // namespace Krate::Plugins
