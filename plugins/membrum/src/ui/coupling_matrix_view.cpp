// ==============================================================================
// CouplingMatrixView -- Phase 6 implementation (Spec 141, US6)
// ==============================================================================

#include "ui/coupling_matrix_view.h"

#include "dsp/matrix_activity_publisher.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"

#include <algorithm>
#include <cmath>

namespace Membrum::UI {

namespace {

// 30 Hz poll timer period (ms) -- R2 mitigation (research.md section 4).
constexpr std::uint32_t kPollTimerMs = 33;

// Palette (matches PadGridView for visual cohesion).
constexpr VSTGUI::CColor kBgColor         = { 18,  18,  22, 255 };
constexpr VSTGUI::CColor kCellEmpty       = { 34,  34,  42, 255 };
constexpr VSTGUI::CColor kCellGrid        = { 50,  50,  60, 255 };
constexpr VSTGUI::CColor kActivityOutline = {255, 200, 110, 255 };
constexpr VSTGUI::CColor kOverrideOutline = {230, 200,  90, 255 };
constexpr VSTGUI::CColor kSoloOutline     = {255, 120, 120, 255 };
constexpr VSTGUI::CColor kDiagonalColor   = { 24,  24,  28, 255 };

// Override click-cycle steps (FR-051).
constexpr float kStep1 = 0.010f;
constexpr float kStep2 = 0.025f;
constexpr float kStep3 = 0.050f;

} // anonymous namespace

// ------------------------------------------------------------------------------
// Override click-cycle (no override -> 0.01 -> 0.025 -> 0.05 -> cleared).
// ------------------------------------------------------------------------------
CouplingMatrixView::OverrideStep
CouplingMatrixView::nextOverrideStep(bool hasOverride, float currentValue) noexcept
{
    if (!hasOverride)
        return {true, kStep1};

    // Tolerant comparisons against the discrete step values.
    constexpr float kEps = 1.0e-4f;
    if (std::fabs(currentValue - kStep1) < kEps)
        return {true, kStep2};
    if (std::fabs(currentValue - kStep2) < kEps)
        return {true, kStep3};
    if (std::fabs(currentValue - kStep3) < kEps)
        return {false, 0.0f};
    // Unknown intermediate value -> advance to the nearest higher step.
    if (currentValue < kStep1) return {true, kStep1};
    if (currentValue < kStep2) return {true, kStep2};
    if (currentValue < kStep3) return {true, kStep3};
    return {false, 0.0f};
}

// ------------------------------------------------------------------------------
// CouplingMatrixView
// ------------------------------------------------------------------------------
CouplingMatrixView::CouplingMatrixView(const VSTGUI::CRect&      size,
                                       CouplingMatrix*           matrix,
                                       MatrixActivityPublisher*  activityPublisher) noexcept
    : CView(size)
    , matrix_(matrix)
    , activityPublisher_(activityPublisher)
{
    activityMask_.fill(0u);
}

CouplingMatrixView::~CouplingMatrixView()
{
    // FR-053 edge case: Solo must never outlive the view.
    clearSolo();
    if (pollTimer_)
    {
        pollTimer_->stop();
        pollTimer_ = nullptr;
    }
}

CouplingMatrixView::CellGeom CouplingMatrixView::cellGeom() const noexcept
{
    const auto& r = getViewSize();
    CellGeom g;
    g.cellW = static_cast<float>(r.getWidth())  / static_cast<float>(kMatrixCells);
    g.cellH = static_cast<float>(r.getHeight()) / static_cast<float>(kMatrixCells);
    return g;
}

VSTGUI::CRect CouplingMatrixView::cellRect(int src, int dst) const noexcept
{
    VSTGUI::CRect out;
    if (src < 0 || src >= kMatrixCells || dst < 0 || dst >= kMatrixCells)
        return out;
    const auto g = cellGeom();
    const auto& r = getViewSize();
    // Convention: src along X (columns), dst along Y (rows). Row 0 at the top.
    out.left   = r.left + src * g.cellW;
    out.top    = r.top  + dst * g.cellH;
    out.right  = out.left + g.cellW;
    out.bottom = out.top  + g.cellH;
    return out;
}

CouplingMatrixView::Cell
CouplingMatrixView::cellFromPoint(VSTGUI::CPoint p) const noexcept
{
    Cell c;
    const auto& r = getViewSize();
    const double localX = p.x - static_cast<double>(r.left);
    const double localY = p.y - static_cast<double>(r.top);
    if (localX < 0.0 || localY < 0.0)
        return c;
    if (localX >= r.getWidth() || localY >= r.getHeight())
        return c;
    const auto g = cellGeom();
    if (g.cellW <= 0.0f || g.cellH <= 0.0f)
        return c;
    c.src = std::clamp(static_cast<int>(localX / static_cast<double>(g.cellW)),
                       0, kMatrixCells - 1);
    c.dst = std::clamp(static_cast<int>(localY / static_cast<double>(g.cellH)),
                       0, kMatrixCells - 1);
    return c;
}

CouplingMatrixView::Cell
CouplingMatrixView::handleMouseDown(VSTGUI::CPoint localPoint,
                                    bool           isShift,
                                    bool           isRight) noexcept
{
    const auto cell = cellFromPoint(localPoint);
    if (cell.src < 0 || cell.dst < 0)
        return cell;
    if (matrix_ == nullptr)
        return cell;
    // The diagonal cells (src == dst) have no meaningful coupling gain --
    // ignore interactions there to match the audio resolver (computedGain is
    // pinned to 0 on the diagonal).
    if (cell.src == cell.dst)
        return {-1, -1};

    if (isShift)
    {
        // FR-053: toggle Solo on this pair.
        if (matrix_->hasSolo() &&
            matrix_->soloSrc() == cell.src &&
            matrix_->soloDst() == cell.dst)
        {
            clearSolo();
        }
        else
        {
            setSoloPath(cell.src, cell.dst);
        }
        invalid();
        return cell;
    }

    if (isRight)
    {
        // Right click = immediate Reset on this pair.
        matrix_->clearOverride(cell.src, cell.dst);
        invalid();
        return cell;
    }

    // Left click: cycle override step.
    const bool  had = matrix_->hasOverrideAt(cell.src, cell.dst);
    const float cur = matrix_->getOverrideGain(cell.src, cell.dst);
    const auto  next = nextOverrideStep(had, cur);
    if (next.hasOverride)
        matrix_->setOverride(cell.src, cell.dst, next.value);
    else
        matrix_->clearOverride(cell.src, cell.dst);
    invalid();
    return cell;
}

void CouplingMatrixView::setSoloPath(int src, int dst) noexcept
{
    if (matrix_ != nullptr)
        matrix_->setSoloPath(src, dst);
}

void CouplingMatrixView::clearSolo() noexcept
{
    if (matrix_ != nullptr)
        matrix_->clearSolo();
}

void CouplingMatrixView::pollActivity() noexcept
{
    if (activityPublisher_ == nullptr)
        return;
    std::array<std::uint32_t, kMatrixCells> fresh{};
    activityPublisher_->snapshot(fresh);
    bool anyChanged = false;
    for (int i = 0; i < kMatrixCells; ++i)
    {
        if (fresh[static_cast<std::size_t>(i)] !=
            activityMask_[static_cast<std::size_t>(i)])
        {
            anyChanged = true;
            break;
        }
    }
    activityMask_ = fresh;
    if (anyChanged)
        invalid();
}

bool CouplingMatrixView::attached(VSTGUI::CView* parent)
{
    const bool ok = CView::attached(parent);
    if (ok && activityPublisher_ != nullptr && !pollTimer_)
    {
        pollTimer_ = VSTGUI::owned(new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer* /*t*/) { pollActivity(); },
            kPollTimerMs,
            true /* fire immediately */));
    }
    return ok;
}

