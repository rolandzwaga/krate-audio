// =============================================================================
// TapPatternEditor Implementation
// =============================================================================
// Spec 046: Custom Tap Pattern Editor, redesigned by specs/iterum-tap-editor/design.md (sections 2-5).
// Everything is drawn and hit-tested in view-local coordinates (design 5.1); the mouse handlers receive points in
// the parent's coordinates and convert them first.
// =============================================================================

#include "tap_pattern_editor.h"
#include "parameters/custom_pattern_defaults.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/controls/ctextlabel.h"

namespace Iterum {

namespace {

[[nodiscard]] VSTGUI::CColor withAlpha(VSTGUI::CColor color, uint8_t alpha) {
    color.alpha = alpha;
    return color;
}

/// The x of a 1 px line centred on a pixel.
[[nodiscard]] double crisp(float x) {
    return std::floor(static_cast<double>(x)) + 0.5;
}

void vLine(VSTGUI::CDrawContext* context, float x, float y0, float y1) {
    const double px = crisp(x);
    context->drawLine(VSTGUI::CPoint(px, y0), VSTGUI::CPoint(px, y1));
}

void hLine(VSTGUI::CDrawContext* context, float y, float x0, float x1) {
    const double py = crisp(y);
    context->drawLine(VSTGUI::CPoint(x0, py), VSTGUI::CPoint(x1, py));
}

/// Every k-th line so that the drawn pitch is at least 4 px (design 2.2 culling; display only); ratio 1 is kept.
[[nodiscard]] std::vector<float> culled(const std::vector<float>& ratios) {
    if (ratios.size() < 3) return ratios;
    const float pitch = (ratios[1] - ratios[0]) * kPlotWidth;
    if (pitch >= 4.0f || pitch <= 0.0f) return ratios;
    const auto step = static_cast<size_t>(std::ceil(4.0f / pitch));
    std::vector<float> out;
    for (size_t k = step - 1; k + 1 < ratios.size(); k += step) out.push_back(ratios[k]);
    out.push_back(ratios.back());
    return out;
}

[[nodiscard]] std::vector<float> ratiosOf(const std::vector<GridLine>& lines, GridLine::Kind kind) {
    std::vector<float> out;
    for (const auto& line : lines) {
        if (line.kind == kind) out.push_back(line.ratio);
    }
    return out;
}

/// The number of code points of a UTF-8 string (the readout carries "×" and "·").
[[nodiscard]] size_t codePoints(const std::string& text) {
    size_t n = 0;
    for (const char c : text) n += ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) ? 1u : 0u;
    return n;
}

}  // namespace

TapPatternEditor::TapPatternEditor(const VSTGUI::CRect& size)
    : CControl(size, nullptr, -1)
    , fallbackFont_(VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11))
{
    // The registered defaults of a fresh instance (design 3.1): quarters, odd eighths, odd sixteenths
    for (size_t i = 0; i < kMaxPatternTaps; ++i) {
        tapTimeRatios_[i] = kDefaultCustomTimeRatios[i];
        tapLevels_[i] = kDefaultTapLevel;
    }
    // Escape reaches onKeyDown only when the view has the focus (design 4.2)
    setWantsFocus(true);
}

TapPatternEditor::~TapPatternEditor() = default;

VSTGUI::CFontDesc* TapPatternEditor::font() const {
    return palette_.valueFont ? palette_.valueFont.get() : fallbackFont_.get();
}

VSTGUI::CPoint TapPatternEditor::toLocal(const VSTGUI::CPoint& where) const {
    const auto& r = getViewSize();
    return {where.x - r.left, where.y - r.top};
}

// =============================================================================
// Drawing (design 5)
// =============================================================================

void TapPatternEditor::draw(VSTGUI::CDrawContext* context) {
    const VSTGUI::CRect& r = getViewSize();
    VSTGUI::CDrawContext::Transform local(*context, VSTGUI::CGraphicsTransform().translate(r.left, r.top));
    context->setDrawMode(VSTGUI::kAliasing);
    context->setLineWidth(1.0);
    context->setFont(font());

    const CustomLength length = editable_ ? customLength_ : presetLength_;
    const bool withSnap = editable_ && snapDivision_ != SnapDivision::Off;

    drawBackground(context);
    drawLevelAxis(context);
    drawGrid(context, length, withSnap);
    drawRuler(context, length, withSnap);
    drawTempo(context);
    drawTaps(context);

    setDirty(false);
}

void TapPatternEditor::drawBackground(VSTGUI::CDrawContext* context) {
    const VSTGUI::CRect rect(0, 0, getViewSize().getWidth(), getViewSize().getHeight());
    context->setFillColor(palette_.sliderTrack);
    context->drawRect(rect, VSTGUI::kDrawFilled);
    context->setFrameColor(palette_.border);
    context->drawRect(VSTGUI::CRect(rect).inset(0.5, 0.5), VSTGUI::kDrawStroked);
}

