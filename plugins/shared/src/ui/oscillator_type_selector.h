#pragma once

// ==============================================================================
// OscillatorTypeSelector - Dropdown Tile Grid Oscillator Type Chooser
// ==============================================================================
// A shared VSTGUI CControl for selecting oscillator types via a compact
// dropdown-style control with a popup 5x2 tile grid. The collapsed state
// shows a waveform icon + display name + dropdown arrow. Clicking opens a
// 260x94px popup overlay with 10 programmatically-drawn waveform icons.
//
// Features:
// - 10 oscillator types with programmatic waveform icons (no bitmaps)
// - Identity color support (OSC A = blue, OSC B = orange)
// - Popup tile grid with smart 4-corner positioning
// - Scroll wheel auditioning (cycles types without opening popup)
// - Keyboard navigation (arrow keys, Enter/Space, Escape)
// - Host automation support (valueChanged() updates display)
// - Multi-instance exclusivity (only one popup open at a time)
// - NaN/inf defensive value handling (FR-042)
//
// Usage in editor.uidesc XML:
//   <view class="OscillatorTypeSelector"
//         origin="10, 50"
//         size="180, 28"
//         control-tag="OSC A Type"
//         osc-identity="a"
//         default-value="0"
//         min-value="0"
//         max-value="1" />
//
// Registered as "OscillatorTypeSelector" via VSTGUI ViewCreator system.
// Spec: 050-oscillator-selector
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

#include <krate/dsp/systems/oscillator_types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Krate::Plugins {

// ==============================================================================
// Value Conversion Functions (FR-042, testable without VSTGUI)
// ==============================================================================

/// Convert normalized parameter value (0.0-1.0) to integer oscillator type
/// index (0-9). Handles NaN, infinity, and out-of-range values defensively.
/// FR-042: NaN/inf values are treated as 0.5 (midpoint).
[[nodiscard]] inline int oscTypeIndexFromNormalized(float value) {
    if (std::isnan(value) || std::isinf(value))
        value = 0.5f;
    value = std::clamp(value, 0.0f, 1.0f);
    return static_cast<int>(std::round(value * 9.0f));
}

/// Convert integer oscillator type index (0-9) to normalized parameter value.
/// Out-of-range indices are clamped to [0, 9].
[[nodiscard]] inline float normalizedFromOscTypeIndex(int index) {
    return static_cast<float>(std::clamp(index, 0, 9)) / 9.0f;
}

// ==============================================================================
// Display Name Tables
// ==============================================================================

/// Full display names for collapsed state and tooltips.
constexpr std::array<const char*, 10> kOscTypeDisplayNames = {
    "PolyBLEP",
    "Wavetable",
    "Phase Distortion",
    "Sync",
    "Additive",
    "Chaos",
    "Particle",
    "Formant",
    "Spectral Freeze",
    "Noise"
};

/// Abbreviated labels for popup cells (space-constrained).
constexpr std::array<const char*, 10> kOscTypePopupLabels = {
    "BLEP",
    "WTbl",
    "PDst",
    "Sync",
    "Add",
    "Chaos",
    "Prtcl",
    "Fmnt",
    "SFrz",
    "Noise"
};

/// Get the full display name for a type index. Clamps out-of-range.
[[nodiscard]] inline const char* oscTypeDisplayName(int index) {
    return kOscTypeDisplayNames[static_cast<size_t>(std::clamp(index, 0, 9))];
}

/// Get the abbreviated popup label for a type index. Clamps out-of-range.
[[nodiscard]] inline const char* oscTypePopupLabel(int index) {
    return kOscTypePopupLabels[static_cast<size_t>(std::clamp(index, 0, 9))];
}

// ==============================================================================
// Waveform Icon Path Data (Humble Object -- FR-038, testable without VSTGUI)
// ==============================================================================

