// ==============================================================================
// Profundum Phase 12 - SpreadCurveView implementation (FR-080)
// ==============================================================================
// Plan:   specs/profundum-phase12-ui/plan.md (section 4.9)
// Design: specs/profundum-phase12-ui/design/advanced-page.md section 6 (D2)
// ==============================================================================

#include "ui/spread_curve_view.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cdrawdefs.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uidescription.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Profundum::UI {

namespace {

// D7 token values (design/accessibility-scaling.md colour table).
const VSTGUI::CColor kFallbackPanel{0x1A, 0x1A, 0x20, 0xFF};
const VSTGUI::CColor kFallbackTrack{0x22, 0x22, 0x2A, 0xFF};
const VSTGUI::CColor kFallbackText{0xD8, 0xD8, 0xE0, 0xFF};
const VSTGUI::CColor kFallbackTextDim{0x9A, 0x9A, 0xA8, 0xFF};
const VSTGUI::CColor kFallbackAccent{0x7A, 0x8C, 0xA8, 0xFF};

constexpr VSTGUI::CCoord kFontSize = 10.0;     // D7 minimum text size
constexpr VSTGUI::CCoord kAxisLabelWidth = 30.0;
constexpr VSTGUI::CCoord kLabelHeight = 12.0;
constexpr VSTGUI::CCoord kCurveWidth = 1.5;

const std::array<float, 64> kZeroCurve{};

}  // namespace

SpreadCurveView::SpreadCurveView(const VSTGUI::CRect& size, const SpreadCurveModel* model)
    : VSTGUI::CView(size),
      model_(model),
      panel_(kFallbackPanel),
      track_(kFallbackTrack),
      text_(kFallbackText),
      textDim_(kFallbackTextDim),
      accent_(kFallbackAccent),
      font_(VSTGUI::makeOwned<VSTGUI::CFontDesc>("", kFontSize)) {}

const std::array<float, 64>& SpreadCurveView::sampledValuesForTest() const noexcept {
    return model_ != nullptr ? model_->w : kZeroCurve;
}

std::array<float, 2> SpreadCurveView::kneeMarksForTest() const noexcept {
    if (model_ == nullptr) {
        return {0.0f, 0.0f};
    }
    return {model_->lowerKneeHz, model_->upperKneeHz};
}

bool SpreadCurveView::wideLabelVisibleForTest() const noexcept {
    return model_ != nullptr && model_->wide;
}

float SpreadCurveView::axisPositionOf(float hz) noexcept {
    if (!(hz > kMinHz)) {
        return 0.0f;
    }
    const float u = std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz);
    return std::clamp(u, 0.0f, 1.0f);
}

void SpreadCurveView::resolvePalette() {
    paletteResolved_ = true;
    auto* frame = getFrame();
    if (frame == nullptr) {
        return;
    }
    auto* editor = dynamic_cast<VSTGUI::VST3Editor*>(frame->getEditor());
    if (editor == nullptr) {
        return;
    }
    const VSTGUI::UIDescription* desc = editor->getUIDescription();
    if (desc == nullptr) {
        return;
    }
    VSTGUI::CColor c;
    if (desc->getColor("panel", c)) {
        panel_ = c;
    }
    if (desc->getColor("track", c)) {
        track_ = c;
    }
    if (desc->getColor("text", c)) {
        text_ = c;
    }
    if (desc->getColor("text-dim", c)) {
        textDim_ = c;
    }
    if (desc->getColor("accent", c)) {
        accent_ = c;
    }
}

void SpreadCurveView::draw(VSTGUI::CDrawContext* context) {
    if (!paletteResolved_) {
        resolvePalette();
    }
    const VSTGUI::CRect r = getViewSize();
    context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
    context->setFillColor(panel_);
    context->drawRect(r, VSTGUI::kDrawFilled);

    // Plot area: axis labels MONO / WIDE on the left, the WIDE note (if any) on
    // the bottom row.
    const VSTGUI::CRect plot(r.left + kAxisLabelWidth, r.top + 4.0, r.right - 4.0, r.bottom - kLabelHeight - 2.0);
    if (plot.getWidth() <= 0.0 || plot.getHeight() <= 0.0) {
        setDirty(false);
        return;
    }
    context->setFillColor(track_);
    context->drawRect(plot, VSTGUI::kDrawFilled);

    context->setFont(font_ ? font_.get() : VSTGUI::kNormalFontSmaller);
    context->setFontColor(textDim_);
    context->drawString("WIDE", VSTGUI::CRect(r.left, plot.top, plot.left - 2.0, plot.top + kLabelHeight),
                        VSTGUI::kRightText);
    context->drawString("MONO", VSTGUI::CRect(r.left, plot.bottom - kLabelHeight, plot.left - 2.0, plot.bottom),
                        VSTGUI::kRightText);

    if (model_ == nullptr) {
        setDirty(false);
        return;
    }
    const SpreadCurveModel& m = *model_;

    // Knee marks: two vertical ticks at the law's own knees (plan 4.6.8 step 6).
    context->setFrameColor(textDim_);
    context->setLineWidth(1.0);
    for (const float kneeHz : {m.lowerKneeHz, m.upperKneeHz}) {
        if (!(kneeHz > 0.0f)) {
            continue;
        }
        const VSTGUI::CCoord x = plot.left + (static_cast<VSTGUI::CCoord>(axisPositionOf(kneeHz)) * plot.getWidth());
        context->drawLine(VSTGUI::CPoint(x, plot.top), VSTGUI::CPoint(x, plot.bottom));
    }

    // The curve: 64 log-spaced points (i / 63 on the axis by construction of
    // hz[i]) joined by straight segments; the vertical scale is 0 .. 1.
    context->setFrameColor(accent_);
    context->setLineWidth(kCurveWidth);
    VSTGUI::CPoint previous;
    for (std::size_t i = 0; i < m.w.size(); ++i) {
        // `!(x > 0)` also sends a NaN to 0 without naming a predicate.
        const float wi = !(m.w[i] > 0.0f) ? 0.0f : std::min(m.w[i], 1.0f);
        const VSTGUI::CCoord x =
            plot.left + (static_cast<VSTGUI::CCoord>(i) / static_cast<VSTGUI::CCoord>(m.w.size() - 1)) * plot.getWidth();
        const VSTGUI::CCoord y = plot.bottom - (static_cast<VSTGUI::CCoord>(wi) * plot.getHeight());
        const VSTGUI::CPoint p(x, y);
        if (i > 0) {
            context->drawLine(previous, p);
        }
        previous = p;
    }

    if (m.wide) {
        context->setFontColor(text_);
        context->drawString("WIDE: Character offsets not drawn",
                            VSTGUI::CRect(plot.left, plot.bottom + 2.0, plot.right, r.bottom), VSTGUI::kLeftText);
    }
    setDirty(false);
}

}  // namespace Profundum::UI