void TapPatternEditor::drawLevelAxis(VSTGUI::CDrawContext* context) const {
    // Level grid lines (design 5.2); the -48 dB line is the plot bottom
    context->setFrameColor(palette_.border);
    for (const float dB : {-6.0f, -12.0f, -24.0f, -36.0f}) {
        hLine(context, kPlotTop + (-dB / -kLevelFloorDb) * kPlotHeight, kPlotLeft, kPlotRight);
    }
    context->setFrameColor(palette_.sliderFrame);
    hLine(context, kPlotBottom, kPlotLeft, kPlotRight);

    // Gutter labels, right-aligned at x 30, 14 px high centred on the line
    context->setFontColor(palette_.textDim);
    const std::pair<float, const char*> labels[] = {
        {0.0f, "0 dB"}, {-12.0f, "-12"}, {-24.0f, "-24"}, {-36.0f, "-36"}, {-48.0f, "-48"}};
    for (const auto& [dB, text] : labels) {
        const float y = kPlotTop + (-dB / -kLevelFloorDb) * kPlotHeight;
        context->drawString(text, VSTGUI::CRect(0, y - 7.0f, kGutterRight, y + 7.0f), VSTGUI::kRightText);
    }
}

void TapPatternEditor::drawGrid(VSTGUI::CDrawContext* context, CustomLength length, bool withSnap) {
    const auto lines = gridLines(length, withSnap ? snapDivision_ : SnapDivision::Off, tempoBpm_);

    // 1. snap lines, accent-dim at 60 % (design 5.3)
    context->setFrameColor(withAlpha(palette_.accentDim, 153));
    for (const float ratio : culled(ratiosOf(lines, GridLine::Snap))) {
        vLine(context, timeRatioToX(ratio), kPlotTop, kPlotBottom);
    }
    // 2. beat lines, text-dim at 25 %
    context->setFrameColor(withAlpha(palette_.textDim, 64));
    for (const float ratio : ratiosOf(lines, GridLine::Beat)) {
        vLine(context, timeRatioToX(ratio), kPlotTop, kPlotBottom);
    }
    // 3. bar lines and the plot edges, text-dim at 45 %
    context->setFrameColor(withAlpha(palette_.textDim, 115));
    for (const float ratio : ratiosOf(lines, GridLine::Bar)) {
        vLine(context, timeRatioToX(ratio), kPlotTop, kPlotBottom);
    }
    vLine(context, kPlotLeft, kPlotTop, kPlotBottom);
    vLine(context, kPlotRight, kPlotTop, kPlotBottom);

    // Over the ceiling (design 2.2): the plot right of c is shaded, a text-dim line marks c
    const float playable = playableFraction(length, tempoBpm_);
    if (playable < 1.0f) {
        const float xc = timeRatioToX(playable);
        context->setFillColor(withAlpha(palette_.background, 128));
        context->drawRect(VSTGUI::CRect(xc, kPlotTop, kPlotRight, kBandBottom), VSTGUI::kDrawFilled);
        context->setFrameColor(palette_.textDim);
        vLine(context, xc, kPlotTop, kBandBottom);
    }
}

void TapPatternEditor::drawRuler(VSTGUI::CDrawContext* context, CustomLength length, bool withSnap) {
    context->setFrameColor(palette_.sliderFrame);
    hLine(context, kRulerBaseline, kPlotLeft, kPlotRight);

    context->setFrameColor(palette_.textDim);
    if (length == CustomLength::Free) {
        // A 3 px tick every 100 ms, an 8 px tick every 500 ms
        for (int k = 0; k <= 50; ++k) {
            const float x = timeRatioToX(static_cast<float>(k) / 50.0f);
            vLine(context, x, kRulerBaseline, kRulerBaseline + ((k % 5 == 0) ? 8.0f : 3.0f));
        }
    } else {
        const auto lines = gridLines(length, SnapDivision::Off, tempoBpm_);
        for (const auto& line : lines) {
            vLine(context, timeRatioToX(line.ratio), kRulerBaseline,
                  kRulerBaseline + (line.kind == GridLine::Bar ? 8.0f : 5.0f));
        }
        vLine(context, kPlotRight, kRulerBaseline, kRulerTickBottom);  // the end of the length
    }
    // Snap ticks, accent, drawn last so they show over a coinciding beat tick (design 2.2, 5.3)
    if (withSnap) {
        context->setFrameColor(palette_.accent);
        for (const float ratio : culled(snapLineRatios(length, snapDivision_, tempoBpm_))) {
            vLine(context, timeRatioToX(ratio), kRulerBaseline, kRulerBaseline + 3.0f);
        }
    }

    // Labels: each has one slot (design 2.2)
    context->setFontColor(palette_.textDim);
    for (const auto& label : rulerLabels(length, tempoBpm_)) {
        constexpr float kSlack = 16.0f;  // the estimate is only an estimate: let the text keep its own width
        const VSTGUI::CRect box = label.rightAligned
                                      ? VSTGUI::CRect(label.left - kSlack, kRulerLabelTop, label.right, kRulerLabelBottom)
                                      : VSTGUI::CRect(label.left, kRulerLabelTop, label.right + kSlack, kRulerLabelBottom);
        context->drawString(label.text.c_str(), box, label.rightAligned ? VSTGUI::kRightText : VSTGUI::kLeftText);
    }
}