namespace OscWaveformIcons {

/// A normalized 2D point (x, y in [0, 1]).
struct NormalizedPoint {
    float x;
    float y;
};

/// A waveform icon path as a sequence of normalized points.
struct IconPath {
    std::array<NormalizedPoint, 12> points{};  ///< Max 12 points per icon
    int count = 0;                              ///< Actual number of points used
    bool closePath = false;                     ///< Whether to close the path
};

/// Get the normalized point data for a given oscillator type's waveform icon.
/// Returns points in [0,1] x [0,1] coordinate space.
/// FR-038: This is the testable function -- no VSTGUI dependency.
[[nodiscard]] inline IconPath getIconPath(Krate::DSP::OscType type) {
    IconPath path;

    switch (type) {
    case Krate::DSP::OscType::PolyBLEP:
        // Sawtooth: rise from bottom-left to top-right, vertical drop, repeat
        path.points = {{
            {0.0f, 0.8f}, {0.45f, 0.2f}, {0.45f, 0.8f},
            {0.95f, 0.2f}, {0.95f, 0.8f}, {1.0f, 0.75f}
        }};
        path.count = 6;
        break;

    case Krate::DSP::OscType::Wavetable:
        // 3 overlapping sine-like waves offset vertically
        path.points = {{
            {0.0f, 0.5f}, {0.15f, 0.25f}, {0.35f, 0.75f}, {0.5f, 0.5f},
            {0.5f, 0.4f}, {0.65f, 0.15f}, {0.85f, 0.65f}, {1.0f, 0.4f}
        }};
        path.count = 8;
        break;

    case Krate::DSP::OscType::PhaseDistortion:
        // Bent sine: gentle start, sharp peak, asymmetric descent
        path.points = {{
            {0.0f, 0.5f}, {0.1f, 0.45f}, {0.25f, 0.15f},
            {0.35f, 0.5f}, {0.6f, 0.85f}, {1.0f, 0.5f}
        }};
        path.count = 6;
        break;

    case Krate::DSP::OscType::Sync:
        // Truncated burst: partial saw cycles getting shorter
        path.points = {{
            {0.0f, 0.5f}, {0.2f, 0.2f}, {0.2f, 0.7f},
            {0.35f, 0.25f}, {0.35f, 0.65f}, {0.45f, 0.3f},
            {0.45f, 0.6f}, {0.55f, 0.35f}
        }};
        path.count = 8;
        break;

    case Krate::DSP::OscType::Additive:
        // 5 vertical bars descending in height (spectrum display)
        path.points = {{
            {0.1f, 0.85f}, {0.1f, 0.15f},
            {0.3f, 0.85f}, {0.3f, 0.3f},
            {0.5f, 0.85f}, {0.5f, 0.4f},
            {0.7f, 0.85f}, {0.7f, 0.55f},
            {0.9f, 0.85f}, {0.9f, 0.65f}
        }};
        path.count = 10;
        break;

    case Krate::DSP::OscType::Chaos:
        // Looping squiggle (Lorenz-like attractor shape)
        path.points = {{
            {0.2f, 0.5f}, {0.05f, 0.2f}, {0.35f, 0.1f}, {0.6f, 0.3f},
            {0.95f, 0.15f}, {0.8f, 0.6f}, {0.5f, 0.85f}, {0.2f, 0.5f}
        }};
        path.count = 8;
        path.closePath = true;
        break;

    case Krate::DSP::OscType::Particle:
        // Scattered dots + arc envelope curve
        path.points = {{
            {0.05f, 0.7f}, {0.15f, 0.35f}, {0.25f, 0.55f}, {0.35f, 0.2f},
            {0.5f, 0.45f}, {0.65f, 0.3f}, {0.8f, 0.6f}, {0.95f, 0.75f}
        }};
        path.count = 8;
        break;

    case Krate::DSP::OscType::Formant:
        // 2-3 resonant humps (vocal formant peaks)
        path.points = {{
            {0.0f, 0.8f}, {0.15f, 0.2f}, {0.3f, 0.7f},
            {0.5f, 0.15f}, {0.7f, 0.65f}, {0.85f, 0.35f}, {1.0f, 0.8f}
        }};
        path.count = 7;
        break;

    case Krate::DSP::OscType::SpectralFreeze:
        // Vertical bars of varying height (frozen spectrum)
        path.points = {{
            {0.05f, 0.85f}, {0.05f, 0.25f},
            {0.2f, 0.85f}, {0.2f, 0.45f},
            {0.4f, 0.85f}, {0.4f, 0.15f},
            {0.6f, 0.85f}, {0.6f, 0.5f},
            {0.8f, 0.85f}, {0.8f, 0.3f}
        }};
        path.count = 10;
        break;

    case Krate::DSP::OscType::Noise:
        // Jagged random-looking horizontal line
        path.points = {{
            {0.0f, 0.5f}, {0.12f, 0.3f}, {0.25f, 0.7f}, {0.37f, 0.25f},
            {0.5f, 0.6f}, {0.62f, 0.35f}, {0.75f, 0.72f}, {1.0f, 0.45f}
        }};
        path.count = 8;
        break;

    default:
        // Fallback: simple horizontal line
        path.points = {{{0.0f, 0.5f}, {1.0f, 0.5f}}};
        path.count = 2;
        break;
    }

    return path;
}

/// Draw a waveform icon into the given rectangle.
/// Uses CGraphicsPath for cross-platform vector drawing.
/// FR-005: 1.5px anti-aliased stroke, no fill.
/// FR-007: Same function for collapsed (20x14) and popup (48x26) sizes.
inline void drawIcon(VSTGUI::CDrawContext* context,
                     const VSTGUI::CRect& targetRect,
                     Krate::DSP::OscType type,
                     const VSTGUI::CColor& strokeColor) {
    auto iconPath = getIconPath(type);
    if (iconPath.count < 2) return;

    auto* gPath = context->createGraphicsPath();
    if (!gPath) return;

    auto w = targetRect.getWidth();
    auto h = targetRect.getHeight();

    // Special handling for Additive and SpectralFreeze (vertical bars)
    bool isBars = (type == Krate::DSP::OscType::Additive ||
                   type == Krate::DSP::OscType::SpectralFreeze);

    if (isBars) {
        // Draw vertical bars (pairs of points: bottom, top)
        for (int i = 0; i + 1 < iconPath.count; i += 2) {
            auto bx = targetRect.left + iconPath.points[i].x * w;
            auto by1 = targetRect.top + iconPath.points[i].y * h;
            auto by2 = targetRect.top + iconPath.points[i + 1].y * h;
            gPath->beginSubpath(VSTGUI::CPoint(bx, by1));
            gPath->addLine(VSTGUI::CPoint(bx, by2));
        }
    } else {
        // Draw connected polyline
        gPath->beginSubpath(VSTGUI::CPoint(
            targetRect.left + iconPath.points[0].x * w,
            targetRect.top + iconPath.points[0].y * h));

        for (int i = 1; i < iconPath.count; ++i) {
            gPath->addLine(VSTGUI::CPoint(
                targetRect.left + iconPath.points[i].x * w,
                targetRect.top + iconPath.points[i].y * h));
        }

        if (iconPath.closePath)
            gPath->closeSubpath();
    }

    context->setFrameColor(strokeColor);
    context->setLineWidth(1.5);
    context->setLineStyle(VSTGUI::CLineStyle(VSTGUI::CLineStyle::kLineCapRound,
                                              VSTGUI::CLineStyle::kLineJoinRound));
    context->drawGraphicsPath(gPath, VSTGUI::CDrawContext::kPathStroked);
    gPath->forget();
}

} // namespace OscWaveformIcons

