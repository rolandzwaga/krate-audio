// ==============================================================================
// Profundum Phase 12 - OffsetRingKnob implementation (FR-061; P-5 ruling)
// ==============================================================================
// Tasks: specs/profundum-phase12-ui/tasks.md (T016 "P-5 re-cut")
// ==============================================================================

#include "ui/offset_ring_knob.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cmath>

namespace Profundum {

namespace {

constexpr float kMinDrawnSpan = 0.001f;  // ArcKnob's own draw threshold (arc_knob.h:339, :344)

}  // namespace

void OffsetRingKnob::setOffset(float signedNormDelta) noexcept {
    if (signedNormDelta != offset_) {
        offset_ = signedNormDelta;
        setDirty();
    }
}

std::optional<OffsetRingKnob::Span> OffsetRingKnob::ringSpan() const noexcept {
    if (offset_ == 0.0f) {
        return std::nullopt;
    }
    const float v = getValueNormalized();
    const float end = v + offset_;
    Span s;
    s.low = std::clamp(std::min(v, end), 0.0f, 1.0f);
    s.high = std::clamp(std::max(v, end), 0.0f, 1.0f);
    return s;
}

void OffsetRingKnob::draw(VSTGUI::CDrawContext* context) {
    // The symmetric base ring is never drawn (P-5): keep its range at 0.
    if (getModulationRange() != 0.0f) {
        setModulationRange(0.0f);
    }
    ArcKnob::draw(context);  // ends with setDirty(false), arc_knob.h:124
    drawOffsetArc(context);
}

void OffsetRingKnob::drawOffsetArc(VSTGUI::CDrawContext* context) const {
    const auto span = ringSpan();
    if (!span || (span->high - span->low) < kMinDrawnSpan) {
        return;
    }

    // Same inner-ring geometry as ArcKnob::drawModulationArc (arc_knob.h:347-357),
    // rebuilt from the public getters because the base helpers are private.
    const VSTGUI::CRect vs = getViewSize();
    const double dim = std::min(vs.getWidth(), vs.getHeight());
    const double arcRadius = (dim / 2.0) - (getIndicatorLength() / 2.0);
    const double modRadius = arcRadius - (getArcLineWidth() / 2.0) - (getModArcLineWidth() / 2.0) - 2.0;
    if (modRadius < 1.0) {
        return;
    }
    const double cx = vs.left + (vs.getWidth() / 2.0);
    const double cy = vs.top + (vs.getHeight() / 2.0);
    const VSTGUI::CRect modRect(cx - modRadius, cy - modRadius, cx + modRadius, cy + modRadius);

    const auto toDeg = [this](float normalised) {
        const double rad = static_cast<double>(getStartAngle()) +
                           (static_cast<double>(normalised) * static_cast<double>(getRangeAngle()));
        return rad * (180.0 / VSTGUI::Constants::pi);
    };

    auto path = VSTGUI::owned(context->createGraphicsPath());
    if (!path) {
        return;
    }
    path->addArc(modRect, toDeg(span->low), toDeg(span->high), true);
    context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
    context->setFrameColor(getModColor());
    context->setLineWidth(getModArcLineWidth());
    context->setLineStyle(VSTGUI::kLineSolid);
    context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
}

}  // namespace Profundum