void TapPatternEditor::drawTempo(VSTGUI::CDrawContext* context) const {
    char bpm[16];
    std::snprintf(bpm, sizeof(bpm), "%.0f", tempoBpm_);
    context->setFontColor(palette_.textDim);
    context->drawString(bpm, VSTGUI::CRect(0, 116, kGutterRight, 127), VSTGUI::kRightText);
    context->drawString("BPM", VSTGUI::CRect(0, 127, kGutterRight, 138), VSTGUI::kRightText);
}

void TapPatternEditor::drawTaps(VSTGUI::CDrawContext* context) {
    const float* ratios = editable_ ? tapTimeRatios_.data() : presetRatios_.data();
    const float* levels = editable_ ? tapLevels_.data() : presetLevels_.data();
    const size_t count = editable_ ? activeTapCount_ : presetCount_;
    const CustomLength length = editable_ ? customLength_ : presetLength_;
    const float playable = playableFraction(length, tempoBpm_);
    const VSTGUI::CColor readOnly = withAlpha(palette_.textDim, 120);
    constexpr float kHalfBar = kTapBarWidth * 0.5f;

    const auto drawOne = [&](size_t i) {
        const float x = timeRatioToX(ratios[i]);
        const bool dragged = std::cmp_equal(i, dragTap_) && dragMode_ != DragMode::None;
        const bool hovered = std::cmp_equal(i, hoveredTap_) &&
                             (hoveredZone_ == HitZone::Handle || hoveredZone_ == HitZone::Grip);
        const bool beyond = ratios[i] > playable + 1e-6f;
        const VSTGUI::CColor handleColor = (dragged || hovered) ? palette_.textLight : palette_.text;

        if (isSilentLevel(levels[i])) {
            // A 12 x 8 handle in the silence band, no bar (design 5.4)
            VSTGUI::CColor silentFill = readOnly;
            if (editable_) {
                silentFill = beyond ? palette_.textDim : handleColor;
            }
            context->setFillColor(silentFill);
            context->drawRect(VSTGUI::CRect(x - kHalfBar, kPlotBottom, x + kHalfBar, kBandBottom), VSTGUI::kDrawFilled);
            return;
        }
        const float top = levelToY(levels[i]);
        VSTGUI::CColor bar = withAlpha(palette_.accent, dragged ? 255 : 170);
        if (beyond) bar = withAlpha(palette_.textDim, dragged ? 255 : 170);
        if (!editable_) bar = readOnly;
        context->setFillColor(bar);
        context->drawRect(VSTGUI::CRect(x - kHalfBar, top, x + kHalfBar, kPlotBottom), VSTGUI::kDrawFilled);
        if (editable_) {
            const float handleBottom = std::min(top + kTapHandleDrawHeight, kPlotBottom);
            context->setFillColor(handleColor);
            context->drawRect(VSTGUI::CRect(x - kHalfBar, top, x + kHalfBar, std::max(handleBottom, top + 1.0f)),
                              VSTGUI::kDrawFilled);
        }
    };
    for (size_t i = 0; i < count; ++i) {
        if (!std::cmp_equal(i, dragTap_) || dragMode_ == DragMode::None) drawOne(i);
    }
    if (dragMode_ != DragMode::None && dragTap_ >= 0 && std::cmp_less(dragTap_, count)) {
        drawOne(static_cast<size_t>(dragTap_));
    }

    // Numbers: one label per group of taps within 8 px, above the group's tallest bar (design 5.4, 5.5). A label
    // that would overprint one already placed (a long "10+11+..." stack beside its neighbour) moves up one 14 px
    // band; each sits on a slider-track backing so a grid line never runs through its digits.
    const float viewWidth = static_cast<float>(getViewSize().getWidth());
    std::vector<VSTGUI::CRect> placed;
    for (const auto& group : labelGroups(ratios, count)) {
        std::string text;
        size_t tallest = group.front();
        float tallestTop = kBandBottom;
        bool containsDragged = false;
        for (const size_t i : group) {
            if (!text.empty()) text += "+";
            text += std::to_string(i + 1);
            const float top = isSilentLevel(levels[i]) ? kPlotBottom : levelToY(levels[i]);  // silent: band 92..106
            if (top < tallestTop) {
                tallestTop = top;
                tallest = i;
            }
            containsDragged = containsDragged || (std::cmp_equal(i, dragTap_) && dragMode_ != DragMode::None);
        }
        const auto width = static_cast<float>(context->getStringWidth(text.c_str())) + 4.0f;
        const float cx = timeRatioToX(ratios[tallest]);
        const float left = std::clamp(cx - width * 0.5f, 0.0f, viewWidth - width);
        VSTGUI::CRect box(left, std::max(0.0f, tallestTop - 14.0f), left + width, 0.0);
        box.bottom = box.top + 14.0;
        const auto collides = [&placed](const VSTGUI::CRect& r) {
            return std::ranges::any_of(placed, [&r](const VSTGUI::CRect& other) {
                return r.left < other.right + 2.0 && r.right > other.left - 2.0 && r.top < other.bottom &&
                       r.bottom > other.top;
            });
        };
        while (box.top > 0.0 && collides(box)) {
            box.offset(0.0, -std::min(14.0, box.top));
        }
        placed.push_back(box);
        if (box.bottom > kPlotTop && cx <= timeRatioToX(playable) + 0.5f) {
            context->setFillColor(palette_.sliderTrack);
            context->drawRect(VSTGUI::CRect(box).inset(0.0, 1.0), VSTGUI::kDrawFilled);
        }
        context->setFontColor(containsDragged ? palette_.textLight : palette_.textDim);
        context->drawString(text.c_str(), box, VSTGUI::kCenterText);
    }
}