// ==============================================================================
// Grid Hit Testing (FR-026, testable without VSTGUI)
// ==============================================================================

/// Hit-test the popup grid cells. Returns cell index (0-9) or -1 if not in a cell.
/// localX/localY are relative to the popup view's top-left corner.
[[nodiscard]] inline int hitTestPopupCell(double localX, double localY) {
    constexpr double kPadding = 6.0;
    constexpr double kCellW = 48.0;
    constexpr double kCellH = 40.0;
    constexpr double kGap = 2.0;

    double gridX = localX - kPadding;
    double gridY = localY - kPadding;

    if (gridX < 0.0 || gridY < 0.0) return -1;

    int col = static_cast<int>(gridX / (kCellW + kGap));
    int row = static_cast<int>(gridY / (kCellH + kGap));

    if (col < 0 || col >= 5 || row < 0 || row >= 2) return -1;

    // Check we are inside the cell, not in the gap
    double cellLocalX = gridX - col * (kCellW + kGap);
    double cellLocalY = gridY - row * (kCellH + kGap);
    if (cellLocalX > kCellW || cellLocalY > kCellH) return -1;

    return row * 5 + col;
}

// ==============================================================================
// OscillatorTypeSelector Control
// ==============================================================================

class OscillatorTypeSelector : public VSTGUI::CControl,
                               public VSTGUI::IMouseObserver,
                               public VSTGUI::IKeyboardHook {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    // Collapsed state layout
    static constexpr VSTGUI::CCoord kCollapsedPadX = 8.0;
    static constexpr VSTGUI::CCoord kIconW = 20.0;
    static constexpr VSTGUI::CCoord kIconH = 14.0;
    static constexpr VSTGUI::CCoord kIconGap = 6.0;
    static constexpr VSTGUI::CCoord kArrowW = 8.0;
    static constexpr VSTGUI::CCoord kArrowH = 5.0;
    static constexpr VSTGUI::CCoord kBorderRadius = 3.0;

    // Popup grid layout
    static constexpr VSTGUI::CCoord kPopupW = 260.0;
    static constexpr VSTGUI::CCoord kPopupH = 94.0;
    static constexpr VSTGUI::CCoord kPopupPadding = 6.0;
    static constexpr VSTGUI::CCoord kCellW = 48.0;
    static constexpr VSTGUI::CCoord kCellH = 40.0;
    static constexpr VSTGUI::CCoord kCellGap = 2.0;
    static constexpr VSTGUI::CCoord kCellIconH = 26.0;
    static constexpr int kGridCols = 5;
    static constexpr int kGridRows = 2;
    static constexpr int kNumTypes = 10;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    OscillatorTypeSelector(const VSTGUI::CRect& size,
                           VSTGUI::IControlListener* listener = nullptr,
                           int32_t tag = -1)
        : CControl(size, listener, tag) {
        setMin(0.0f);
        setMax(1.0f);
        setWantsFocus(true);
    }

    OscillatorTypeSelector(const OscillatorTypeSelector& other)
        : CControl(other)
        , identityColor_(other.identityColor_)
        , identityId_(other.identityId_)
        , popupOpen_(false)
        , popupView_(nullptr)
        , hoveredCell_(-1)
        , focusedCell_(-1)
        , isHovered_(false) {}

    ~OscillatorTypeSelector() override {
        if (popupOpen_)
            closePopup();
        if (sOpenInstance_ == this)
            sOpenInstance_ = nullptr;
    }

    CLASS_METHODS(OscillatorTypeSelector, CControl)

    // =========================================================================
    // Identity Configuration (FR-006)
    // =========================================================================

    /// Set the oscillator identity (determines highlight color).
    /// "a" = blue rgb(100,180,255), "b" = orange rgb(255,140,100).
    void setIdentity(const std::string& identity) {
        identityId_ = identity;
        if (identity == "b") {
            identityColor_ = VSTGUI::CColor(255, 140, 100, 255);
        } else {
            identityColor_ = VSTGUI::CColor(100, 180, 255, 255);
        }
        invalid();
    }

    [[nodiscard]] const std::string& getIdentity() const { return identityId_; }

    [[nodiscard]] VSTGUI::CColor getIdentityColor() const { return identityColor_; }

    // =========================================================================
    // State Query
    // =========================================================================

    /// Get the current oscillator type index (0-9).
    [[nodiscard]] int getCurrentIndex() const {
        return oscTypeIndexFromNormalized(getValueNormalized());
    }

    /// Get the current oscillator type enum value.
    [[nodiscard]] Krate::DSP::OscType getCurrentType() const {
        return static_cast<Krate::DSP::OscType>(getCurrentIndex());
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
        // Tooltips for popup cells handled via IMouseObserver
        event.consumed = true;
    }

    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override {
        if (event.deltaY == 0.0) {
            event.consumed = true;
            return;
        }

        int currentIdx = getCurrentIndex();
        int delta = (event.deltaY > 0.0) ? 1 : -1;
        int newIdx = (currentIdx + delta + kNumTypes) % kNumTypes;

        selectType(newIdx);

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
    // CControl Overrides: Value Changed (FR-028, host automation)
    // =========================================================================

    void valueChanged() override {
        CControl::valueChanged();
        invalid();
    }

    // =========================================================================
    // IMouseObserver Overrides (modal popup dismissal)
    // =========================================================================

    void onMouseEvent(VSTGUI::MouseEvent& event, VSTGUI::CFrame* frame) override {
        if (!popupOpen_) return;

        if (event.type == VSTGUI::EventType::MouseMove) {
            handlePopupMouseMove(event);
            return;
        }

        if (event.type == VSTGUI::EventType::MouseDown) {
            // Check if click is inside popup
            if (popupView_) {
                auto popupRect = popupView_->getViewSize();
                if (popupRect.pointInside(event.mousePosition)) {
                    // Hit test popup cell
                    double localX = event.mousePosition.x - popupRect.left;
                    double localY = event.mousePosition.y - popupRect.top;
                    int cell = hitTestPopupCell(localX, localY);

                    if (cell >= 0) {
                        selectType(cell);
                        closePopup();
                        event.consumed = true;
                        return;
                    }
                }
            }

            // Click outside popup or on gap -> close without selection change
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

        // Escape: close popup without selection change
        if (event.virt == VSTGUI::VirtualKey::Escape) {
            closePopup();
            event.consumed = true;
            return;
        }

        // Enter/Space: select focused cell, close popup
        if (event.virt == VSTGUI::VirtualKey::Return ||
            event.virt == VSTGUI::VirtualKey::Space) {
            if (focusedCell_ >= 0 && focusedCell_ < kNumTypes) {
                selectType(focusedCell_);
            }
            closePopup();
            event.consumed = true;
            return;
        }

        // Arrow keys: navigate focus in popup grid
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
    // Drawing: Collapsed State (FR-009 - FR-011)
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

        int idx = getCurrentIndex();
        auto oscType = static_cast<Krate::DSP::OscType>(idx);

        // Waveform icon (20x14, identity color, 1.5px stroke)
        VSTGUI::CCoord iconY = r.top + (r.getHeight() - kIconH) / 2.0;
        VSTGUI::CRect iconRect(
            r.left + kCollapsedPadX, iconY,
            r.left + kCollapsedPadX + kIconW, iconY + kIconH);
        OscWaveformIcons::drawIcon(context, iconRect, oscType, identityColor_);

        // Display name (11px font, rgb(220,220,225))
        VSTGUI::CCoord nameX = iconRect.right + kIconGap;
        VSTGUI::CCoord arrowAreaW = kCollapsedPadX + kArrowW + kCollapsedPadX;
        VSTGUI::CRect nameRect(nameX, r.top, r.right - arrowAreaW, r.bottom);

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("", 11);
        context->setFont(font);
        context->setFontColor(VSTGUI::CColor(220, 220, 225, 255));
        context->drawString(oscTypeDisplayName(idx), nameRect,
                            VSTGUI::kLeftText, true);

        // Dropdown arrow (8x5, right-aligned)
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
    // Popup: Open / Close (FR-014 - FR-016, FR-041)
    // =========================================================================

    void openPopup() {
        if (popupOpen_) return;

        // Close any other open instance (FR-041)
        if (sOpenInstance_ && sOpenInstance_ != this)
            sOpenInstance_->closePopup();

        auto* frame = getFrame();
        if (!frame) return;

        // Compute popup position with 4-corner fallback (FR-015)
        VSTGUI::CRect popupRect = computePopupRect();

        // Create popup overlay container
        popupView_ = new PopupView(popupRect, *this);
        frame->addView(popupView_);

        // Register modal hooks
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
                frame->removeView(popupView_, true); // true = forget
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
    // Popup: Positioning (FR-015)
    // =========================================================================

    [[nodiscard]] VSTGUI::CRect computePopupRect() const {
        // Convert control bounds from parent-local to frame coordinates.
        // getViewSize() is in parent coords, but the popup is added to CFrame
        // and needs frame-absolute coordinates.
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

        // 4 candidate positions
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
        return candidates[0]; // Default: below-left
    }

    // =========================================================================
    // Popup: Drawing (FR-022 - FR-024)
    // =========================================================================

    // Inner class for the popup overlay that draws the tile grid
    class PopupView : public VSTGUI::CViewContainer {
    public:
        PopupView(const VSTGUI::CRect& size, OscillatorTypeSelector& owner)
            : CViewContainer(size), owner_(owner) {
            setBackgroundColor(VSTGUI::CColor(0, 0, 0, 0)); // transparent, we draw manually
        }

        void drawRect(VSTGUI::CDrawContext* context,
                      const VSTGUI::CRect& /*updateRect*/) override {
            auto r = getViewSize();

            // Shadow (4px blur approximated by darker rect offset)
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

                // Border
                context->setFrameColor(VSTGUI::CColor(70, 70, 75, 255));
                context->setLineWidth(1.0);
                context->setLineStyle(VSTGUI::kLineSolid);
                context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathStroked);
                bgPath->forget();
            }

            // Draw grid cells
            int selectedIdx = owner_.getCurrentIndex();
            auto identityColor = owner_.getIdentityColor();

            for (int row = 0; row < kGridRows; ++row) {
                for (int col = 0; col < kGridCols; ++col) {
                    int cellIdx = row * kGridCols + col;
                    drawPopupCell(context, r, cellIdx, col, row,
                                  selectedIdx, identityColor);
                }
            }
        }

    private:
        void drawPopupCell(VSTGUI::CDrawContext* context,
                           const VSTGUI::CRect& popupRect,
                           int cellIdx, int col, int row,
                           int selectedIdx,
                           const VSTGUI::CColor& identityColor) const {
            auto cellRect = getCellRect(popupRect, col, row);
            auto oscType = static_cast<Krate::DSP::OscType>(cellIdx);
            bool isSelected = (cellIdx == selectedIdx);
            bool isHovered = (cellIdx == owner_.hoveredCell_);
            bool isFocused = (cellIdx == owner_.focusedCell_);

            // Cell background
            if (isSelected) {
                // FR-022: Selected cell background (10% opacity identity color)
                VSTGUI::CColor selBg(identityColor.red, identityColor.green,
                                     identityColor.blue, 25);
                context->setFillColor(selBg);
                context->drawRect(cellRect, VSTGUI::kDrawFilled);
            }

            if (isHovered && !isSelected) {
                // FR-024: Hover tint
                context->setFillColor(VSTGUI::CColor(255, 255, 255, 15));
                context->drawRect(cellRect, VSTGUI::kDrawFilled);
            }

            // Cell border
            if (isSelected) {
                context->setFrameColor(identityColor);
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
                ? identityColor
                : VSTGUI::CColor(140, 140, 150, 255);
            OscWaveformIcons::drawIcon(context, iconRect, oscType, iconColor);

            // Label (9px font, centered below icon)
            VSTGUI::CRect labelRect(cellRect.left, cellRect.top + kCellIconH,
                                    cellRect.right, cellRect.bottom);
            auto labelFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("", 9);
            context->setFont(labelFont);
            VSTGUI::CColor labelColor = isSelected
                ? identityColor
                : VSTGUI::CColor(140, 140, 150, 255);
            context->setFontColor(labelColor);
            context->drawString(oscTypePopupLabel(cellIdx), labelRect,
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

        OscillatorTypeSelector& owner_;
    };

    // =========================================================================
    // Popup: Mouse Move Handling (FR-043 tooltips)
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
        int cell = hitTestPopupCell(localX, localY);

        if (cell != hoveredCell_) {
            hoveredCell_ = cell;
            if (cell >= 0) {
                popupView_->setTooltipText(oscTypeDisplayName(cell));
            } else {
                popupView_->setTooltipText(nullptr);
            }
            popupView_->invalid();
        }
    }

    // =========================================================================
    // Selection (FR-017, FR-027)
    // =========================================================================

    void selectType(int index) {
        float newValue = normalizedFromOscTypeIndex(index);
        beginEdit();
        // Use setValueNormalized() instead of setValue() because VSTGUI's
        // parameter binding (updateControlValue) changes our min/max from
        // 0/1 to 0/stepCount for discrete parameters. setValue() with an
        // already-normalized value would be double-normalized by
        // getValueNormalized(), causing all selections to collapse to 0.
        setValueNormalized(newValue);
        valueChanged();
        endEdit();
        invalid();
    }

    // =========================================================================
    // Keyboard Navigation (FR-025, FR-032)
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

    VSTGUI::CColor identityColor_{100, 180, 255, 255};  ///< Blue default (OSC A)
    std::string identityId_{"a"};
    bool popupOpen_ = false;
    PopupView* popupView_ = nullptr;                     ///< Owned by CFrame while open
    int hoveredCell_ = -1;
    int focusedCell_ = -1;
    bool isHovered_ = false;

    static inline OscillatorTypeSelector* sOpenInstance_ = nullptr;
};

// =============================================================================
// ViewCreator Registration (FR-035)
// =============================================================================
// Registers "OscillatorTypeSelector" with the VSTGUI UIViewFactory.
// getBaseViewName() -> "CControl" ensures all CControl attributes
// (control-tag, default-value, min-value, max-value, etc.) are applied.

struct OscillatorTypeSelectorCreator : VSTGUI::ViewCreatorAdapter {
    OscillatorTypeSelectorCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "OscillatorTypeSelector";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Oscillator Type Selector";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new OscillatorTypeSelector(VSTGUI::CRect(0, 0, 180, 28),
                                          nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* /*description*/) const override {
        auto* sel = dynamic_cast<OscillatorTypeSelector*>(view);
        if (!sel) return false;

        if (auto identity = attributes.getAttributeValue("osc-identity"))
            sel->setIdentity(*identity);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("osc-identity");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "osc-identity") return kStringType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* /*desc*/) const override {
        auto* sel = dynamic_cast<OscillatorTypeSelector*>(view);
        if (!sel) return false;
        if (attributeName == "osc-identity") {
            stringValue = sel->getIdentity();
            return true;
        }
        return false;
    }
};

/// Inline variable (C++17) -- safe for inclusion from multiple translation units.
/// Include this header from each plugin's entry.cpp to register the view type.
inline OscillatorTypeSelectorCreator gOscillatorTypeSelectorCreator;

} // namespace Krate::Plugins
