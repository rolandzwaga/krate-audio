// ==============================================================================
// PitchEnvelopeDisplay -- Phase 6 implementation (Spec 141, US8, T080)
// ==============================================================================

#include "ui/pitch_envelope_display.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"

#include <algorithm>
#include <cmath>

namespace Membrum::UI {

namespace {

// Palette matches the rest of the Phase 6 custom views.
constexpr VSTGUI::CColor kBgColor      = { 18,  18,  22, 255 };
constexpr VSTGUI::CColor kBorderColor  = { 50,  50,  60, 255 };
constexpr VSTGUI::CColor kCurveColor   = {180, 140,  90, 255 };
constexpr VSTGUI::CColor kHandleColor  = {255, 200, 110, 255 };
constexpr VSTGUI::CColor kHandleActive = {255, 230, 150, 255 };

[[nodiscard]] inline float clamp01(float n) noexcept
{
    return std::clamp(n, 0.0f, 1.0f);
}

} // namespace

// ------------------------------------------------------------------------------
// Construction / destruction
// ------------------------------------------------------------------------------
PitchEnvelopeDisplay::PitchEnvelopeDisplay(const VSTGUI::CRect& size,
                                           ParamTags            tags) noexcept
    : CView(size)
    , tags_(tags)
{
}

PitchEnvelopeDisplay::~PitchEnvelopeDisplay() = default;

// ------------------------------------------------------------------------------
// Parameter sync
// ------------------------------------------------------------------------------
void PitchEnvelopeDisplay::setStartNormalized(float n) noexcept
{
    startN_ = clamp01(n);
    setDirty();
}

void PitchEnvelopeDisplay::setEndNormalized(float n) noexcept
{
    endN_ = clamp01(n);
    setDirty();
}

void PitchEnvelopeDisplay::setTimeNormalized(float n) noexcept
{
    timeN_ = clamp01(n);
    setDirty();
}

void PitchEnvelopeDisplay::setCurveNormalized(float n) noexcept
{
    curveN_ = clamp01(n);
    setDirty();
}

void PitchEnvelopeDisplay::setNormalized(std::uint32_t paramId, float n) noexcept
{
    if (paramId == tags_.start)  { setStartNormalized(n); return; }
    if (paramId == tags_.end)    { setEndNormalized(n);   return; }
    if (paramId == tags_.time)   { setTimeNormalized(n);  return; }
    if (paramId == tags_.curve)  { setCurveNormalized(n); return; }
}

// ------------------------------------------------------------------------------
// Geometry helpers
// ------------------------------------------------------------------------------
double PitchEnvelopeDisplay::toLocalXFromTime(float n) const noexcept
{
    const auto& r = getViewSize();
    const double left  = r.left   + kPadding;
    const double right = r.right  - kPadding;
    return left + clamp01(n) * (right - left);
}

double PitchEnvelopeDisplay::toLocalYFromPitch(float n) const noexcept
{
    // High pitch -> top of view, low pitch -> bottom.
    const auto& r = getViewSize();
    const double top    = r.top    + kPadding;
    const double bottom = r.bottom - kPadding;
    return bottom - clamp01(n) * (bottom - top);
}

float PitchEnvelopeDisplay::fromLocalXToTime(double x) const noexcept
{
    const auto& r = getViewSize();
    const double left  = r.left   + kPadding;
    const double right = r.right  - kPadding;
    const double span  = std::max(1.0, right - left);
    const double n     = (x - left) / span;
    return clamp01(static_cast<float>(n));
}

float PitchEnvelopeDisplay::fromLocalYToPitch(double y) const noexcept
{
    const auto& r = getViewSize();
    const double top    = r.top    + kPadding;
    const double bottom = r.bottom - kPadding;
    const double span   = std::max(1.0, bottom - top);
    const double n      = (bottom - y) / span;
    return clamp01(static_cast<float>(n));
}

VSTGUI::CPoint PitchEnvelopeDisplay::handleCenter(DragTarget h) const noexcept
{
    const auto& r = getViewSize();
    const double left  = r.left   + kPadding;
    const double right = r.right  - kPadding;

    switch (h)
    {
    case DragTarget::Start:
        // Top-left anchor for Start.
        return VSTGUI::CPoint{ left, toLocalYFromPitch(startN_) };
    case DragTarget::End:
        // Bottom-right anchor for End.
        return VSTGUI::CPoint{ right, toLocalYFromPitch(endN_) };
    case DragTarget::Time:
        // Horizontal handle: x follows timeN_, y sits mid-envelope.
        return VSTGUI::CPoint{ toLocalXFromTime(timeN_),
                               (toLocalYFromPitch(startN_) +
                                toLocalYFromPitch(endN_)) * 0.5 };
    case DragTarget::None:
    default:
        return VSTGUI::CPoint{ 0.0, 0.0 };
    }
}

PitchEnvelopeDisplay::DragTarget
PitchEnvelopeDisplay::hitTest(VSTGUI::CPoint p) const noexcept
{
    DragTarget  best    = DragTarget::None;
    double      bestSq  = kHitRadius * kHitRadius;

    auto test = [&](DragTarget t) {
        const auto c  = handleCenter(t);
        const double dx = p.x - c.x;
        const double dy = p.y - c.y;
        const double d2 = dx * dx + dy * dy;
        if (d2 <= bestSq)
        {
            bestSq = d2;
            best   = t;
        }
    };

    test(DragTarget::Start);
    test(DragTarget::End);
    test(DragTarget::Time);
    return best;
}

// ------------------------------------------------------------------------------
// Edit dispatch
// ------------------------------------------------------------------------------
void PitchEnvelopeDisplay::dispatch(std::uint32_t paramId,
                                    EditOp        op,
                                    float         normalized) const noexcept
{
    if (editCallback_)
        editCallback_(paramId, op, normalized);
}

// ------------------------------------------------------------------------------
// Mouse handlers (testable)
// ------------------------------------------------------------------------------
void PitchEnvelopeDisplay::handleMouseDown(VSTGUI::CPoint localPoint) noexcept
{
    const auto target = hitTest(localPoint);
    if (target == DragTarget::None)
        return;

    dragTarget_ = target;

    switch (target)
    {
    case DragTarget::Start:
    {
        const float n = fromLocalYToPitch(localPoint.y);
        startN_ = n;
        dispatch(tags_.start, EditOp::Begin,   n);
        dispatch(tags_.start, EditOp::Perform, n);
        break;
    }
    case DragTarget::End:
    {
        const float n = fromLocalYToPitch(localPoint.y);
        endN_ = n;
        dispatch(tags_.end, EditOp::Begin,   n);
        dispatch(tags_.end, EditOp::Perform, n);
        break;
    }
    case DragTarget::Time:
    {
        const float n = fromLocalXToTime(localPoint.x);
        timeN_ = n;
        dispatch(tags_.time, EditOp::Begin,   n);
        dispatch(tags_.time, EditOp::Perform, n);
        break;
    }
    case DragTarget::None:
    default:
        break;
    }
    setDirty();
}

void PitchEnvelopeDisplay::handleMouseMove(VSTGUI::CPoint localPoint) noexcept
{
    if (dragTarget_ == DragTarget::None)
        return;

    switch (dragTarget_)
    {
    case DragTarget::Start:
    {
        const float n = fromLocalYToPitch(localPoint.y);
        startN_ = n;
        dispatch(tags_.start, EditOp::Perform, n);
        break;
    }
    case DragTarget::End:
    {
        const float n = fromLocalYToPitch(localPoint.y);
        endN_ = n;
        dispatch(tags_.end, EditOp::Perform, n);
        break;
    }
    case DragTarget::Time:
    {
        const float n = fromLocalXToTime(localPoint.x);
        timeN_ = n;
        dispatch(tags_.time, EditOp::Perform, n);
        break;
    }
    case DragTarget::None:
    default:
        break;
    }
    setDirty();
}

void PitchEnvelopeDisplay::handleMouseUp(VSTGUI::CPoint /*localPoint*/) noexcept
{
    if (dragTarget_ == DragTarget::None)
        return;

    switch (dragTarget_)
    {
    case DragTarget::Start:  dispatch(tags_.start, EditOp::End, startN_); break;
    case DragTarget::End:    dispatch(tags_.end,   EditOp::End, endN_);   break;
    case DragTarget::Time:   dispatch(tags_.time,  EditOp::End, timeN_);  break;
    case DragTarget::None:
    default: break;
    }
    dragTarget_ = DragTarget::None;
    setDirty();
}

// ------------------------------------------------------------------------------
// VSTGUI mouse event overrides
// ------------------------------------------------------------------------------
void PitchEnvelopeDisplay::onMouseDownEvent(VSTGUI::MouseDownEvent& event)
{
    const auto before = dragTarget_;
    handleMouseDown(event.mousePosition);
    if (dragTarget_ != DragTarget::None && before == DragTarget::None)
        event.consumed = true;
}

void PitchEnvelopeDisplay::onMouseMoveEvent(VSTGUI::MouseMoveEvent& event)
{
    if (dragTarget_ == DragTarget::None)
        return;
    handleMouseMove(event.mousePosition);
    event.consumed = true;
}

void PitchEnvelopeDisplay::onMouseUpEvent(VSTGUI::MouseUpEvent& event)
{
    if (dragTarget_ == DragTarget::None)
        return;
    handleMouseUp(event.mousePosition);
    event.consumed = true;
}

bool PitchEnvelopeDisplay::removed(CView* parent)
{
    // Ensure any in-flight drag is terminated cleanly so beginEdit/endEdit
    // pairs remain balanced when the editor closes mid-drag.
    if (dragTarget_ != DragTarget::None)
    {
        handleMouseUp(VSTGUI::CPoint{ 0.0, 0.0 });
    }
    return CView::removed(parent);
}

// ------------------------------------------------------------------------------
// draw() -- simple 2D envelope: Start (top-left) -> End (bottom-right) with a
// Time marker on the connecting line. All rendering is VSTGUI-only (T083).
// ------------------------------------------------------------------------------
void PitchEnvelopeDisplay::draw(VSTGUI::CDrawContext* ctx)
{
    if (ctx == nullptr)
        return;

    const auto& viewRect = getViewSize();

    ctx->setFillColor(kBgColor);
    ctx->drawRect(viewRect, VSTGUI::kDrawFilled);

    ctx->setFrameColor(kBorderColor);
    ctx->setLineWidth(1.0);
    ctx->drawRect(viewRect, VSTGUI::kDrawStroked);

    const auto startPt = handleCenter(DragTarget::Start);
    const auto endPt   = handleCenter(DragTarget::End);
    const auto timePt  = handleCenter(DragTarget::Time);

    // Envelope curve (straight segment between Start and End for v0.6.0; the
    // Exp vs. Lin curve selector is a visual-future enhancement).
    ctx->setFrameColor(kCurveColor);
    ctx->setLineWidth(2.0);
    ctx->drawLine(startPt, endPt);

    auto drawHandle = [&](const VSTGUI::CPoint& c, DragTarget target) {
        const VSTGUI::CRect hr{
            c.x - kHandleRadius, c.y - kHandleRadius,
            c.x + kHandleRadius, c.y + kHandleRadius
        };
        ctx->setFillColor(dragTarget_ == target ? kHandleActive : kHandleColor);
        ctx->drawRect(hr, VSTGUI::kDrawFilled);
        ctx->setFrameColor(kBorderColor);
        ctx->drawRect(hr, VSTGUI::kDrawStroked);
    };

    drawHandle(startPt, DragTarget::Start);
    drawHandle(endPt,   DragTarget::End);
    drawHandle(timePt,  DragTarget::Time);

    setDirty(false);
}

} // namespace Membrum::UI