// =============================================================================
// Hit Testing (design 4.1)
// =============================================================================

TapHit TapPatternEditor::hitTest(const float* ratios, const float* levels, size_t count, float x, float y) {
    count = std::min(count, kMaxPatternTaps);
    constexpr float kHalfHit = kTapHitWidth * 0.5f;
    // Handles and grips first, the highest index first
    for (size_t k = count; k-- > 0;) {
        if (std::abs(x - timeRatioToX(ratios[k])) > kHalfHit) continue;
        if (isSilentLevel(levels[k])) {
            if (y >= kPlotBottom && y <= kBandBottom) return {.tap = static_cast<int>(k), .zone = HitZone::Grip};
            continue;
        }
        const float top = levelToY(levels[k]);
        if (kPlotBottom - top >= kTallTapMinHeight) {
            if (y >= top - kHandleAbove && y <= top + kHandleBelow) {
                return {.tap = static_cast<int>(k), .zone = HitZone::Handle};
            }
        } else if (y >= top - kHandleAbove && y <= kBandBottom) {
            return {.tap = static_cast<int>(k), .zone = HitZone::Grip};
        }
    }
    // Then bodies
    for (size_t k = count; k-- > 0;) {
        if (std::abs(x - timeRatioToX(ratios[k])) > kHalfHit || isSilentLevel(levels[k])) continue;
        const float top = levelToY(levels[k]);
        if (kPlotBottom - top >= kTallTapMinHeight && y > top + kHandleBelow && y <= kPlotBottom) {
            return {.tap = static_cast<int>(k), .zone = HitZone::Body};
        }
    }
    return {};
}

TapHit TapPatternEditor::hitZoneAt(float x, float y) const {
    return hitTest(tapTimeRatios_.data(), tapLevels_.data(), activeTapCount_, x, y);
}

VSTGUI::CCursorType TapPatternEditor::cursorForZone(HitZone zone, bool editable) {
    if (!editable) return VSTGUI::kCursorDefault;
    switch (zone) {
        case HitZone::Body: return VSTGUI::kCursorHSize;
        case HitZone::Handle: return VSTGUI::kCursorVSize;
        case HitZone::Grip: return VSTGUI::kCursorSizeAll;
        case HitZone::None:
        default: return VSTGUI::kCursorDefault;
    }
}

// =============================================================================
// Mouse Handling (design 4.2)
// =============================================================================

