// ==============================================================================
// Profundum Phase 12 - HarmonicBarView implementation (FR-070, FR-073, FR-091)
// ==============================================================================
// Plan:   specs/profundum-phase12-ui/plan.md (section 4.8)
// Design: specs/profundum-phase12-ui/design/advanced-page.md section 3 (D2)
// ==============================================================================

#include "ui/harmonic_bar_view.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cdrawdefs.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uidescription.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace Profundum::UI {

namespace {

// D7 token values (design/accessibility-scaling.md colour table).
const VSTGUI::CColor kFallbackPanel{0x1A, 0x1A, 0x20, 0xFF};
const VSTGUI::CColor kFallbackTrack{0x22, 0x22, 0x2A, 0xFF};
const VSTGUI::CColor kFallbackTextDim{0x9A, 0x9A, 0xA8, 0xFF};
const VSTGUI::CColor kFallbackAnchor{0xD0, 0x8A, 0x5C, 0xFF};
const VSTGUI::CColor kFallbackMass{0x7F, 0xB0, 0xD8, 0xFF};

constexpr VSTGUI::CCoord kLabelHeight = 12.0;  // one 10 px text line (D7)
constexpr VSTGUI::CCoord kFontSize = 10.0;
constexpr VSTGUI::CCoord kAnchorLaneFraction = 0.12;
constexpr VSTGUI::CCoord kLaneGap = 6.0;
constexpr VSTGUI::CCoord kMinBarWidth = 2.0;
constexpr double kLog2MaxPartial = 6.0;  // log2(64): the right edge of the axis

// The region shading colours derive from the mass token (D2: "Sub / Body /
// Presence shading derives from it").
constexpr std::uint8_t kSubShadeAlpha = 0x30;
constexpr std::uint8_t kBodyShadeAlpha = 0x20;
constexpr std::uint8_t kPresenceShadeAlpha = 0x12;

VSTGUI::CColor withAlpha(VSTGUI::CColor c, std::uint8_t alpha) noexcept {
    c.alpha = alpha;
    return c;
}

/// x of log2(n) inside [left, right] for n in [1, 64] (n may be fractional for
/// the region boundaries).
VSTGUI::CCoord xOfPartial(double n, VSTGUI::CCoord left, VSTGUI::CCoord right) noexcept {
    const double u = std::clamp(std::log2(n) / kLog2MaxPartial, 0.0, 1.0);
    return left + (u * (right - left));
}

}  // namespace

// ==============================================================================
// Construction / lifecycle (FR-091)
// ==============================================================================

HarmonicBarView::HarmonicBarView(const VSTGUI::CRect& size, const ProfundumSpectrumFrame* frame, Variant v)
    : VSTGUI::CView(size),
      frame_(frame),
      variant_(v),
      panel_(kFallbackPanel),
      track_(kFallbackTrack),
      textDim_(kFallbackTextDim),
      anchor_(kFallbackAnchor),
      mass_(kFallbackMass),
      font_(VSTGUI::makeOwned<VSTGUI::CFontDesc>("", kFontSize)) {}

HarmonicBarView::~HarmonicBarView() {
    if (timer_) {
        timer_->stop();
        timer_ = nullptr;
    }
}

bool HarmonicBarView::attached(VSTGUI::CView* parent) {
    const bool ok = VSTGUI::CView::attached(parent);
    if (ok && !timer_) {
        lastTick_ = std::chrono::steady_clock::now();
        timer_ = VSTGUI::owned(new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer* /*t*/) { tick(); }, kTimerMs, true));
    }
    return ok;
}

bool HarmonicBarView::removed(VSTGUI::CView* parent) {
    // Cancel BEFORE teardown so no tick reads the controller's frame afterwards.
    if (timer_) {
        timer_->stop();
        timer_ = nullptr;
    }
    return VSTGUI::CView::removed(parent);
}

// ==============================================================================
// Display only (FR-070, SC-024)
// ==============================================================================

VSTGUI::CMouseEventResult HarmonicBarView::onMouseDown(VSTGUI::CPoint& /*where*/,
                                                       const VSTGUI::CButtonState& /*buttons*/) {
    return VSTGUI::kMouseEventNotHandled;
}