bool CouplingMatrixView::removed(VSTGUI::CView* parent)
{
    // FR-053 edge case: disengage Solo and cancel the poll timer before the
    // view is torn down.
    clearSolo();
    if (pollTimer_)
    {
        pollTimer_->stop();
        pollTimer_ = nullptr;
    }
    return CView::removed(parent);
}

void CouplingMatrixView::onMouseDownEvent(VSTGUI::MouseDownEvent& event)
{
    const bool isShift = event.modifiers.has(VSTGUI::ModifierKey::Shift);
    const bool isRight = (event.buttonState == VSTGUI::MouseButton::Right);
    const auto hit = handleMouseDown(event.mousePosition, isShift, isRight);
    if (hit.src >= 0 && hit.dst >= 0)
        event.consumed = true;
}

// ------------------------------------------------------------------------------
// draw() -- heat map over 32x32 cells. Uses only VSTGUI primitives (CRect,
// CColor, CDrawContext) per FR-090 / T071.
// ------------------------------------------------------------------------------
void CouplingMatrixView::draw(VSTGUI::CDrawContext* ctx)
{
    if (ctx == nullptr)
        return;

    const auto& viewRect = getViewSize();
    ctx->setFillColor(kBgColor);
    ctx->drawRect(viewRect, VSTGUI::kDrawFilled);

    const auto g = cellGeom();
    if (g.cellW <= 0.0f || g.cellH <= 0.0f)
        return;

    const int soloSrc = (matrix_ != nullptr) ? matrix_->soloSrc() : -1;
    const int soloDst = (matrix_ != nullptr) ? matrix_->soloDst() : -1;

    for (int dst = 0; dst < kMatrixCells; ++dst)
    {
        const std::uint32_t activityBits =
            activityMask_[static_cast<std::size_t>(dst)];
        // NOTE: activity publisher bitfield is indexed by source pad; bit D
        // set -> pair (src=index, dst=D). We iterate with src as the outer
        // loop for intensity drawing and test activity bits accordingly.
        (void)activityBits; // actually indexed per-src below
        for (int src = 0; src < kMatrixCells; ++src)
        {
            VSTGUI::CRect cell;
            cell.left   = viewRect.left + src * g.cellW;
            cell.top    = viewRect.top  + dst * g.cellH;
            cell.right  = cell.left + g.cellW;
            cell.bottom = cell.top  + g.cellH;

            if (src == dst)
            {
                ctx->setFillColor(kDiagonalColor);
                ctx->drawRect(cell, VSTGUI::kDrawFilled);
                ctx->setFrameColor(kCellGrid);
                ctx->setLineWidth(1.0);
                ctx->drawRect(cell, VSTGUI::kDrawStroked);
                continue;
            }

            // Intensity: effectiveGain in [0, kMaxCoefficient] -> [0, 1].
            float gain = 0.0f;
            if (matrix_ != nullptr)
                gain = matrix_->getEffectiveGain(src, dst);
            const float intensity = std::clamp(
                gain / CouplingMatrix::kMaxCoefficient, 0.0f, 1.0f);

            VSTGUI::CColor fill = kCellEmpty;
            if (intensity > 0.0f)
            {
                // Warm gradient from kCellEmpty -> kActivityOutline.
                fill.red   = static_cast<std::uint8_t>(
                    kCellEmpty.red   + (kActivityOutline.red   - kCellEmpty.red)   * intensity);
                fill.green = static_cast<std::uint8_t>(
                    kCellEmpty.green + (kActivityOutline.green - kCellEmpty.green) * intensity);
                fill.blue  = static_cast<std::uint8_t>(
                    kCellEmpty.blue  + (kActivityOutline.blue  - kCellEmpty.blue)  * intensity);
                fill.alpha = 255;
            }
            ctx->setFillColor(fill);
            ctx->drawRect(cell, VSTGUI::kDrawFilled);

            // Activity outline (audio thread has flagged this pair this block).
            const std::uint32_t srcActivity =
                activityMask_[static_cast<std::size_t>(src)];
            const bool active = (srcActivity & (1u << dst)) != 0u;
            const bool overridden = matrix_ != nullptr &&
                                    matrix_->hasOverrideAt(src, dst);
            const bool isSolo = (soloSrc == src && soloDst == dst);

            if (isSolo)
            {
                ctx->setFrameColor(kSoloOutline);
                ctx->setLineWidth(2.0);
                ctx->drawRect(cell, VSTGUI::kDrawStroked);
            }
            else if (active)
            {
                ctx->setFrameColor(kActivityOutline);
                ctx->setLineWidth(1.5);
                ctx->drawRect(cell, VSTGUI::kDrawStroked);
            }
            else if (overridden)
            {
                ctx->setFrameColor(kOverrideOutline);
                ctx->setLineWidth(1.0);
                ctx->drawRect(cell, VSTGUI::kDrawStroked);
            }
            else
            {
                ctx->setFrameColor(kCellGrid);
                ctx->setLineWidth(1.0);
                ctx->drawRect(cell, VSTGUI::kDrawStroked);
            }
        }
    }

    setDirty(false);
}

} // namespace Membrum::UI