VSTGUI::CMouseEventResult TapPatternEditor::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons)
{
    // Right-click is ignored (T018.5); a read-only preset pattern takes no edits (design 5.9)
    if (shouldIgnoreRightClick(buttons.isRightButton()) || !editable_) {
        return VSTGUI::kMouseEventNotHandled;
    }
    if (auto* frame = getFrame()) {
        frame->setFocusView(this);
    }
    const VSTGUI::CPoint p = toLocal(where);
    const auto x = static_cast<float>(p.x);
    const auto y = static_cast<float>(p.y);
    const TapHit hit = hitZoneAt(x, y);
    if (hit.tap < 0) {
        return VSTGUI::kMouseEventNotHandled;
    }
    const auto i = static_cast<size_t>(hit.tap);

    // Double-click: time to the free place nearest D(i, n), level 0 dB, both bracketed (design 3.4, ruling 5)
    if (buttons.isDoubleClick()) {
        const float playable = playableFraction(customLength_, tempoBpm_);
        const float ideal = defaultTapTime(i, activeTapCount_, playable);
        auto probe = tapTimeRatios_;
        probe[i] = ideal;  // the ideal, not the stored place, is tested first
        const float time = placeTap(probe, activeTapCount_, i, ideal,
                                    placementGrids(customLength_, snapDivision_, tempoBpm_), playable);
        sinkBegin(timeId(i));
        sinkBegin(levelId(i));
        tapTimeRatios_[i] = time;
        tapLevels_[i] = kDefaultTapLevel;
        sinkPerform(timeId(i), time);
        sinkPerform(levelId(i), kDefaultTapLevel);
        sinkEnd(timeId(i));
        sinkEnd(levelId(i));
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    dragTap_ = hit.tap;
    dragMode_ = DragMode::GripPending;
    if (hit.zone == HitZone::Body) {
        dragMode_ = DragMode::Time;
    } else if (hit.zone == HitZone::Handle) {
        dragMode_ = DragMode::Level;
    }
    deadZoneCrossed_ = false;
    shiftHeld_ = buttons.isShiftSet();
    downX_ = baseX_ = x;
    downY_ = baseY_ = y;
    preDragTimeRatio_ = baseRatio_ = tapTimeRatios_[i];
    preDragLevel_ = baseLevel_ = tapLevels_[i];
    timeBegun_ = dragMode_ != DragMode::Level;
    levelBegun_ = dragMode_ != DragMode::Time;
    if (timeBegun_) sinkBegin(timeId(i));
    if (levelBegun_) sinkBegin(levelId(i));

    hoveredTap_ = hit.tap;
    hoveredZone_ = hit.zone;
    showReadout(i);
    invalid();
    return VSTGUI::kMouseEventHandled;
}

void TapPatternEditor::applyDragMove(float x, float y, bool shift) {
    const auto i = static_cast<size_t>(dragTap_);

    // Shift pressed or released mid-drag re-bases the drag, so the tap never jumps (design 4.2)
    if (shift != shiftHeld_) {
        shiftHeld_ = shift;
        baseX_ = x;
        baseY_ = y;
        baseRatio_ = tapTimeRatios_[i];
        baseLevel_ = tapLevels_[i];
    }

    // A grip moves nothing until 2 px of travel; then the axis with the larger travel wins
    if (dragMode_ == DragMode::GripPending) {
        const float dx = x - downX_;
        const float dy = y - downY_;
        if (std::max(std::abs(dx), std::abs(dy)) < kDeadZonePx) return;
        dragMode_ = std::abs(dx) >= std::abs(dy) ? DragMode::Time : DragMode::Level;
        deadZoneCrossed_ = true;
    }

    const float fine = shift ? kFineDivisor : 1.0f;
    if (dragMode_ == DragMode::Time) {
        if (!deadZoneCrossed_) {
            if (std::abs(x - downX_) < kDeadZonePx) return;
            deadZoneCrossed_ = true;
        }
        float ratio = baseRatio_ + (x - baseX_) / (kPlotWidth * fine);
        if (snapDivision_ != SnapDivision::Off && !shift) {
            ratio = snapToLines(ratio, gridLines(customLength_, snapDivision_, tempoBpm_));
        } else {
            ratio = clampRatio(ratio);
        }
        if (ratio != tapTimeRatios_[i]) {
            tapTimeRatios_[i] = ratio;
            sinkPerform(timeId(i), ratio);
        }
    } else if (dragMode_ == DragMode::Level) {
        if (!deadZoneCrossed_) {
            if (std::abs(y - downY_) < kDeadZonePx) return;
            deadZoneCrossed_ = true;
        }
        const float level = levelFromDragDb(levelDragDb(baseLevel_, y - baseY_, shift));
        if (level != tapLevels_[i]) {
            tapLevels_[i] = level;
            sinkPerform(levelId(i), level);
        }
    }
    showReadout(i);
    invalid();
}

VSTGUI::CMouseEventResult TapPatternEditor::onMouseMoved(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons)
{
    const VSTGUI::CPoint p = toLocal(where);
    const auto x = static_cast<float>(p.x);
    const auto y = static_cast<float>(p.y);
    if (dragMode_ == DragMode::None || dragTap_ < 0) {
        updateHover(x, y);
        return VSTGUI::kMouseEventNotHandled;
    }
    applyDragMove(x, y, buttons.isShiftSet());
    return VSTGUI::kMouseEventHandled;
}

VSTGUI::CMouseEventResult TapPatternEditor::onMouseUp(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& /*buttons*/)
{
    if (dragMode_ == DragMode::None) {
        return VSTGUI::kMouseEventNotHandled;
    }
    finishDrag();
    const VSTGUI::CPoint p = toLocal(where);
    updateHover(static_cast<float>(p.x), static_cast<float>(p.y));
    return VSTGUI::kMouseEventHandled;
}

VSTGUI::CMouseEventResult TapPatternEditor::onMouseCancel() {
    if (dragMode_ != DragMode::None) {
        cancelDrag();
    }
    hideReadout();
    return VSTGUI::kMouseEventHandled;
}

int32_t TapPatternEditor::onKeyDown(VstKeyCode& keyCode) {
    // Escape cancels the drag and restores the pre-drag value (design 4.2)
    if (dragMode_ != DragMode::None && keyCode.virt == VKEY_ESCAPE) {
        cancelDrag();
        hideReadout();
        return 1;
    }
    return -1;
}

void TapPatternEditor::finishDrag() {
    if (dragTap_ >= 0) {
        const auto i = static_cast<size_t>(dragTap_);
        if (timeBegun_) sinkEnd(timeId(i));
        if (levelBegun_) sinkEnd(levelId(i));
    }
    dragTap_ = -1;
    dragMode_ = DragMode::None;
    timeBegun_ = false;
    levelBegun_ = false;
    deadZoneCrossed_ = false;
    invalid();
}

void TapPatternEditor::cancelDrag() {
    if (dragMode_ == DragMode::None || dragTap_ < 0) return;
    const auto i = static_cast<size_t>(dragTap_);
    // The pre-drag value, once, for the axis that moved; then end every begun id
    if (timeBegun_ && tapTimeRatios_[i] != preDragTimeRatio_) {
        tapTimeRatios_[i] = preDragTimeRatio_;
        sinkPerform(timeId(i), preDragTimeRatio_);
    }
    if (levelBegun_ && tapLevels_[i] != preDragLevel_) {
        tapLevels_[i] = preDragLevel_;
        sinkPerform(levelId(i), preDragLevel_);
    }
    finishDrag();
}

VSTGUI::CMouseEventResult TapPatternEditor::onMouseExited(
    VSTGUI::CPoint& /*where*/,
    const VSTGUI::CButtonState& /*buttons*/)
{
    if (auto* frame = getFrame()) {
        frame->setCursor(VSTGUI::kCursorDefault);
    }
    if (hoveredTap_ != -1) {
        hoveredTap_ = -1;
        hoveredZone_ = HitZone::None;
        invalid();
    }
    if (dragMode_ == DragMode::None) {
        hideReadout();
    }
    return VSTGUI::kMouseEventHandled;
}

void TapPatternEditor::dispatchEvent(VSTGUI::Event& event) {
    // Ctrl/Cmd-click resets the tap's level to 0 dB (design 4.2, ruling 5). Consumed here, ahead of CControl's
    // default-value listener, which would set the value of tag 920 (not a parameter).
    if (event.type == VSTGUI::EventType::MouseDown) {
        auto& down = VSTGUI::castMouseDownEvent(event);
        if (CControl::CheckDefaultValueEventFunc(this, down)) {
            down.consumed = true;
            down.ignoreFollowUpMoveAndUpEvents(true);
            if (editable_ && dragMode_ == DragMode::None) {
                const VSTGUI::CPoint p = toLocal(down.mousePosition);
                const TapHit hit = hitZoneAt(static_cast<float>(p.x), static_cast<float>(p.y));
                if (hit.tap >= 0) {
                    const auto i = static_cast<size_t>(hit.tap);
                    sinkBegin(levelId(i));
                    if (tapLevels_[i] != kDefaultTapLevel) {
                        tapLevels_[i] = kDefaultTapLevel;
                        sinkPerform(levelId(i), kDefaultTapLevel);
                    }
                    sinkEnd(levelId(i));
                    invalid();
                }
            }
            return;
        }
    }
    CControl::dispatchEvent(event);
}

// =============================================================================
// Hover, cursor, readout (design 4.3)
// =============================================================================

void TapPatternEditor::updateHover(float x, float y) {
    const TapHit hit = editable_ ? hitZoneAt(x, y)
                                 : hitTest(presetRatios_.data(), presetLevels_.data(), presetCount_, x, y);
    if (hit.tap != hoveredTap_ || hit.zone != hoveredZone_) {
        hoveredTap_ = hit.tap;
        hoveredZone_ = hit.zone;
        invalid();
    }
    if (auto* frame = getFrame()) {
        frame->setCursor(cursorForZone(hit.zone, editable_));
    }
    if (hit.tap >= 0) {
        showReadout(static_cast<size_t>(hit.tap));
    } else {
        hideReadout();
    }
}

void TapPatternEditor::showReadout(size_t tapIndex) {
    auto* frame = getFrame();
    if (frame == nullptr || tapIndex >= kMaxPatternTaps) return;

    const bool dragging = dragMode_ != DragMode::None;
    const float ratio = editable_ ? tapTimeRatios_[tapIndex] : presetRatios_[tapIndex];
    const float level = editable_ ? tapLevels_[tapIndex] : presetLevels_[tapIndex];
    const std::string text =
        editable_ ? formatTapReadout(ratio, level, customLength_, snapDivision_, tempoBpm_, dragging && shiftHeld_)
                  : formatTapReadout(ratio, level, presetLength_, SnapDivision::Off, tempoBpm_, false);

    // 20 px high, centred on the tap, 6 px above the bar top (above the handle), clamped inside the frame
    constexpr VSTGUI::CCoord kCharWidth = 6.5;
    constexpr VSTGUI::CCoord kPadding = 10.0;
    constexpr VSTGUI::CCoord kHeight = 20.0;
    const VSTGUI::CCoord width = static_cast<VSTGUI::CCoord>(codePoints(text)) * kCharWidth + 2.0 * kPadding;
    const float barTop = isSilentLevel(level) ? kPlotBottom : levelToY(level);
    VSTGUI::CPoint anchor(getViewSize().left + timeRatioToX(ratio), getViewSize().top + barTop - kHandleAbove - 6.0f);
    localToFrame(anchor);
    VSTGUI::CRect rect(anchor.x - width / 2.0, anchor.y - kHeight, anchor.x + width / 2.0, anchor.y);
    const VSTGUI::CRect bounds = frame->getViewSize();
    if (rect.left < bounds.left) rect.offset(bounds.left - rect.left, 0);
    if (rect.right > bounds.right) rect.offset(bounds.right - rect.right, 0);
    if (rect.top < bounds.top) rect.offset(0, bounds.top - rect.top);

    if (readout_ == nullptr) {
        readout_ = new VSTGUI::CTextLabel(rect, text.c_str(), nullptr,
                                          VSTGUI::CParamDisplay::kRoundRectStyle | VSTGUI::CParamDisplay::kNoFrame);
        readout_->setFont(font());
        readout_->setFontColor(palette_.text);
        readout_->setBackColor(VSTGUI::CColor(0x1e, 0x1e, 0x1e, 220));
        readout_->setRoundRectRadius(4.0);
        readout_->setHoriAlign(VSTGUI::kCenterText);
        readout_->setMouseEnabled(false);
        frame->addView(readout_);
    } else {
        readout_->setText(text.c_str());
        if (readout_->getViewSize() != rect) {
            readout_->invalid();
            readout_->setViewSize(rect);
            readout_->setMouseableArea(rect);
        }
        readout_->invalid();
    }
}

void TapPatternEditor::hideReadout() {
    if (readout_ == nullptr) return;
    if (auto* frame = getFrame()) {
        frame->removeView(readout_);  // the frame releases it
    } else {
        readout_->forget();
    }
    readout_ = nullptr;
}

bool TapPatternEditor::removed(VSTGUI::CView* parent) {
    // VSTGUI does not cancel a mouse-down view on removal: close any open gesture here
    if (dragMode_ != DragMode::None) finishDrag();
    hideReadout();
    return CControl::removed(parent);
}

// =============================================================================
// Tap Data Accessors
// =============================================================================

void TapPatternEditor::setTapTimeRatio(size_t tapIndex, float ratio) {
    if (tapIndex >= kMaxPatternTaps) return;
    // While tap k is dragged, incoming values of its own ids cannot fight the drag (design 3.6)
    if (dragMode_ != DragMode::None && std::cmp_equal(tapIndex, dragTap_)) return;
    tapTimeRatios_[tapIndex] = clampRatio(ratio);
    invalid();
}

void TapPatternEditor::setTapLevel(size_t tapIndex, float level) {
    if (tapIndex >= kMaxPatternTaps) return;
    if (dragMode_ != DragMode::None && std::cmp_equal(tapIndex, dragTap_)) return;
    tapLevels_[tapIndex] = clampRatio(level);
    invalid();
}

float TapPatternEditor::getTapTimeRatio(size_t tapIndex) const {
    return tapIndex < kMaxPatternTaps ? tapTimeRatios_[tapIndex] : 0.0f;
}

float TapPatternEditor::getTapLevel(size_t tapIndex) const {
    return tapIndex < kMaxPatternTaps ? tapLevels_[tapIndex] : 0.0f;
}

void TapPatternEditor::setActiveTapCount(size_t count) {
    activeTapCount_ = std::min(count, kMaxPatternTaps);

    // Cancel the drag if the dragged tap is now hidden (T018.6)
    if (dragMode_ != DragMode::None && std::cmp_greater_equal(dragTap_, activeTapCount_)) {
        cancelDrag();
        hideReadout();
    }

    // Reveal only: the stored taps appear at their parameters, nothing is written (design 3.2, 3.3)
    invalid();
}

void TapPatternEditor::placeRevealedTaps(size_t oldCount, size_t newCount) {
    setActiveTapCount(newCount);
    const size_t n = activeTapCount_;
    const float playable = playableFraction(customLength_, tempoBpm_);
    const PlacementGrids grids = placementGrids(customLength_, snapDivision_, tempoBpm_);
    for (size_t i = std::min(oldCount, n); i < n; ++i) {
        const float placed = placeTap(tapTimeRatios_, n, i, defaultTapTime(i, n, playable), grids, playable);
        if (placed != tapTimeRatios_[i]) {
            tapTimeRatios_[i] = placed;
            writeBracketed(timeId(i), placed);
        }
    }
    invalid();
}

void TapPatternEditor::setCustomLength(CustomLength length) {
    customLength_ = length;
    invalid();
}

void TapPatternEditor::setTempo(double bpm) {
    tempoBpm_ = bpm;
    invalid();
}

void TapPatternEditor::setPresetPattern(const std::array<float, kMaxPatternTaps>& ratios,
                                        const std::array<float, kMaxPatternTaps>& levels, size_t count,
                                        CustomLength length) {
    presetRatios_ = ratios;
    presetLevels_ = levels;
    presetCount_ = std::min(count, kMaxPatternTaps);
    presetLength_ = length;
    invalid();
}

float TapPatternEditor::getPresetTapTimeRatio(size_t tapIndex) const {
    return tapIndex < kMaxPatternTaps ? presetRatios_[tapIndex] : 0.0f;
}

float TapPatternEditor::getPresetTapTimeMs(size_t tapIndex) const {
    if (tapIndex >= kMaxPatternTaps) return 0.0f;
    return std::min(presetRatios_[tapIndex] * spanMs(presetLength_, tempoBpm_), Krate::DSP::kCustomFreeLengthMs);
}

float TapPatternEditor::getPresetTapLevel(size_t tapIndex) const {
    return tapIndex < kMaxPatternTaps ? presetLevels_[tapIndex] : 0.0f;
}

void TapPatternEditor::setEditable(bool editable) {
    if (!editable && dragMode_ != DragMode::None) {
        cancelDrag();
    }
    if (editable != editable_) {
        hideReadout();
        hoveredTap_ = -1;
        hoveredZone_ = HitZone::None;
    }
    editable_ = editable;
    invalid();
}

// =============================================================================
// Writes through the sink (design 3.5)
// =============================================================================

void TapPatternEditor::sinkBegin(Steinberg::Vst::ParamID id) {
    if (sink_.begin) sink_.begin(id);
}

void TapPatternEditor::sinkPerform(Steinberg::Vst::ParamID id, float value) {
    if (sink_.perform) sink_.perform(id, value);
}

void TapPatternEditor::sinkEnd(Steinberg::Vst::ParamID id) {
    if (sink_.end) sink_.end(id);
}

void TapPatternEditor::writeBracketed(Steinberg::Vst::ParamID id, float value) {
    sinkBegin(id);
    sinkPerform(id, value);
    sinkEnd(id);
}

// =============================================================================
// Reset to Default (design 3.4)
// =============================================================================

void TapPatternEditor::resetToDefault() {
    // Every active tap to D(i, n) of the playable length at 0 dB; inactive taps are not touched (P19)
    const float playable = playableFraction(customLength_, tempoBpm_);
    for (size_t i = 0; i < activeTapCount_; ++i) {
        const float ratio = defaultTapTime(i, activeTapCount_, playable);
        tapTimeRatios_[i] = ratio;
        tapLevels_[i] = kDefaultTapLevel;
        writeBracketed(timeId(i), ratio);
        writeBracketed(levelId(i), kDefaultTapLevel);
    }
    invalid();
}

} // namespace Iterum