VSTGUI::CMouseEventResult HarmonicBarView::onMouseMoved(VSTGUI::CPoint& /*where*/,
                                                        const VSTGUI::CButtonState& /*buttons*/) {
    return VSTGUI::kMouseEventNotHandled;
}

VSTGUI::CMouseEventResult HarmonicBarView::onMouseUp(VSTGUI::CPoint& /*where*/,
                                                     const VSTGUI::CButtonState& /*buttons*/) {
    return VSTGUI::kMouseEventNotHandled;
}

// ==============================================================================
// Tick (plan 4.8)
// ==============================================================================

float HarmonicBarView::heightFromPower(float power, float referencePower) noexcept {
    // `!(x > 0)` also sends a NaN to 0 without naming a predicate.
    if (!(power > 0.0f) || !(referencePower > 0.0f)) {
        return 0.0f;
    }
    const float db = 10.0f * std::log10(power / referencePower);
    const float h = (db - kFloorDb) / (-kFloorDb);
    if (!(h > 0.0f)) {
        return 0.0f;
    }
    return std::min(h, 1.0f);
}

void HarmonicBarView::tick() {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - lastTick_).count();
    lastTick_ = now;
    onTick(dt);
}

void HarmonicBarView::onTick(float dtSeconds) {
    if (frame_ == nullptr) {
        return;
    }
    const ProfundumSpectrumFrame& f = *frame_;
    if (f.sequence != lastSeq_) {
        if ((f.flags & kFrameFlagSilent) != 0) {
            decaying_ = true;
        } else {
            decaying_ = false;
            const std::size_t count = std::min<std::size_t>(f.numPartials, kSpectrumMaxPartials);
            float reference = f.anchorPower > 0.0f ? f.anchorPower : 0.0f;
            for (std::size_t n = 0; n < count; ++n) {
                reference = std::max(reference, f.partialPower[n]);
            }
            for (std::size_t n = 0; n < kSpectrumMaxPartials; ++n) {
                heights_[n] = n < count ? heightFromPower(f.partialPower[n], reference) : 0.0f;
            }
            anchorHeight_ = heightFromPower(f.anchorPower, reference);
            drawnPartials_ = static_cast<std::uint8_t>(count);
        }
        lastSeq_ = f.sequence;
    }

    if (decaying_) {
        const float step = std::max(dtSeconds, 0.0f) / kSilenceDecaySeconds;
        for (float& h : heights_) {
            h = std::max(h - step, 0.0f);
        }
        anchorHeight_ = std::max(anchorHeight_ - step, 0.0f);
    }
    invalid();
}

// ==============================================================================
// Draw (D2 section 3)
// ==============================================================================

void HarmonicBarView::resolvePalette() {
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
    if (desc->getColor("text-dim", c)) {
        textDim_ = c;
    }
    if (desc->getColor("anchor", c)) {
        anchor_ = c;
    }
    if (desc->getColor("mass", c)) {
        mass_ = c;
    }
}

void HarmonicBarView::draw(VSTGUI::CDrawContext* context) {
    if (!paletteResolved_) {
        resolvePalette();
    }
    const VSTGUI::CRect r = getViewSize();
    context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
    context->setFillColor(panel_);
    context->drawRect(r, VSTGUI::kDrawFilled);

    const VSTGUI::CCoord plotTop = r.top + 2.0;
    const VSTGUI::CCoord plotBottom = r.bottom - kLabelHeight;
    const VSTGUI::CCoord plotHeight = std::max<VSTGUI::CCoord>(plotBottom - plotTop, 0.0);

    // Lanes: the Anchor on the left, the Mass bars to its right.
    const VSTGUI::CCoord anchorRight = r.left + (r.getWidth() * kAnchorLaneFraction);
    const VSTGUI::CCoord massLeft = anchorRight + kLaneGap;
    const VSTGUI::CCoord massRight = r.right - 2.0;
    const VSTGUI::CCoord barWidth =
        std::max(kMinBarWidth, (massRight - massLeft) / static_cast<VSTGUI::CCoord>(kSpectrumMaxPartials) * 0.6);
    // Inset the axis by half a bar so n = 1 and n = 64 stay inside the lane.
    const VSTGUI::CCoord axisLeft = massLeft + (barWidth / 2.0);
    const VSTGUI::CCoord axisRight = massRight - (barWidth / 2.0);

    // Region shading: Sub (n = 1), Body (2 - 8), Presence (9 - 64). The edges sit
    // halfway (in log2 n) between the neighbouring partials.
    const VSTGUI::CCoord subBodyEdge = xOfPartial(std::numbers::sqrt2, axisLeft, axisRight);
    const VSTGUI::CCoord bodyPresenceEdge = xOfPartial(std::sqrt(72.0), axisLeft, axisRight);
    context->setFillColor(withAlpha(mass_, kSubShadeAlpha));
    context->drawRect(VSTGUI::CRect(massLeft, plotTop, subBodyEdge, plotBottom), VSTGUI::kDrawFilled);
    context->setFillColor(withAlpha(mass_, kBodyShadeAlpha));
    context->drawRect(VSTGUI::CRect(subBodyEdge, plotTop, bodyPresenceEdge, plotBottom), VSTGUI::kDrawFilled);
    context->setFillColor(withAlpha(mass_, kPresenceShadeAlpha));
    context->drawRect(VSTGUI::CRect(bodyPresenceEdge, plotTop, massRight, plotBottom), VSTGUI::kDrawFilled);

    // Anchor lane.
    const VSTGUI::CRect anchorLane(r.left + 2.0, plotTop, anchorRight, plotBottom);
    context->setFillColor(track_);
    context->drawRect(anchorLane, VSTGUI::kDrawFilled);
    if (anchorHeight_ > 0.0f) {
        const VSTGUI::CCoord top = plotBottom - (plotHeight * static_cast<VSTGUI::CCoord>(anchorHeight_));
        const VSTGUI::CCoord inset = std::min<VSTGUI::CCoord>(anchorLane.getWidth() * 0.2, 6.0);
        context->setFillColor(anchor_);
        context->drawRect(VSTGUI::CRect(anchorLane.left + inset, top, anchorLane.right - inset, plotBottom),
                          VSTGUI::kDrawFilled);
    }

    // Mass bars: one per n <= numPartials at x = log2 n.
    context->setFillColor(mass_);
    for (std::size_t n = 0; n < drawnPartials_ && n < kSpectrumMaxPartials; ++n) {
        const float h = heights_[n];
        if (!(h > 0.0f)) {
            continue;
        }
        const VSTGUI::CCoord cx = xOfPartial(static_cast<double>(n + 1), axisLeft, axisRight);
        const VSTGUI::CCoord top = plotBottom - (plotHeight * static_cast<VSTGUI::CCoord>(h));
        context->drawRect(VSTGUI::CRect(cx - (barWidth / 2.0), top, cx + (barWidth / 2.0), plotBottom),
                          VSTGUI::kDrawFilled);
    }

    // Labels (10 px, D7).
    context->setFont(font_ ? font_.get() : VSTGUI::kNormalFontSmaller);
    const VSTGUI::CRect labelRow(r.left, plotBottom, r.right, r.bottom);
    context->setFontColor(anchor_);
    context->drawString("ANCHOR", VSTGUI::CRect(r.left, labelRow.top, anchorRight + kLaneGap, labelRow.bottom),
                        VSTGUI::kCenterText);
    if (variant_ == Variant::Main) {
        // The ANCHOR / MASS split: a divider between the lanes and the MASS caption.
        context->setFrameColor(textDim_);
        context->setLineWidth(1.0);
        const VSTGUI::CCoord splitX = anchorRight + (kLaneGap / 2.0);
        context->drawLine(VSTGUI::CPoint(splitX, plotTop), VSTGUI::CPoint(splitX, plotBottom));
        context->setFontColor(mass_);
        context->drawString("MASS", VSTGUI::CRect(massLeft, labelRow.top, massRight, labelRow.bottom),
                            VSTGUI::kCenterText);
    } else {
        context->setFontColor(textDim_);
        context->drawString("SUB", VSTGUI::CRect(massLeft, labelRow.top, subBodyEdge, labelRow.bottom),
                            VSTGUI::kCenterText);
        context->drawString("BODY", VSTGUI::CRect(subBodyEdge, labelRow.top, bodyPresenceEdge, labelRow.bottom),
                            VSTGUI::kCenterText);
        context->drawString("PRESENCE", VSTGUI::CRect(bodyPresenceEdge, labelRow.top, massRight, labelRow.bottom),
                            VSTGUI::kCenterText);
    }
    setDirty(false);
}

}  // namespace Profundum::UI
